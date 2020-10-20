/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLC/include/random.h>

#include <LLD/include/global.h>
#include <LLD/cache/binary_key.h>

#include <LLP/types/tritium.h>
#include <LLP/include/global.h>
#include <LLP/include/manager.h>
#include <LLP/templates/events.h>

#include <TAO/API/include/global.h>
#include <TAO/API/include/sessionmanager.h>
#include <TAO/API/include/utils.h>

#include <TAO/Operation/include/enum.h>
#include <TAO/Operation/include/execute.h>

#include <TAO/Register/include/build.h>
#include <TAO/Register/include/names.h>
#include <TAO/Register/types/object.h>

#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/include/enum.h>
#include <TAO/Ledger/include/process.h>

#include <TAO/Ledger/types/client.h>
#include <TAO/Ledger/types/locator.h>
#include <TAO/Ledger/types/mempool.h>
#include <TAO/Ledger/types/merkle.h>
#include <TAO/Ledger/types/stakepool.h>
#include <TAO/Ledger/types/syncblock.h>

#ifndef NO_WALLET
#include <Legacy/wallet/wallet.h>
#else
#include <Legacy/types/merkle.h>
#endif

#include <Legacy/include/evaluate.h>

#include <Util/include/args.h>
#include <Util/include/debug.h>
#include <Util/include/runtime.h>
#include <Util/include/version.h>


#include <climits>
#include <memory>
#include <iomanip>
#include <bitset>

/* We use this to setup the node to start syncing from orphans with no transactions sent with blocks.
 * This helps with stress testing and debugging the missing transaction algorithms
 */
//#define DEBUG_MISSING

namespace LLP
{
    using namespace LLP::Tritium;

    /* Declaration of sessions mutex. (private). */
    std::mutex TritiumNode::SESSIONS_MUTEX;


    /* Declaration of sessions sets. (private). */
    std::map<uint64_t, std::pair<uint32_t, uint32_t>> TritiumNode::mapSessions;


    /* Declaration of block height at the start sync. */
    std::atomic<uint32_t> TritiumNode::nSyncStart(0);


    /* Declaration of timer to track sync time */
    runtime::timer TritiumNode::SYNCTIMER;


    /* If node is completely sychronized. */
    std::atomic<bool> TritiumNode::fSynchronized(false);


    /* Last block that was processed. */
    std::atomic<uint64_t> TritiumNode::nLastTimeReceived(0);


    /* Remaining time left to finish syncing. */
    std::atomic<uint64_t> TritiumNode::nRemainingTime(0);


    /** This node's address, as seen by the peer **/
    LLP::BaseAddress TritiumNode::thisAddress;


    /* The local relay inventory cache. */
    static LLD::KeyLRU cacheInventory = LLD::KeyLRU(1024 * 1024);

    /** Mutex for controlling access to the p2p requests map. **/
    std::mutex TritiumNode::P2P_REQUESTS_MUTEX;

    /** map of P2P request timestamps by source genesis hash. **/
    std::map<uint256_t, uint64_t> TritiumNode::mapP2PRequests;


    /** Default Constructor **/
    TritiumNode::TritiumNode()
    : BaseConnection<MessagePacket>()
    , fLoggedIn(false)
    , fAuthorized(false)
    , fInitialized(false)
    , nSubscriptions(0)
    , nNotifications(0)
    , vNotifications()
    , nLastPing(0)
    , nLastSamples(0)
    , mapLatencyTracker()
    , hashGenesis(0)
    , nTrust(0)
    , nProtocolVersion(0)
    , nCurrentSession(0)
    , nCurrentHeight(0)
    , hashCheckpoint(0)
    , hashBestChain(0)
    , hashLastIndex(0)
    , nConsecutiveOrphans(0)
    , nConsecutiveFails(0)
    , strFullVersion()
    , nUnsubscribed(0)
    , nTriggerNonce(0)
    {
    }


    /** Constructor **/
    TritiumNode::TritiumNode(Socket SOCKET_IN, DDOS_Filter* DDOS_IN, bool fDDOSIn)
    : BaseConnection<MessagePacket>(SOCKET_IN, DDOS_IN, fDDOSIn)
    , fLoggedIn(false)
    , fAuthorized(false)
    , fInitialized(false)
    , nSubscriptions(0)
    , nNotifications(0)
    , vNotifications()
    , nLastPing(0)
    , nLastSamples(0)
    , mapLatencyTracker()
    , hashGenesis(0)
    , nTrust(0)
    , nProtocolVersion(0)
    , nCurrentSession(0)
    , nCurrentHeight(0)
    , hashCheckpoint(0)
    , hashBestChain(0)
    , hashLastIndex(0)
    , nConsecutiveOrphans(0)
    , nConsecutiveFails(0)
    , strFullVersion()
    , nUnsubscribed(0)
    , nTriggerNonce(0)
    {
    }


    /** Constructor **/
    TritiumNode::TritiumNode(DDOS_Filter* DDOS_IN, bool fDDOSIn)
    : BaseConnection<MessagePacket>(DDOS_IN, fDDOSIn)
    , fLoggedIn(false)
    , fAuthorized(false)
    , fInitialized(false)
    , nSubscriptions(0)
    , nNotifications(0)
    , vNotifications()
    , nLastPing(0)
    , nLastSamples(0)
    , mapLatencyTracker()
    , hashGenesis(0)
    , nTrust(0)
    , nProtocolVersion(0)
    , nCurrentSession(0)
    , nCurrentHeight(0)
    , hashCheckpoint(0)
    , hashBestChain(0)
    , hashLastIndex(0)
    , nConsecutiveOrphans(0)
    , nConsecutiveFails(0)
    , strFullVersion()
    , nUnsubscribed(0)
    , nTriggerNonce(0)
    {
    }


    /** Default Destructor **/
    TritiumNode::~TritiumNode()
    {
    }


    /** Virtual Functions to Determine Behavior of Message LLP. **/
    void TritiumNode::Event(uint8_t EVENT, uint32_t LENGTH)
    {
        switch(EVENT)
        {
            case EVENTS::CONNECT:
            {
                debug::log(1, NODE, fOUTGOING ? "Outgoing" : "Incoming", " Connection Established");

                /* Set the laset ping time. */
                nLastPing    = runtime::unifiedtimestamp();

                /* Respond with version message if incoming connection. */
                if(fOUTGOING)
                    PushMessage(ACTION::VERSION, PROTOCOL_VERSION, SESSION_ID, version::CLIENT_VERSION_BUILD_STRING);

                break;
            }

            case EVENTS::HEADER:
            {
                /* Check for initialization. */
                if(nCurrentSession == 0 && nProtocolVersion == 0 && INCOMING.MESSAGE != ACTION::VERSION && DDOS)
                    DDOS->rSCORE += 25;

                break;
            }

            case EVENTS::PACKET:
            {
                /* Check a packet's validity once it is finished being read. */
                if(Incoming())
                {
                    /* Give higher score for Bad Packets. */
                    if(INCOMING.Complete() && !INCOMING.IsValid() && DDOS)
                        DDOS->rSCORE += 15;
                }


                if(INCOMING.Complete())
                {
                    if(config::nVerbose >= 5)
                        PrintHex(INCOMING.GetBytes());
                }

                break;
            }


            /* Processed event is used for events triggers. */
            case EVENTS::PROCESSED:
            {
                break;
            }


            case EVENTS::GENERIC:
            {
                /* Make sure node responded on unsubscriion within 30 seconds. */
                if(nUnsubscribed != 0 && nUnsubscribed + 30 < runtime::timestamp())
                {
                    /* Debug output. */
                    debug::drop(NODE, "failed to receive unsubscription within 30 seconds");

                    /* Disconnect this node. */
                    Disconnect();

                    return;
                }

                /* Handle sending the pings to remote node.. */
                if(nLastPing + 15 < runtime::unifiedtimestamp())
                {
                    /* Create a random nonce. */
                    uint64_t nNonce = LLC::GetRand();
                    nLastPing = runtime::unifiedtimestamp();

                    /* Keep track of latency for this ping. */
                    mapLatencyTracker.insert(std::pair<uint64_t, runtime::timer>(nNonce, runtime::timer()));
                    mapLatencyTracker[nNonce].Start();

                    /* Push new message. */
                    PushMessage(ACTION::PING, nNonce);

                    /* Rebroadcast transactions. */
                    if(!TAO::Ledger::ChainState::Synchronizing())
                    {
                        #ifndef NO_WALLET
                        Legacy::Wallet::GetInstance().ResendWalletTransactions();
                        #endif
                    }
                }


                /* Handle subscribing to events from other nodes. */
                if(!fInitialized.load() && fSynchronized.load() && nCurrentSession != 0)
                {
                    /* Simple log to let us know we are making the subscription requests. */
                    debug::log(1, NODE, "Initializing Subscriptions with REMOTE HOST");

                    /* Grab list of memory pool transactions. */
                    if(!config::fClient.load())
                    {
                        /* Subscribe to notifications. */
                        Subscribe(
                               SUBSCRIPTION::BESTCHAIN
                             | SUBSCRIPTION::BESTHEIGHT
                             | SUBSCRIPTION::CHECKPOINT
                             | SUBSCRIPTION::BLOCK
                             | SUBSCRIPTION::TRANSACTION
                        );

                        PushMessage(ACTION::LIST, uint8_t(TYPES::MEMPOOL));
                    }
                    else
                    {
                        /* Subscribe to notifications. */
                        Subscribe(
                               SUBSCRIPTION::BESTCHAIN
                             | SUBSCRIPTION::BESTHEIGHT
                             | SUBSCRIPTION::BLOCK
                        );
                    }

                    /* Set node as initialized. */
                    fInitialized.store(true);
                }


                /* Disable AUTH for older protocol versions. */
                if(nProtocolVersion >= MIN_TRITIUM_VERSION && !fLoggedIn.load())
                {
                    /* Generate an AUTH message to send to all peers */
                    DataStream ssMessage = LLP::TritiumNode::GetAuth(true);
                    if(ssMessage.size() > 0)
                    {
                        /* Authorize before we subscribe. */
                        WritePacket(NewMessage(ACTION::AUTH, ssMessage));
                        fLoggedIn.store(true);
                    }
                }


                /* Unreliabilitiy re-requesting (max time since getblocks) */
                if(TAO::Ledger::ChainState::Synchronizing()
                && nCurrentSession == TAO::Ledger::nSyncSession.load()
                && nCurrentSession != 0
                && nLastTimeReceived.load() + 30 < runtime::timestamp())
                {
                    debug::log(0, NODE, "Sync Node Timeout");

                    /* Switch to a new node. */
                    SwitchNode();

                    /* Reset the event timeouts. */
                    nLastTimeReceived.store(runtime::timestamp());
                }

                break;
            }


            case EVENTS::DISCONNECT:
            {
                /* Debut output. */
                std::string strReason;
                switch(LENGTH)
                {
                    case DISCONNECT::TIMEOUT:
                        strReason = "Timeout";
                        break;

                    case DISCONNECT::ERRORS:
                        strReason = "Errors";
                        break;

                    case DISCONNECT::POLL_ERROR:
                        strReason = "Poll Error";
                        break;

                    case DISCONNECT::POLL_EMPTY:
                        strReason = "Unavailable";
                        break;

                    case DISCONNECT::DDOS:
                        strReason = "DDOS";
                        break;

                    case DISCONNECT::FORCE:
                        strReason = "Force";
                        break;

                    case DISCONNECT::PEER:
                        strReason = "Peer Hangup";
                        break;

                    case DISCONNECT::BUFFER:
                        strReason = "Flood Control";
                        break;

                    case DISCONNECT::TIMEOUT_WRITE:
                        strReason = "Flood Control Timeout";
                        break;

                    default:
                        strReason = "Unknown";
                        break;
                }

                /* Debug output for node disconnect. */
                debug::log(1, NODE, fOUTGOING ? "Outgoing" : "Incoming",
                    " Disconnected (", strReason, ")");

                /* Update address status. */
                if(TRITIUM_SERVER->GetAddressManager())
                    TRITIUM_SERVER->GetAddressManager()->AddAddress(GetAddress(), ConnectState::DROPPED);

                /* Handle if sync node is disconnected. */
                if(nCurrentSession == TAO::Ledger::nSyncSession.load())
                {
                    /* Debug output for node disconnect. */
                    debug::log(0, NODE, "Sync Node Disconnected (", strReason, ")");

                    SwitchNode();
                }


                {
                    LOCK(SESSIONS_MUTEX);

                    /* Check for sessions to free. */
                    if(mapSessions.count(nCurrentSession))
                    {
                        /* Make sure that we aren't freeing our session if handling duplicate connections. */
                        const std::pair<uint32_t, uint32_t>& pair = mapSessions[nCurrentSession];
                        if(pair.first == nDataThread && pair.second == nDataIndex)
                            mapSessions.erase(nCurrentSession);
                    }
                }

                /* Reset session, notifications, subscriptions etc */
                nCurrentSession = 0;
                nUnsubscribed = 0;
                nNotifications = 0;

                break;
            }
        }
    }


