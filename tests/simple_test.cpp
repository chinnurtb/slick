/* simple_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 30 Nov 2013
   FreeBSD-style copyright and disclaimer apply

   Simple tests for the endpoints.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "provider.h"
#include "client.h"
#include "utils.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace slick;

namespace { Port portCounter = 20000; }


BOOST_AUTO_TEST_CASE(basics)
{
    const Port listenPort = portCounter++;

    enum { Pings = 32 };
    size_t pingRecv = 0, pongRecv = 0;

    SourcePoller poller;

    EndpointProvider provider(listenPort);
    poller.add(provider);

    provider.onNewConnection = [] (ConnectionHandle conn) {
        printf("prv: new %d\n", conn);;
    };
    provider.onLostConnection = [] (ConnectionHandle conn) {
        printf("prv: lost %d\n", conn);;
    };

    provider.onPayload = [&] (ConnectionHandle conn, Payload&& data) {
        std::string msg = proto::toString(data);
        printf("prv: got(%d) %s\n", conn, msg.c_str());
        provider.broadcast(proto::fromString("PONG"));
        pingRecv++;
    };

    EndpointClient client;
    poller.add(client);

    client.onNewConnection = [] (ConnectionHandle conn) {
        printf("cli: new %d\n", conn);;
    };
    client.onLostConnection = [] (ConnectionHandle conn) {
        printf("cli: lost %d\n", conn);;
    };

    client.onPayload = [&] (ConnectionHandle conn, Payload&& data) {
        std::string msg = proto::toString(data);
        printf("cli: got(%d) %s\n", conn, msg.c_str());
        pongRecv++;
    };

    Connection conn(client, "localhost", listenPort);

    std::atomic<bool> shutdown(false);
    auto pollFn = [&] { while (!shutdown) poller.poll(); };
    std::thread pollTh(pollFn);

    for (size_t i = 0; i < Pings; ++i) {
        stringstream ss; ss << "PING { ";
        for (size_t j = 0; j <= i; ++j) ss << to_string(j) << " ";
        ss << "}";

        client.broadcast(proto::fromString(ss.str()));
    }

    slick::sleep(100);
    shutdown = true;
    pollTh.join();

    BOOST_CHECK_EQUAL(Pings, pingRecv);
    BOOST_CHECK_EQUAL(Pings, pongRecv);
}

BOOST_AUTO_TEST_CASE(n_to_n)
{
    enum { N = 100 };

    // PROVIDERS ---------------------------------------------------------------

    const Port listenPortStart = 30000;

    SourcePoller provPoller;

    array<shared_ptr<EndpointProvider>, N> providers;
    array<size_t, N> clientIdSums;
    clientIdSums.fill(0);

    for (size_t id = 0; id < N; ++id) {
        providers[id] = make_shared<EndpointProvider>(listenPortStart + id);
        provPoller.add(*providers[id]);

        weak_ptr<EndpointProvider> prov(providers[id]);
        providers[id]->onPayload = [=, &clientIdSums] (ConnectionHandle conn, Payload&& data) {
            clientIdSums[id] += proto::toInt<size_t>(data);

            auto ptr = prov.lock();
            ptr->send(conn, proto::fromInt<size_t>(id + 1));
        };
    }

    std::atomic<bool> provShutdown(false);
    auto provPollFn = [&] { while (!provShutdown) provPoller.poll(); };
    std::thread provPollTh(provPollFn);


    // CLIENTS -----------------------------------------------------------------

    SourcePoller clientPoller;

    EndpointClient client;
    clientPoller.add(client);

    std::atomic<size_t> provIdSum(0);
    client.onPayload = [&] (ConnectionHandle, Payload&& data) {
        provIdSum += proto::toInt<size_t>(data);
    };

    array<shared_ptr<Connection>, N> connections;

    for (size_t id = 0; id < N; ++id) {
        connections[id] = 
            make_shared<Connection>(client, "localhost", listenPortStart + id);
    }

    std::atomic<bool> clientShutdown(false);
    auto clientPollFn = [&] { while (!clientShutdown) clientPoller.poll(); };
    std::thread clientPollTh(clientPollFn);


    // TEST --------------------------------------------------------------------

    client.broadcast(proto::fromInt<size_t>(1));

    size_t exp = (N * (N + 1)) / 2;
    while (provIdSum != exp);

    provShutdown = true;
    clientShutdown = true;

    provPollTh.join();
    clientPollTh.join();

    for (size_t i = 0; i < N; ++i)
        BOOST_CHECK_EQUAL(clientIdSums[i], 1);
}


BOOST_AUTO_TEST_CASE(nice_disconnect)
{
    const Port listenPort = portCounter++;

    std::atomic<bool> gotClient(false);
    std::atomic<bool> lostClient(false);

    SourcePoller poller;

    EndpointProvider provider(listenPort);
    poller.add(provider);

    provider.onNewConnection = [&] (ConnectionHandle conn) {
        gotClient = true;
        printf("prv: new %d\n", conn);;
    };
    provider.onLostConnection = [&] (ConnectionHandle conn) {
        lostClient = true;
        printf("prv: lost %d\n", conn);;
    };

    EndpointClient client;
    poller.add(client);

    std::atomic<bool> shutdown(false);
    auto pollFn = [&] { while (!shutdown) poller.poll(); };
    std::thread pollTh(pollFn);

    slick::sleep(1);

    auto conn = make_shared<Connection>(client, "localhost", listenPort);
    while (!gotClient);

    conn.reset();
    while(!lostClient);

    shutdown = true;
    pollTh.join();
}

BOOST_AUTO_TEST_CASE(hard_disconnect)
{
    const Port listenPort = portCounter++;

    Fork fork;
    disableBoostTestSignalHandler();

    if (fork.isParent()) {
        std::atomic<bool> gotClient(false);
        std::atomic<bool> lostClient(false);

        SourcePoller poller;

        EndpointProvider provider(listenPort);
        poller.add(provider);

        provider.onNewConnection = [&] (ConnectionHandle conn) {
            gotClient = true;
            printf("prv: new %d\n", conn);;
        };
        provider.onLostConnection = [&] (ConnectionHandle conn) {
            lostClient = true;
            printf("prv: lost %d\n", conn);;
        };

        std::atomic<bool> shutdown(false);
        auto pollFn = [&] { while (!shutdown) poller.poll(); };
        std::thread pollTh(pollFn);

        while(!gotClient);

        fork.killChild();

        while(!lostClient);

        shutdown = true;
        pollTh.join();
    }

    else {
        SourcePoller poller;

        EndpointClient client;
        poller.add(client);

        std::atomic<bool> shutdown(false);
        auto pollFn = [&] { while (!shutdown) poller.poll(); };
        std::thread pollTh(pollFn);

        Connection conn(client, "localhost", listenPort);

        while(true);
    }
}

