/* provider.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 17 Nov 2013
   FreeBSD-style copyright and disclaimer apply

   Endpoint provider
*/

#pragma once

#include "socket.h"

namespace slick {


/******************************************************************************/
/* CLIENT HANDLE                                                              */
/******************************************************************************/

// \todo hide the details in a class.
typedef int ClientHandle;

/******************************************************************************/
/* ENDPOINT PROVIDER                                                          */
/******************************************************************************/

struct EndpointProvider
{
    enum {
        HeartbeatFrequencyMs = 200,
        HeartbeatThresholdMs = 1000,
    }

    EndpointProvider(std::string name, const char* port);
    ~EndpointProvider() { shutdown(); }

    int fd() const { return pollFd; }

    void publish(const std::string& endpoint);
    void poll();
    void shutdown();

    std::function<void(ClientHandle h)> onNewClient;
    std::function<void(ClientHandle h)> onLostClient;

    std::function<void(ClientHandle h, Message&& m)> onMessage;

    void send(ClientHandle client, Message&& msg);
    void send(ClientHandle client, const Message& msg)
    {
        send(client, Message(msg));
    }

    void broadcast(Message&& msg);
    void broadcast(const Message& msg)
    {
        broadcast(Message(msg));
    }

    // \todo Would be nice to have multicast support.

private:

    void connectClient(int fd);
    void disconnectClient(int fd);

    void recvMessage(int fd);
    void sendMessage(int fd);

    void sendHeartbeats();
    void sendHeader(int fd):

    std::string name;
    int pollFd;
    PassiveSockets sockets;
    size_t pollThread;

    struct ClientState
    {
        ClientState() :
            addr({ 0 }), addrlen(sizeof addr),
            lastHeartbeatRecv(-1), lastHearbeatSent(-1),
            rtt(-1), bytesSent(0), bytesRecv(0),
            writable(true)
        {}

        struct sockaddr addr;
        socklen_t addrlen;

        double lastHeartbeatRecv;
        double lastHeartbeatSent;

        double rtt;
        size_t bytesSent;
        size_t bytesRecv;

        bool writable;
        std::vector<Message> sendQueue;

        void send(Message&& msg);
        void send(const Message& msg);
        void flushQueue();
    };

    std::unordered_map<ClientHandle, ClientState> clients;
};


} // slick