    /** Main message handler once a packet is recieved. **/
    bool TritiumNode::ProcessPacket()
    {
        /* Deserialize the packeet from incoming packet payload. */
        DataStream ssPacket(INCOMING.DATA, SER_NETWORK, PROTOCOL_VERSION);
        switch(INCOMING.MESSAGE)
        {
            /* Handle for the version command. */
            case ACTION::VERSION:
            {
                /* Check for duplicate version messages. */
                if(nCurrentSession != 0)
                    return debug::drop(NODE, "duplicate version message");

                /* Hard requirement for version. */
                ssPacket >> nProtocolVersion;

                /* Get the current session-id. */
                ssPacket >> nCurrentSession;

                /* Get the version string. */
                ssPacket >> strFullVersion;

                /* Check for invalid session-id. */
                if(nCurrentSession == 0)
                    return debug::drop(NODE, "invalid session-id");

                /* Check for a connect to self. */
                if(nCurrentSession == SESSION_ID)
                {
                    /* Cache self-address in the banned list of the Address Manager. */
                    if(TRITIUM_SERVER->GetAddressManager())
                        TRITIUM_SERVER->GetAddressManager()->Ban(GetAddress());

                    return debug::drop(NODE, "connected to self");
                }

                /* Check if session is already connected. */
                {
                    LOCK(SESSIONS_MUTEX);
                    if(mapSessions.count(nCurrentSession))
                        return debug::drop(NODE, "duplicate connection");

                    /* Set this to the current session. */
                    mapSessions[nCurrentSession] = std::make_pair(nDataThread, nDataIndex);
                }

                /* Check versions. */
                if(nProtocolVersion < MIN_PROTO_VERSION)
                    return debug::drop(NODE, "connection using obsolete protocol version");

                /* Client mode only wants connections to correct version. */
                if(config::fClient.load() && nProtocolVersion < MIN_TRITIUM_VERSION)
                    return debug::drop(NODE, "-client mode requires version ", MIN_TRITIUM_VERSION);

                /* Respond with version message if incoming connection. */
                if(Incoming())
                {
                    /* Respond with version message. */
                    PushMessage(ACTION::VERSION,
                        PROTOCOL_VERSION,
                        SESSION_ID,
                        version::CLIENT_VERSION_BUILD_STRING);

                    /* Add to address manager. */
                    if(TRITIUM_SERVER->GetAddressManager())
                        TRITIUM_SERVER->GetAddressManager()->AddAddress(GetAddress(), ConnectState::CONNECTED);
                }

                /* Send Auth immediately after version and before any other messages*/
                //Auth(true);

                /* If we dont yet know our IP address and the peer is on the newer protocol version then request the IP address */
                {
                    /* Lock session mutex to prevent other sessions from accessing thisAddress */
                    LOCK(SESSIONS_MUTEX);

                    if(!thisAddress.IsValid() && nProtocolVersion >= MIN_TRITIUM_VERSION)
                        PushMessage(ACTION::GET, uint8_t(TYPES::PEERADDRESS));
                }

                #ifdef DEBUG_MISSING
                fSynchronized.store(true);
                #endif

                /* If not synchronized and making an outbound connection, start the sync */
                if(!fSynchronized.load())
                {
                    /* See if this is a local testnet, in which case we will allow a sync on incoming or outgoing */
                    bool fLocalTestnet = config::fTestNet.load() && !config::GetBoolArg("-dns", true);

                    /* Start sync on startup, or override any legacy syncing currently in process. */
                    if(TAO::Ledger::nSyncSession.load() == 0 && (!Incoming() || fLocalTestnet))
                    {
                        /* Set the sync session-id. */
                        TAO::Ledger::nSyncSession.store(nCurrentSession);

                        /* Reset last time received. */
                        nLastTimeReceived.store(runtime::timestamp());

                        debug::log(0, NODE, "New sync address set");

                        /* Cache the height at the start of the sync */
                        nSyncStart.store(TAO::Ledger::ChainState::stateBest.load().nHeight);

                        /* Make sure the sync timer is stopped.  We don't start this until we receive our first sync block*/
                        SYNCTIMER.Stop();

                        /* Subscribe to this node. */
                        Subscribe(SUBSCRIPTION::LASTINDEX | SUBSCRIPTION::BESTCHAIN | SUBSCRIPTION::BESTHEIGHT);

                        /* Ask for list of blocks if this is current sync node. */
                        PushMessage(ACTION::LIST,
                            config::fClient.load() ? uint8_t(SPECIFIER::CLIENT) : uint8_t(SPECIFIER::SYNC),
                            uint8_t(TYPES::BLOCK),
                            uint8_t(TYPES::LOCATOR),
                            TAO::Ledger::Locator(TAO::Ledger::ChainState::hashBestChain.load()),
                            uint1024_t(0)
                        );
                    }
                }

                /* Relay to subscribed nodes a new connection was seen. */
                TRITIUM_SERVER->Relay
                (
                    ACTION::NOTIFY,
                    uint8_t(TYPES::ADDRESS),
                    BaseAddress(GetAddress())
                );

                /* Subscribe to address notifications only. */
                Subscribe(SUBSCRIPTION::ADDRESS);

                break;
            }


            /* Handle for auth / deauthcommand. */
            case ACTION::AUTH:
            case ACTION::DEAUTH:
            {
                /* Disable AUTH for older protocol versions. */
                if(nProtocolVersion < MIN_TRITIUM_VERSION)
                    return true;

                /* Disable AUTH messages when synchronizing. */
                if(TAO::Ledger::ChainState::Synchronizing())
                    return true;

                /* Hard requirement for genesis. */
                ssPacket >> hashGenesis;

                /* Get the signature information. */
                if(hashGenesis == 0)
                    return debug::drop(NODE, "ACTION::AUTH: cannot authorize with reserved genesis");

                /* Get the timestamp */
                uint64_t nTimestamp;
                ssPacket >> nTimestamp;

                /* Check the timestamp. */
                if(nTimestamp > runtime::unifiedtimestamp() || nTimestamp < runtime::unifiedtimestamp() - 10)
                    return debug::drop(NODE, "ACTION::AUTH: timestamp out of rang (stale)");

                /* Get the nonce */
                uint64_t nNonce;
                ssPacket >> nNonce;

                /* Check the nNonce for expected values. */
                if(nNonce != nCurrentSession)
                    return debug::drop(NODE, "ACTION::AUTH: invalid session-id ", nNonce);

                /* Get the public key. */
                std::vector<uint8_t> vchPubKey;
                ssPacket >> vchPubKey;


                /* Build the byte stream from genesis+nonce in order to verify the signature */
                DataStream ssCheck(SER_NETWORK, PROTOCOL_VERSION);
                ssCheck << hashGenesis << nTimestamp << nNonce;

                /* Get a hash of the data. */
                uint256_t hashCheck = LLC::SK256(ssCheck.begin(), ssCheck.end());

                /* Get the signature. */
                std::vector<uint8_t> vchSig;
                ssPacket >> vchSig;

                /* Verify the signature */
                if(!TAO::Ledger::SignatureChain::Verify(hashGenesis, "network", hashCheck.GetBytes(), vchPubKey, vchSig))
                    return debug::drop(NODE, "ACTION::AUTH: invalid transaction signature");

                /* Get the crypto register. */
                TAO::Register::Object trust;
                if(!LLD::Register->ReadState(TAO::Register::Address(std::string("trust"),
                    hashGenesis, TAO::Register::Address::TRUST), trust, TAO::Ledger::FLAGS::MEMPOOL))
                    return debug::drop(NODE, "ACTION::AUTH: authorization failed, missing trust register");

                /* Parse the object. */
                if(!trust.Parse())
                    return debug::drop(NODE, "ACTION::AUTH: failed to parse trust register");

                /* Set the node's current trust score. */
                nTrust = trust.get<uint64_t>("trust");

                /* Set to authorized node if passed all cryptographic checks. */
                fAuthorized = true;
                debug::log(0, NODE, "ACTION::AUTH: ", hashGenesis.SubString(), " AUTHORIZATION ACCEPTED");

                PushMessage(RESPONSE::AUTHORIZED, hashGenesis);


                break;
            }


            /* Positive AUTH response. */
            case RESPONSE::AUTHORIZED:
            {
                /* Grab the genesis. */
                uint256_t hashGenesis;
                ssPacket >> hashGenesis;

                debug::log(0, NODE, "RESPONSE::AUTHORIZED: ", hashGenesis.SubString(), " AUTHORIZATION ACCEPTED");

                if(config::fClient.load())
                {
                    /* Subscribe to sig chain transactions */
                    Subscribe(SUBSCRIPTION::SIGCHAIN);

                    /* Subscribe to notifications for this genesis */
                    SubscribeNotification(hashGenesis);

                    /* Subscribe to notifications for any tokens we own, or any tokens that we have accounts for */
                    
                    /* Get the list of accounts and tokens owned by this sig chain */
                    std::vector<TAO::Register::Address> vAddresses;
                    TAO::API::ListAccounts(hashGenesis, vAddresses, true, false);

                    /* Now iterate through and find all tokens and token accounts */
                    for(const auto& hashAddress : vAddresses)
                    {
                        /* For tokens just subscribe to it */
                        if(hashAddress.IsToken())
                            SubscribeNotification(hashAddress);
                        else if(hashAddress.IsAccount())
                        {
                           /* Get the token account object. */
                            TAO::Register::Object account;
                            if(!LLD::Register->ReadState(hashAddress, account, TAO::Ledger::FLAGS::LOOKUP))
                                return debug::drop(NODE, "Token/account not found");

                            /* Parse the object register. */
                            if(!account.Parse())
                                return debug::drop(NODE, "Object failed to parse"); 

                            /* Get the token */
                            uint256_t hashToken = account.get<uint256_t>("token");

                            /* If it is not a NXS account, and we have not already subscribed to it, subscribe to it */
                            if(hashToken != 0 && std::find(vNotifications.begin(), vNotifications.end(), hashAddress) == vNotifications.end())
                                SubscribeNotification(hashAddress);
                        }
                    }
                }

                break;
            }


            /* Handle for the subscribe command. */
            case ACTION::SUBSCRIBE:
            case ACTION::UNSUBSCRIBE:
            case RESPONSE::UNSUBSCRIBED:
            {
                /* Let node know it unsubscribed successfully. */
                if(INCOMING.MESSAGE == RESPONSE::UNSUBSCRIBED)
                {
                    /* Check for unsoliced messages. */
                    if(nUnsubscribed == 0)
                        return debug::drop(NODE, "unsolicted RESPONSE::UNSUBSCRIBE");

                    /* Reset the timer. */
                    nUnsubscribed = 0;
                }

                /* Set the limits. */
                int32_t nLimits = 16;

                /* Loop through the binary stream. */
                while(!ssPacket.End() && nLimits-- > 0)
                {
                    /* Read the type. */
                    uint8_t nType = 0;
                    ssPacket >> nType;

                    /* Switch based on type. */
                    switch(nType)
                    {
                        /* Subscribe to getting blocks. */
                        case TYPES::BLOCK:
                        {
                            /* Subscribe. */
                            if(INCOMING.MESSAGE == ACTION::SUBSCRIBE)
                            {
                                /* Set the block flag. */
                                nNotifications |= SUBSCRIPTION::BLOCK;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::SUBSCRIBE: BLOCK: ", std::bitset<16>(nNotifications));
                            }
                            else if(INCOMING.MESSAGE == ACTION::UNSUBSCRIBE)
                            {
                                /* Unset the block flag. */
                                nNotifications &= ~SUBSCRIPTION::BLOCK;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::UNSUBSCRIBE::BLOCK: ", std::bitset<16>(nNotifications));
                            }
                            else
                            {
                                /* Unset the block flag. */
                                nSubscriptions &= ~SUBSCRIPTION::BLOCK;

                                /* Debug output. */
                                debug::log(3, NODE, "RESPONSE::UNSUBSCRIBED::BLOCK: ", std::bitset<16>(nSubscriptions));
                            }

                            break;
                        }

                        /* Subscribe to getting transactions. */
                        case TYPES::TRANSACTION:
                        {
                            /* Subscribe. */
                            if(INCOMING.MESSAGE == ACTION::SUBSCRIBE)
                            {
                                /* Set the block flag. */
                                nNotifications |= SUBSCRIPTION::TRANSACTION;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::SUBSCRIBE::TRANSACTION: ", std::bitset<16>(nNotifications));
                            }
                            else if(INCOMING.MESSAGE == ACTION::UNSUBSCRIBE)
                            {
                                /* Unset the transaction flag. */
                                nNotifications &= ~SUBSCRIPTION::TRANSACTION;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::UNSUBSCRIBE::TRANSACTION: ", std::bitset<16>(nNotifications));
                            }
                            else
                            {
                                /* Unset the transaction flag. */
                                nSubscriptions &= ~SUBSCRIPTION::TRANSACTION;

                                /* Debug output. */
                                debug::log(3, NODE, "RESPONSE::UNSUBSCRIBED::TRANSACTION: ", std::bitset<16>(nSubscriptions));
                            }

                            break;
                        }

                        /* Subscribe to getting best height. */
                        case TYPES::BESTHEIGHT:
                        {
                            /* Subscribe. */
                            if(INCOMING.MESSAGE == ACTION::SUBSCRIBE)
                            {
                                /* Set the best height flag. */
                                nNotifications |= SUBSCRIPTION::BESTHEIGHT;

                                /* Notify node of current block height. */
                                PushMessage(ACTION::NOTIFY,
                                    uint8_t(TYPES::BESTHEIGHT), TAO::Ledger::ChainState::nBestHeight.load());

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::SUBSCRIBE::BESTHEIGHT: ", std::bitset<16>(nNotifications));
                            }
                            else if(INCOMING.MESSAGE == ACTION::UNSUBSCRIBE)
                            {
                                /* Unset the height flag. */
                                nNotifications &= ~SUBSCRIPTION::BESTHEIGHT;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::UNSUBSCRIBE::BESTHEIGHT: ", std::bitset<16>(nNotifications));
                            }
                            else
                            {
                                /* Unset the height flag. */
                                nSubscriptions &= ~SUBSCRIPTION::BESTHEIGHT;

                                /* Debug output. */
                                debug::log(3, NODE, "RESPONSE::UNSUBSCRIBED::BESTHEIGHT: ", std::bitset<16>(nSubscriptions));
                            }

                            break;
                        }

                        /* Subscribe to getting checkpoints. */
                        case TYPES::CHECKPOINT:
                        {
                            /* Subscribe. */
                            if(INCOMING.MESSAGE == ACTION::SUBSCRIBE)
                            {
                                /* Set the checkpoints flag. */
                                nNotifications |= SUBSCRIPTION::CHECKPOINT;

                                /* Notify node of current block height. */
                                PushMessage(ACTION::NOTIFY,
                                    uint8_t(TYPES::CHECKPOINT), TAO::Ledger::ChainState::hashCheckpoint.load());

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::SUBSCRIBE::CHECKPOINT: ", std::bitset<16>(nNotifications));
                            }
                            else if(INCOMING.MESSAGE == ACTION::UNSUBSCRIBE)
                            {
                                /* Unset the checkpoints flag. */
                                nNotifications &= ~SUBSCRIPTION::CHECKPOINT;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::UNSUBSCRIBE::CHECKPOINT: ", std::bitset<16>(nNotifications));
                            }
                            else
                            {
                                /* Unset the checkpoints flag. */
                                nSubscriptions &= ~SUBSCRIPTION::CHECKPOINT;

                                /* Debug output. */
                                debug::log(3, NODE, "RESPONSE::UNSUBSCRIBED::CHECKPOINT: ", std::bitset<16>(nSubscriptions));
                            }

                            break;
                        }

                        /* Subscribe to getting addresses. */
                        case TYPES::ADDRESS:
                        {
                            /* Subscribe. */
                            if(INCOMING.MESSAGE == ACTION::SUBSCRIBE)
                            {
                                /* Set the address flag. */
                                nNotifications |= SUBSCRIPTION::ADDRESS;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::SUBSCRIBE::ADDRESS: ", std::bitset<16>(nNotifications));
                            }
                            else if(INCOMING.MESSAGE == ACTION::UNSUBSCRIBE)
                            {
                                /* Unset the address flag. */
                                nNotifications &= ~SUBSCRIPTION::ADDRESS;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::UNSUBSCRIBE::ADDRESS: ", std::bitset<16>(nNotifications));
                            }
                            else
                            {
                                /* Unset the address flag. */
                                nSubscriptions &= ~SUBSCRIPTION::ADDRESS;

                                /* Debug output. */
                                debug::log(3, NODE, "RESPONSE::UNSUBSCRIBED::ADDRESS: ", std::bitset<16>(nSubscriptions));
                            }

                            break;
                        }

                        /* Subscribe to getting last index on list commands. */
                        case TYPES::LASTINDEX:
                        {
                            /* Subscribe. */
                            if(INCOMING.MESSAGE == ACTION::SUBSCRIBE)
                            {
                                /* Check for client mode since this method should never be called except by a client. */
                                if(config::fClient.load())
                                    return debug::drop(NODE, "ACTION::SUBSCRIBE::LASTINDEX disabled in -client mode");

                                /* Set the last flag. */
                                nNotifications |= SUBSCRIPTION::LASTINDEX;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::SUBSCRIBE::LAST: ", std::bitset<16>(nNotifications));
                            }
                            else if(INCOMING.MESSAGE == ACTION::UNSUBSCRIBE)
                            {
                                /* Check for client mode since this method should never be called except by a client. */
                                if(config::fClient.load())
                                    return debug::drop(NODE, "ACTION::UNSUBSCRIBE::LASTINDEX disabled in -client mode");

                                /* Unset the last flag. */
                                nNotifications &= ~SUBSCRIPTION::LASTINDEX;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::UNSUBSCRIBE::LAST: ", std::bitset<16>(nNotifications));
                            }
                            else
                            {
                                /* Unset the last flag. */
                                nSubscriptions &= ~SUBSCRIPTION::LASTINDEX;

                                /* Debug output. */
                                debug::log(3, NODE, "RESPONSE::UNSUBSCRIBED::LASTINDEX: ", std::bitset<16>(nSubscriptions));
                            }

                            break;
                        }

                        /* Subscribe to getting transactions. */
                        case TYPES::BESTCHAIN:
                        {
                            /* Subscribe. */
                            if(INCOMING.MESSAGE == ACTION::SUBSCRIBE)
                            {
                                /* Set the best chain flag. */
                                nNotifications |= SUBSCRIPTION::BESTCHAIN;

                                /* Notify node of current block height. */
                                PushMessage(ACTION::NOTIFY,
                                    uint8_t(TYPES::BESTCHAIN), TAO::Ledger::ChainState::hashBestChain.load());

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::SUBSCRIBE::BESTCHAIN: ", std::bitset<16>(nNotifications));
                            }
                            else if(INCOMING.MESSAGE == ACTION::UNSUBSCRIBE)
                            {
                                /* Unset the bestchain flag. */
                                nNotifications &= ~SUBSCRIPTION::BESTCHAIN;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::UNSUBSCRIBE::BESTCHAIN: " , std::bitset<16>(nNotifications));
                            }
                            else
                            {
                                /* Unset the bestchain flag. */
                                nSubscriptions &= ~SUBSCRIPTION::BESTCHAIN;

                                /* Debug output. */
                                debug::log(3, NODE, "RESPONSE::UNSUBSCRIBED::BESTCHAIN: ", std::bitset<16>(nSubscriptions));
                            }

                            break;
                        }


                        /* Subscribe to getting transactions. */
                        case TYPES::SIGCHAIN:
                        {
                            /* Check for available protocol version. */
                            if(nProtocolVersion < MIN_TRITIUM_VERSION)
                                return true;

                            /* Check that node is logged in. */
                            if(!fAuthorized || hashGenesis == 0)
                                return debug::drop(NODE, "ACTION::SUBSCRIBE::SIGCHAIN: Access Denied");

                            /* Subscribe. */
                            if(INCOMING.MESSAGE == ACTION::SUBSCRIBE)
                            {
                                /* Check for client mode since this method should never be called except by a client. */
                                if(config::fClient.load())
                                    return debug::drop(NODE, "ACTION::SUBSCRIBE::SIGCHAIN disabled in -client mode");

                                /* Set the best chain flag. */
                                nNotifications |= SUBSCRIPTION::SIGCHAIN;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::SUBSCRIBE::SIGCHAIN: ", std::bitset<16>(nNotifications));
                            }
                            else if(INCOMING.MESSAGE == ACTION::UNSUBSCRIBE)
                            {
                                /* Check for client mode since this method should never be called except by a client. */
                                if(config::fClient.load())
                                    return debug::drop(NODE, "ACTION::UNSUBSCRIBE::SIGCHAIN disabled in -client mode");

                                /* Unset the bestchain flag. */
                                nNotifications &= ~SUBSCRIPTION::SIGCHAIN;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::UNSUBSCRIBE::SIGCHAIN: " , std::bitset<16>(nNotifications));
                            }
                            else
                            {
                                /* Unset the bestchain flag. */
                                nSubscriptions &= ~SUBSCRIPTION::SIGCHAIN;

                                /* Debug output. */
                                debug::log(3, NODE, "RESPONSE::UNSUBSCRIBED::SIGCHAIN: ", std::bitset<16>(nSubscriptions));
                            }

                            break;
                        }

                        /* Subscribe to getting event transcations. */
                        case TYPES::NOTIFICATION:
                        {
                            /* Check for available protocol version. */
                            if(nProtocolVersion < MIN_TRITIUM_VERSION)
                                return true;

                            /* Check that node is logged in. */
                            if(!fAuthorized || hashGenesis == 0)
                                return debug::drop(NODE, "ACTION::SUBSCRIBE::NOTIFICATION: Access Denied");

                            /* Deserialize the address for the event subscription */
                            uint256_t hashAddress;
                            ssPacket >> hashAddress;

                            /* Subscribe. */
                            if(INCOMING.MESSAGE == ACTION::SUBSCRIBE)
                            {
                                /* Check for client mode since this method should never be called except by a client. */
                                if(config::fClient.load())
                                    return debug::drop(NODE, "ACTION::SUBSCRIBE::NOTIFICATION disabled in -client mode");

                                /* Set the best chain flag. */
                                nNotifications |= SUBSCRIPTION::NOTIFICATION;

                                /* Check that peer hasn't already subscribed to too many addresses, for overflow protection */
                                if(vNotifications.size() == 10000)
                                    return debug::drop(NODE, "ACTION::SUBSCRIBE::NOTIFICATION exceeded max subscriptions");

                                /* Add the address to the notifications vector for this peer */
                                vNotifications.push_back(hashAddress);

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::SUBSCRIBE::NOTIFICATION: ", hashAddress.ToString());
                            }
                            else if(INCOMING.MESSAGE == ACTION::UNSUBSCRIBE)
                            {
                                /* Check for client mode since this method should never be called except by a client. */
                                if(config::fClient.load())
                                    return debug::drop(NODE, "ACTION::UNSUBSCRIBE::NOTIFICATION disabled in -client mode");

                                /* Unset the bestchain flag. */
                                nNotifications &= ~SUBSCRIPTION::NOTIFICATION;

                                /* Remove the address from the notifications vector for this peer */
                                vNotifications.erase(std::find(vNotifications.begin(), vNotifications.end(), hashAddress));

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::UNSUBSCRIBE::NOTIFICATION: " , hashAddress.ToString());
                            }
                            else
                            {
                                /* Unset the bestchain flag. */
                                nSubscriptions &= ~SUBSCRIPTION::NOTIFICATION;

                                /* Debug output. */
                                debug::log(3, NODE, "RESPONSE::UNSUBSCRIBED::NOTIFICATION: ", hashAddress.ToString());
                            }

                            break;
                        }

                        /* Catch unsupported types. */
                        default:
                        {
                            /* Give score for bad types. */
                            if(DDOS)
                                DDOS->rSCORE += 50;
                        }
                    }
                }

                /* Let node know it unsubscribed successfully. */
                if(INCOMING.MESSAGE == ACTION::UNSUBSCRIBE)
                    WritePacket(NewMessage(RESPONSE::UNSUBSCRIBED, ssPacket));

                break;
            }


            /* Handle for list command. */
            case ACTION::LIST:
            {
                /* Check for client mode since this method should never be called except by a client. */
                if(config::fClient.load())
                    return true; //gracefully ignore these for now since there is no current way for remote nodes to know we are in client mode

                /* Set the limits. 3000 seems to be the optimal amount to overcome higher-latency connections during sync */
                int32_t nLimits = 3001;

                /* Loop through the binary stream. */
                while(!ssPacket.End() && nLimits != 0)
                {
                    /* Get the next type in stream. */
                    uint8_t nType = 0;
                    ssPacket >> nType;

                    /* Check for legacy or transactions specifiers. */
                    bool fLegacy = false, fTransactions = false, fSyncBlock = false, fClientBlock = false;
                    if(nType == SPECIFIER::LEGACY || nType == SPECIFIER::TRANSACTIONS
                    || nType == SPECIFIER::SYNC   || nType == SPECIFIER::CLIENT)
                    {
                        /* Set specifiers. */
                        fLegacy       = (nType == SPECIFIER::LEGACY);
                        fTransactions = (nType == SPECIFIER::TRANSACTIONS);
                        fSyncBlock    = (nType == SPECIFIER::SYNC);
                        fClientBlock  = (nType == SPECIFIER::CLIENT);

                        /* Go to next type in stream. */
                        ssPacket >> nType;
                    }

                    /* Switch based on codes. */
                    switch(nType)
                    {
                        /* Standard type for a block. */
                        case TYPES::BLOCK:
                        {
                            /* Get the index of block. */
                            uint1024_t hashStart;

                            /* Get the object type. */
                            uint8_t nObject = 0;
                            ssPacket >> nObject;

                            /* Switch based on object. */
                            switch(nObject)
                            {
                                /* Check for start from uint1024 type. */
                                case TYPES::UINT1024_T:
                                {
                                    /* Deserialize start. */
                                    ssPacket >> hashStart;

                                    break;
                                }

                                /* Check for start from a locator. */
                                case TYPES::LOCATOR:
                                {
                                    /* Deserialize locator. */
                                    TAO::Ledger::Locator locator;
                                    ssPacket >> locator;

                                    /* Check locator size. */
                                    uint32_t nSize = locator.vHave.size();
                                    if(nSize > 30)
                                        return debug::drop(NODE, "locator size ", nSize, " is too large");

                                    /* Find common ancestor block. */
                                    for(const auto& have : locator.vHave)
                                    {
                                        /* Check the database for the ancestor block. */
                                        if(LLD::Ledger->HasBlock(have))
                                        {
                                            /* Check if locator found genesis. */
                                            if(have != TAO::Ledger::ChainState::Genesis())
                                            {
                                                /* Grab the block that's found. */
                                                TAO::Ledger::BlockState state;
                                                if(!LLD::Ledger->ReadBlock(have, state))
                                                    return debug::drop(NODE, "failed to read locator block");

                                                /* Check for being in main chain. */
                                                if(!state.IsInMainChain())
                                                    continue;

                                                hashStart = state.hashPrevBlock;
                                            }
                                            else //on genesis, don't rever to previous block
                                                hashStart = have;

                                            break;
                                        }
                                    }

                                    /* Debug output. */
                                    if(config::nVerbose >= 3)
                                        debug::log(3, NODE, "ACTION::LIST: Locator ", hashStart.SubString(), " found");

                                    break;
                                }

                                default:
                                    return debug::drop(NODE, "malformed starting index");
                            }

                            /* Get the ending hash. */
                            uint1024_t hashStop;
                            ssPacket >> hashStop;

                            /* Keep track of the last state. */
                            TAO::Ledger::BlockState stateLast;
                            if(!LLD::Ledger->ReadBlock(hashStart, stateLast))
                                return debug::drop(NODE, "failed to read starting block");

                            /* Do a sequential read to obtain the list.
                               3000 seems to be the optimal amount to overcome higher-latency connections during sync */
                            std::vector<TAO::Ledger::BlockState> vStates;
                            while(!fBufferFull.load() && --nLimits >= 0 && hashStart != hashStop && LLD::Ledger->BatchRead(hashStart, "block", vStates, 3000, true))
                            {
                                /* Loop through all available states. */
                                for(auto& state : vStates)
                                {
                                    /* Update start every iteration. */
                                    hashStart = state.GetHash();

                                    /* Skip if not in main chain. */
                                    if(!state.IsInMainChain())
                                        continue;

                                    /* Check for matching hashes. */
                                    if(state.hashPrevBlock != stateLast.GetHash())
                                    {
                                        if(config::nVerbose >= 3)
                                            debug::log(3, FUNCTION, "Reading block ", stateLast.hashNextBlock.SubString());

                                        /* Read the correct block from next index. */
                                        if(!LLD::Ledger->ReadBlock(stateLast.hashNextBlock, state))
                                        {
                                            nLimits = 0;
                                            break;
                                        }

                                        /* Update hashStart. */
                                        hashStart = state.GetHash();
                                    }

                                    /* Cache the block hash. */
                                    stateLast = state;

                                    /* Handle for special sync block type specifier. */
                                    if(fSyncBlock)
                                    {
                                        /* Build the sync block from state. */
                                        TAO::Ledger::SyncBlock block(state);

                                        /* Push message in response. */
                                        PushMessage(TYPES::BLOCK, uint8_t(SPECIFIER::SYNC), block);
                                    }

                                    /* Handle for a client block header. */
                                    else if(fClientBlock)
                                    {
                                        /* Build the client block from state. */
                                        TAO::Ledger::ClientBlock block(state);

                                        /* Push message in response. */
                                        PushMessage(TYPES::BLOCK, uint8_t(SPECIFIER::CLIENT), block);
                                    }
                                    else
                                    {
                                        /* Check for version to send correct type */
                                        if(state.nVersion < 7)
                                        {
                                            /* Build the legacy block from state. */
                                            Legacy::LegacyBlock block(state);

                                            /* Push message in response. */
                                            PushMessage(TYPES::BLOCK, uint8_t(SPECIFIER::LEGACY), block);
                                        }
                                        else
                                        {
                                            /* Build the legacy block from state. */
                                            TAO::Ledger::TritiumBlock block(state);

                                            /* Check for transactions. */
                                            if(fTransactions)
                                            {
                                                /* Loop through transactions. */
                                                for(const auto& proof : block.vtx)
                                                {
                                                    /* Basic checks for legacy transactions. */
                                                    if(proof.first == TAO::Ledger::TRANSACTION::LEGACY)
                                                    {
                                                        /* Check the memory pool. */
                                                        Legacy::Transaction tx;
                                                        if(!LLD::Legacy->ReadTx(proof.second, tx, TAO::Ledger::FLAGS::MEMPOOL))
                                                            continue;

                                                        /* Push message of transaction. */
                                                        PushMessage(TYPES::TRANSACTION, uint8_t(SPECIFIER::LEGACY), tx);
                                                    }

                                                    /* Basic checks for tritium transactions. */
                                                    else if(proof.first == TAO::Ledger::TRANSACTION::TRITIUM)
                                                    {
                                                        /* Check the memory pool. */
                                                        TAO::Ledger::Transaction tx;
                                                        if(!LLD::Ledger->ReadTx(proof.second, tx, TAO::Ledger::FLAGS::MEMPOOL))
                                                            continue;


                                                        /* Push message of transaction. */
                                                        PushMessage(TYPES::TRANSACTION, uint8_t(SPECIFIER::TRITIUM), tx);
                                                    }
                                                }
                                            }

                                            /* Push message in response. */
                                            PushMessage(TYPES::BLOCK, uint8_t(SPECIFIER::TRITIUM), block);
                                        }
                                    }

                                    /* Check for stop hash. */
                                    if(--nLimits <= 0 || hashStart == hashStop || fBufferFull.load()) //1MB limit
                                    {
                                        /* Regular debug for normal limits */
                                        if(config::nVerbose >= 3)
                                        {
                                            /* Special message for full write buffers. */
                                            if(fBufferFull.load())
                                                debug::log(3, FUNCTION, "Buffer is FULL ", Buffered(), " bytes");

                                            debug::log(3, FUNCTION, "Limits ", nLimits, " Reached ", hashStart.SubString(), " == ", hashStop.SubString());
                                        }

                                        break;
                                    }
                                }
                            }

                            /* Check for last subscription. */
                            if(nNotifications & SUBSCRIPTION::LASTINDEX)
                                PushMessage(ACTION::NOTIFY, uint8_t(TYPES::LASTINDEX), uint8_t(TYPES::BLOCK), fBufferFull.load() ? stateLast.hashPrevBlock : hashStart);

                            break;
                        }

                        /* Standard type for a block. */
                        case TYPES::TRANSACTION:
                        {
                            /* Get the index of block. */
                            uint512_t hashStart;
                            ssPacket >> hashStart;

                            /* Get the ending hash. */
                            uint512_t hashStop;
                            ssPacket >> hashStop;

                            /* Check for invalid specifiers. */
                            if(fTransactions)
                                return debug::drop(NODE, "cannot use SPECIFIER::TRANSACTIONS for transaction lists");

                            /* Check for invalid specifiers. */
                            if(fSyncBlock)
                                return debug::drop(NODE, "cannot use SPECIFIER::SYNC for transaction lists");

                            /* Check for legacy. */
                            if(fLegacy)
                            {
                                /* Do a sequential read to obtain the list. */
                                std::vector<Legacy::Transaction> vtx;
                                while(LLD::Legacy->BatchRead(hashStart, "tx", vtx, 100))
                                {
                                    /* Loop through all available states. */
                                    for(const auto& tx : vtx)
                                    {
                                        /* Get a copy of the hash. */
                                        uint512_t hash = tx.GetHash();

                                        /* Check if indexed. */
                                        if(!LLD::Ledger->HasIndex(hash))
                                            continue;

                                        /* Cache the block hash. */
                                        hashStart = hash;

                                        /* Push the transaction. */
                                        PushMessage(TYPES::TRANSACTION, uint8_t(SPECIFIER::LEGACY), tx);

                                        /* Check for stop hash. */
                                        if(--nLimits == 0 || hashStart == hashStop || fBufferFull.load())
                                            break;
                                    }

                                    /* Check for stop or limits. */
                                    if(nLimits == 0 || hashStart == hashStop || fBufferFull.load())
                                        break;
                                }
                            }
                            else
                            {

                                /* Do a sequential read to obtain the list. */
                                std::vector<TAO::Ledger::Transaction> vtx;
                                while(LLD::Ledger->BatchRead(hashStart, "tx", vtx, 100))
                                {
                                    /* Loop through all available states. */
                                    for(const auto& tx : vtx)
                                    {
                                        /* Get a copy of the hash. */
                                        uint512_t hash = tx.GetHash();

                                        /* Check if indexed. */
                                        if(!LLD::Ledger->HasIndex(hash))
                                            continue;

                                        /* Cache the block hash. */
                                        hashStart = hash;

                                        /* Push the transaction. */
                                        PushMessage(TYPES::TRANSACTION, uint8_t(SPECIFIER::TRITIUM), tx);

                                        /* Check for stop hash. */
                                        if(--nLimits == 0 || hashStart == hashStop || fBufferFull.load())
                                            break;
                                    }

                                    /* Check for stop or limits. */
                                    if(nLimits == 0 || hashStart == hashStop || fBufferFull.load())
                                        break;
                                }
                            }

                            break;
                        }


                        /* Standard type for a block. */
                        case TYPES::ADDRESS:
                        {
                            /* Get the total list amount. */
                            uint32_t nTotal;
                            ssPacket >> nTotal;

                            /* Check for size constraints. */
                            if(nTotal > 10000)
                            {
                                /* Give penalties for size violation. */
                                if(DDOS)
                                    DDOS->rSCORE += 20;

                                /* Set value to max range. */
                                nTotal = 10000;
                            }

                            /* Get addresses from manager. */
                            std::vector<BaseAddress> vAddr;
                            if(TRITIUM_SERVER->GetAddressManager())
                                TRITIUM_SERVER->GetAddressManager()->GetAddresses(vAddr);

                            /* Add the best 1000 (or less) addresses. */
                            const uint32_t nCount = std::min((uint32_t)vAddr.size(), nTotal);
                            for(uint32_t n = 0; n < nCount; ++n)
                                PushMessage(TYPES::ADDRESS, vAddr[n]);

                            break;
                        }


                        /* Standard type for a block. */
                        case TYPES::MEMPOOL:
                        {
                            /* Get a list of transactions from mempool. */
                            std::vector<uint512_t> vHashes;

                            /* List tritium transactions if legacy isn't specified. */
                            if(!fLegacy)
                            {
                                if(TAO::Ledger::mempool.List(vHashes, std::numeric_limits<uint32_t>::max(), false))
                                {
                                    /* Loop through the available hashes. */
                                    for(const auto& hash : vHashes)
                                    {
                                        /* Get the transaction from memory pool. */
                                        TAO::Ledger::Transaction tx;
                                        if(!TAO::Ledger::mempool.Get(hash, tx))
                                            break; //we don't want to add more dependants if this fails

                                        /* Push the transaction. */
                                        PushMessage(TYPES::TRANSACTION, uint8_t(SPECIFIER::TRITIUM), tx);
                                    }
                                }
                            }

                            /* Get a list of legacy transactions from pool. */
                            vHashes.clear();
                            if(TAO::Ledger::mempool.List(vHashes, std::numeric_limits<uint32_t>::max(), true))
                            {
                                /* Loop through the available hashes. */
                                for(const auto& hash : vHashes)
                                {
                                    /* Get the transaction from memory pool. */
                                    Legacy::Transaction tx;
                                    if(!TAO::Ledger::mempool.Get(hash, tx))
                                        break; //we don't want to add more dependants if this fails

                                    /* Push the transaction. */
                                    PushMessage(TYPES::TRANSACTION, uint8_t(SPECIFIER::LEGACY), tx);
                                }
                            }

                            break;
                        }


                        /* Standard type for a sigchain listing. */
                        case TYPES::SIGCHAIN:
                        {
                            /* Check for available protocol version. */
                            if(nProtocolVersion < MIN_TRITIUM_VERSION)
                                return true;

                            /* Get the sigchain-id. */
                            uint256_t hashSigchain;
                            ssPacket >> hashSigchain;

                            /* Get the index of block. */
                            uint512_t hashStart;
                            ssPacket >> hashStart;

                            /* Check for empty hash start. */
                            bool fGenesis = (hashStart == 0);
                            if(hashStart == 0 && !LLD::Ledger->ReadGenesis(hashSigchain, hashStart))
                                break;

                            /* Check for empty hash stop. */
                            uint512_t hashThis;
                            if(hashThis == 0 && !LLD::Ledger->ReadLast(hashSigchain, hashThis, TAO::Ledger::FLAGS::MEMPOOL))
                                break;

                            /* Read sigchain entries. */
                            std::vector<TAO::Ledger::MerkleTx> vtx;
                            while(!config::fShutdown.load())
                            {
                                /* Check for genesis. */
                                if(!fGenesis && hashStart == hashThis)
                                    break;

                                /* Read from disk. */
                                TAO::Ledger::Transaction tx;
                                if(!LLD::Ledger->ReadTx(hashThis, tx, TAO::Ledger::FLAGS::MEMPOOL))
                                    break;

                                /* Build a markle transaction. */
                                TAO::Ledger::MerkleTx merkle = TAO::Ledger::MerkleTx(tx);

                                /* Build the merkle branch if the tx has been confirmed (i.e. it is not in the mempool) */
                                if(!TAO::Ledger::mempool.Has(hashThis))
                                    merkle.BuildMerkleBranch();

                                /* Insert into container. */
                                vtx.push_back(merkle);

                                /* Check for genesis. */
                                if(fGenesis && hashStart == hashThis)
                                    break;

                                hashThis = tx.hashPrevTx;
                            }

                            /* Reverse container to message forward. */
                            for(auto tx = vtx.rbegin(); tx != vtx.rend(); ++tx)
                                PushMessage(TYPES::MERKLE, uint8_t(SPECIFIER::TRITIUM), (*tx));

                            break;
                        }


                        /* Standard type for a sigchain listing. */
                        case TYPES::NOTIFICATION:
                        {
                            /* Check for available protocol version. */
                            if(nProtocolVersion < MIN_TRITIUM_VERSION)
                                return true;

                            /* Get the sigchain-id. */
                            uint256_t hashSigchain;
                            ssPacket >> hashSigchain;

                            /* Get the last event */
                            debug::log(1, "ACTION::LIST: ", fLegacy ? "LEGACY " : "", "NOTIFICATION for ", hashSigchain.SubString());

                            /* Check for legacy. */
                            uint32_t nSequence = 0;
                            if(fLegacy)
                            {
                                std::vector<Legacy::MerkleTx> vtx;
                                LLD::Legacy->ReadSequence(hashSigchain, nSequence);

                                /* Look back through all events to find those that are not yet processed. */
                                Legacy::Transaction tx;
                                while(LLD::Legacy->ReadEvent(hashSigchain, --nSequence, tx))
                                {
                                    /* Build a markle transaction. */
                                    Legacy::MerkleTx merkle = Legacy::MerkleTx(tx);
                                    merkle.BuildMerkleBranch();

                                    /* Insert into container. */
                                    vtx.push_back(merkle);
                                }

                                /* Reverse container to message forward. */
                                for(auto tx = vtx.rbegin(); tx != vtx.rend(); ++tx)
                                    PushMessage(TYPES::MERKLE, uint8_t(SPECIFIER::LEGACY), (*tx));
                            }
                            else
                            {
                                std::vector<TAO::Ledger::MerkleTx> vtx;
                                if(!LLD::Ledger->ReadSequence(hashSigchain, nSequence))
                                    nSequence = 0;

                                /* Look back through all events to find those that are not yet processed. */
                                TAO::Ledger::Transaction tx;
                                while(LLD::Ledger->ReadEvent(hashSigchain, --nSequence, tx))
                                {
                                    /* Build a markle transaction. */
                                    TAO::Ledger::MerkleTx merkle = TAO::Ledger::MerkleTx(tx);
                                    merkle.BuildMerkleBranch();

                                    /* Insert into container. */
                                    vtx.push_back(merkle);
                                }

                                /* Reverse container to message forward. */
                                for(auto tx = vtx.rbegin(); tx != vtx.rend(); ++tx)
                                    PushMessage(TYPES::MERKLE, uint8_t(SPECIFIER::TRITIUM), (*tx));
                            }



                            break;
                        }


                        /* Catch malformed notify binary streams. */
                        default:
                            return debug::drop(NODE, "ACTION::LIST malformed binary stream");
                    }
                }

                /* Check for trigger nonce. */
                if(nTriggerNonce != 0)
                {
                    PushMessage(RESPONSE::COMPLETED, nTriggerNonce);
                    nTriggerNonce = 0;
                }

                break;
            }


            /* Handle for get command. */
            case ACTION::GET:
            {
                /* Loop through the binary stream. 3000 seems to be the optimal amount to overcome higher-latency connections during sync */
                int32_t nLimits = 3000;
                while(!ssPacket.End() && --nLimits > 0)
                {
                    /* Get the next type in stream. */
                    uint8_t nType = 0;
                    ssPacket >> nType;

                    /* Check for legacy or transactions specifiers. */
                    bool fLegacy = false, fPoolstake = false, fTransactions = false, fClient = false;
                    if(nType == SPECIFIER::LEGACY || nType == SPECIFIER::POOLSTAKE
                    || nType == SPECIFIER::TRANSACTIONS || nType == SPECIFIER::CLIENT)
                    {
                        /* Set specifiers. */
                        fLegacy       = (nType == SPECIFIER::LEGACY);
                        fPoolstake    = (nType == SPECIFIER::POOLSTAKE);
                        fTransactions = (nType == SPECIFIER::TRANSACTIONS);
                        fClient       = (nType == SPECIFIER::CLIENT);

                        /* Go to next type in stream. */
                        ssPacket >> nType;
                    }

                    /* Switch based on codes. */
                    switch(nType)
                    {
                        /* Standard type for a block. */
                        case TYPES::BLOCK:
                        {
                            /* Check for valid specifier. */
                            if(fLegacy || fPoolstake)
                                return debug::drop(NODE, "ACTION::GET: invalid specifier for TYPES::BLOCK");

                            /* Check for client mode since this method should never be called except by a client. */
                            if(config::fClient.load())
                                return debug::drop(NODE, "ACTION::GET::BLOCK disabled in -client mode");

                            /* Get the index of block. */
                            uint1024_t hashBlock;
                            ssPacket >> hashBlock;

                            /* Check the database for the block. */
                            TAO::Ledger::BlockState state;
                            if(LLD::Ledger->ReadBlock(hashBlock, state))
                            {
                                /* Push legacy blocks for less than version 7. */
                                if(state.nVersion < 7)
                                {
                                    /* Check for bad client requests. */
                                    if(fClient)
                                        return debug::drop(NODE, "ACTION::GET: CLIENT specifier disabled for legacy blocks");

                                    /* Build legacy block from state. */
                                    Legacy::LegacyBlock block(state);

                                    /* Push block as response. */
                                    PushMessage(TYPES::BLOCK, uint8_t(SPECIFIER::LEGACY), block);
                                }
                                else
                                {
                                    /* Handle for client blocks. */
                                    if(fClient)
                                    {
                                        /* Build the client block and send off. */
                                        TAO::Ledger::ClientBlock block(state);

                                        /* Push the new client block. */
                                        PushMessage(TYPES::BLOCK, uint8_t(SPECIFIER::CLIENT), block);

                                        /* Debug output. */
                                        debug::log(3, NODE, "ACTION::GET: CLIENT::BLOCK ", hashBlock.SubString());

                                        break;
                                    }

                                    /* Build tritium block from state. */
                                    TAO::Ledger::TritiumBlock block(state);

                                    /* Check for transactions. */
                                    if(fTransactions)
                                    {
                                        /* Loop through transactions. */
                                        for(const auto& proof : block.vtx)
                                        {
                                            /* Basic checks for legacy transactions. */
                                            if(proof.first == TAO::Ledger::TRANSACTION::LEGACY)
                                            {
                                                /* Check the memory pool. */
                                                Legacy::Transaction tx;
                                                if(!LLD::Legacy->ReadTx(proof.second, tx, TAO::Ledger::FLAGS::MEMPOOL))
                                                    continue;

                                                /* Push message of transaction. */
                                                PushMessage(TYPES::TRANSACTION, uint8_t(SPECIFIER::LEGACY), tx);
                                            }

                                            /* Basic checks for tritium transactions. */
                                            else if(proof.first == TAO::Ledger::TRANSACTION::TRITIUM)
                                            {
                                                /* Check the memory pool. */
                                                TAO::Ledger::Transaction tx;
                                                if(!LLD::Ledger->ReadTx(proof.second, tx, TAO::Ledger::FLAGS::MEMPOOL))
                                                    continue;


                                                /* Push message of transaction. */
                                                PushMessage(TYPES::TRANSACTION, uint8_t(SPECIFIER::TRITIUM), tx);
                                            }
                                        }
                                    }

                                    /* Push block as response. */
                                    PushMessage(TYPES::BLOCK, uint8_t(SPECIFIER::TRITIUM), block);
                                }
                            }

                            /* Debug output. */
                            debug::log(3, NODE, "ACTION::GET: BLOCK ", hashBlock.SubString());

                            break;
                        }

                        /* Standard type for a transaction. */
                        case TYPES::TRANSACTION:
                        {
                            /* Check for valid specifier. */
                            if(fTransactions || fClient)
                                return debug::drop(NODE, "ACTION::GET::TRANSACTION: invalid specifier for TYPES::TRANSACTION");

                            /* Get the index of transaction. */
                            uint512_t hashTx;
                            ssPacket >> hashTx;

                            /* Check for legacy. */
                            if(fLegacy)
                            {
                                /* Check for client mode since this method should never be except by a client. */
                                if(config::fClient.load())
                                    return debug::drop(NODE, "ACTION::GET::LEGACY::TRANSACTION disabled in -client mode");

                                /* Check legacy database. */
                                Legacy::Transaction tx;
                                if(LLD::Legacy->ReadTx(hashTx, tx, TAO::Ledger::FLAGS::MEMPOOL))
                                    PushMessage(TYPES::TRANSACTION, uint8_t(SPECIFIER::LEGACY), tx);
                            }
                            /* Check for poolstake. */
                            else if(fPoolstake)
                            {
                                /* Check for poolstake specifier active */
                                if(nProtocolVersion < MIN_TRITIUM_VERSION)
                                    return debug::drop(NODE, "ACTION::GET: poolstake specifier not active");

                                uint8_t nTTL;
                                ssPacket >> nTTL;

                                /* Check stake pool for pooled coinstake. */
                                TAO::Ledger::Transaction tx;
                                if(TAO::Ledger::stakepool.Get(hashTx, tx))
                                    PushMessage(TYPES::TRANSACTION, uint8_t(SPECIFIER::POOLSTAKE), uint8_t(nTTL), tx);
                            }
                            else
                            {
                                /* Check ledger database. */
                                TAO::Ledger::Transaction tx;
                                if(LLD::Ledger->ReadTx(hashTx, tx, TAO::Ledger::FLAGS::MEMPOOL))
                                {
                                    /* Check if producer is being asked for, and send block instead. */
                                    if(tx.IsCoinBase() || tx.IsCoinStake() || tx.IsPrivate())
                                    {
                                        /* Read block state from disk. */
                                        TAO::Ledger::BlockState state;
                                        if(LLD::Ledger->ReadBlock(hashTx, state))
                                        {
                                            /* Send off tritium block. */
                                            TAO::Ledger::TritiumBlock block(state);
                                            PushMessage(TYPES::BLOCK, uint8_t(SPECIFIER::TRITIUM), block);
                                        }
                                    }
                                    else
                                        PushMessage(TYPES::TRANSACTION, uint8_t(SPECIFIER::TRITIUM), tx);
                                }

                            }

                            /* Debug output. */
                            debug::log(3, NODE, "ACTION::GET: TRANSACTION ", hashTx.SubString());

                            break;
                        }


                        /* Standard type for a merkle transaction. */
                        case TYPES::MERKLE:
                        {
                            /* Check for available protocol version. */
                            if(nProtocolVersion < MIN_TRITIUM_VERSION)
                                return true;

                            /* Check for valid specifier. */
                            if(fTransactions || fClient || fLegacy)
                                return debug::drop(NODE, "ACTION::GET::MERKLE: invalid specifier for TYPES::MERKLE");

                            /* Get the index of transaction. */
                            uint512_t hashTx;
                            ssPacket >> hashTx;

                            /* Check ledger database. */
                            TAO::Ledger::Transaction tx;
                            if(LLD::Ledger->ReadTx(hashTx, tx, TAO::Ledger::FLAGS::MEMPOOL))
                            {
                                /* Build a markle transaction. */
                                TAO::Ledger::MerkleTx merkle = TAO::Ledger::MerkleTx(tx);

                                /* Build the merkle branch if the tx has been confirmed (i.e. it is not in the mempool) */
                                if(!TAO::Ledger::mempool.Has(hashTx))
                                    merkle.BuildMerkleBranch();

                                PushMessage(TYPES::MERKLE, uint8_t(SPECIFIER::TRITIUM), merkle);
                            }

                            /* Debug output. */
                            debug::log(3, NODE, "ACTION::GET: MERKLE TRANSACTION ", hashTx.SubString());

                            break;
                        }


                        /* Standard type for a genesis transaction. */
                        case TYPES::GENESIS:
                        {
                            /* Check for available protocol version. */
                            if(nProtocolVersion < MIN_TRITIUM_VERSION)
                                return true;

                            /* Check for valid specifier. */
                            if(fTransactions || fClient || fLegacy)
                                return debug::drop(NODE, "ACTION::GET::GENESIS: invalid specifier for TYPES::GENESIS");

                            /* Get the index of transaction. */
                            uint256_t hashGenesis;
                            ssPacket >> hashGenesis;

                            /* Get the genesis txid. */
                            uint512_t hashTx;
                            if(LLD::Ledger->ReadGenesis(hashGenesis, hashTx))
                            {
                                TAO::Ledger::Transaction tx;
                                if(LLD::Ledger->ReadTx(hashTx, tx, TAO::Ledger::FLAGS::MEMPOOL))
                                {
                                    /* Build a markle transaction. */
                                    TAO::Ledger::MerkleTx merkle = TAO::Ledger::MerkleTx(tx);

                                    /* Build the merkle branch if the tx has been confirmed (i.e. it is not in the mempool) */
                                    if(!TAO::Ledger::mempool.Has(hashTx))
                                        merkle.BuildMerkleBranch();

                                    PushMessage(TYPES::MERKLE, uint8_t(SPECIFIER::TRITIUM), merkle);
                                }
                            }

                            /* Debug output. */
                            debug::log(3, NODE, "ACTION::GET: GENESIS TRANSACTION ", hashTx.SubString());

                            break;
                        }


                        /* Standard type for last sigchain transaction. */
                        case TYPES::SIGCHAIN:
                        {
                            /* Check for available protocol version. */
                            if(nProtocolVersion < MIN_TRITIUM_VERSION)
                                return true;

                            /* Check for valid specifier. */
                            if(fTransactions || fClient || fLegacy)
                                return debug::drop(NODE, "ACTION::GET::SIGCHAIN: invalid specifier for TYPES::SIGCHAIN");

                            /* Get the index of transaction. */
                            uint256_t hashGenesis;
                            ssPacket >> hashGenesis;

                            /* Get the genesis txid. */
                            uint512_t hashTx;
                            if(LLD::Ledger->ReadLast(hashGenesis, hashTx))
                            {
                                TAO::Ledger::Transaction tx;
                                if(LLD::Ledger->ReadTx(hashTx, tx, TAO::Ledger::FLAGS::MEMPOOL))
                                {
                                    /* Build a markle transaction. */
                                    TAO::Ledger::MerkleTx merkle = TAO::Ledger::MerkleTx(tx);

                                    /* Build the merkle branch if the tx has been confirmed (i.e. it is not in the mempool) */
                                    if(!TAO::Ledger::mempool.Has(hashTx))
                                        merkle.BuildMerkleBranch();

                                    PushMessage(TYPES::MERKLE, uint8_t(SPECIFIER::TRITIUM), merkle);
                                }
                            }

                            /* Debug output. */
                            debug::log(3, NODE, "ACTION::GET: SIGCHAIN TRANSACTION ", hashTx.SubString());

                            break;
                        }


                        /* Standard type for last sigchain transaction. */
                        case TYPES::REGISTER:
                        {
                            /* Check for available protocol version. */
                            if(nProtocolVersion < MIN_TRITIUM_VERSION)
                                return true;

                            /* Check for valid specifier. */
                            if(fTransactions || fClient || fLegacy)
                                return debug::drop(NODE, "ACTION::GET::REGISTER: invalid specifier for TYPES::REGISTER");

                            /* Get the index of transaction. */
                            uint256_t hashRegister;
                            ssPacket >> hashRegister;

                            /* Check for existing localdb indexes. */
                            std::pair<uint512_t, uint64_t> pairIndex;
                            if(LLD::Local->ReadIndex(hashRegister, pairIndex))
                            {
                                /* Check for cache expiration. */
                                if(runtime::unifiedtimestamp() <= pairIndex.second)
                                {
                                    /* Get the transaction from disk. */
                                    TAO::Ledger::Transaction tx;
                                    if(!LLD::Ledger->ReadTx(pairIndex.first, tx, TAO::Ledger::FLAGS::MEMPOOL))
                                        break;

                                    /* Build a markle transaction. */
                                    TAO::Ledger::MerkleTx merkle = TAO::Ledger::MerkleTx(tx);

                                    /* Build the merkle branch if the tx has been confirmed (i.e. it is not in the mempool) */
                                    if(!TAO::Ledger::mempool.Has(pairIndex.first))
                                        merkle.BuildMerkleBranch();

                                    /* Send off the transaction to remote node. */
                                    PushMessage(TYPES::MERKLE, uint8_t(SPECIFIER::TRITIUM), merkle);

                                    debug::log(0, NODE, "ACTION::GET: Using INDEX CACHE for ", hashRegister.SubString());

                                    break;
                                }
                            }

                            /* Get the register from disk. */
                            TAO::Register::State state;
                            if(!LLD::Register->ReadState(hashRegister, state, TAO::Ledger::FLAGS::MEMPOOL))
                                break;

                            /* Make adjustment to history check and detect if the register is owned by system. */
                            uint256_t hashOwner = state.hashOwner;
                            if(hashOwner.GetType() == TAO::Ledger::GENESIS::SYSTEM)
                                hashOwner.SetType(TAO::Ledger::GenesisType());

                            /* Read the last hash of owner. */
                            uint512_t hashLast = 0;
                            if(!LLD::Ledger->ReadLast(hashOwner, hashLast, TAO::Ledger::FLAGS::MEMPOOL))
                                break;

                            /* Iterate through sigchain for register updates. */
                            while(hashLast != 0)
                            {
                                /* Get the transaction from disk. */
                                TAO::Ledger::Transaction tx;
                                if(!LLD::Ledger->ReadTx(hashLast, tx, TAO::Ledger::FLAGS::MEMPOOL))
                                    break;

                                /* Handle DDOS. */
                                if(fDDOS && DDOS)
                                    DDOS->rSCORE += 1;

                                /* Set the next last. */
                                hashLast = !tx.IsFirst() ? tx.hashPrevTx : 0;

                                /* Check through all the contracts. */
                                for(int32_t nContract = tx.Size() - 1; nContract >= 0; --nContract)
                                {
                                    /* Get the contract. */
                                    const TAO::Operation::Contract& contract = tx[nContract];

                                    /* Reset the operation stream position in case it was loaded from mempool and therefore still in previous state */
                                    contract.Reset();

                                    /* Get the operation byte. */
                                    uint8_t OPERATION = 0;
                                    contract >> OPERATION;

                                    /* Check for conditional OP */
                                    switch(OPERATION)
                                    {
                                        case TAO::Operation::OP::VALIDATE:
                                        {
                                            /* Seek through validate. */
                                            contract.Seek(68);
                                            contract >> OPERATION;

                                            break;
                                        }

                                        case TAO::Operation::OP::CONDITION:
                                        {
                                            /* Get new operation. */
                                            contract >> OPERATION;
                                        }
                                    }

                                    /* Check for key operations. */
                                    switch(OPERATION)
                                    {
                                        /* Break when at the register declaration. */
                                        case TAO::Operation::OP::WRITE:
                                        case TAO::Operation::OP::CREATE:
                                        case TAO::Operation::OP::APPEND:
                                        case TAO::Operation::OP::CLAIM:
                                        case TAO::Operation::OP::DEBIT:
                                        case TAO::Operation::OP::CREDIT:
                                        case TAO::Operation::OP::TRUST:
                                        case TAO::Operation::OP::GENESIS:
                                        case TAO::Operation::OP::LEGACY:
                                        case TAO::Operation::OP::FEE:
                                        {
                                            /* Seek past claim txid. */
                                            if(OPERATION == TAO::Operation::OP::CLAIM ||
                                               OPERATION == TAO::Operation::OP::CREDIT)
                                                contract.Seek(68);

                                            /* Extract the address from the contract. */
                                            TAO::Register::Address hashAddress;
                                            if(OPERATION == TAO::Operation::OP::TRUST ||
                                               OPERATION == TAO::Operation::OP::GENESIS)
                                            {
                                                hashAddress =
                                                    TAO::Register::Address(std::string("trust"), state.hashOwner, TAO::Register::Address::TRUST);
                                            }
                                            else
                                                contract >> hashAddress;

                                            /* Check for same address. */
                                            if(hashAddress != hashRegister)
                                                break;

                                            /* Build a markle transaction. */
                                            TAO::Ledger::MerkleTx merkle = TAO::Ledger::MerkleTx(tx);

                                            /* Build the merkle branch if the tx has been confirmed (i.e. it is not in the mempool) */
                                            if(!TAO::Ledger::mempool.Has(hashLast))
                                                merkle.BuildMerkleBranch();

                                            /* Send off the transaction to remote node. */
                                            PushMessage(TYPES::MERKLE, uint8_t(SPECIFIER::TRITIUM), merkle);

                                            /* Build indexes for optimized processing. */
                                            debug::log(0, NODE, "ACTION::GET: Update INDEX for register ", hashAddress.SubString());
                                            std::pair<uint512_t, uint64_t> pairIndex = std::make_pair(tx.GetHash(), runtime::unifiedtimestamp() + 3600);
                                            if(!LLD::Local->WriteIndex(hashAddress, pairIndex)) //Index expires 1 hour after created
                                                break;

                                            /* Break out of main hash last. */
                                            hashLast = 0;

                                            break;
                                        }

                                        default:
                                            continue;
                                    }
                                }
                            }

                            /* Debug output. */
                            debug::log(3, NODE, "ACTION::GET: REGISTER ", hashRegister.SubString());

                            break;
                        }

                        case TYPES::PEERADDRESS:
                        {
                            /* Send back the peer's own address from our connection */
                            PushMessage(TYPES::PEERADDRESS, addr);
                            break;
                        }

                        /* Catch malformed notify binary streams. */
                        default:
                            return debug::drop(NODE, "ACTION::GET malformed binary stream");
                    }
                }

                /* Check for trigger nonce. */
                if(nTriggerNonce != 0)
                {
                    PushMessage(RESPONSE::COMPLETED, nTriggerNonce);
                    nTriggerNonce = 0;
                }

                break;
            }


            /* Handle for notify command. */
            case ACTION::NOTIFY:
            {
                /* Create response data stream. */
                DataStream ssResponse(SER_NETWORK, PROTOCOL_VERSION);

                /* Loop through the binary stream.
                   3000 seems to be the optimal amount to overcome higher-latency connections during sync */
                int32_t nLimits = 3000;
                while(!ssPacket.End() && --nLimits > 0)
                {
                    /* Get the next type in stream. */
                    uint8_t nType = 0;
                    ssPacket >> nType;

                    /* Check for legacy or poolstake specifier. */
                    bool fLegacy = false;
                    bool fPoolstake = false;
                    if(nType == SPECIFIER::LEGACY || nType == SPECIFIER::POOLSTAKE)
                    {
                        /* Set specifiers. */
                        fLegacy    = (nType == SPECIFIER::LEGACY);
                        fPoolstake = (nType == SPECIFIER::POOLSTAKE);

                        /* Go to next type in stream. */
                        ssPacket >> nType;
                    }

                    /* Switch based on codes. */
                    switch(nType)
                    {
                        /* Standard type for a block. */
                        case TYPES::BLOCK:
                        {
                            /* Check for subscription. */
                            if(!(nSubscriptions & SUBSCRIPTION::BLOCK))
                                return debug::drop(NODE, "BLOCK: unsolicited notification");

                            /* Check for legacy. */
                            if(fLegacy)
                                return debug::drop(NODE, "block notify can't have legacy specifier");

                            /* Get the index of block. */
                            uint1024_t hashBlock;
                            ssPacket >> hashBlock;

                            /* Check for client mode. */
                            if(config::fClient.load())
                            {
                                /* Check the database for the block. */
                                if(!LLD::Client->HasBlock(hashBlock))
                                    ssResponse << uint8_t(SPECIFIER::CLIENT) << uint8_t(TYPES::BLOCK) << hashBlock;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::NOTIFY: CLIENT BLOCK ", hashBlock.SubString());
                            }
                            else
                            {
                                /* Check the database for the block. */
                                if(!LLD::Ledger->HasBlock(hashBlock))
                                    ssResponse << uint8_t(TYPES::BLOCK) << hashBlock;

                                /* Debug output. */
                                debug::log(3, NODE, "ACTION::NOTIFY: BLOCK ", hashBlock.SubString());
                            }

                            break;
                        }

                        /* Standard type for a block. */
                        case TYPES::TRANSACTION:
                        case TYPES::SIGCHAIN:
                        case TYPES::NOTIFICATION:
                        {
                            /* Check for active subscriptions. */
                            if(nType == TYPES::TRANSACTION && !(nSubscriptions & SUBSCRIPTION::TRANSACTION))
                                return debug::drop(NODE, "ACTION::NOTIFY::TRANSACTION: unsolicited notification");

                            /* Sigchain specific validation and de-serialization. */
                            if(nType == TYPES::SIGCHAIN)
                            {
                                /* Check for available protocol version. */
                                if(nProtocolVersion < MIN_TRITIUM_VERSION)
                                    return true;

                                /* Check for subscription. */
                                if(!(nSubscriptions & SUBSCRIPTION::SIGCHAIN))
                                    return debug::drop(NODE, "ACTION::NOTIFY::SIGCHAIN: unsolicited notification");

                                /* Get the sigchain genesis. */
                                uint256_t hashSigchain = 0;
                                ssPacket >> hashSigchain;

                                /* Check for expected genesis. */
                                uint256_t hashLogin = TAO::API::users->GetGenesis(0);
                                if(hashSigchain != hashLogin)
                                    return debug::drop(NODE, "ACTION::NOTIFY::SIGCHAIN: unexpected genesis-id ", hashLogin.SubString());
                            }
                            /* Notification validation */
                            else if(nType == TYPES::NOTIFICATION)
                            {
                                /* Check for available protocol version. */
                                if(nProtocolVersion < MIN_TRITIUM_VERSION)
                                    return true;

                                /* Check for subscription. */
                                if(!(nSubscriptions & SUBSCRIPTION::NOTIFICATION))
                                    return debug::drop(NODE, "ACTION::NOTIFY::NOTIFICATION: unsolicited notification");

                                /* Get the  address . */
                                uint256_t hashAddress = 0;
                                ssPacket >> hashAddress;

                                /* Get the genesis hash of the logged in user */
                                uint256_t hashLogin = TAO::API::users->GetGenesis(0);

                                /* If the address is a genesis hash, then make sure that it is for the currently logged in user */
                                if(hashAddress.GetType() == TAO::Ledger::GenesisType())
                                {
                                    /* Check for expected genesis. */
                                    if(hashAddress != hashLogin)
                                        return debug::drop(NODE, "ACTION::NOTIFY::NOTIFICATION: unexpected genesis-id ", hashAddress.SubString());
                                }
                                /* Otherwise check that it is for a register that the logged in user has subscribed to */
                                else if(std::find(vNotifications.begin(), vNotifications.end(), hashAddress) == vNotifications.end())
                                {
                                    return debug::drop(NODE, "ACTION::NOTIFY::NOTIFICATION: unexpected register address ", hashAddress.SubString());
                                }

                            }

                            /* Get the index of transaction. */
                            uint512_t hashTx = 0;
                            ssPacket >> hashTx;

                            /* Handle for -client mode which deals with merkle transactions. */
                            if(config::fClient.load())
                            {
                                /* Check ledger database. */
                                if(!cacheInventory.Has(hashTx) && !LLD::Client->HasTx(hashTx, TAO::Ledger::FLAGS::MEMPOOL))
                                {
                                    /* Debug output. */
                                    debug::log(3, NODE, "ACTION::NOTIFY: MERKLE TRANSACTION ", hashTx.SubString());

                                    /* Add legacy flag if necessary */
                                    if(fLegacy)
                                        ssResponse << uint8_t(SPECIFIER::LEGACY);

                                    ssResponse << uint8_t(TYPES::MERKLE) << hashTx;
                                }

                                break;
                            }

                            /* Check for legacy. */
                            if(fLegacy)
                            {
                                /* Check legacy database. */
                                if(!cacheInventory.Has(hashTx) && !LLD::Legacy->HasTx(hashTx, TAO::Ledger::FLAGS::MEMPOOL))
                                {
                                    /* Debug output. */
                                    debug::log(3, NODE, "ACTION::NOTIFY: LEGACY TRANSACTION ", hashTx.SubString());

                                    ssResponse << uint8_t(SPECIFIER::LEGACY) << uint8_t(TYPES::TRANSACTION) << hashTx;
                                }
                            }
                            /* Check for pool stake. */
                            else if(fPoolstake)
                            {
                                /* Check for poolstake specifier active */
                                if(nProtocolVersion < MIN_TRITIUM_VERSION)
                                    return debug::drop(NODE, "ACTION::NOTIFY: poolstake specifier not active");

                                uint8_t nTTL;
                                ssPacket >> nTTL;

                                /* Check stake pool. */
                                if(!TAO::Ledger::stakepool.Has(hashTx))
                                {
                                    /* Debug output. */
                                    debug::log(3, NODE, "ACTION::NOTIFY: POOL STAKE TRANSACTION ", hashTx.SubString());

                                    ssResponse << uint8_t(SPECIFIER::POOLSTAKE) << uint8_t(TYPES::TRANSACTION) << hashTx << nTTL;
                                }
                            }
                            else
                            {
                                /* Check ledger database. */
                                if(!cacheInventory.Has(hashTx) && !LLD::Ledger->HasTx(hashTx, TAO::Ledger::FLAGS::MEMPOOL))
                                {
                                    /* Debug output. */
                                    debug::log(3, NODE, "ACTION::NOTIFY: TRITIUM TRANSACTION ", hashTx.SubString());

                                    ssResponse << uint8_t(TYPES::TRANSACTION) << hashTx;
                                }
                            }

                            break;
                        }

                        /* Standard type for height. */
                        case TYPES::BESTHEIGHT:
                        {
                            /* Check for subscription. */
                            if(!(nSubscriptions & SUBSCRIPTION::BESTHEIGHT))
                                return debug::drop(NODE, "BESTHEIGHT: unsolicited notification");

                            /* Check for legacy. */
                            if(fLegacy)
                                return debug::drop(NODE, "height can't have legacy specifier");

                            /* Keep track of current height. */
                            ssPacket >> nCurrentHeight;

                            /* Debug output. */
                            debug::log(3, NODE, "ACTION::NOTIFY: BESTHEIGHT ", nCurrentHeight);

                            break;
                        }

                        /* Standard type for a checkpoint. */
                        case TYPES::CHECKPOINT:
                        {
                            /* Check for subscription. */
                            if(!(nSubscriptions & SUBSCRIPTION::CHECKPOINT))
                                return debug::drop(NODE, "CHECKPOINT: unsolicited notification");

                            /* Check for legacy. */
                            if(fLegacy)
                                return debug::drop(NODE, "checkpoint can't have legacy specifier");

                            /* Keep track of current checkpoint. */
                            ssPacket >> hashCheckpoint;

                            /* Debug output. */
                            debug::log(3, NODE, "ACTION::NOTIFY: CHECKPOINT ", hashCheckpoint.SubString());

                            break;
                        }


                        /* Standard type for a checkpoint. */
                        case TYPES::LASTINDEX:
                        {
                            /* Check for subscription. */
                            if(!(nSubscriptions & SUBSCRIPTION::LASTINDEX))
                                return debug::drop(NODE, "ACTION::NOTIFY: LASTINDEX: unsolicited notification");

                            /* Get the data type. */
                            uint8_t nType = 0;
                            ssPacket >> nType;

                            /* Switch based on different last index values. */
                            switch(nType)
                            {
                                /* Last index for a block is always uint1024_t. */
                                case TYPES::BLOCK:
                                {
                                    /* Check for legacy. */
                                    if(fLegacy)
                                        return debug::drop(NODE, "ACTION::NOTIFY: LASTINDEX: block can't have legacy specifier");

                                    /* Keep track of current checkpoint. */
                                    uint1024_t hashLast;
                                    ssPacket >> hashLast;

                                    /* Check if is sync node. */
                                    if(nCurrentSession == TAO::Ledger::nSyncSession.load())
                                    {
                                        /* Check for complete synchronization. */
                                        if(hashLast == TAO::Ledger::ChainState::hashBestChain.load()
                                        && hashLast == hashBestChain)
                                        {
                                            /* Set state to synchronized. */
                                            fSynchronized.store(true);
                                            TAO::Ledger::nSyncSession.store(0);

                                            /* Unsubcribe from last. */
                                            Unsubscribe(SUBSCRIPTION::LASTINDEX);

                                            /* Total blocks synchronized */
                                            uint32_t nBlocks = TAO::Ledger::ChainState::stateBest.load().nHeight - nSyncStart.load();

                                            /* Calculate the time to sync*/
                                            uint32_t nElapsed = SYNCTIMER.Elapsed();
                                            if(nElapsed == 0)
                                                nElapsed = 1;

                                            double dRate = nBlocks / nElapsed;

                                            /* Log that sync is complete. */
                                            debug::log(0, NODE, "ACTION::NOTIFY: Synchronization COMPLETE at ", hashBestChain.SubString());
                                            debug::log(0, NODE, "ACTION::NOTIFY: Synchronized ", nBlocks, " blocks in ", nElapsed, " seconds [", dRate, " blocks/s]" );

                                        }
                                        else
                                        {
                                            /* Ask for list of blocks. */
                                            PushMessage(ACTION::LIST,
                                                config::fClient.load() ? uint8_t(SPECIFIER::CLIENT) : uint8_t(SPECIFIER::SYNC),
                                                uint8_t(TYPES::BLOCK),
                                                uint8_t(TYPES::UINT1024_T),
                                                hashLast,
                                                uint1024_t(0)
                                            );
                                        }
                                    }

                                    /* Set the last index. */
                                    hashLastIndex = hashLast;

                                    /* Debug output. */
                                    debug::log(3, NODE, "ACTION::NOTIFY: LASTINDEX ", hashLast.SubString());

                                    break;
                                }
                            }

                            break;
                        }


                        /* Standard type for a checkpoint. */
                        case TYPES::BESTCHAIN:
                        {
                            /* Check for subscription. */
                            if(!(nSubscriptions & SUBSCRIPTION::BESTCHAIN))
                                return debug::drop(NODE, "BESTCHAIN: unsolicited notification");

                            /* Keep track of current checkpoint. */
                            ssPacket >> hashBestChain;

                            /* Check if is sync node. */
                            if(TAO::Ledger::nSyncSession.load() != 0
                            && nCurrentSession == TAO::Ledger::nSyncSession.load()
                            && LLD::Ledger->HasBlock(hashBestChain))
                            {
                                /* Set state to synchronized. */
                                fSynchronized.store(true);
                                TAO::Ledger::nSyncSession.store(0);

                                /* Unsubcribe from last. */
                                Unsubscribe(SUBSCRIPTION::LASTINDEX);

                                /* Log that sync is complete. */
                                debug::log(0, NODE, "ACTION::NOTIFY: Synchonization COMPLETE at ", hashBestChain.SubString());
                            }

                            /* Debug output. */
                            debug::log(3, NODE, "ACTION::NOTIFY: BESTCHAIN ", hashBestChain.SubString());

                            break;
                        }


                        /* Standard type for na address. */
                        case TYPES::ADDRESS:
                        {
                            /* Check for subscription. */
                            if(!(nSubscriptions & SUBSCRIPTION::ADDRESS))
                                return debug::drop(NODE, "ADDRESS: unsolicited notification");

                            /* Get the base address. */
                            BaseAddress addr;
                            ssPacket >> addr;

                            /* Add addresses to manager.. */
                            if(TRITIUM_SERVER->GetAddressManager())
                                TRITIUM_SERVER->GetAddressManager()->AddAddress(addr);

                            /* Debug output. */
                            debug::log(0, NODE, "ACTION::NOTIFY: ADDRESS ", addr.ToStringIP());

                            break;
                        }

                        /* Catch malformed notify binary streams. */
                        default:
                            return debug::drop(NODE, "ACTION::NOTIFY malformed binary stream");
                    }
                }

                /* Push a request for the data from notifications. */
                if(ssResponse.size() != 0)
                    WritePacket(NewMessage(ACTION::GET, ssResponse));

                break;
            }


            /* Handle for ping command. */
            case ACTION::PING:
            {
                /* Get the nonce. */
                uint64_t nNonce = 0;
                ssPacket >> nNonce;

                /* Push the pong response. */
                PushMessage(ACTION::PONG, nNonce);

                /* Bump DDOS score. */
                if(DDOS) //a ping shouldn't be sent too much
                    DDOS->rSCORE += 10;

                break;
            }


            /* Handle a pong command. */
            case ACTION::PONG:
            {
                /* Get the nonce. */
                uint64_t nNonce = 0;
                ssPacket >> nNonce;

                /* If the nonce was not received or known from pong. */
                if(!mapLatencyTracker.count(nNonce))
                {
                    /* Bump DDOS score for spammed PONG messages. */
                    if(DDOS)
                        DDOS->rSCORE += 10;

                    return true;
                }

                /* Calculate the Average Latency of the Connection. */
                nLatency = mapLatencyTracker[nNonce].ElapsedMilliseconds();
                mapLatencyTracker.erase(nNonce);

                /* Set the latency used for address manager within server */
                if(TRITIUM_SERVER->GetAddressManager())
                    TRITIUM_SERVER->GetAddressManager()->SetLatency(nLatency, GetAddress());

                /* Debug Level 3: output Node Latencies. */
                debug::log(3, NODE, "Latency (Nonce ", std::hex, nNonce, " - ", std::dec, nLatency, " ms)");

                break;
            }


            /* Standard type for a timeseed. */
            case TYPES::TIMESEED:
            {
                /* Check for subscription. */
                if(!(nSubscriptions & SUBSCRIPTION::TIMESEED))
                    return debug::drop(NODE, "TYPES::TIMESEED: unsolicited data");

                /* Check for authorized node. */
                if(!Authorized())
                    return debug::drop(NODE, "cannot send timeseed if not authorized");

                /* Check trust threshold. */
                if(nTrust < 60 * 60)
                    return debug::drop(NODE, "cannot send timeseed with no trust");

                /* Get the time seed from network. */
                int64_t nTimeSeed = 0;
                ssPacket >> nTimeSeed;

                /* Keep track of the time seeds if accepted. */
                debug::log(2, NODE, "timeseed ", nTimeSeed, " ACCEPTED");

                break;
            }


            /* Standard type for a block. */
            case TYPES::ADDRESS:
            {
                /* Check for subscription. */
                if(!(nSubscriptions & SUBSCRIPTION::ADDRESS))
                    return debug::drop(NODE, "TYPES::ADDRESS: unsolicited data");

                /* Get the base address. */
                BaseAddress addr;
                ssPacket >> addr;

                /* Add addresses to manager.. */
                if(TRITIUM_SERVER->GetAddressManager())
                    TRITIUM_SERVER->GetAddressManager()->AddAddress(addr);

                break;
            }


            /* Handle incoming block. */
            case TYPES::BLOCK:
            {
                /* Check for subscription. */
                if(!(nSubscriptions & SUBSCRIPTION::BLOCK) && TAO::Ledger::nSyncSession.load() != nCurrentSession)
                    return debug::drop(NODE, "TYPES::BLOCK: unsolicited data");

                /* Star the sync timer if this is the first sync block */
                if(!SYNCTIMER.Running())
                    SYNCTIMER.Start();

                /* Get the specifier. */
                uint8_t nSpecifier = 0;
                ssPacket >> nSpecifier;

                /* Switch based on specifier. */
                uint8_t nStatus = 0;
                switch(nSpecifier)
                {
                    /* Handle for a legacy transaction. */
                    case SPECIFIER::LEGACY:
                    {
                        /* Check for client mode since this method should never be called except by a client. */
                        if(config::fClient.load())
                            return debug::drop(NODE, "TYPES::BLOCK::LEGACY: disabled in -client mode");

                        /* Get the block from the stream. */
                        Legacy::LegacyBlock block;
                        ssPacket >> block;

                        /* Process the block. */
                        TAO::Ledger::Process(block, nStatus);

                        /* Check for duplicate and ask for previous block. */
                        if(!(nStatus & TAO::Ledger::PROCESS::DUPLICATE)
                        && !(nStatus & TAO::Ledger::PROCESS::IGNORED)
                        &&  (nStatus & TAO::Ledger::PROCESS::ORPHAN))
                        {
                            /* Ask for list of blocks. */
                            PushMessage(ACTION::LIST,
                                #ifndef DEBUG_MISSING
                                (config::fClient.load() ? uint8_t(SPECIFIER::CLIENT) : uint8_t(SPECIFIER::TRANSACTIONS)),
                                #endif
                                uint8_t(TYPES::BLOCK),
                                uint8_t(TYPES::LOCATOR),
                                TAO::Ledger::Locator(TAO::Ledger::ChainState::hashBestChain.load()),
                                uint1024_t(block.hashPrevBlock)
                            );
                        }

                        break;
                    }

                    /* Handle for a tritium transaction. */
                    case SPECIFIER::TRITIUM:
                    {
                        /* Check for client mode since this method should never be called except by a client. */
                        if(config::fClient.load())
                            return debug::drop(NODE, "TYPES::BLOCK::TRITIUM: disabled in -client mode");

                        /* Get the block from the stream. */
                        TAO::Ledger::TritiumBlock block;
                        ssPacket >> block;

                        /* Process the block. */
                        TAO::Ledger::Process(block, nStatus);

                        /* Check for missing transactions. */
                        if(nStatus & TAO::Ledger::PROCESS::INCOMPLETE)
                        {
                            /* Create response data stream. */
                            DataStream ssResponse(SER_NETWORK, PROTOCOL_VERSION);

                            /* Create a list of requested transactions. */
                            for(const auto& tx : block.vMissing)
                            {
                                /* Check for legacy. */
                                if(tx.first == TAO::Ledger::TRANSACTION::LEGACY)
                                    ssResponse << uint8_t(SPECIFIER::LEGACY);

                                /* Push to stream. */
                                ssResponse << uint8_t(TYPES::TRANSACTION) << tx.second;

                                /* Log the missing data. */
                                debug::log(0, FUNCTION, "requesting missing tx ", tx.second.SubString());
                            }

                            /* Check for repeated missing loops. */
                            if(DDOS)
                            {
                                /* Iterate a failure for missing transactions. */
                                nConsecutiveFails += block.vMissing.size();

                                /* Bump DDOS score. */
                                DDOS->rSCORE += (block.vMissing.size() * 10);
                            }


                            /* Ask for the block again last TODO: this can be cached for further optimization. */
                            ssResponse << uint8_t(TYPES::BLOCK) << block.hashMissing;

                            /* Push the packet response. */
                            if(ssResponse.size() != 0)
                                WritePacket(NewMessage(ACTION::GET, ssResponse));
                        }

                        /* Check for duplicate and ask for previous block. */
                        if(!(nStatus & TAO::Ledger::PROCESS::DUPLICATE)
                        && !(nStatus & TAO::Ledger::PROCESS::IGNORED)
                        && !(nStatus & TAO::Ledger::PROCESS::INCOMPLETE)
                        &&  (nStatus & TAO::Ledger::PROCESS::ORPHAN))
                        {
                            /* Ask for list of blocks. */
                            PushMessage(ACTION::LIST,
                                #ifndef DEBUG_MISSING
                                (config::fClient.load() ? uint8_t(SPECIFIER::CLIENT) : uint8_t(SPECIFIER::TRANSACTIONS)),
                                #endif
                                uint8_t(TYPES::BLOCK),
                                uint8_t(TYPES::LOCATOR),
                                TAO::Ledger::Locator(TAO::Ledger::ChainState::hashBestChain.load()),
                                uint1024_t(block.hashPrevBlock)
                            );
                        }

                        break;
                    }

                    /* Handle for a tritium transaction. */
                    case SPECIFIER::SYNC:
                    {
                        /* Check for client mode since this method should never be called except by a client. */
                        if(config::fClient.load())
                            return debug::drop(NODE, "TYPES::BLOCK::SYNC: disabled in -client mode");

                        /* Check if this is an unsolicited sync block. */
                        //if(nCurrentSession != TAO::Ledger::nSyncSession)
                        //    return debug::drop(FUNCTION, "unsolicted sync block");

                        /* Get the block from the stream. */
                        TAO::Ledger::SyncBlock block;
                        ssPacket >> block;

                        /* Check version switch. */
                        if(block.nVersion >= 7)
                        {
                            /* Build a tritium block from sync block. */
                            TAO::Ledger::TritiumBlock tritium(block);

                            /* Verbose debug output. */
                            if(config::nVerbose >= 3)
                                debug::log(3, FUNCTION, "received sync block ", tritium.GetHash().SubString(), " height = ", block.nHeight);

                            /* Process the block. */
                            TAO::Ledger::Process(tritium, nStatus);
                        }
                        else
                        {
                            /* Build a tritium block from sync block. */
                            Legacy::LegacyBlock legacy(block);

                            /* Verbose debug output. */
                            if(config::nVerbose >= 3)
                                debug::log(3, FUNCTION, "received sync block ", legacy.GetHash().SubString(), " height = ", block.nHeight);

                            /* Process the block. */
                            TAO::Ledger::Process(legacy, nStatus);
                        }

                        break;
                    }


                    /* Handle for a tritium transaction. */
                    case SPECIFIER::CLIENT:
                    {
                        /* Get the block from the stream. */
                        TAO::Ledger::ClientBlock block;
                        ssPacket >> block;

                        /* Process the block. */
                        TAO::Ledger::Process(block, nStatus);

                        /* Check for duplicate and ask for previous block. */
                        if(!(nStatus & TAO::Ledger::PROCESS::DUPLICATE)
                        && !(nStatus & TAO::Ledger::PROCESS::IGNORED)
                        &&  (nStatus & TAO::Ledger::PROCESS::ORPHAN))
                        {
                            /* Ask for list of blocks. */
                            PushMessage(ACTION::LIST,
                                uint8_t(SPECIFIER::CLIENT),
                                uint8_t(TYPES::BLOCK),
                                uint8_t(TYPES::LOCATOR),
                                TAO::Ledger::Locator(TAO::Ledger::ChainState::hashBestChain.load()),
                                uint1024_t(block.hashPrevBlock)
                            );
                        }

                        break;

                        /* Log received. */
                        debug::log(3, FUNCTION, "received client block ", block.GetHash().SubString(), " height = ", block.nHeight);

                        break;
                    }

                    /* Default catch all. */
                    default:
                        return debug::drop(NODE, "invalid type specifier for block");
                }

                /* Check for specific status messages. */
                if(nStatus & TAO::Ledger::PROCESS::ACCEPTED)
                {
                    /* Reset the fails and orphans. */
                    nConsecutiveOrphans = 0;
                    nConsecutiveFails   = 0;

                    /* Reset last time received. */
                    if(nCurrentSession == TAO::Ledger::nSyncSession.load())
                        nLastTimeReceived.store(runtime::timestamp());
                }

                /* Check for failure status messages. */
                if(nStatus & TAO::Ledger::PROCESS::REJECTED)
                    ++nConsecutiveFails;

                /* Check for orphan status messages. */
                if(nStatus & TAO::Ledger::PROCESS::ORPHAN)
                    ++nConsecutiveOrphans;

                /* Detect large orphan chains and ask for new blocks from origin again. */
                if(nConsecutiveOrphans >= 1000)
                {
                    {
                        LOCK(TAO::Ledger::PROCESSING_MUTEX);

                        /* Clear the memory to prevent DoS attacks. */
                        TAO::Ledger::mapOrphans.clear();
                    }

                    /* Switch to another available node. */
                    if(TAO::Ledger::ChainState::Synchronizing() && TAO::Ledger::nSyncSession.load() == nCurrentSession)
                        SwitchNode();

                    /* Disconnect from a node with large orphan chain. */
                    return debug::drop(NODE, "node reached orphan limit");
                }


                /* Check for failure limit on node. */
                if(nConsecutiveFails >= 1000)
                {
                    /* Switch to another available node. */
                    if(TAO::Ledger::ChainState::Synchronizing() && TAO::Ledger::nSyncSession.load() == nCurrentSession)
                        SwitchNode();

                    /* Drop pesky nodes. */
                    return debug::drop(NODE, "node reached failure limit");
                }

                break;
            }


            /* Handle incoming transaction. */
            case TYPES::TRANSACTION:
            {
                /* Check for subscription. */
                if(!(nSubscriptions & SUBSCRIPTION::TRANSACTION))
                    return debug::drop(NODE, "TYPES::TRANSACTION: unsolicited data");

                /* Get the specifier. */
                uint8_t nSpecifier = 0;
                ssPacket >> nSpecifier;

                /* Switch based on type. */
                switch(nSpecifier)
                {
                    /* Handle for a legacy transaction. */
                    case SPECIFIER::LEGACY:
                    {
                        /* Get the transction from the stream. */
                        Legacy::Transaction tx;
                        ssPacket >> tx;

                        /* Accept into memory pool. */
                        if(TAO::Ledger::mempool.Accept(tx, this))
                        {
                            /* Relay the transaction notification. */
                            TRITIUM_SERVER->Relay
                            (
                                ACTION::NOTIFY,
                                uint8_t(SPECIFIER::LEGACY),
                                uint8_t(TYPES::TRANSACTION),
                                tx.GetHash()
                            );

                            /* Reset consecutive failures. */
                            nConsecutiveFails   = 0;
                            nConsecutiveOrphans = 0;
                        }
                        else
                            ++nConsecutiveFails;

                        break;
                    }

                    /* Handle for a tritium transaction. */
                    case SPECIFIER::TRITIUM:
                    {
                        /* Get the transction from the stream. */
                        TAO::Ledger::Transaction tx;
                        ssPacket >> tx;

                        /* Accept into memory pool. */
                        if(TAO::Ledger::mempool.Accept(tx, this))
                        {
                            /* Relay the transaction notification. */
                            uint512_t hashTx = tx.GetHash();
                            TRITIUM_SERVER->Relay
                            (
                                /* Standard transaction relay. */
                                ACTION::NOTIFY,
                                uint8_t(TYPES::TRANSACTION),
                                hashTx,

                                /* Handle sigchain related notifications. */
                                uint8_t(TYPES::SIGCHAIN),
                                tx.hashGenesis,
                                hashTx
                            );

                            /* Reset consecutive failures. */
                            nConsecutiveFails   = 0;
                            nConsecutiveOrphans = 0;
                        }
                        else
                            ++nConsecutiveFails;


                        break;
                    }

                    /* Handle a pooled coinstake. */
                    case SPECIFIER::POOLSTAKE:
                    {
                        /* Check for poolstake specifier active */
                        if(nProtocolVersion < MIN_TRITIUM_VERSION)
                            return debug::drop(NODE, "TYPES::TRANSACTION: poolstake specifier not active");

                        /* Get the transction from the stream. */
                        TAO::Ledger::Transaction tx;
                        uint8_t nTTL;

                        ssPacket >> nTTL;
                        ssPacket >> tx;

                        /* Accept into stake pool. */
                        if(TAO::Ledger::stakepool.Accept(tx, this))
                        {
                            /* Relay the transaction notification if more TTL count */
                            if(nTTL > 0)
                            {
                                uint512_t hashTx = tx.GetHash();
                                --nTTL;

                                TRITIUM_SERVER->Relay
                                (
                                    ACTION::NOTIFY,
                                    uint8_t(SPECIFIER::POOLSTAKE),
                                    uint8_t(TYPES::TRANSACTION),
                                    hashTx,
                                    nTTL
                                );
                            }

                            /* Reset consecutive failures. */
                            nConsecutiveFails   = 0;
                            nConsecutiveOrphans = 0;
                        }
                        else
                            ++nConsecutiveFails;


                        break;
                    }

                    /* Default catch all. */
                    default:
                        return debug::drop(NODE, "invalid type specifier for transaction");
                }

                /* Check for failure limit on node. */
                if(nConsecutiveFails >= 1000)
                    return debug::drop(NODE, "TX::node reached failure limit");

                /* Check for orphan limit on node. */
                if(nConsecutiveOrphans >= 1000)
                    return debug::drop(NODE, "TX::node reached ORPHAN limit");

                break;
            }


            /* Handle incoming merkle transaction. */
            case TYPES::MERKLE:
            {
                /* Check for subscription. */
                if(!config::fClient.load())
                    return debug::drop(NODE, "TYPES::MERKLE: unavailable when not in -client mode");

                //if(!(nSubscriptions & SUBSCRIPTION::SIGCHAIN))
                //    return debug::drop(NODE, "TYPES::MERKLE: unsolicited data");

                /* Get the specifier. */
                uint8_t nSpecifier = 0;
                ssPacket >> nSpecifier;

                /* Switch based on type. */
                switch(nSpecifier)
                {
                    /* Handle for a legacy transaction. */
                    case SPECIFIER::LEGACY:
                    {
                        /* Get the transction from the stream. */
                        Legacy::MerkleTx tx;
                        ssPacket >> tx;

                        /* Check if we have this transaction already. */
                        if(!LLD::Client->HasTx(tx.GetHash()))
                        {
                            /* Grab the block to check merkle path. */
                            TAO::Ledger::ClientBlock block;
                            if(LLD::Client->ReadBlock(tx.hashBlock, block))
                            {
                                /* Cache the txid. */
                                uint512_t hashTx = tx.GetHash();

                                /* Check the merkle branch. */
                                if(!tx.CheckMerkleBranch(block.hashMerkleRoot))
                                    return debug::error(FUNCTION, "merkle transaction has invalid path");

                                /* Commit transaction to disk. */
                                LLD::TxnBegin(TAO::Ledger::FLAGS::BLOCK);
                                if(!LLD::Client->WriteTx(hashTx, tx))
                                {
                                    LLD::TxnAbort(TAO::Ledger::FLAGS::BLOCK);
                                    return debug::error(FUNCTION, "failed to write transaction");
                                }

                                /* Index the transaction to it's block. */
                                if(!LLD::Client->IndexBlock(hashTx, tx.hashBlock))
                                {
                                    LLD::TxnAbort(TAO::Ledger::FLAGS::BLOCK);
                                    return debug::error(FUNCTION, "failed to write block indexing entry");
                                }

                                /* UTXO to Sig Chain support - The only reason we would be receiving a legacy transaction in client
                                   mode is if we are being sent a legacy event.  The event would normally be written to the DB in
                                   Transaction::Connect, but we cannot connect legacy transactions in client mode as we will not
                                   have all of the inputs.  Therefore, we need to check the outputs to see if any of them
                                   are to a register address we know about and, if so, write an event for the account holder */
                                for(const auto txout : tx.vout )
                                {
                                    uint256_t hashTo;
                                    if(Legacy::ExtractRegister(txout.scriptPubKey, hashTo))
                                    {
                                        /* Read the owner of register. (check this for MEMPOOL, too) */
                                        TAO::Register::State state;
                                        if(!LLD::Register->ReadState(hashTo, state))
                                            return debug::error(FUNCTION, "failed to read register to");

                                        /* Commit an event for receiving sigchain in the legay DB. */
                                        if(!LLD::Legacy->WriteEvent(state.hashOwner, hashTx))
                                            return debug::error(FUNCTION, "failed to write event for account ", state.hashOwner.SubString());
                                    }
                                }

                                /* Flush to disk and clear mempool. */
                                LLD::TxnCommit(TAO::Ledger::FLAGS::BLOCK);
                                TAO::Ledger::mempool.Remove(hashTx);

                                if(config::nVerbose >= 3)
                                    tx.print();

                                debug::log(0, hashTx.SubString(), " ACCEPTED");
                            }
                        }

                        break;
                    }

                    /* Handle for a tritium transaction. */
                    case SPECIFIER::TRITIUM:
                    {
                        /* Get the transction from the stream. */
                        TAO::Ledger::MerkleTx tx;
                        ssPacket >> tx;

                        /* Cache the txid. */
                        uint512_t hashTx = tx.GetHash();

                        /* Check if we have this transaction already. */
                        if(!LLD::Client->HasTx(hashTx))
                        {
                            /* Check for empty merkle tx. */
                            if(tx.hashBlock != 0)
                            {
                                /* Grab the block to check merkle path. */
                                TAO::Ledger::ClientBlock block;
                                if(LLD::Client->ReadBlock(tx.hashBlock, block))
                                {
                                    /* Check the merkle branch. */
                                    if(!tx.CheckMerkleBranch(block.hashMerkleRoot))
                                        return debug::error(FUNCTION, "merkle transaction has invalid path");

                                    if(config::nVerbose >= 3)
                                        tx.print();

                                    /* Commit transaction to disk. */
                                    LLD::TxnBegin(TAO::Ledger::FLAGS::BLOCK);
                                    if(!LLD::Client->WriteTx(hashTx, tx))
                                    {
                                        LLD::TxnAbort(TAO::Ledger::FLAGS::BLOCK);
                                        return debug::error(FUNCTION, "failed to write transaction");
                                    }

                                    /* Index the transaction to it's block. */
                                    if(!LLD::Client->IndexBlock(hashTx, tx.hashBlock))
                                    {
                                        LLD::TxnAbort(TAO::Ledger::FLAGS::BLOCK);
                                        return debug::error(FUNCTION, "failed to write block indexing entry");
                                    }

                                    /* Connect transaction in memory. */
                                    if(!tx.Connect(TAO::Ledger::FLAGS::BLOCK))
                                    {
                                        LLD::TxnAbort(TAO::Ledger::FLAGS::BLOCK);
                                        return debug::error(FUNCTION, "tx ", hashTx.SubString(), " REJECTED: ", debug::GetLastError());
                                    }

                                    /* Flush to disk and clear mempool. */
                                    LLD::TxnCommit(TAO::Ledger::FLAGS::BLOCK);
                                    TAO::Ledger::mempool.Remove(hashTx);

                                    debug::log(0, hashTx.SubString(), " ACCEPTED");
                                }
                                else
                                {
                                    debug::error(0, hashTx.SubString(), "REJECTED: missing block ", tx.hashBlock.SubString());
                                }
                            }
                            else
                            {
                                debug::log(0, "No merkle branch for tx ", hashTx.SubString());
                                TAO::Ledger::mempool.Accept(tx, this);
                            }

                        }

                        break;
                    }

                    /* Default catch all. */
                    default:
                        return debug::drop(NODE, "invalid type specifier for TYPES::MERKLE");
                }

                break;
            }


            /* Handle an event trigger. */
            case TYPES::TRIGGER:
            {
                /* De-serialize the trigger nonce. */
                ssPacket >> nTriggerNonce;

                break;
            }


            /* Handle an event trigger. */
            case RESPONSE::COMPLETED:
            {
                /* De-serialize the trigger nonce. */
                uint64_t nNonce = 0;
                ssPacket >> nNonce;

                TriggerEvent(INCOMING.MESSAGE, nNonce);

                break;
            }

            case RESPONSE::VALIDATED:
            {
                /* deserialize the type */
                uint8_t nType;
                ssPacket >> nType;

                /* De-serialize the trigger nonce. */
                uint64_t nNonce = 0;
                ssPacket >> nNonce;

                switch(nType)
                {
                    case TYPES::TRANSACTION:
                    {
                        /* get the valid flag */
                        bool fValid = false;
                        ssPacket >> fValid;

                        /* deserialize the transaction hash */
                        uint512_t hashTx;
                        ssPacket >> hashTx;

                        if(fValid)
                        {
                            /* Trigger event with this nonce. */
                            TriggerEvent(INCOMING.MESSAGE, nNonce, fValid, hashTx);
                        }
                        else
                        {
                            /* deserialize the contract ID */
                            uint32_t nContract;
                            ssPacket >> nContract;

                            /* Trigger active events with this nonce. */
                            TriggerEvent(INCOMING.MESSAGE, nNonce, fValid, hashTx, nContract);
                        }

                        break;
                    }
                    default:
                    {
                        /* Trigger active events with this nonce. */
                        TriggerEvent(INCOMING.MESSAGE, nNonce);
                    }
                }

                break;
            }

            case ACTION::VALIDATE:
            {
                /* deserialize the type */
                uint8_t nType;
                ssPacket >> nType;

                switch(nType)
                {
                    case TYPES::TRANSACTION:
                    {
                        /* deserialize the transaction */
                        TAO::Ledger::Transaction tx;
                        ssPacket >> tx;

                        /* Validating a transaction simply sanitizes the contracts within it.  If any of them fail then we stop
                           sanitizing and return the contract ID that failed */
                        bool fSanitized = false;

                        /* Temporary map for pre-states to be passed into the sanitization Build() for each contract. */
                        std::map<uint256_t, TAO::Register::State> mapStates;

                        /* Loop through each contract in the transaction. */
                        for(uint32_t nContract = 0; nContract < tx.Size(); ++nContract)
                        {
                            /* Get the contract. */
                            TAO::Operation::Contract& contract = tx[nContract];

                            /* Lock the mempool at this point so that we can see if the transaction would be accepted into the mempool */
                            RLOCK(TAO::Ledger::mempool.MUTEX);

                            try
                            {
                                /* Start a ACID transaction (to be disposed). */
                                LLD::TxnBegin(TAO::Ledger::FLAGS::MEMPOOL);

                                fSanitized = TAO::Register::Build(contract, mapStates, TAO::Ledger::FLAGS::MEMPOOL)
                                            && TAO::Operation::Execute(contract, TAO::Ledger::FLAGS::MEMPOOL);

                                /* Abort the mempool ACID transaction once the contract is sanitized */
                                LLD::TxnAbort(TAO::Ledger::FLAGS::MEMPOOL);

                            }
                            catch(const std::exception& e)
                            {
                                /* Abort the mempool ACID transaction */
                                LLD::TxnAbort(TAO::Ledger::FLAGS::MEMPOOL);

                                /* Log the error and attempt to continue processing */
                                debug::error(FUNCTION, e.what());

                                fSanitized = false;
                            }

                            /* If this contract failed, then respond with the failed contract ID */
                            if(!fSanitized)
                            {
                                PushMessage(RESPONSE::VALIDATED, uint8_t(TYPES::TRANSACTION), nTriggerNonce, false, tx.GetHash(), nContract);

                                /* Stop processing any more contracts  */
                                break;
                            }
                        }

                        /* If none failed then send a validated response */
                        PushMessage(RESPONSE::VALIDATED, uint8_t(TYPES::TRANSACTION), nTriggerNonce, true, tx.GetHash());

                        break;
                    }
                    default:
                    {
                        return debug::drop(NODE, "ACTION::VALIDATE invalid type specified");
                    }
                }

                break;
            }

            case ACTION::REQUEST:
            {
                /* deserialize the type */
                uint8_t nType;
                ssPacket >> nType;

                switch(nType)
                {
                    /* Caller is requesting a peer to peer connection to communicate via the messaging LLP*/
                    case TYPES::P2PCONNECTION:
                    {
                        /* get the source genesis hash */
                        uint256_t hashFrom;
                        ssPacket >> hashFrom;

                        /* Get the connection request */
                        LLP::P2P::ConnectionRequest request;
                        ssPacket >> request;


                        /* Get the public key. */
                        std::vector<uint8_t> vchPubKey;
                        ssPacket >> vchPubKey;

                        /* Get the signature. */
                        std::vector<uint8_t> vchSig;
                        ssPacket >> vchSig;

                        /* Check the timestamp. If the request is older than 30s then it is stale so ignore the message */
                        if(request.nTimestamp > runtime::unifiedtimestamp() || request.nTimestamp < runtime::unifiedtimestamp() - 30)
                        {
                            debug::log(3, NODE, "ACTION::REQUEST::P2P: timestamp out of range (stale)");
                            return true;
                        }


                        /* See whether we have processed a P2P request from this user in the last 5 seconds.
                           If so then ignore the message. If not then relay the message to our peers.
                           NOTE: we relay the message regardless of whether the destination genesis is logged in on this node,
                           as the user may be logged in on multiple nodes and  might want to process the request on a
                           different node/device. */
                        {
                            LOCK(P2P_REQUESTS_MUTEX);
                            if(mapP2PRequests.count(hashFrom) == 0 || mapP2PRequests[hashFrom] < request.nTimestamp - 5)
                            {
                                /* Check that the source and destination genesis exists before relaying.  NOTE: We skip this 
                                   in client mode as we will only have local scope and not know about all genesis hashes 
                                   on the network.  Therefore relaying is limited to full nodes only */
                                if(!config::fClient)
                                {
                                    /* Check that the source genesis exists. */
                                    if(!LLD::Ledger->HasGenesis(request.hashPeer))
                                        return debug::drop(NODE, "ACTION::REQUEST::P2P: invalid destination genesis hash");

                                    /* Check that the source genesis exists. */
                                    if(!LLD::Ledger->HasGenesis(hashFrom))
                                        return debug::drop(NODE, "ACTION::REQUEST::P2P: invalid source genesis hash");
                                }

                                /* Verify the signature before relaying.  Again we don't do this in client mode as we only have
                                   local scope and won't be able to access the crypto object register of the hashFrom */
                                if(!config::fClient)
                                { 
                                    /* Build the byte stream from the request data in order to verify the signature */
                                    DataStream ssCheck(SER_NETWORK, PROTOCOL_VERSION);
                                    ssCheck << hashFrom << request;

                                    /* Verify the signature */
                                    if(!TAO::Ledger::SignatureChain::Verify(hashFrom, "network", ssCheck.Bytes(), vchPubKey, vchSig))
                                        return debug::error(NODE, "ACTION::REQUEST::P2P: invalid transaction signature");
                                     
                                    /* Reset the packet data pointer */
                                    ssPacket.Reset();

                                    /* Relay the P2P request */
                                    TRITIUM_SERVER->Relay
                                    (
                                        uint8_t(ACTION::REQUEST),
                                        uint8_t(TYPES::P2PCONNECTION),
                                        hashFrom,
                                        request,
                                        vchPubKey,
                                        vchSig
                                    );
                                }

                                /* Check to see whether the destination genesis is logged in on this node */
                                if(TAO::API::users->LoggedIn(request.hashPeer))
                                {
                                    /* Get the users session */
                                    TAO::API::Session& session = TAO::API::users->GetSession(request.hashPeer);

                                    /* If an incoming request already exists from this peer then remove it */
                                    if(session.HasP2PRequest(request.strAppID, hashFrom, true))
                                        session.DeleteP2PRequest(request.strAppID, hashFrom, true);

                                    /* Add this incoming request to the P2P requests queue for this user */
                                    LLP::P2P::ConnectionRequest requestIncoming = { runtime::unifiedtimestamp(), request.strAppID, hashFrom, request.nSession, request.address, request.nPort, request.nSSLPort };
                                    session.AddP2PRequest(requestIncoming, true);

                                    debug::log(3, NODE, "P2P Request received from " , hashFrom.ToString(), " for appID ", request.strAppID );
                                }
                            }

                            /* Log this request */
                            mapP2PRequests[hashFrom] = request.nTimestamp;


                        }

                        break;

                    }
                    default:
                    {
                        return debug::drop(NODE, "ACTION::REQUEST invalid type specified");
                    }
                }

                break;
            }

            case TYPES::PEERADDRESS:
            {
                {
                    /* Lock session mutex to prevent other sessions from accessing thisAddress */
                    LOCK(SESSIONS_MUTEX);

                    /* Ignore the message if we have already obtained our IP address */
                    if(!thisAddress.IsValid())
                    {
                        /* Deserialize the address */
                        BaseAddress addr;
                        ssPacket >> addr;

                        thisAddress = addr;
                    }
                }

                break;
            }

            default:
                return debug::drop(NODE, "invalid protocol message ", INCOMING.MESSAGE);
        }

        /* Check for authorization. */
        if(DDOS && !Authorized())
            DDOS->rSCORE += 5; //untrusted nodes get less requests

        /* Check for a version message. */
        if(nProtocolVersion == 0 || nCurrentSession == 0)
            return debug::drop(NODE, "first message wasn't a version message");

        return true;
    }


