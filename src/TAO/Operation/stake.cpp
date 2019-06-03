/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLD/include/global.h>

#include <TAO/Operation/include/operations.h>

#include <TAO/Register/types/object.h>

/* Global TAO namespace. */
namespace TAO
{

    /* Operation Layer namespace. */
    namespace Operation
    {

        /* Move balance to pending stake for trust account. */
        bool Stake(const uint256_t& hashAddress, const uint64_t nAmount, const uint8_t nFlags, TAO::Ledger::Transaction &tx)
        {
            /* Read the register from the database. */
            TAO::Register::Object trustAccount;

            /* Write pre-states. */
            if((nFlags & TAO::Register::FLAGS::PRESTATE))
            {
                /* Add stake can be pre-Genesis or post-Genesis */
                if(LLD::regDB->HasTrust(tx.hashGenesis))
                {
                    if(!LLD::regDB->ReadTrust(tx.hashGenesis, trustAccount))
                        return debug::error(FUNCTION, "Trust register address doesn't exist");
                }
                else
                {
                    if(!LLD::regDB->ReadState(hashAddress, trustAccount, nFlags))
                        return debug::error(FUNCTION, "Trust register address doesn't exist ", hashAddress.ToString());
                }

                /* Set the register pre-states. */
                tx.ssRegister << uint8_t(TAO::Register::STATES::PRESTATE) << trustAccount;
            }

            /* Get pre-states on write. */
            if(nFlags & TAO::Register::FLAGS::WRITE
            || nFlags & TAO::Register::FLAGS::MEMPOOL)
            {
                /* Get the state byte. */
                uint8_t nState = 0; //RESERVED
                tx.ssRegister >> nState;

                /* Check for the pre-state. */
                if(nState != TAO::Register::STATES::PRESTATE)
                        return debug::error(FUNCTION, "Register script not in pre-state");

                /* Get the pre-state. */
                tx.ssRegister >> trustAccount;
            }

            /* Check ownership of register. */
            if(trustAccount.hashOwner != tx.hashGenesis)
                return debug::error(FUNCTION, tx.hashGenesis.ToString(), "Caller not authorized to add stake to trust account");

            /* Parse the account object register. */
            if(!trustAccount.Parse())
                return debug::error(FUNCTION, "Failed to parse account object register");

            /* Get account starting values */
            uint64_t nBalancePrev = trustAccount.get<uint64_t>("balance");
            uint64_t nPendingStakePrev = trustAccount.get<uint64_t>("pending_stake");

            if(nAmount > nBalancePrev)
                return debug::error(FUNCTION, "cannot add stake exceeding existing trust account balance");

            /* Move requested funds from balance to pending stake */
            uint64_t nPendingStake = nPendingStakePrev + nAmount;
            uint64_t nBalance = nBalancePrev - nAmount;

            /* Write the new pending stake to object register. */
            if(!trustAccount.Write("pending_stake", nPendingStake))
                return debug::error(FUNCTION, "pending stake could not be written to object register");

            /* Write the new balance to object register. */
            if(!trustAccount.Write("balance", nBalance))
                return debug::error(FUNCTION, "balance could not be written to object register");

            /* Update the state register's timestamp. */
            trustAccount.nTimestamp = tx.nTimestamp;
            trustAccount.SetChecksum();

            /* Check that the register is in a valid state. */
            if(!trustAccount.IsValid())
                return debug::error(FUNCTION, "trust address ", tx.hashGenesis.ToString(), " is in invalid state");

            /* Write post-state checksum. */
            if((nFlags & TAO::Register::FLAGS::POSTSTATE))
                tx.ssRegister << uint8_t(TAO::Register::STATES::POSTSTATE) << trustAccount.GetHash();

            /* Verify the post-state checksum. */
            if(nFlags & TAO::Register::FLAGS::WRITE
            || nFlags & TAO::Register::FLAGS::MEMPOOL)
            {
                /* Check register post-state checksum. */
                uint8_t nState = 0; //RESERVED
                tx.ssRegister >> nState;

                /* Check for the pre-state. */
                if(nState != TAO::Register::STATES::POSTSTATE)
                    return debug::error(FUNCTION, "register script not in post-state");

                /* Get the post state checksum. */
                uint64_t nChecksum;
                tx.ssRegister >> nChecksum;

                /* Check for matching post states. */
                if(nChecksum != trustAccount.GetHash())
                    return debug::error(FUNCTION, "register script has invalid post-state");

                /* Update the register database with the index. As with pre-state, support both pre-Genesis and post-Genesis. */
                if((nFlags & TAO::Register::FLAGS::WRITE))
                {
                    if(LLD::regDB->HasTrust(tx.hashGenesis))
                    {
                        if(!LLD::regDB->WriteTrust(tx.hashGenesis, trustAccount))
                            return debug::error(FUNCTION, "failed to write trust account");
                    }
                    else
                    {
                        if(!LLD::regDB->WriteState(hashAddress, trustAccount))
                            return debug::error(FUNCTION, "failed to write new state");
                    }
                }
            }

            return true;
        }
    }
}
