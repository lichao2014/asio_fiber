#include <iostream>
#include <sstream>

#include "boost/asio.hpp"
#include "boost/beast.hpp"
#include "boost/program_options.hpp"
#include "boost/system.hpp"
#include "boost/algorithm/string.hpp"
#include "boost/container/static_vector.hpp"
#include "boost/convert.hpp"
#include "boost/convert/strtol.hpp"
#include "boost/signals2/signal.hpp"
#include "boost/core/ignore_unused.hpp"
#include "boost/scope_exit.hpp"

#ifdef _USE_SSL
    #include "boost/asio/ssl.hpp"
#endif

#include "asio_fiber/async_result.h"
#include "asio_fiber/thread.h"

namespace fibers = boost::fibers;
namespace this_fiber = boost::this_fiber;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

struct Options
{
    size_t count = 0;
    std::string addr;
    std::string redirect;
    std::string origin;
    std::string tcurl;
    std::string app;

    bool parse(int argc, const char *argv[])
    {
        namespace po = boost::program_options;

        po::options_description desc("sched302 test tool");
        desc.add_options()
            ("help,H", "print help info")
            ("count,C", po::value(&count)->default_value(0), "302 self repeat count")
            ("addr,A", po::value(&addr)->default_value("0.0.0.0:8080"), "local addr [host:port]")
            ("redirect,R", po::value(&redirect), "302 redirect addr [host:port]")
            ("origin", po::value(&origin)->default_value("tct"), "302 response Origin header")
            ("tcurl", po::value(&tcurl)->default_value("http://tpl.edgeorgn.com/live"), "302 response TcUrl header")
            ("app", po::value(&app)->default_value("live"), "302 response stream app");

        po::variables_map vars;
        try
        {
            po::store(po::parse_command_line(argc, argv, desc), vars);
        }
        catch (const std::exception& e)
        {
            std::cerr << "parse opts failed,err=" << e.what() << std::endl;
            return false;
        }

        vars.notify();

        if (vars.count("help"))
        {
            desc.print(std::clog, 4);
            return false;
        }

        return true;
    }

    boost::system::result<net::ip::tcp::endpoint> get_laddr() const
    {
        using namespace boost;

        boost::container::static_vector<std::string, 2> args;
        boost::algorithm::split(args, this->addr, boost::is_any_of(":"));
        if (args.size() != 2)
        {
            std::cerr << "bad addr param" << std::endl;
            return system::errc::make_error_code(system::errc::bad_address);
        }

        system::error_code ec;
        auto addr = net::ip::address::from_string(args[0], ec);
        if (ec)
        {
            std::cerr << "bad addr ip param" << std::endl;
            return ec;
        }

        auto r = boost::convert<uint16_t>(args[1], boost::cnv::strtol{});
        if (!r.has_value())
        {
            std::cerr << "bad addr port param" << std::endl;
            return system::errc::make_error_code(system::errc::bad_address);
        }

        return net::ip::tcp::endpoint{ addr, *r };
    }
} g_opts;

struct AppCtx
{
    size_t req_count = 0;
    boost::signals2::signal<void()> on_close;

    void close()
    {
        on_close();
    }
};

template<typename AsyncStream>
struct StreamTraits
{
    static void close(AsyncStream& stream)
    {
        stream.close();
    }

    static typename std::decay<AsyncStream>::type::endpoint_type
    local_endpoint(const AsyncStream& stream)
    {
        return stream.local_endpoint();
    }
};

#ifdef _USE_SSL
template<typename AsyncStream>
struct StreamTraits<net::ssl::stream<AsyncStream>>
{
    static void close(net::ssl::stream<AsyncStream>& stream)
    {
        stream.async_shutdown(asio_fiber::yield);
        stream.lowest_layer().close();
    }

    static typename AsyncStream::endpoint_type local_endpoint(const net::ssl::stream<AsyncStream>& stream)
    {
        return stream.lowest_layer().local_endpoint();
    }
};
#endif