    /*  Non-Blocking Packet reader to build a packet from TCP Connection.
     *  This keeps thread from spending too much time for each Connection. */
    void TritiumNode::ReadPacket()
    {
        if(!INCOMING.Complete())
        {
            /** Handle Reading Packet Length Header. **/
            if(!INCOMING.Header() && Available() >= 8)
            {
                std::vector<uint8_t> BYTES(8, 0);
                if(Read(BYTES, 8) == 8)
                {
                    DataStream ssHeader(BYTES, SER_NETWORK, MIN_PROTO_VERSION);
                    ssHeader >> INCOMING;

                    Event(EVENTS::HEADER);
                }
            }

            /** Handle Reading Packet Data. **/
            uint32_t nAvailable = Available();
            if(INCOMING.Header() && nAvailable > 0 && !INCOMING.IsNull() && INCOMING.DATA.size() < INCOMING.LENGTH)
            {
                /* The maximum number of bytes to read is th number of bytes specified in the message length, 
                   minus any already read on previous reads*/
                uint32_t nMaxRead = (uint32_t)(INCOMING.LENGTH - INCOMING.DATA.size());
                
                /* Vector to receve the read bytes. This should be the smaller of the number of bytes currently available or the
                   maximum amount to read */
                std::vector<uint8_t> DATA(std::min(nAvailable, nMaxRead), 0);

                /* Read up to the buffer size. */
                int32_t nRead = Read(DATA, DATA.size()); 
                
                /* If something was read, insert it into the packet data.  NOTE: that due to SSL packet framing we could end up 
                   reading less bytes than appear available.  Therefore we only copy the number of bytes actually read */
                if(nRead > 0)
                    INCOMING.DATA.insert(INCOMING.DATA.end(), DATA.begin(), DATA.begin() + nRead);
                    
                /* If the packet is now considered complete, fire the packet complete event */
                if(INCOMING.Complete())
                    Event(EVENTS::PACKET, static_cast<uint32_t>(DATA.size()));
            }
        }
    }


