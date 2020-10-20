/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLD/include/global.h>

#include <TAO/API/types/finance.h>
#include <TAO/API/types/names.h>
#include <TAO/API/include/global.h>
#include <TAO/API/include/json.h>

#include <TAO/Ledger/types/sigchain.h>

#include <TAO/Operation/include/enum.h>

#include <TAO/Register/types/object.h>

/* Global TAO namespace. */
namespace TAO
{

    /* API Layer namespace. */
    namespace API
    {

        /* Get the data from a digital asset */
        json::json Finance::Get(const json::json& params, bool fHelp)
        {
            json::json ret;

            /* Get the Register ID. */
            TAO::Register::Address hashRegister;

            /* Attempt to deduce the register address from name. */
            if(params.find("name") != params.end() && !params["name"].get<std::string>().empty())
                hashRegister = Names::ResolveAddress(params, params["name"].get<std::string>());

            /* Get the RAW address from hex. */
            else if(params.find("address") != params.end())
                hashRegister.SetBase58(params["address"].get<std::string>());
            else
                throw APIException(-33, "Missing name / address");

            /* Get the token / account object. */
            TAO::Register::Object object;
            if(!LLD::Register->ReadState(hashRegister, object, TAO::Ledger::FLAGS::MEMPOOL))
                throw APIException(-13, "Account not found");

            if(config::fClient.load() && object.hashOwner != users->GetCallersGenesis(params))
                throw APIException(-300, "API can only be used to lookup data for the currently logged in signature chain when running in client mode");

            /* Parse the object register. */
            if(!object.Parse())
                throw APIException(-14, "Object failed to parse");

            /* Check the object standard. */
            uint8_t nStandard = object.Standard();
            if(nStandard != TAO::Register::OBJECTS::ACCOUNT && nStandard != TAO::Register::OBJECTS::TRUST)
                throw APIException(-65, "Object is not an account");

            /* Populate the response JSON */
            ret["owner"]    = TAO::Register::Address(object.hashOwner).ToString();
            ret["created"]  = object.nCreated;
            ret["modified"] = object.nModified;

            json::json data  =TAO::API::ObjectToJSON(params, object, hashRegister);

            /* Copy the asset data in to the response after the type/checksum */
            ret.insert(data.begin(), data.end());


            /* If the caller has requested to filter on a fieldname then filter out the json response to only include that field */
            FilterResponse(params, ret);

            return ret;
        }
    }
}
