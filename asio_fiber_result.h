#pragma once

#include <tuple>
#include <chrono>
#include <type_traits>

#include "boost/asio/async_result.hpp"
#include "boost/asio/error.hpp"
#include "boost/system/result.hpp"
#include "boost/fiber/context.hpp"
#include "boost/optional.hpp"
#include "boost/assert.hpp"

namespace asio_fiber
{

template<bool Timeout>
class YieldContext {};

template<>
class YieldContext<true> 
{
public:
    using Clock = std::chrono::steady_clock;

    constexpr explicit YieldContext(Clock::time_point tp = (Clock::time_point::max)()) noexcept : _expired(tp) {}
    constexpr explicit YieldContext(Clock::duration dur) noexcept : _expired(Clock::now() + dur) {}

    Clock::time_point get_expired() const noexcept { return _expired; }
    bool has_expired() const noexcept { return _expired != (Clock::time_point::max)(); }
private:
    Clock::time_point _expired;
};

constexpr YieldContext<false> yield;

template<typename T>
constexpr YieldContext<true> timeout_yield(T&& t) { return YieldContext<true>{ std::forward<T>(t) }; }

template<typename ... Ts>
struct YieldReturn
{
    using type = std::tuple<Ts...>;
};

template<typename T>
struct YieldReturn<T>
{
    using type = T;
};

template<>
struct YieldReturn<>
{
    using type = void;
};

template<bool Timeout>
class YieldPolicy
{
public:
    template<typename H>
    void init(H& h) noexcept
    {
        auto&& token = h.get_token();
        if (token.has_expired())
        {
            _timeout_ctx.emplace(token.get_expired());
            h.set_slot(_timeout_ctx->cs.slot());
        }
    }

    bool wait(boost::fibers::context *fctx, bool& is_done)
    {
        if (_timeout_ctx)
        {
            fctx->wait_until(_timeout_ctx->tp);

            if (is_done)
            {
                return true;
            }

            _is_timeout = true;
            _timeout_ctx->cs.emit(boost::asio::cancellation_type::total);
        }

        return false;
    }

    template<typename Return, typename E, typename ... Args>
    void on_completion(Return& return_value, E&& ec, Args&& ... args) noexcept
    {
        if (_is_timeout)
        {
            return_value = boost::asio::error::make_error_code(boost::asio::error::timed_out);
        }
        else
        {
            if (ec)
            {
                return_value = std::forward<E>(ec);
            }
            else
            {
                return_value.emplace(std::forward<Args>(args)...);
            }
        }
    }

private:
    struct TimeoutCtx
    {
        std::chrono::steady_clock::time_point tp;
        boost::asio::cancellation_signal cs;

        explicit TimeoutCtx(std::chrono::steady_clock::time_point tp) noexcept : tp(tp) {}
    };

    boost::optional<TimeoutCtx> _timeout_ctx;
    bool _is_timeout = false;
};

template<>
class YieldPolicy<false>
{
public:
    template<typename H>
    void init(H&) noexcept {}

    bool wait(boost::fibers::context* fctx, bool& is_done) noexcept { return false; }

    template<typename Return, typename E, typename ... Args>
    void on_completion(Return& return_value, E&& ec, Args&& ... args) noexcept
    {
        if (ec)
        {
            return_value = std::forward<E>(ec);
        }
        else
        {
            return_value.emplace(std::forward<Args>(args)...);
        }
    }
};
}

namespace boost
{
namespace asio
{
template<bool Timeout, typename ... Ts>
class async_result<asio_fiber::YieldContext<Timeout>, void(boost::system::error_code, Ts...)> : private asio_fiber::YieldPolicy<Timeout>
{
public:
    using return_type = system::result<typename asio_fiber::YieldReturn<Ts...>::type>;

    class completion_handler_type
    {
    public:
        using cancellation_slot_type = boost::asio::cancellation_slot;

        template<typename T>
        explicit completion_handler_type(T&& token) noexcept : _token(std::forward<T>(token)) {}

        template<typename E, typename ... Args>
        void operator()(E&& ec, Args&& ... args)
        {
            _result->on_completion(std::forward<E>(ec), std::forward<Args>(args)...);
        }

        void set_result(async_result* result) noexcept { _result = result; }
        void set_slot(cancellation_slot_type slot) noexcept { _slot = slot; }

        const asio_fiber::YieldContext<Timeout>& get_token() const noexcept { return _token; }
        cancellation_slot_type get_cancellation_slot() const noexcept { return _slot; }
    private:
        asio_fiber::YieldContext<Timeout> _token;
        async_result* _result = nullptr;
        cancellation_slot_type _slot;
    };

    explicit async_result(completion_handler_type& h) noexcept
        : _fctx(boost::fibers::context::active())
    {
        BOOST_ASSERT(_fctx != nullptr);
        h.set_result(this);

        YieldPolicy::init(h);
    }

    return_type get()
    {
        while (!_is_done)
        {
            _is_waiting = true;

            if (YieldPolicy::wait(_fctx, _is_done))
            {
                break;
            }

            _fctx->suspend();
        }

        BOOST_ASSERT(_is_done);
        return std::move(_return_value);
    }
private:
    template<typename E, typename ... Args>
    void on_completion(E&& ec, Args&& ... args) noexcept
    {
        YieldPolicy::on_completion(_return_value, std::forward<E>(ec), std::forward<Args>(args)...);

        BOOST_ASSERT(!_is_done);
        _is_done = true;

        if (_is_waiting)
        {
            boost::fibers::context::active()->schedule(_fctx);
        }
    }

    return_type _return_value{ system::error_code() };
    fibers::context* _fctx = nullptr;
    bool _is_done = false;
    bool _is_waiting = false;
};
}
}