    /* Determine if a node is authorized and therfore trusted. */
    bool TritiumNode::Authorized() const
    {
        return hashGenesis != 0 && fAuthorized;
    }


    /* Unsubscribe from another node for notifications. */
    void TritiumNode::Unsubscribe(const uint16_t nFlags)
    {
        /* Set the timestamp that we unsubscribed at. */
        nUnsubscribed = runtime::timestamp();

        /* Unsubscribe over the network. */
        Subscribe(nFlags, false);
    }


    /* Subscribe to another node for notifications. */
    void TritiumNode::Subscribe(const uint16_t nFlags, bool fSubscribe)
    {
        /* Build subscription message. */
        DataStream ssMessage(SER_NETWORK, MIN_PROTO_VERSION);

        /* Check for block. */
        if(nFlags & SUBSCRIPTION::BLOCK)
        {
            /* Build the message. */
            ssMessage << uint8_t(TYPES::BLOCK);

            /* Check for subscription. */
            if(fSubscribe)
            {
                /* Set the flag. */
                nSubscriptions |=  SUBSCRIPTION::BLOCK;

                /* Debug output. */
                debug::log(3, NODE, "SUBSCRIBING TO BLOCK ", std::bitset<16>(nSubscriptions));
            }
            else
            {
                /* Debug output. */
                debug::log(3, NODE, "UNSUBSCRIBING FROM BLOCK ", std::bitset<16>(nSubscriptions));
            }
        }

        /* Check for transaction. */
        if(nFlags & SUBSCRIPTION::TRANSACTION)
        {
            /* Build the message. */
            ssMessage << uint8_t(TYPES::TRANSACTION);

            /* Check for subscription. */
            if(fSubscribe)
            {
                /* Set the flag. */
                nSubscriptions |=  SUBSCRIPTION::TRANSACTION;

                /* Debug output. */
                debug::log(3, NODE, "SUBSCRIBING TO TRANSACTION ", std::bitset<16>(nSubscriptions));
            }
            else
            {
                /* Debug output. */
                debug::log(3, NODE, "UNSUBSCRIBING FROM TRANSACTION ", std::bitset<16>(nSubscriptions));
            }
        }

        /* Check for time seed. */
        if(nFlags & SUBSCRIPTION::TIMESEED)
        {
            /* Build the message. */
            ssMessage << uint8_t(TYPES::TIMESEED);

            /* Check for subscription. */
            if(fSubscribe)
            {
                /* Set the flag. */
                nSubscriptions |=  SUBSCRIPTION::TIMESEED;

                /* Debug output. */
                debug::log(3, NODE, "SUBSCRIBING TO TIMESEED ", std::bitset<16>(nSubscriptions));
            }
            else
            {
                /* Debug output. */
                debug::log(3, NODE, "UNSUBSCRIBING FROM TIMESEED ", std::bitset<16>(nSubscriptions));
            }
        }

        /* Check for height. */
        if(nFlags & SUBSCRIPTION::BESTHEIGHT)
        {
            /* Build the message. */
            ssMessage << uint8_t(TYPES::BESTHEIGHT);

            /* Check for subscription. */
            if(fSubscribe)
            {
                /* Set the flag. */
                nSubscriptions |=  SUBSCRIPTION::BESTHEIGHT;

                /* Debug output. */
                debug::log(3, NODE, "SUBSCRIBING TO BESTHEIGHT ", std::bitset<16>(nSubscriptions));
            }
            else
            {
                /* Debug output. */
                debug::log(3, NODE, "UNSUBSCRIBING FROM BESTHEIGHT ", std::bitset<16>(nSubscriptions));
            }
        }

        /* Check for checkpoint. */
        if(nFlags & SUBSCRIPTION::CHECKPOINT)
        {
            /* Build the message. */
            ssMessage << uint8_t(TYPES::CHECKPOINT);

            /* Check for subscription. */
            if(fSubscribe)
            {
                /* Set the flag. */
                nSubscriptions |=  SUBSCRIPTION::CHECKPOINT;

                /* Debug output. */
                debug::log(3, NODE, "SUBSCRIBING TO CHECKPOINT ", std::bitset<16>(nSubscriptions));
            }
            else
            {
                /* Debug output. */
                debug::log(3, NODE, "UNSUBSCRIBING FROM CHECKPOINT ", std::bitset<16>(nSubscriptions));
            }
        }

        /* Check for address. */
        if(nFlags & SUBSCRIPTION::ADDRESS)
        {
            /* Build the message. */
            ssMessage << uint8_t(TYPES::ADDRESS);

            /* Check for subscription. */
            if(fSubscribe)
            {
                /* Set the flag. */
                nSubscriptions |=  SUBSCRIPTION::ADDRESS;

                /* Debug output. */
                debug::log(3, NODE, "SUBSCRIBING TO ADDRESS ", std::bitset<16>(nSubscriptions));
            }
            else
            {
                /* Debug output. */
                debug::log(3, NODE, "UNSUBSCRIBING FROM ADDRESS ", std::bitset<16>(nSubscriptions));
            }
        }

        /* Check for last. */
        if(nFlags & SUBSCRIPTION::LASTINDEX)
        {
            /* Build the message. */
            ssMessage << uint8_t(TYPES::LASTINDEX);

            /* Check for subscription. */
            if(fSubscribe)
            {
                /* Set the flag. */
                nSubscriptions |=  SUBSCRIPTION::LASTINDEX;

                /* Debug output. */
                debug::log(3, NODE, "SUBSCRIBING TO LASTINDEX ", std::bitset<16>(nSubscriptions));
            }
            else
            {
                /* Debug output. */
                debug::log(3, NODE, "UNSUBSCRIBING FROM LASTINDEX ", std::bitset<16>(nSubscriptions));
            }
        }

        /* Check for last. */
        if(nFlags & SUBSCRIPTION::BESTCHAIN)
        {
            /* Build the message. */
            ssMessage << uint8_t(TYPES::BESTCHAIN);

            /* Check for subscription. */
            if(fSubscribe)
            {
                /* Set the flag. */
                nSubscriptions |=  SUBSCRIPTION::BESTCHAIN;

                /* Debug output. */
                debug::log(3, NODE, "SUBSCRIBING TO BESTCHAIN ", std::bitset<16>(nSubscriptions));
            }
            else
            {
                /* Debug output. */
                debug::log(3, NODE, "UNSUBSCRIBING FROM BESTCHAIN ", std::bitset<16>(nSubscriptions));
            }
        }


        /* Check for sigchain. */
        if(nFlags & SUBSCRIPTION::SIGCHAIN)
        {
            /* Build the message. */
            ssMessage << uint8_t(TYPES::SIGCHAIN);

            /* Check for subscription. */
            if(fSubscribe)
            {
                /* Set the flag. */
                nSubscriptions |=  SUBSCRIPTION::SIGCHAIN;

                /* Debug output. */
                debug::log(3, NODE, "SUBSCRIBING TO SIGCHAIN ", std::bitset<16>(nSubscriptions));
            }
            else
            {
                /* Debug output. */
                debug::log(3, NODE, "UNSUBSCRIBING FROM SIGCHAIN ", std::bitset<16>(nSubscriptions));
            }
        }

        /* Write the subscription packet. */
        WritePacket(NewMessage((fSubscribe ? ACTION::SUBSCRIBE : ACTION::UNSUBSCRIBE), ssMessage));
    }


