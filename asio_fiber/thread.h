#pragma once

#include <type_traits>
#include <vector>
#include <thread>

#include "boost/asio/executor_work_guard.hpp"
#include "boost/fiber/operations.hpp"

#include "asio_fiber/algo.h"
#include "stop_token.h"

namespace asio_fiber
{

class ThreadContext : public boost::asio::io_context
{
public:
    StopSource stop_source;

    void stop()
    {
        this->dispatch([this] { do_stop(); });
    }
private:
    void do_stop()
    {
        this->stop_source.stop();
        boost::asio::io_context::stop();
    }
};

class ThreadGuard
{
public:
    ThreadGuard() : ThreadGuard(std::make_shared<ThreadContext>()) {}

    explicit ThreadGuard(const std::shared_ptr<ThreadContext>& ctx) : _ctx(ctx)
    {
        boost::fibers::use_scheduling_algorithm<Algorithm>(_ctx);
    }

    ~ThreadGuard() { _ctx->stop(); }

    template<typename F, typename ... Args>
    auto operator()(F&& f, Args&& ... args)
        -> decltype(f(std::declval<const std::shared_ptr<ThreadContext> &>(), args...))
    {
        auto work = boost::asio::make_work_guard(*_ctx);
        return std::forward<F>(f)(_ctx, std::forward<Args>(args)...);
    }

    template<typename F, typename ... Args>
    auto operator()(F&& f, Args&& ... args)
        -> decltype(f(std::declval<ThreadContext&>(), args...))
    {
        auto work = boost::asio::make_work_guard(*_ctx);
        return std::forward<F>(f)(*_ctx, std::forward<Args>(args)...);
    }
private:
    ThreadGuard(const ThreadGuard&) = delete;
    void operator=(const ThreadGuard&) = delete;

    std::shared_ptr<ThreadContext> _ctx;
};

class ThreadEntry
{
public:
    ThreadEntry(): _ctx(std::make_shared<ThreadContext>()) {}

    template<typename F>
    ThreadEntry(F&& f) : ThreadEntry()
    {
        start(std::forward<F>(f));
    }

    template<typename F>
    void start(F&& f)
    {
        struct Wrap
        {
            typename std::decay<F>::type f;
            ThreadEntry* entry;

            void operator()()
            {
                ThreadGuard guard(entry->_ctx);
                guard(std::move(this->f));
            }
        };

        _th = std::thread(Wrap{ std::forward<F>(f), this });
    }

    void stop()
    {
        if (_th.joinable())
        {
            _ctx->stop();
            _th.join();
        }
    }

private:
    std::shared_ptr<ThreadContext> _ctx;
    std::thread _th;
};

class ThreadGroup
{
public:
    template<typename F>
    void add_thread(F&& f)
    {
        _threads.emplace_back(std::forward<F>(f));
    }

    template<typename F>
    void add_threads(size_t n, F f)
    {
        for (size_t i = 0; i < n; ++i)
        {
            add_thread(f);
        }
    }

    void join_all()
    {
        for (auto&& thread : _threads)
        {
            thread.stop();
        }
    }
private:
    std::vector<ThreadEntry> _threads;
};
}
