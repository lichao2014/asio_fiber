#include <memory>
#include <thread>

#include "boost/asio.hpp"
#include "boost/fiber/future.hpp"
#include "boost/fiber/operations.hpp"
#include "boost/beast.hpp"

#include "asio_fiber_result.h"
#include "asio_fiber_algo.h"

namespace fibers = boost::fibers;
namespace this_fiber = boost::this_fiber;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

int main()
{
    auto ioc = std::make_shared<net::io_context>();
    fibers::use_scheduling_algorithm<asio_fiber::Algorithm>(ioc);
    this_fiber::yield();
#if 1
    auto acceptor = std::make_shared<net::ip::tcp::acceptor>(*ioc, net::ip::tcp::v4());

    boost::system::error_code ec;
    acceptor->bind({ net::ip::tcp::v4(), 8080 }, ec);
    if (ec)
    {
        std::clog << "bind failed" << ec.message() << std::endl;
        ioc->stop();
        return -1;
    }

    acceptor->listen(net::socket_base::max_listen_connections, ec);
    if (ec)
    {
        ioc->stop();
        return -1;
    }

    this_fiber::yield();

#if 0
    fibers::fiber([&, acceptor] {
        while (true)
        {
            auto client = acceptor->async_accept(asio_fiber::yield);
            if (!client)
            {
                break;
            }

            std::clog << "accept " << client->remote_endpoint() << std::endl;

            fibers::fiber([client = std::move(*client)]() mutable {
                beast::flat_buffer buf(8096);
                http::request<http::dynamic_body> req;

                auto ret = http::async_read(client, buf, req, asio_fiber::yield);
                if (!ret)
                {
                    client.close();
                    return;
                }

                if ("/test" == req.target())
                {
                    return;
                }

                http::response<http::string_body> resp{ http::status::ok, req.version() };

                resp.body() = "hello";
                resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                resp.set(http::field::content_type, "text/html");

                http::async_write(client, resp, asio_fiber::timeout_yield(std::chrono::seconds(1)));

                client.close();
            }).detach();
        }
    }).detach();
#endif
    fibers::fiber([&, ioc] {
        net::steady_timer t(*ioc);
        while (!ioc->stopped())
        {
            t.expires_after(std::chrono::seconds(10));
            auto sig = t.async_wait(asio_fiber::timeout_yield(std::chrono::seconds(1)));
            if (sig)
            {
                break;
            }

            std::clog << "sig=" << sig.error() << std::endl;
        }

        ioc->stop();
    }).detach();

    fibers::fiber([&, ioc] {
        net::signal_set t(*ioc, SIGTERM, SIGINT);
        while (!ioc->stopped())
        {
            auto sig = t.async_wait(asio_fiber::yield);
            if (sig)
            {
                break;
            }

            std::clog << "sig=" << sig.error() << std::endl;
        }

        ioc->stop();
    }).detach();

    this_fiber::yield();
    ioc->run();
    this_fiber::yield();
    acceptor->close();
#endif

    return 0;
}