    /* Unsubscribe from another node for notifications. */
    void TritiumNode::UnsubscribeNotification(const uint256_t& hashAddress)
    {
        /* Set the timestamp that we unsubscribed at. */
        nUnsubscribed = runtime::timestamp();

        /* Unsubscribe over the network. */
        SubscribeNotification(hashAddress, false);
    }


    /* Subscribe to another node for notifications. */
    void TritiumNode::SubscribeNotification(const uint256_t& hashAddress, bool fSubscribe)
    {
        /* Build subscription message. */
        DataStream ssMessage(SER_NETWORK, MIN_PROTO_VERSION);

        /* Build the message. */
        ssMessage << uint8_t(TYPES::NOTIFICATION) << hashAddress;

        /* Check for subscription. */
        if(fSubscribe)
        {
            /* Set the flag. */
            nSubscriptions |=  SUBSCRIPTION::NOTIFICATION;

            /* Store the address subscribed to so that we can validate when the peer sends us notifications */
            vNotifications.push_back(hashAddress);

            /* Debug output. */
            debug::log(3, NODE, "SUBSCRIBING TO NOTIFICATION ", std::bitset<16>(nSubscriptions));
        }
        else
        {
            /* Remove the address from the notifications vector for this user */
            vNotifications.erase(std::find(vNotifications.begin(), vNotifications.end(), hashAddress));

            /* Debug output. */
            debug::log(3, NODE, "UNSUBSCRIBING FROM NOTIFICATION ", std::bitset<16>(nSubscriptions));
        }

        /* Write the subscription packet. */
        WritePacket(NewMessage((fSubscribe ? ACTION::SUBSCRIBE : ACTION::UNSUBSCRIBE), ssMessage));
    }