template<typename AsyncStream>
boost::system::result<void>
service_fn(AsyncStream client, const std::shared_ptr<AppCtx>& app_ctx)
{
    beast::flat_buffer buf(8096);
    http::request<http::dynamic_body> req;

    auto ret = http::async_read(client, buf, req, asio_fiber::yield);
    if (!ret)
    {
        std::clog << "client read failed,err=" << ret.error().message() << std::endl;
        return ret.error();
    }

    std::clog << "Got http req=" << req.target() << std::endl;

    http::response<http::empty_body> resp{ http::status::found, req.version() };
    resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    resp.set(http::field::origin, g_opts.origin);
    resp.set("X-ReqCount", std::to_string(app_ctx->req_count));
    resp.set("TcUrl", g_opts.tcurl);
    resp.prepare_payload();

    std::ostringstream loc_builder;
    loc_builder << "http://";

    if (app_ctx->req_count++ >= g_opts.count && !g_opts.redirect.empty())
    {
        app_ctx->req_count = 0;
        loc_builder << g_opts.redirect;
    }
    else
    {
        loc_builder << StreamTraits<AsyncStream>::local_endpoint(client);
    }

    loc_builder << "/" << g_opts.app;
    auto loc = loc_builder.str();

    resp.set(http::field::location, loc);

    auto r = http::async_write(client, resp, asio_fiber::yield);

    std::clog << "Send http response=" << loc << ",ok=" << r.has_value() << std::endl;

    return {};
}

boost::system::result<void>
serve_http(net::io_context& io_ctx, const std::shared_ptr<AppCtx>& app_ctx)
{
    auto r = g_opts.get_laddr();
    if (!r)
    {
        return r.error();
    }

    boost::system::error_code ec;

    auto acceptor = std::make_shared<net::ip::tcp::acceptor>(io_ctx, net::ip::tcp::v4());
    acceptor->bind(*r, ec);
    if (ec)
    {
        std::cerr << "bind failed" << ec.message() << std::endl;
        return ec;
    }

    acceptor->listen(net::socket_base::max_listen_connections, ec);
    if (ec)
    {
        std::cerr << "listen failed" << ec.message() << std::endl;
        return ec;
    }

    std::clog << "Listen at " << acceptor->local_endpoint() << std::endl;

    boost::signals2::scoped_connection scoped = app_ctx->on_close.connect([acceptor] { acceptor->close(); });
    boost::ignore_unused(scoped);

    BOOST_SCOPE_EXIT(&acceptor) {
        acceptor->close();
    } BOOST_SCOPE_EXIT_END;

#ifdef _USE_SSL
    net::ssl::context ssl_ctx(net::ssl::context_base::tls_server);

    ssl_ctx.use_certificate_file("server.crt", net::ssl::context_base::pem);
    ssl_ctx.set_password_callback(
        [] (size_t size, net::ssl::context_base::password_purpose) -> std::string {
            return "123456";
        }
    );
    ssl_ctx.use_private_key_file("server.key", net::ssl::context_base::pem, ec);

    if (ec)
    {
        std::cerr << "use_private_key_file failed" << ec.message() << std::endl;
        return ec;
    }
#endif
    while (true)
    {
        auto client = acceptor->async_accept(asio_fiber::yield);
        if (!client)
        {
            return client.error();
        }

        std::clog << "Accept client=" << client->remote_endpoint() << std::endl;

#ifdef _USE_SSL
        net::ssl::stream<net::ip::tcp::socket> ssl_client(std::move(*client), ssl_ctx);

        auto hs_ret = ssl_client.async_handshake(net::ssl::stream_base::server, asio_fiber::yield);
        if (!hs_ret)
        {
            std::cerr << "ssl hs failed,err=" << hs_ret.error().message() << std::endl;
            continue;
        }

        fibers::fiber(service_fn<decltype(ssl_client)>, std::move(ssl_client), app_ctx).detach();
#else
        fibers::fiber(service_fn<decltype(*client)>, std::move(*client), app_ctx).detach();
#endif
    }

    return {};
}

boost::system::result<void>
async_main(net::io_context& io_ctx)
{
    auto app_ctx = std::make_shared<AppCtx>();

    fibers::fiber(serve_http, std::ref(io_ctx), app_ctx).detach();

    net::signal_set t(io_ctx, SIGTERM, SIGINT);
    auto sig = t.async_wait(asio_fiber::yield);
    if (!sig)
    {
        std::clog << "sig=" << sig.error() << std::endl;
    }

    app_ctx->close();

    return {};
}

int main(int argc, const char *argv[])
{
    if (!g_opts.parse(argc, argv))
    {
        return -1;
    }

    asio_fiber::ThreadGuard guard;
    return guard(async_main) ? 0 : -1;
}
