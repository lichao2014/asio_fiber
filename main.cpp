#include <iostream>

#include "boost/asio.hpp"
#include "boost/beast.hpp"

#include "asio_fiber/async_result.h"
#include "asio_fiber/thread.h"

namespace fibers = boost::fibers;
namespace this_fiber = boost::this_fiber;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

int async_main(const std::shared_ptr<net::io_context>& io_ctx)
{
#if 1
    auto acceptor = std::make_shared<net::ip::tcp::acceptor>(*io_ctx, net::ip::tcp::v4());

    boost::system::error_code ec;
    acceptor->bind({ net::ip::tcp::v4(), 8080 }, ec);
    if (ec)
    {
        std::clog << "bind failed" << ec.message() << std::endl;
        return -1;
    }

    acceptor->listen(net::socket_base::max_listen_connections, ec);
    if (ec)
    {
        return -1;
    }

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
                    std::clog << "client read failed,err=" << ret.error().message() << std::endl;
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

                http::async_write(client, resp, asio_fiber::yield);

                client.close();
            }).detach();
        }
    }).detach();
#endif
    fibers::fiber([&, io_ctx] {
        net::steady_timer t(*io_ctx);
        while (!io_ctx->stopped())
        {
            t.expires_after(std::chrono::seconds(10));
            auto sig = t.async_wait(asio_fiber::timeout_yield(std::chrono::seconds(1)));
            if (sig)
            {
                break;
            }

            std::clog << "sig=" << sig.error() << std::endl;
        }
    }).detach();

    net::signal_set t(*io_ctx, SIGTERM, SIGINT);
    auto sig = t.async_wait(asio_fiber::yield);
    if (!sig)
    {
        std::clog << "sig=" << sig.error() << std::endl;
    }

    acceptor->close();

    return 0;
}

int main()
{
    asio_fiber::Guard guard;
    return guard(async_main);
}
