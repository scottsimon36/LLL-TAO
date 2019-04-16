/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLD/include/global.h>

#include <TAO/API/include/accounts.h>

#include <TAO/Operation/include/output.h>

#include <TAO/Register/include/unpack.h>
#include <TAO/Register/objects/account.h>

#include <TAO/Ledger/include/create.h>
#include <TAO/Ledger/types/mempool.h>

#include <Util/include/hex.h>

/* Global TAO namespace. */
namespace TAO
{

    /* API Layer namespace. */
    namespace API
    {

        /* Get a user's account. */
        json::json Accounts::Notifications(const json::json& params, bool fHelp)
        {
            /* JSON return value. */
            json::json ret;

            /* Get the Genesis ID. */
            uint256_t hashGenesis = 0;

            /* Watch for destination genesis. */
            if(params.find("genesis") != params.end())
                hashGenesis.SetHex(params["genesis"].get<std::string>());
            else if(params.find("username") != params.end())
                hashGenesis = TAO::Ledger::SignatureChain::Genesis(params["username"].get<std::string>().c_str());
            else if(!config::fAPISessions && mapSessions.count(0))
            {
                /* If no specific genesis or username have been provided then fall back to the active sig chain */
                hashGenesis = mapSessions[0]->Genesis();
            }
            else
                throw APIException(-25, "Missing Genesis or Username");

            /* Check for paged parameter. */
            uint32_t nPage = 0;
            if(params.find("page") != params.end())
                nPage = atoi(params["page"].get<std::string>().c_str());

            /* Check for username parameter. */
            uint32_t nLimit = 100;
            if(params.find("limit") != params.end())
                nLimit = atoi(params["limit"].get<std::string>().c_str());

            /* Get verbose levels. */
            uint32_t nVerbose = 0;
            if(params.find("verbose") != params.end())
                nVerbose = atoi(params["verbose"].get<std::string>().c_str());

            /* Get the last transaction. */
            uint512_t hashLast = 0;
            if(!LLD::legDB->ReadLast(hashGenesis, hashLast))
                throw APIException(-28, "No transactions found");

            /* Keep a running list of owned registers. */
            std::vector< std::pair<uint256_t, uint64_t> > vRegisters;

            /* Loop until genesis. */
            while(hashLast != 0)
            {
                /* Get the transaction from disk. */
                TAO::Ledger::Transaction tx;
                if(!LLD::legDB->ReadTx(hashLast, tx))
                    throw APIException(-28, "Failed to read transaction");

                /* Set the next last. */
                hashLast = tx.hashPrevTx;

                /* Attempt to unpack a register script. */
                TAO::Register::State state;
                if(!TAO::Register::Unpack(tx, state))
                    continue;

                /* Check that it is an account. */
                if(state.nType != TAO::Register::STATE::ACCOUNT)
                    continue;

                /* Get the account. */
                TAO::Register::Account account;
                state >> account;

                /* Skip over identifier 0. */
                if(account.nIdentifier == 0)
                    continue;

                /* Get the token address. */
                uint256_t hashToken;
                if(!LLD::regDB->ReadIdentifier(account.nIdentifier, hashToken))
                    continue;

                /* Push the token identifier to list to check. */
                vRegisters.push_back(std::make_pair(hashToken, account.nBalance));
            }

            /* Start with sequence 0 (chronological order). */
            uint32_t nSequence = 0;

            /* Loop until genesis. */
            uint32_t nTotal = 0;

            /* Get notifications for foreign token registers. */
            for(const auto& hash : vRegisters)
            {
                /* Loop through all events for given token (split payments). */
                while(!config::fShutdown)
                {
                    /* Get the current page. */
                    uint32_t nCurrentPage = nTotal / nLimit;

                    /* Get the transaction from disk. */
                    TAO::Ledger::Transaction tx;
                    if(!LLD::legDB->ReadEvent(hash.first, nSequence, tx))
                        break;

                    /* Check the paged data. */
                    if(nCurrentPage < nPage)
                        continue;

                    if(nCurrentPage > nPage)
                        break;

                    json::json obj;
                    obj["version"]   = tx.nVersion;
                    obj["sequence"]  = tx.nSequence;
                    obj["timestamp"] = tx.nTimestamp;

                    /* Genesis and hashes are verbose 1 and up. */
                    if(nVerbose >= 1)
                    {
                        obj["genesis"]   = tx.hashGenesis.ToString();
                        obj["nexthash"]  = tx.hashNext.ToString();
                        obj["prevhash"]  = tx.hashPrevTx.ToString();
                    }

                    /* Signatures and public keys are verbose level 2 and up. */
                    if(nVerbose >= 2)
                    {
                        obj["pubkey"]    = HexStr(tx.vchPubKey.begin(), tx.vchPubKey.end());
                        obj["signature"] = HexStr(tx.vchSig.begin(),    tx.vchSig.end());
                    }
                    obj["hash"]      = tx.GetHash().ToString();
                    obj["operation"]  = TAO::Operation::Output(tx);

                    ret.push_back(obj);

                    /* Iterate sequence forward. */
                    ++nSequence;
                }
            }

            /* Get notifications for personal genesis indexes. */
            nSequence = 0;
            while(!config::fShutdown)
            {
                /* Get the current page. */
                uint32_t nCurrentPage = nTotal / nLimit;

                /* Get the transaction from disk. */
                TAO::Ledger::Transaction tx;
                if(!LLD::legDB->ReadEvent(hashGenesis, nSequence, tx))
                    break;

                /* Check the paged data. */
                if(nCurrentPage < nPage)
                    continue;

                if(nCurrentPage > nPage)
                    break;

                json::json obj;
                obj["version"]   = tx.nVersion;
                obj["sequence"]  = tx.nSequence;
                obj["timestamp"] = tx.nTimestamp;

                /* Genesis and hashes are verbose 1 and up. */
                if(nVerbose >= 1)
                {
                    obj["genesis"]   = tx.hashGenesis.ToString();
                    obj["nexthash"]  = tx.hashNext.ToString();
                    obj["prevhash"]  = tx.hashPrevTx.ToString();
                }

                /* Signatures and public keys are verbose level 2 and up. */
                if(nVerbose >= 2)
                {
                    obj["pubkey"]    = HexStr(tx.vchPubKey.begin(), tx.vchPubKey.end());
                    obj["signature"] = HexStr(tx.vchSig.begin(),    tx.vchSig.end());
                }
                obj["hash"]      = tx.GetHash().ToString();
                obj["operation"]  = TAO::Operation::Output(tx);

                ret.push_back(obj);

                /* Iterate sequence forward. */
                ++nSequence;
            }

            /* Check for size. */
            if(ret.empty())
                throw APIException(-26, "No notifications available");

            return ret;
        }
    }
}