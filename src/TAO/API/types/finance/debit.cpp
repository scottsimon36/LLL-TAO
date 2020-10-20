/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLC/hash/SK.h>

#include <LLD/include/global.h>

#include <TAO/API/include/global.h>
#include <TAO/API/include/utils.h>
#include <TAO/API/include/conditions.h>

#include <TAO/Operation/include/enum.h>
#include <TAO/Operation/include/execute.h>

#include <TAO/Register/include/enum.h>
#include <TAO/Register/types/object.h>

#include <TAO/Ledger/include/constants.h>
#include <TAO/Ledger/include/create.h>
#include <TAO/Ledger/types/mempool.h>
#include <TAO/Ledger/types/sigchain.h>

#include <TAO/Ledger/include/chainstate.h>
#include <Legacy/wallet/wallet.h>

#include <Util/templates/datastream.h>
#include <Util/include/string.h>

using namespace TAO::Operation;

/* Global TAO namespace. */
namespace TAO
{

    /* API Layer namespace. */
    namespace API
    {

        /* Debit a NXS account. */
        json::json Finance::Debit(const json::json& params, bool fHelp)
        {
            json::json ret;

            /* Authenticate the users credentials */
            if(!users->Authenticate(params))
                throw APIException(-139, "Invalid credentials");

            /* Get the PIN to be used for this API call */
            SecureString strPIN = users->GetPin(params, TAO::Ledger::PinUnlock::TRANSACTIONS);

            /* Get the session to be used for this API call */
            Session& session = users->GetSession(params);

            /* Lock the signature chain. */
            LOCK(session.CREATE_MUTEX);

            /* Create the transaction. */
            TAO::Ledger::Transaction tx;
            if(!Users::CreateTransaction(session.GetAccount(), strPIN, tx))
                throw APIException(-17, "Failed to create transaction");

            /* The account to debit from. */
            TAO::Register::Address hashFrom;

            /* If name is provided then use this to deduce the register address,
             * otherwise try to find the raw hex encoded address. */
            if(params.find("name") != params.end() && !params["name"].get<std::string>().empty())
                hashFrom = Names::ResolveAddress(params, params["name"].get<std::string>());
            else if(params.find("address") != params.end())
                hashFrom.SetBase58(params["address"].get<std::string>());
            else
                throw APIException(-33, "Missing name or address");

            /* Get the account object. */
            TAO::Register::Object accountFrom;
            if(!LLD::Register->ReadState(hashFrom, accountFrom))
                throw APIException(-13, "Account not found");

            /* Parse the object register. */
            if(!accountFrom.Parse())
                throw APIException(-14, "Object failed to parse");

            /* Get the object standard. */
            uint8_t nStandard = accountFrom.Standard();

            /* Check the object standard. */
            if(nStandard != TAO::Register::OBJECTS::ACCOUNT && nStandard != TAO::Register::OBJECTS::TRUST)
                throw APIException(-65, "Object is not an account");

            /* Check the account is a NXS account */
            //if(accountFrom.get<uint256_t>("token") != 0)
            //    throw APIException(-66, "Account is not a NXS account.  Please use the tokens API for debiting non-NXS token accounts.");

            /* Get current balance and significant figures. */
            uint8_t nDecimals = GetDecimals(accountFrom);
            uint64_t nCurrentBalance = accountFrom.get<uint64_t>("balance");

            /* vector of recipient addresses and amounts */
            std::vector<json::json> vRecipients;

            /* Check for the existence of recipients array */
            if( params.find("recipients") != params.end() && !params["recipients"].is_null())
            {
                if(!params["recipients"].is_array())
                    throw APIException(-216, "recipients field must be an array.");

                /* Get the recipients json array */
                json::json jsonRecipients = params["recipients"];


                /* Check that there are recipient objects in the array */
                if(jsonRecipients.size() == 0)
                    throw APIException(-217, "recipients array is empty");

                /* Add them to to the vector for processing */
                for(const auto& jsonRecipient : jsonRecipients)
                    vRecipients.push_back(jsonRecipient);
            }
            else
            {
                /* Sending to only one recipient */
                vRecipients.push_back(params);
            }

            /* Check that there are not too many recipients to fit into one transaction */
            if(vRecipients.size() > 99)
                throw APIException(-215, "Max number of recipients (99) exceeded");

            /* Flag that there is at least one legacy recipient */
            bool fHasLegacy = false;

            /* Current contract ID */
            uint8_t nContract = 0;

            /* Process the recipients */
            for(const auto& jsonRecipient : vRecipients)
            {
                /* Double check that there are not too many recipients to fit into one transaction */
                if(nContract >= 99)
                    throw APIException(-215, "Max number of recipients (99) exceeded");

                /* Check for amount parameter. */
                if(jsonRecipient.find("amount") == jsonRecipient.end())
                    throw APIException(-46, "Missing amount");

                /* Get the amount to debit. */
                uint64_t nAmount = std::stod(jsonRecipient["amount"].get<std::string>()) * pow(10, nDecimals);

                /* Check the amount is not too small once converted by the token Decimals */
                if(nAmount == 0)
                    throw APIException(-68, "Amount too small");

                /* Check they have the required funds */
                if(nAmount > nCurrentBalance)
                    throw APIException(-69, "Insufficient funds");

                /* The register address of the recipient acccount. */
                TAO::Register::Address hashTo;
                std::string strAddressTo;

                /* Check to see whether caller has provided name_to or address_to */
                if(jsonRecipient.find("name_to") != jsonRecipient.end())
                    hashTo = Names::ResolveAddress(params, jsonRecipient["name_to"].get<std::string>());
                else if(jsonRecipient.find("address_to") != jsonRecipient.end())
                {
                    strAddressTo = jsonRecipient["address_to"].get<std::string>();

                    /* Decode the base58 register address */
                    if(IsRegisterAddress(strAddressTo))
                        hashTo.SetBase58(strAddressTo);

                    /* Check that it is valid */
                    if(!hashTo.IsValid())
                        throw APIException(-165, "Invalid address_to");
                }
                else
                    throw APIException(-64, "Missing recipient account name_to / address_to");



                /* Build the transaction payload object. */
                if(hashTo.IsLegacy())
                {
                    Legacy::NexusAddress legacyAddress;
                    legacyAddress.SetString(strAddressTo);

                    /* legacy payload */
                    Legacy::Script script;
                    script.SetNexusAddress(legacyAddress);

                    tx[nContract] << (uint8_t)OP::LEGACY << hashFrom << nAmount << script;

                    /* Set the legacy flag */
                    fHasLegacy = true;

                    /* Increment the contract ID */
                    nContract++;
                }
                else
                {
                    /* flag indicating if the contract is a debit to a tokenized asset  */
                    bool fTokenizedDebit = false;

                    /* flag indicating that this is a send to self */
                    bool fSendToSelf = false;

                    /* Get the recipent account object. */
                    TAO::Register::Object recipient;

                    /* If we successfully read the register then valiate it */
                    if(LLD::Register->ReadState(hashTo, recipient, TAO::Ledger::FLAGS::LOOKUP))
                    {
                        /* Parse the object register. */
                        if(!recipient.Parse())
                            throw APIException(-14, "Object failed to parse");

                        /* Check recipient account type */
                        switch(recipient.Base())
                        {
                            case TAO::Register::OBJECTS::ACCOUNT:
                            {
                                if(recipient.get<uint256_t>("token") != accountFrom.get<uint256_t>("token"))
                                    throw APIException(-209, "Recipient account is for a different token.");

                                fSendToSelf = recipient.hashOwner == accountFrom.hashOwner;

                                break;
                            }
                            case TAO::Register::OBJECTS::NONSTANDARD :
                            {
                                fTokenizedDebit = true;

                                /* For payments to objects, they must be owned by a token */
                                if(recipient.hashOwner.GetType() != TAO::Register::Address::TOKEN)
                                    throw APIException(-211, "Recipient object has not been tokenized.");

                                break;
                            }
                            default :
                                throw APIException(-209, "Recipient is not a valid account.");
                        }
                    }

                    /* If in client mode we won't have the recipient register, so we have to loosen the checks to only look at
                       the receiving register address type, rather than check that the account/asset exists */
                    else if(config::fClient.load())
                    {
                        if(hashTo.IsObject())
                            fTokenizedDebit = true;
                        else if(!hashTo.IsAccount())
                            throw APIException(-209, "Recipient is not a valid account");
                    }

                    else
                    {
                        throw APIException(-209, "Recipient is not a valid account");
                    }


                    /* The optional payment reference */
                    uint64_t nReference = 0;
                    if(jsonRecipient.find("reference") != jsonRecipient.end())
                    {
                        /* The reference as a string */
                        std::string strReference = jsonRecipient["reference"].get<std::string>();

                        /* Check that the reference contains only numeric characters before attempting to convert it */
                        if(!IsAllDigit(strReference) || !IsUINT64(strReference))
                            throw APIException(-167, "Invalid reference");

                        /* Convert the reference to uint64 */
                        nReference = stoull(strReference);
                    }

                    /* Build the OP:DEBIT */
                    tx[nContract] << (uint8_t)OP::DEBIT << hashFrom << hashTo << nAmount << nReference;

                    /* Add expiration condition unless sending to self */
                    if(!fSendToSelf)
                        AddExpires( jsonRecipient, session.GetAccount()->Genesis(), tx[nContract], fTokenizedDebit);

                    /* Increment the contract ID */
                    nContract++;

                    /* Reduce the current balance by the amount for this recipient */
                    nCurrentBalance -= nAmount;
                }
            }

            /* Add the fee. If the sending account is a NXS account then we take the fee from that, otherwise we will leave it 
            to the AddFee method to take the fee from the default NXS account */
            if(accountFrom.get<uint256_t>("token") == 0)
                AddFee(tx, hashFrom);
            else
                AddFee(tx);

            /* Execute the operations layer. */
            if(!tx.Build())
                throw APIException(-44, "Transaction failed to build");

            /* Sign the transaction. */
            if(!tx.Sign(session.GetAccount()->Generate(tx.nSequence, strPIN)))
                throw APIException(-31, "Ledger failed to sign transaction");

            /* Execute the operations layer. */
            if(!TAO::Ledger::mempool.Accept(tx))
                throw APIException(-32, "Failed to accept");

            /* If this has a legacy transaction and not in client mode  then we need to make sure it shows in the legacy wallet */
            if(fHasLegacy && !config::fClient.load())
            {
                #ifndef NO_WALLET

                TAO::Ledger::BlockState state;
                Legacy::Wallet::GetInstance().AddToWalletIfInvolvingMe(tx, state, true);

                #endif
            }
            /* Build a JSON response object. */
            ret["txid"] = tx.GetHash().ToString();

            return ret;
        }
    }
}