    /* Builds an Auth message for this node.*/
    DataStream TritiumNode::GetAuth(bool fAuth)
    {
        /* Build auth message. */
        DataStream ssMessage(SER_NETWORK, MIN_PROTO_VERSION);

        /* Only send auth messages if the auth key has been cached */
        if(TAO::API::users->LoggedIn() && TAO::API::GetSessionManager().Get(0, false).GetNetworkKey() != 0)
        {
            /* Get the Session */
            TAO::API::Session& session = TAO::API::GetSessionManager().Get(0, false);

            /* The genesis of the currently logged in user */
            uint256_t hashSigchain = session.GetAccount()->Genesis();

            uint64_t nTimestamp = runtime::unifiedtimestamp();

            /* Add the basic auth data to the message */
            ssMessage << hashSigchain <<  nTimestamp << SESSION_ID;

            /* Get a hash of the data. */
            uint256_t hashCheck = LLC::SK256(ssMessage.begin(), ssMessage.end());

            /* The public key for the "network" key*/
            std::vector<uint8_t> vchPubKey;
            std::vector<uint8_t> vchSig;


            /* Generate the public key and signature for the message data */
            session.GetAccount()->Sign("network", hashCheck.GetBytes(), session.GetNetworkKey(), vchPubKey, vchSig);

            /* Add the public key to the message */
            ssMessage << vchPubKey;
            ssMessage << vchSig;

            debug::log(0, FUNCTION, "SIGNING MESSAGE: ", hashSigchain.SubString(), " at timestamp ", nTimestamp);
        }

        return ssMessage;
    }


