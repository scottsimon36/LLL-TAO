/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_TEMPLATES_SERVER_H
#define NEXUS_LLP_TEMPLATES_SERVER_H


#include <LLP/templates/data.h>
#include <LLP/include/legacy_address.h>
#include <LLP/include/manager.h>
#include <LLP/include/server_config.h>

#include <map>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <cstdint>

namespace LLP
{
    /* forward declarations */
    class AddressManager;
    class DDOS_Filter;
    class InfoAddress;
    typedef struct ssl_st SSL;


    /** Server
     *
     *  Base Class to create a Custom LLP Server.
     *
     *  Protocol Type class must inherit Connection,
     *  and provide a ProcessPacket method.
     *  Optional Events by providing GenericEvent method.
     *
     **/
    template <class ProtocolType>
    class Server
    {
    private:

        /** Server's listenting port. **/
        uint16_t PORT;


        /** The listener socket instance. **/
        std::pair<int32_t, int32_t> hListenSocket;


        /** Determine if Server should use an SSL connection. **/
        std::atomic<bool> fSSL;


        /** The DDOS variables. **/
        std::map<BaseAddress, DDOS_Filter *> DDOS_MAP;


        /** DDOS flag for off or on. **/
        std::atomic<bool> fDDOS;


        /** The moving average timespan for DDOS throttling. **/
        uint32_t DDOS_TIMESPAN;


        /** Maximum number of data threads for this server. **/
        uint16_t MAX_THREADS;


        /** The data type to keep track of current running threads. **/
        std::vector<DataThread<ProtocolType> *> DATA_THREADS;


        /** condition variable for manager thread. **/
        std::condition_variable MANAGER;


        /** Listener Thread for accepting incoming connections. **/
        std::thread          LISTEN_THREAD;


        /** Meter Thread for tracking incoming and outgoing packet counts. **/
        std::thread          METER_THREAD;


        /** Port mapping thread for opening port in router. **/
        std::thread          UPNP_THREAD;


        /** Connection Manager thread. **/
        std::thread MANAGER_THREAD;


        /** Address for handling outgoing connections **/
        AddressManager *pAddressManager;


        /** The sleep time of address manager. **/
        uint32_t nSleepTime;

        
        /** Max number of outgoing connections this server can make. **/
        uint32_t nMaxOutgoing;


        /** Maximum number connections in total that this server can handle.  Must be greater than nMaxOutgoing **/
        uint32_t nMaxConnections;


    public:

    

        /** Name
         *
         *  Returns the name of the protocol type of this server.
         *
         **/
        std::string Name();


        /** Constructor **/
        Server<ProtocolType>(const ServerConfig& config);


        /** Default Destructor **/
        virtual ~Server<ProtocolType>();


        /** GetPort
         *
         *  Returns the port number for this Server.
         *
         **/
        uint16_t GetPort() const;


        /** GetAddressManager
         *
         *  Returns the address manager instance for this server.
         *
         **/
        AddressManager* GetAddressManager() const;


        /** Shutdown
         *
         *  Cleanup and shutdown subsystems
         *
         **/
        void Shutdown();


        /** AddNode
         *
         *  Add a node address to the internal address manager
         *
         *  @param[in] strAddress	IPv4 Address of outgoing connection
         *  @param[in] strPort		Port of outgoing connection
         *
         **/
        void AddNode(std::string strAddress, uint16_t nPort, bool fLookup = false);


        /** AddConnection
         *
         *  Public Wraper to Add a Connection Manually.
         *
         *  @param[in] strAddress	IPv4 Address of outgoing connection
         *  @param[in] strPort		Port of outgoing connection
         *  @param[in] fLookup		Flag indicating whether address lookup should occur
         *  @param[in] args variadic args to forward to the data thread constructor
         *
         *  @return	Returns 1 If successful, 0 if unsuccessful, -1 on errors.
         *
         **/
        template<typename... Args>
        bool AddConnection(std::string strAddress, uint16_t nPort, bool fLookup, Args&&... args)
        {
            /* Initialize DDOS Protection for Incoming IP Address. */
            BaseAddress addrConnect(strAddress, nPort, fLookup);

            /* Make sure address is valid. */
            if(!addrConnect.IsValid())
            {
                /* Ban the address. */
                if(pAddressManager)
                    pAddressManager->Ban(addrConnect);

                return false;
            }


            /* Create new DDOS Filter if Needed. */
            if(fDDOS.load())
            {
                if(!DDOS_MAP.count(addrConnect))
                    DDOS_MAP[addrConnect] = new DDOS_Filter(DDOS_TIMESPAN);

                /* DDOS Operations: Only executed when DDOS is enabled. */
                if(DDOS_MAP[addrConnect]->Banned())
                    return false;
            }

            /* Find a balanced Data Thread to Add Connection to. */
            int32_t nThread = FindThread();
            if(nThread < 0)
                return false;

            /* Select the proper data thread. */
            DataThread<ProtocolType> *dt = DATA_THREADS[nThread];

            /* Attempt the connection. */
            if(!dt->NewConnection(addrConnect, DDOS_MAP[addrConnect], fSSL.load(),  std::forward<Args>(args)...))
            {
                /* Add the address to the address manager if it exists. */
                if(pAddressManager)
                    pAddressManager->AddAddress(addrConnect, ConnectState::FAILED);

                return false;
            }

            /* Add the address to the address manager if it exists. */
            if(pAddressManager)
                pAddressManager->AddAddress(addrConnect, ConnectState::CONNECTED);

            return true;
        }


