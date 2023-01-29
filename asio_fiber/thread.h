#pragma once

#include <type_traits>
#include <vector>
#include <thread>

#include "boost/asio/executor_work_guard.hpp"
#include "boost/fiber/operations.hpp"

#include "asio_fiber/algo.h"
#include "asio_fiber/stop_token.h"

namespace asio_fiber
{

class ThreadContext : public boost::asio::io_context
{
public:
    template<typename C = ThreadContext>
    static typename std::enable_if<std::is_base_of<ThreadContext, C>::value, C *>::type
    current() noexcept
    {
        return static_cast<C *>(get_instance());
    }

    void stop()
    {
        this->dispatch([this] { do_stop(); });
    }
private:
    template<typename C>
    friend class ThreadGuard;

    void use_in_guard() { get_instance() = this; }

    template<typename C>
    friend class Object;

    void do_stop()
    {
        _stop_source.stop();
        boost::asio::io_context::stop();
    }

    static ThreadContext*& get_instance() noexcept
    {
        static thread_local ThreadContext* s_instance = nullptr;
        return s_instance;
    }

    StopSource _stop_source;
};

template<typename C = ThreadContext>
class ThreadGuard
{
public:
    ThreadGuard() : ThreadGuard(std::make_shared<C>()) {}

    explicit ThreadGuard(const std::shared_ptr<C>& ctx) noexcept : _ctx(ctx)
    {
        boost::fibers::use_scheduling_algorithm<Algorithm>(_ctx);
        _ctx->use_in_guard();
    }

    ~ThreadGuard() { _ctx->stop(); }

    template<typename F, typename ... Args>
    auto operator()(F&& f, Args&& ... args)
        -> decltype(f(args...))
    {
        auto work = boost::asio::make_work_guard(*_ctx);
        return std::forward<F>(f)(std::forward<Args>(args)...);
    }

    template<typename F, typename ... Args>
    auto operator()(F&& f, Args&& ... args)
        -> decltype(f(std::declval<const std::shared_ptr<C> &>(), args...))
    {
        auto work = boost::asio::make_work_guard(*_ctx);
        return std::forward<F>(f)(_ctx, std::forward<Args>(args)...);
    }

    template<typename F, typename ... Args>
    auto operator()(F&& f, Args&& ... args)
        -> decltype(f(std::declval<C&>(), args...))
    {
        auto work = boost::asio::make_work_guard(*_ctx);
        return std::forward<F>(f)(*_ctx, std::forward<Args>(args)...);
    }
private:
    ThreadGuard(const ThreadGuard&) = delete;
    void operator=(const ThreadGuard&) = delete;

    std::shared_ptr<C> _ctx;
};

template<typename C = ThreadContext>
class Thread
{
public:
    Thread(): _ctx(std::make_shared<C>()) {}

    template<typename F>
    Thread(F&& f) : Thread()
    {
        start(std::forward<F>(f));
    }

    template<typename F>
    void start(F&& f)
    {
        struct Wrap
        {
            typename std::decay<F>::type f;
            Thread* owner;

            void operator()()
            {
                ThreadGuard<C> guard(this->owner->_ctx);
                guard(std::move(this->f));
            }
        };

        _impl = std::thread(Wrap{ std::forward<F>(f), this });
    }

    void stop()
    {
        _ctx->stop();

        if (_impl.joinable())
        {
            _impl.join();
        }
    }

    template<typename F>
    void post(F&& f)
    {
        _ctx->post(std::forward<F>(f));
    }

    template<typename F>
    void dispatch(F&& f)
    {
        _ctx->dispatch(std::forward<F>(f));
    }

private:
    std::shared_ptr<C> _ctx;
    std::thread _impl;
};

template<typename C = ThreadContext>
class ThreadGroup
{
public:
    ~ThreadGroup() { stop_all(); }

    template<typename F>
    void add_thread(F&& f)
    {
        std::unique_ptr<Thread<C>> ptr(new Thread<C>(std::forward<F>(f)));
        _threads.emplace_back(std::move(ptr));
    }

    template<typename F>
    void add_threads(size_t n, F f)
    {
        for (size_t i = 0; i < n; ++i)
        {
            add_thread(f);
        }
    }

    void stop_all()
    {
        for (auto&& thread : _threads)
        {
            thread->stop();
        }

        _threads.clear();
    }

    template<typename F>
    void post(F f)
    {
        for (auto&& thread : _threads)
        {
            thread->post(f);
        }
    }
private:
    std::vector<std::unique_ptr<Thread<C>>> _threads;
};

}
