#include <memory>
#include <thread>

#include "boost/asio.hpp"
#include "boost/fiber/future.hpp"
#include "boost/fiber/operations.hpp"
#include "boost/beast.hpp"

#include "asio_fiber_result.h"
#include "asio_fiber_algo.h"

using namespace boost;

namespace http = beast::http;

struct AppCtx : asio::io_context
{
    bool is_started = false;
};

int main()
{
    auto ioc = std::make_shared<AppCtx>();
    fibers::use_scheduling_algorithm<asio_fiber::Algorithm>(ioc);

    asio::ip::tcp::acceptor acceptor(*ioc, asio::ip::tcp::v4());

    boost::system::error_code ec;
    acceptor.bind({ asio::ip::tcp::v4(), 8080 }, ec);
    if (ec)
    {
        std::clog << "bind failed" << ec.message() << std::endl;
        ioc->stop();
        return -1;
    }

    acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec)
    {
        ioc->stop();
        return -1;
    }

    fibers::fiber([&] {
        while (true)
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
                    client.close();
                    return;
                }

                if ("/test" == req.target())
                {
                    return;
                }

                beast::flat_buffer x;

                http::response<http::file_body> resp{ http::status::ok, req.version() };

                beast::error_code ec;
                resp.body().open("D:/proj/c/boost_test/main.cpp", beast::file_mode::read, ec);
                resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                resp.set(http::field::content_type, "text/html");

                http::async_write(client, resp, asio_fiber::timeout_yield(std::chrono::seconds(1)));

                client.close();
            }).detach();
        }
    }).detach();

    fibers::fiber([&, ioc] {
        if (!ioc->is_started)
        {
            return;
        }

        asio::signal_set t(*ioc, SIGTERM, SIGINT);
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

    ioc->is_started = true;
    ioc->run();

    acceptor.close();

    return 0;
}