    /*  Authorize this node to the connected node */
    void TritiumNode::Auth(bool fAuth)
    {
        /* Get the auth message */
        DataStream ssMessage = GetAuth(fAuth);

        /* Check whether it is valid before sending it */
        if(ssMessage.size() > 0)
            WritePacket(NewMessage((fAuth ? ACTION::AUTH : ACTION::DEAUTH), ssMessage));
    }


    /* Checks if a node is subscribed to receive a notification. */
    const DataStream TritiumNode::RelayFilter(const uint16_t nMsg, const DataStream& ssData) const
    {
        /* The data stream to relay*/
        DataStream ssRelay(SER_NETWORK, MIN_PROTO_VERSION);

        /* Switch based on message type */
        switch(nMsg)
        {
            /* Filter out request messages so that we don't send them to peers on older protocol versions */
            case ACTION::REQUEST :
            {
                /* Get the request type */
                uint8_t nType = 0;
                ssData >> nType;

                /* Switch based on type. */
                switch(nType)
                {
                    case TYPES::P2PCONNECTION:
                    {
                        /* Ensure the peer is on a high enough version to receive the P2PCONNECTION message */
                        if(nProtocolVersion >= MIN_TRITIUM_VERSION)
                            ssRelay = ssData;
                        break;
                    }
                    default:
                    {
                        /* Default to letting the message be relayed */
                        ssRelay = ssData;
                        break;
                    }
                }

                break;
            }

            /* Filter notifications. */
            case ACTION::NOTIFY:
            {
                /* Build a response data stream. */
                while(!ssData.End())
                {
                    /* Get the first notify type. */
                    uint8_t nType = 0;
                    ssData >> nType;

                    /* Check for legacy or poolstake specifier. */
                    bool fLegacy = false;
                    bool fPoolstake = false;
                    if(nType == SPECIFIER::LEGACY || nType == SPECIFIER::POOLSTAKE)
                    {
                        /* Set specifiers. */
                        fLegacy    = (nType == SPECIFIER::LEGACY);
                        fPoolstake = (nType == SPECIFIER::POOLSTAKE);

                        /* Go to next type in stream. */
                        ssData >> nType;
                    }

                    /* Switch based on type. */
                    switch(nType)
                    {
                        /* Check for block subscription. */
                        case TYPES::BLOCK:
                        {
                            /* Get the index. */
                            uint1024_t hashBlock;
                            ssData >> hashBlock;

                            /* Check subscription. */
                            if(nNotifications & SUBSCRIPTION::BLOCK)
                            {
                                /* Write block to stream. */
                                ssRelay << uint8_t(TYPES::BLOCK);
                                ssRelay << hashBlock;
                            }

                            break;
                        }


                        /* Check for transaction subscription. */
                        case TYPES::TRANSACTION:
                        {
                            /* Get the index. */
                            uint512_t hashTx;
                            ssData >> hashTx;

                            /* Check subscription. */
                            if(nNotifications & SUBSCRIPTION::TRANSACTION)
                            {
                                /* Check for legacy. */
                                if(fLegacy)
                                    ssRelay << uint8_t(SPECIFIER::LEGACY);

                                /* Check for pool stake. */
                                else if(fPoolstake)
                                    ssRelay << uint8_t(SPECIFIER::POOLSTAKE);

                                /* Write transaction to stream. */
                                ssRelay << uint8_t(TYPES::TRANSACTION);
                                ssRelay << hashTx;

                                /* Add TTL for poolstake */
                                if(fPoolstake)
                                {
                                    uint8_t nTTL;
                                    ssData >> nTTL;

                                    ssRelay << nTTL;
                                }
                            }

                            break;
                        }


                        /* Check for height subscription. */
                        case TYPES::BESTHEIGHT:
                        {
                            /* Get the index. */
                            uint32_t nHeight;
                            ssData >> nHeight;

                            /* Check for legacy. */
                            if(fLegacy)
                            {
                                debug::error(FUNCTION, "BESTHEIGHT cannot have legacy specifier");
                                continue;
                            }

                            /* Check subscription. */
                            if(nNotifications & SUBSCRIPTION::BESTHEIGHT)
                            {
                                /* Write transaction to stream. */
                                ssRelay << uint8_t(TYPES::BESTHEIGHT);
                                ssRelay << nHeight;
                            }

                            break;
                        }


                        /* Check for checkpoint subscription. */
                        case TYPES::CHECKPOINT:
                        {
                            /* Get the index. */
                            uint1024_t hashCheck;
                            ssData >> hashCheck;

                            /* Check for legacy. */
                            if(fLegacy)
                            {
                                debug::error(FUNCTION, "CHECKPOINT cannot have legacy specifier");
                                continue;
                            }

                            /* Check subscription. */
                            if(nNotifications & SUBSCRIPTION::CHECKPOINT)
                            {
                                /* Write transaction to stream. */
                                ssRelay << uint8_t(TYPES::CHECKPOINT);
                                ssRelay << hashCheck;
                            }

                            break;
                        }


                        /* Check for best chain subscription. */
                        case TYPES::BESTCHAIN:
                        {
                            /* Get the index. */
                            uint1024_t hashBest;
                            ssData >> hashBest;

                            /* Check for legacy. */
                            if(fLegacy)
                            {
                                debug::error(FUNCTION, "BESTCHAIN cannot have legacy specifier");
                                continue;
                            }

                            /* Check subscription. */
                            if(nNotifications & SUBSCRIPTION::BESTCHAIN)
                            {
                                /* Write transaction to stream. */
                                ssRelay << uint8_t(TYPES::BESTCHAIN);
                                ssRelay << hashBest;
                            }

                            break;
                        }


                        /* Check for address subscription. */
                        case TYPES::ADDRESS:
                        {
                            /* Get the index. */
                            BaseAddress addr;
                            ssData >> addr;

                            /* Check for legacy. */
                            if(fLegacy)
                            {
                                debug::error(FUNCTION, "ADDRESS cannot have legacy specifier");
                                continue;
                            }

                            /* Check subscription. */
                            if(nNotifications & SUBSCRIPTION::ADDRESS)
                            {
                                /* Write transaction to stream. */
                                ssRelay << uint8_t(TYPES::ADDRESS);
                                ssRelay << addr;
                            }

                            break;
                        }


                        /* Check for sigchain subscription. */
                        case TYPES::SIGCHAIN:
                        {
                            /* Get the index. */
                            uint256_t hashSigchain = 0;
                            ssData >> hashSigchain;

                            /* Get the txid. */
                            uint512_t hashTx = 0;
                            ssData >> hashTx;

                            /* Check subscription. */
                            if(nNotifications & SUBSCRIPTION::SIGCHAIN)
                            {
                                /* Check for matching sigchain-id. */
                                if(hashSigchain != hashGenesis)
                                    break;

                                /* Check for legacy. */
                                if(fLegacy)
                                    ssRelay << uint8_t(SPECIFIER::LEGACY);

                                /* Write transaction to stream. */
                                ssRelay << uint8_t(TYPES::SIGCHAIN);
                                ssRelay << hashSigchain;
                                ssRelay << hashTx;
                            }

                            break;
                        }


                        /* Check for notification subscription. */
                        case TYPES::NOTIFICATION:
                        {
                            /* Get the sig chain / register that the transaction relates to. */
                            uint256_t hashAddress = 0;
                            ssData >> hashAddress;

                            /* Get the txid. */
                            uint512_t hashTx = 0;
                            ssData >> hashTx;

                            /* Check subscription. */
                            if(nNotifications & SUBSCRIPTION::NOTIFICATION)
                            {
                                /* Check that the address is one that has been subscribed to */
                                if(std::find(vNotifications.begin(), vNotifications.end(), hashAddress) == vNotifications.end())
                                    break;

                                /* Check for legacy. */
                                if(fLegacy)
                                    ssRelay << uint8_t(SPECIFIER::LEGACY);

                                /* Write transaction to stream. */
                                ssRelay << uint8_t(TYPES::NOTIFICATION);
                                ssRelay << hashAddress;
                                ssRelay << hashTx;
                            }

                            break;
                        }

                        /* Default catch (relay up to this point) */
                        default:
                        {
                            debug::error(FUNCTION, "Malformed binary stream");
                            return ssRelay;
                        }
                    }
                }

                break;
            }
            default:
            {
                /* default behaviour is to let the message be relayed */
                ssRelay = ssData;
                break;
            }
        }


        return ssRelay;
    }


