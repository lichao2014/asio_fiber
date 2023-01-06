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

boost::system::result<void> async_http(asio_fiber::ThreadContext& ctx)
{
    using Acceptor = asio_fiber::StopGuard<net::ip::tcp::acceptor>;
    Acceptor acceptor{ ctx, net::ip::tcp::v4() };

    boost::system::error_code ec;
    acceptor.bind({ net::ip::tcp::v4(), 8080 }, ec);
    if (ec)
    {
        std::clog << "bind failed" << ec.message() << std::endl;
        return ec;
    }

    acceptor.listen(net::socket_base::max_listen_connections, ec);
    if (ec)
    {
        std::clog << "listen failed" << ec.message() << std::endl;
        return ec;
    }

    while (!ctx.stopped())
    {
        auto client = acceptor.async_accept(asio_fiber::yield);
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

    return {};
}

int async_main(asio_fiber::ThreadContext& ctx)
{
    fibers::fiber([&] {
        asio_fiber::StopGuard<net::steady_timer> t(ctx);
        while (!ctx.stopped())
        {
            t.expires_after(std::chrono::seconds(1));
            auto sig = t.async_wait(asio_fiber::yield);
            if (!sig)
            {
                break;
            }

            std::clog << "on_timer " << std::chrono::steady_clock::now().time_since_epoch().count() << std::endl;
        }
    }).detach();

    asio_fiber::ThreadGroup tg;
    tg.add_thread([] (asio_fiber::ThreadContext& ctx) { async_http(ctx); });

    net::signal_set t(ctx, SIGTERM, SIGINT);
    auto sig = t.async_wait(asio_fiber::yield);
    if (!sig)
    {
        std::clog << "sig=" << sig.error() << std::endl;
    }

    tg.join_all();

    return 0;
}

int main()
{
    asio_fiber::ThreadGuard guard;
    return guard(async_main);
}
