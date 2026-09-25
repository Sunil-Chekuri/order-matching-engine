#include <gtest/gtest.h>

#include "net/tcp_client.h"
#include "net/tcp_server.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

// These are the only tests in the suite that need a real socket, and
// there are deliberately few of them: everything about *what* a request
// means is covered in protocol_test.cpp without any networking. What is
// left to prove here is the transport itself — that a reply comes back,
// that the stream is reframed into lines correctly, that several
// clients can be served at once, and that the server shuts down without
// hanging.
//
// Every server binds port 0, so the OS picks a free port and the suite
// never collides with something already listening or with a parallel
// ctest job.
namespace
{
    struct ServerFixture
    {
        OrderGateway gateway;
        TcpServer server{gateway};

        ServerFixture()
        {
            started = server.start(0);
        }

        ~ServerFixture()
        {
            server.stop();
        }

        bool connectClient(TcpClient &client)
        {
            return client.connectTo("127.0.0.1", server.port());
        }

        bool started = false;
    };
}

TEST(TcpServerTest, StartsOnAnEphemeralPortAndReportsIt)
{
    ServerFixture fixture;

    ASSERT_TRUE(fixture.started);
    EXPECT_TRUE(fixture.server.isRunning());
    EXPECT_NE(fixture.server.port(), 0);
}

TEST(TcpServerTest, RoundTripsASingleRequest)
{
    ServerFixture fixture;
    ASSERT_TRUE(fixture.started);

    TcpClient client;
    ASSERT_TRUE(fixture.connectClient(client));

    std::string response;
    ASSERT_TRUE(client.request("PING", response));

    EXPECT_EQ(response, "OK pong");
}

TEST(TcpServerTest, SubmitsAndCancelsOverTheWire)
{
    ServerFixture fixture;
    ASSERT_TRUE(fixture.started);

    TcpClient client;
    ASSERT_TRUE(fixture.connectClient(client));

    std::string response;

    ASSERT_TRUE(client.request("SUBMIT 1 BUY LIMIT 100.0 10", response));
    EXPECT_EQ(response, "OK submitted 1 resting 10");

    ASSERT_TRUE(client.request("SNAPSHOT 1", response));
    EXPECT_EQ(response,
              "OK {\"bids\":[{\"price\":100,\"quantity\":10,\"orders\":1}],"
              "\"asks\":[]}");

    ASSERT_TRUE(client.request("CANCEL 1", response));
    EXPECT_EQ(response, "OK cancelled 1");

    ASSERT_TRUE(client.request("SNAPSHOT 1", response));
    EXPECT_EQ(response, "OK {\"bids\":[],\"asks\":[]}");
}

TEST(TcpServerTest, SeveralRequestsInOneWriteGetSeveralReplies)
{
    // The framing test that matters. TCP is a byte stream: these three
    // commands arrive in a single recv, and a server that assumed "one
    // read, one request" would answer the first and silently swallow
    // the other two. It passes by hand either way — only a batched
    // write catches it.
    ServerFixture fixture;
    ASSERT_TRUE(fixture.started);

    TcpClient client;
    ASSERT_TRUE(fixture.connectClient(client));

    ASSERT_TRUE(client.sendLine("PING\nPING\nSUBMIT 1 BUY LIMIT 100.0 10"));

    std::string first;
    std::string second;
    std::string third;

    ASSERT_TRUE(client.readLine(first));
    ASSERT_TRUE(client.readLine(second));
    ASSERT_TRUE(client.readLine(third));

    EXPECT_EQ(first, "OK pong");
    EXPECT_EQ(second, "OK pong");
    EXPECT_EQ(third, "OK submitted 1 resting 10");
}

TEST(TcpServerTest, ARequestSplitAcrossWritesIsStillAnsweredOnce)
{
    // The other half of the framing problem: half a command now, the
    // rest later. The server must buffer rather than act on a partial
    // line or reject it.
    ServerFixture fixture;
    ASSERT_TRUE(fixture.started);

    TcpClient client;
    ASSERT_TRUE(fixture.connectClient(client));

    ASSERT_TRUE(client.sendRaw("SUBMIT 1 BUY LI"));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_TRUE(client.sendRaw("MIT 100.0 10\n"));

    std::string response;
    ASSERT_TRUE(client.readLine(response));

    EXPECT_EQ(response, "OK submitted 1 resting 10");
}

