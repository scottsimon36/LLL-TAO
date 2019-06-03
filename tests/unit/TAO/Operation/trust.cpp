/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLC/include/random.h>

#include <LLD/include/global.h>

#include <TAO/Operation/include/enum.h>
#include <TAO/Operation/include/execute.h>
#include <TAO/Operation/include/enum.h>

#include <TAO/Register/include/rollback.h>
#include <TAO/Register/include/create.h>

#include <TAO/Ledger/types/transaction.h>

#include <unit/catch2/catch.hpp>

TEST_CASE( "Trust Operation Tests", "[operation]" )
{
    using namespace TAO::Register;
    using namespace TAO::Operation;

    uint256_t hashTrust    = LLC::GetRand256();
    uint256_t hashGenesis  = LLC::GetRand256();

    uint512_t hashLastTrust;

    //create a trust account register
    {
        {
            //create the transaction object
            TAO::Ledger::Transaction tx;
            tx.hashGenesis = hashGenesis;
            tx.nSequence   = 0;
            tx.nTimestamp  = runtime::timestamp();

            //create object
            Object trustRegister = CreateTrust();

            //payload
            tx << uint8_t(OP::REGISTER) << hashTrust << uint8_t(REGISTER::OBJECT) << trustRegister.GetState();

            //generate the prestates and poststates
            REQUIRE(Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

            //commit to disk
            REQUIRE(Execute(tx, FLAGS::WRITE));
        }

        {
            //check the trust register
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadState(hashTrust, trustAccount));

            //parse
            REQUIRE(trustAccount.Parse());

            //check standards
            REQUIRE(trustAccount.Standard() == OBJECTS::TRUST);
            REQUIRE(trustAccount.Base()     == OBJECTS::ACCOUNT);

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 0);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 0);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 0);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 0);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);

            //update and write
            trustAccount.Write("balance", (uint64_t)5000);
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 5000);
            REQUIRE(LLD::regDB->WriteState(hashTrust, trustAccount));
        }

        {
            //verify update
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadState(hashTrust, trustAccount));

            //parse
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 5000);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 0);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 0);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 0);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);
        }
    }


    //Add stake
    {
        //create the transaction object
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = 1;
        tx.nTimestamp  = runtime::timestamp();

        //payload with added stake amount
        tx << uint8_t(OP::STAKE) << hashTrust << uint64_t(4000);

        //generate the prestates and poststates
        REQUIRE(Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

        //commit to disk
        REQUIRE(Execute(tx, FLAGS::WRITE));

        //check register values
        {
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadState(hashTrust, trustAccount));

            //parse register
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 1000);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 0);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 0);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 4000);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);
        }
    }


    //test Trust w/o Genesis NOTE: intended failure
    {
        //create the transaction object
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = 2;
        tx.nTimestamp  = runtime::timestamp();

        hashLastTrust = LLC::GetRand512();

        //payload
        tx << uint8_t(OP::TRUST) << hashLastTrust << uint64_t(555) << uint64_t(6);

        //generate the prestates and poststates (trust w/o genesis should fail)
        REQUIRE(!Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

        //check register values
        {
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadState(hashTrust, trustAccount));

            //parse register
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 1000);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 0);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 0);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 4000);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);

            //check trust not indexed
            REQUIRE(!LLD::regDB->HasTrust(hashGenesis));
        }
    }


    //test Genesis
    {
        //create the transaction object
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = 2;
        tx.nTimestamp  = runtime::timestamp();

        //payload with coinstake reward
        tx << uint8_t(OP::GENESIS) << hashTrust << uint64_t(5);

        //generate the prestates and poststates
        REQUIRE(Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

        //commit to disk
        REQUIRE(Execute(tx, FLAGS::WRITE));

        //save last trust hash
        hashLastTrust = tx.GetHash();

        //check register values
        {
            //check trust account indexing
            REQUIRE(LLD::regDB->HasTrust(hashGenesis));

            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadTrust(hashGenesis, trustAccount));

            //parse register
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 1005);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 0);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 4000);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 0);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);
        }
    }


    //test Trust
    {
        //create the transaction object
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = 3;
        tx.nTimestamp  = runtime::timestamp();

        //payload with new trust score and coinstake reward
        tx << uint8_t(OP::TRUST) << hashLastTrust << uint64_t(720) << uint64_t(6);

        //generate the prestates and poststates
        REQUIRE(Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

        //commit to disk
        REQUIRE(Execute(tx, FLAGS::WRITE));

        //save last trust hash
        hashLastTrust = tx.GetHash();

        //check register values
        {
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadTrust(hashGenesis, trustAccount));

            //parse register
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 1011);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 720);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 4000);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 0);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);
        }
    }


    //test second Trust
    {
        //create the transaction object
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = 4;
        tx.nTimestamp  = runtime::timestamp();

        //payload with new trust score and coinstake reward
        tx << uint8_t(OP::TRUST) << hashLastTrust << uint64_t(1600) << uint64_t(7);

        //generate the prestates and poststates
        REQUIRE(Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

        //commit to disk
        REQUIRE(Execute(tx, FLAGS::WRITE));

        //save last trust hash
        hashLastTrust = tx.GetHash();

        //check register values
        {
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadTrust(hashGenesis, trustAccount));

            //parse register
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 1018);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 1600);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 4000);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 0);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);
        }
    }


    //Add more stake
    {
        //create the transaction object
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = 5;
        tx.nTimestamp  = runtime::timestamp();

        //payload with added stake amount
        tx << uint8_t(OP::STAKE) << hashTrust << uint64_t(1000);

        //generate the prestates and poststates
        REQUIRE(Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

        //commit to disk
        REQUIRE(Execute(tx, FLAGS::WRITE));

        //check register values
        {
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadTrust(hashGenesis, trustAccount));

            //parse register
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 18);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 1600);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 4000);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 1000);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);
        }
    }


    //Unstake a portion of added stake
    {
        //create the transaction object
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = 6;
        tx.nTimestamp  = runtime::timestamp();

        //payload with removed stake amount and trust penalty
        tx << uint8_t(OP::UNSTAKE) << hashTrust << uint64_t(500) << uint64_t(0);

        //generate the prestates and poststates
        REQUIRE(Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

        //commit to disk
        REQUIRE(Execute(tx, FLAGS::WRITE));

        //check register values
        {
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadTrust(hashGenesis, trustAccount));

            //parse register
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 518);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 1600);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 4000);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 500);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);
        }
    }


    //Unstake more than pending_stake amount
    {
        //create the transaction object
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = 7;
        tx.nTimestamp  = runtime::timestamp();

        //payload with removed stake amount and trust penalty
        tx << uint8_t(OP::UNSTAKE) << hashTrust << uint64_t(1500) << uint64_t(400);

        //generate the prestates and poststates
        REQUIRE(Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

        //commit to disk
        REQUIRE(Execute(tx, FLAGS::WRITE));

        //check register values
        {
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadTrust(hashGenesis, trustAccount));

            //parse register
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 2018);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 1200);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 3000);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 0);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);
        }
    }


    //Unstake with no pending_stake amount
    {
        //create the transaction object
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = 8;
        tx.nTimestamp  = runtime::timestamp();

        //payload with removed stake amount and trust penalty
        tx << uint8_t(OP::UNSTAKE) << hashTrust << uint64_t(1000) << uint64_t(400);

        //generate the prestates and poststates
        REQUIRE(Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

        //commit to disk
        REQUIRE(Execute(tx, FLAGS::WRITE));

        //check register values
        {
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadTrust(hashGenesis, trustAccount));

            //parse register
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 3018);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 800);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 2000);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 0);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);
        }
    }


    //test Trust after unstake
    {
        //create the transaction object
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = 9;
        tx.nTimestamp  = runtime::timestamp();

        //payload with new trust score and coinstake reward
        tx << uint8_t(OP::TRUST) << hashLastTrust << uint64_t(1000) << uint64_t(3);

        //generate the prestates and poststates
        REQUIRE(Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

        //commit to disk
        REQUIRE(Execute(tx, FLAGS::WRITE));

        //save last trust hash
        hashLastTrust = tx.GetHash();

        //check register values
        {
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadTrust(hashGenesis, trustAccount));

            //parse register
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 3021);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 1000);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 2000);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 0);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);
        }
    }


    //Add more stake
    {
        //create the transaction object
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = 10;
        tx.nTimestamp  = runtime::timestamp();

        //payload with added stake amount
        tx << uint8_t(OP::STAKE) << hashTrust << uint64_t(1000);

        //generate the prestates and poststates
        REQUIRE(Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

        //commit to disk
        REQUIRE(Execute(tx, FLAGS::WRITE));

        //check register values
        {
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadTrust(hashGenesis, trustAccount));

            //parse register
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 2021);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 1000);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 2000);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 1000);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);
        }
    }


    //test Trust after adding stake
    {
        //create the transaction object
        TAO::Ledger::Transaction tx;
        tx.hashGenesis = hashGenesis;
        tx.nSequence   = 11;
        tx.nTimestamp  = runtime::timestamp();

        //payload with new trust score and coinstake reward
        tx << uint8_t(OP::TRUST) << hashLastTrust << uint64_t(1250) << uint64_t(4);

        //generate the prestates and poststates
        REQUIRE(Execute(tx, FLAGS::PRESTATE | FLAGS::POSTSTATE));

        //commit to disk
        REQUIRE(Execute(tx, FLAGS::WRITE));

        //save last trust hash
        hashLastTrust = tx.GetHash();

        //check register values
        {
            TAO::Register::Object trustAccount;
            REQUIRE(LLD::regDB->ReadTrust(hashGenesis, trustAccount));

            //parse register
            REQUIRE(trustAccount.Parse());

            //check values
            REQUIRE(trustAccount.get<uint64_t>("balance")        == 2025);
            REQUIRE(trustAccount.get<uint64_t>("trust")          == 1250);
            REQUIRE(trustAccount.get<uint64_t>("stake")          == 3000);
            REQUIRE(trustAccount.get<uint64_t>("pending_stake")  == 0);
            REQUIRE(trustAccount.get<uint256_t>("token_address") == 0);
        }
    }
}