        /** GetConnections
         *
         *  Constructs a vector of all active connections across all threads
         *
         *  @return Returns a vector of active connections
         **/
        std::vector<memory::atomic_ptr<ProtocolType>*> GetConnections() const;


        /** GetConnectionCount
         *
         *  Get the number of active connection pointers from data threads.
         *
         *  @return Returns the count of active connections
         *
         **/
        uint32_t GetConnectionCount(const uint8_t nFlags = FLAGS::ALL);


        /** Get Connection
         *
         *  Select a random and currently open connection
         *
         **/
        memory::atomic_ptr<ProtocolType>& GetConnection();


        /** Get Connection
         *
         *  Get the best connection based on latency
         *
         *  @param[in] pairExclude The connection that should be excluded from the search.
         *
         **/
        memory::atomic_ptr<ProtocolType>& GetConnection(const std::pair<uint32_t, uint32_t>& pairExclude);


        /** Get Connection
         *
         *  Get the best connection based on data thread index.
         *
         **/
        memory::atomic_ptr<ProtocolType>& GetConnection(const uint32_t nDataThread, const uint32_t nDataIndex);


        /** GetSpecificConnection
         *
         *  Get connection matching variable args, which are passed on to the ProtocolType instance.
         *
         **/
        template<typename... Args>
        memory::atomic_ptr<ProtocolType>& GetSpecificConnection(Args&&... args)
        {
            /* Thread ID and index of the matchingconnection */
            int16_t nRetThread = -1;
            int16_t nRetIndex  = -1;

            /* Loop through all threads */
            for(uint16_t nThread = 0; nThread < MAX_THREADS; ++nThread)
            {
                /* Loop through connections in data thread. */
                uint16_t nSize = static_cast<uint16_t>(DATA_THREADS[nThread]->CONNECTIONS->size());
                for(uint16_t nIndex = 0; nIndex < nSize; ++nIndex)
                {
                    try
                    {
                        /* Get the current atomic_ptr. */
                        memory::atomic_ptr<ProtocolType>& CONNECTION = DATA_THREADS[nThread]->CONNECTIONS->at(nIndex);
                        if(!CONNECTION)
                            continue;

                        /* check the details */
                        if(CONNECTION->Matches(std::forward<Args>(args)...))
                        {

                            nRetThread = nThread;
                            nRetIndex  = nIndex;

                            /* Break out as we have found a match */
                            break;
                        }
                    }
                    catch(const std::exception& e)
                    {
                        //debug::error(FUNCTION, e.what());
                    }
                }

                /* break if we have a match */
                if(nRetThread != -1 && nRetIndex != -1)
                    break;

            }

            /* Handle if no connections were found. */
            static memory::atomic_ptr<ProtocolType> pNULL;
            if(nRetThread == -1 || nRetIndex == -1)
                return pNULL;

            return DATA_THREADS[nRetThread]->CONNECTIONS->at(nRetIndex);
        }


        /** Relay
         *
         *  Relays data to all nodes on the network.
         *
         **/
        template<typename MessageType, typename... Args>
        void Relay(const MessageType& message, Args&&... args)
        {
            /* Relay message to each data thread, which will relay message to each connection of each data thread */
            for(uint16_t nThread = 0; nThread < MAX_THREADS; ++nThread)
                DATA_THREADS[nThread]->Relay(message, args...);
        }


        /** Relay_
         *
         *  Relays raw binary data to the network. Accepts only binary stream pre-serialized.
         *
         **/
        template<typename MessageType>
        void _Relay(const MessageType& message, const DataStream& ssData)
        {
            /* Relay message to each data thread, which will relay message to each connection of each data thread */
            for(uint16_t nThread = 0; nThread < MAX_THREADS; ++nThread)
                DATA_THREADS[nThread]->_Relay(message, ssData);
        }


        /** GetAddresses
         *
         *  Get the active connection pointers from data threads.
         *
         *  @return Returns the list of active connections in a vector.
         *
         **/
        std::vector<LegacyAddress> GetAddresses();


        /** DisconnectAll
        *
        *  Notifies all data threads to disconnect their connections
        *
        **/
        void DisconnectAll();


        /** NotifyEvent
         *
         *  Tell the server an event has occured to wake up thread if it is sleeping. This can be used to orchestrate communication
         *  among threads if a strong ordering needs to be guaranteed.
         *
         **/
        void NotifyEvent();


    private:


        /** Manager
         *
         *  Address Manager Thread.
         *
         **/
        void Manager();


        /** FindThread
         *
         *  Determine the first thread with the least amount of active connections.
         *  This keeps them load balanced across all server threads.
         *
         *  @return Returns the index of the found thread. or -1 if not found.
         *
         **/
        int32_t FindThread();


        /** ListeningThread
         *
         *  Main Listening Thread of LLP Server. Handles new Connections and
         *  DDOS associated with Connection if enabled.
         *
         *  @param[in] fIPv4
         *
         **/
        void ListeningThread(bool fIPv4);


        /** BindListenPort
         *
         *  Bind connection to a listening port.
         *
         *  @param[in] hListenSocket
         *  @param[in] fIPv4 Flag indicating the connection is IPv4
         *  @param[in] fRemote Flag indicating that the socket should listen on all interfaced (true) or local only (false)
         *
         *  @return
         *
         **/
        bool BindListenPort(int32_t & hListenSocket, bool fIPv4 = true, bool fRemote = false);


        /** Meter
         *
         *  LLP Meter Thread. Tracks the Requests / Second.
         *
         **/
        void Meter();


        /** UPnP
         *
         *  UPnP Thread. If UPnP is enabled then this thread will set up the required port forwarding.
         *
         **/
        void UPnP();

    };

}

#endif