TEST(TcpServerTest, AnInvalidRequestDoesNotDropTheConnection)
{
    // A bad request is a client error, not a connection error. If the
    // exception from Order's validation escaped the protocol layer it
    // would kill this connection thread and the next read would fail.
    ServerFixture fixture;
    ASSERT_TRUE(fixture.started);

    TcpClient client;
    ASSERT_TRUE(fixture.connectClient(client));

    std::string response;

    ASSERT_TRUE(client.request("NONSENSE", response));
    EXPECT_EQ(response, "ERR unknown_command");

    ASSERT_TRUE(client.request("SUBMIT 1 BUY LIMIT -5 10", response));
    EXPECT_EQ(response.rfind("ERR rejected", 0), 0u) << response;

    ASSERT_TRUE(client.request("PING", response));
    EXPECT_EQ(response, "OK pong");
}

TEST(TcpServerTest, QuitClosesTheConnectionAfterReplying)
{
    ServerFixture fixture;
    ASSERT_TRUE(fixture.started);

    TcpClient client;
    ASSERT_TRUE(fixture.connectClient(client));

    std::string response;
    ASSERT_TRUE(client.request("QUIT", response));
    EXPECT_EQ(response, "OK bye");

    // The reply is sent first, then the socket closes — so the next
    // read fails rather than returning anything.
    std::string ignored;
    EXPECT_FALSE(client.readLine(ignored));
}

TEST(TcpServerTest, ClientsShareOneBook)
{
    ServerFixture fixture;
    ASSERT_TRUE(fixture.started);

    TcpClient writer;
    TcpClient reader;
    ASSERT_TRUE(fixture.connectClient(writer));
    ASSERT_TRUE(fixture.connectClient(reader));

    std::string response;
    ASSERT_TRUE(writer.request("SUBMIT 1 BUY LIMIT 100.0 10", response));

    // A second connection is a second thread against the same gateway,
    // so it must see what the first one did.
    ASSERT_TRUE(reader.request("QTY 1", response));
    EXPECT_EQ(response, "OK qty 10");
}

TEST(TcpServerTest, ConcurrentClientsAreAllServedAndAllAccountedFor)
{
    // The engine is thread-safe per symbol (Week 2), which is what lets
    // the server run a thread per connection with no lock of its own.
    // This checks that end to end: every order submitted concurrently
    // is present in the final counters exactly once.
    ServerFixture fixture;
    ASSERT_TRUE(fixture.started);

    const int client_count = 4;
    const int per_client = 25;

    std::atomic<int> accepted{0};

    std::vector<std::thread> workers;

    for (int c = 0; c < client_count; ++c)
    {
        workers.emplace_back(
            [&fixture, &accepted, c, per_client]()
            {
                TcpClient client;

                if (!fixture.connectClient(client))
                    return;

                for (int i = 0; i < per_client; ++i)
                {
                    std::string response;

                    const int id = c * 1000 + i;

                    if (client.request(
                            "SUBMIT " + std::to_string(id) +
                                " BUY LIMIT 100.0 10 SHARED",
                            response) &&
                        response.rfind("OK", 0) == 0)
                    {
                        ++accepted;
                    }
                }
            });
    }

    for (std::thread &worker : workers)
        worker.join();

    EXPECT_EQ(accepted.load(), client_count * per_client);

    TcpClient checker;
    ASSERT_TRUE(fixture.connectClient(checker));

    std::string response;
    ASSERT_TRUE(checker.request("METRICS", response));

    const std::string expected =
        "\"orders_submitted\":" + std::to_string(client_count * per_client);

    EXPECT_NE(response.find(expected), std::string::npos) << response;
}

TEST(TcpServerTest, StopIsIdempotentAndLeavesNothingRunning)
{
    ServerFixture fixture;
    ASSERT_TRUE(fixture.started);

    TcpClient client;
    ASSERT_TRUE(fixture.connectClient(client));

    // Stopping with a connection still open is the case that hangs if
    // the connection threads are never woken — a thread parked in recv()
    // does not notice a flag.
    fixture.server.stop();
    EXPECT_FALSE(fixture.server.isRunning());

    fixture.server.stop();
    EXPECT_FALSE(fixture.server.isRunning());
}

TEST(TcpServerTest, StopReleasesThePortItWasBoundTo)
{
    // Proves stop() really closed the listening socket, not just
    // flipped a flag: the port can be bound again immediately.
    //
    // The obvious version of this test — connect to the dead port and
    // expect failure — costs about two seconds on Windows, because a
    // refused loopback connect goes through SYN retries before giving
    // up. Rebinding checks the same property and returns at once, which
    // matters for a suite whose whole runtime is a few seconds.
    OrderGateway gateway;

    unsigned short port = 0;

    {
        TcpServer server(gateway);
        ASSERT_TRUE(server.start(0));
        port = server.port();
        server.stop();
    }

    OrderGateway second_gateway;
    TcpServer second(second_gateway);

    EXPECT_TRUE(second.start(port));
    EXPECT_EQ(second.port(), port);

    second.stop();
}
