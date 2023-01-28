#pragma once

#include <tuple>
#include <chrono>
#include <type_traits>

#include "boost/asio/async_result.hpp"
#include "boost/asio/error.hpp"
#include "boost/asio/cancellation_signal.hpp"
#include "boost/system/result.hpp"
#include "boost/fiber/context.hpp"
#include "boost/optional.hpp"
#include "boost/assert.hpp"

namespace asio_fiber
{

class TimeoutContext
{
public:
    using Clock = std::chrono::steady_clock;

    constexpr TimeoutContext(Clock::time_point expire_at = (Clock::time_point::max)()) noexcept
        : _expire_at(expire_at) {}

    template<typename Rep, typename Period>
    TimeoutContext(std::chrono::duration<Rep, Period> duration) noexcept
        : _expire_at(Clock::now() + duration) {}

    Clock::time_point expire_at() const noexcept { return _expire_at; }
    bool has_expired() const noexcept { return _expire_at != (Clock::time_point::max)(); }
private:
    Clock::time_point _expire_at;
};

template<bool Timeout>
class YieldContext {};

template<>
class YieldContext<true> : public TimeoutContext
{
public:
    using TimeoutContext::TimeoutContext;
};

constexpr YieldContext<false> yield() { return {}; }

template<typename T>
YieldContext<true> yield(T&& t) { return YieldContext<true>{ std::forward<T>(t) }; }

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
            _timeout_ctx.emplace(token.expire_at());
            h.set_slot(_timeout_ctx->slot());
        }
    }

    bool wait(boost::fibers::context *fctx, bool& is_done)
    {
        if (_timeout_ctx)
        {
            fctx->wait_until(_timeout_ctx->expire_at());

            if (is_done)
            {
                return true;
            }

            _is_timeout = true;
            _timeout_ctx->emit(boost::asio::cancellation_type::total);
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
                return_value = Return(std::forward<E>(ec));
            }
            else
            {
                return_value = Return(std::forward<Args>(args)...);
            }
        }
    }

private:
    struct TimeoutCtx : boost::asio::cancellation_signal, TimeoutContext
    {
        using TimeoutContext::TimeoutContext;
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
            return_value = Return(std::forward<E>(ec));
        }
        else
        {
            return_value = Return(std::forward<Args>(args)...);
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
    using yield_policy = asio_fiber::YieldPolicy<Timeout>;

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

        yield_policy::init(h);
    }

    return_type get()
    {
        while (!_is_done)
        {
            _is_waiting = true;

            if (yield_policy::wait(_fctx, _is_done))
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
        yield_policy::on_completion(_return_value, std::forward<E>(ec), std::forward<Args>(args)...);

        BOOST_ASSERT(!_is_done);
        _is_done = true;

        if (_is_waiting)
        {
            auto fctx = boost::fibers::context::active();
            fctx->schedule(_fctx);
        }
    }

    return_type _return_value { system::error_code() };
    fibers::context* _fctx = nullptr;
    bool _is_done = false;
    bool _is_waiting = false;
};
}
}