    /* Determine whether a session is connected. */
    bool TritiumNode::SessionActive(const uint64_t nSession)
    {
        LOCK(SESSIONS_MUTEX);

        return mapSessions.count(nSession);
    }


    /* Get a node by connected session. */
    memory::atomic_ptr<TritiumNode>& TritiumNode::GetNode(const uint64_t nSession)
    {
        LOCK(SESSIONS_MUTEX);

        /* Check for connected session. */
        static memory::atomic_ptr<TritiumNode> pNULL;
        if(!mapSessions.count(nSession))
            return pNULL;

        /* Get a reference of session. */
        const std::pair<uint32_t, uint32_t>& pair = mapSessions[nSession];
        return TRITIUM_SERVER->GetConnection(pair.first, pair.second);
    }


    /* Helper function to switch the nodes on sync. */
    void TritiumNode::SwitchNode()
    {
        std::pair<uint32_t, uint32_t> pairSession;
        { LOCK(SESSIONS_MUTEX);

            /* Check for session. */
            if(!mapSessions.count(TAO::Ledger::nSyncSession.load()))
                return;

            /* Set the current session. */
            pairSession = mapSessions[TAO::Ledger::nSyncSession.load()];
        }

        /* Normal case of asking for a getblocks inventory message. */
        memory::atomic_ptr<TritiumNode>& pnode = TRITIUM_SERVER->GetConnection(pairSession);
        if(pnode != nullptr)
        {
            /* Send out another getblocks request. */
            try
            {
                /* Get the current sync node. */
                memory::atomic_ptr<TritiumNode>& pcurrent = TRITIUM_SERVER->GetConnection(pairSession.first, pairSession.second);
                pcurrent->Unsubscribe(SUBSCRIPTION::LASTINDEX | SUBSCRIPTION::BESTCHAIN);

                /* Set the sync session-id. */
                TAO::Ledger::nSyncSession.store(pnode->nCurrentSession);

                /* Subscribe to this node. */
                pnode->Subscribe(SUBSCRIPTION::LASTINDEX | SUBSCRIPTION::BESTCHAIN | SUBSCRIPTION::BESTHEIGHT);
                pnode->PushMessage(ACTION::LIST,
                    config::fClient.load() ? uint8_t(SPECIFIER::CLIENT) : uint8_t(SPECIFIER::SYNC),
                    uint8_t(TYPES::BLOCK),
                    uint8_t(TYPES::LOCATOR),
                    TAO::Ledger::Locator(TAO::Ledger::ChainState::hashBestChain.load()),
                    uint1024_t(0)
                );

                /* Reset last time received. */
                nLastTimeReceived.store(runtime::timestamp());
            }
            catch(const std::exception& e)
            {
                /* Recurse on failure. */
                debug::error(FUNCTION, e.what());

                SwitchNode();
            }
        }
        else
        {
            /* Reset the current sync node. */
            TAO::Ledger::nSyncSession.store(0);

            /* Logging to verify (for debugging). */
            debug::log(0, FUNCTION, "No Sync Nodes Available");
        }
    }
}
