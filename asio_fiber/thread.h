#pragma once

#include <type_traits>
#include <vector>
#include <thread>

#include "boost/asio/executor_work_guard.hpp"
#include "boost/fiber/operations.hpp"

#include "asio_fiber/algo.h"

namespace asio_fiber
{

class Guard
{
public:
    Guard()
        : _io_ctx(std::make_shared<boost::asio::io_context>())
    {
        boost::fibers::use_scheduling_algorithm<Algorithm>(_io_ctx);
    }

    ~Guard() { _io_ctx->stop(); }

    template<typename F, typename ... Args>
    typename std::result_of<F(const std::shared_ptr<boost::asio::io_context>&, Args...)>::type
    operator()(F&& f, Args&& ... args)
    {
        auto work = boost::asio::make_work_guard(_io_ctx);
        return std::forward<F>(f)(_io_ctx, std::forward<Args>(args)...);
    }
private:
    Guard(const Guard&) = delete;
    void operator=(const Guard&) = delete;

    std::shared_ptr<boost::asio::io_context> _io_ctx;
};

class ThreadGroup
{
public:
    template<typename F, typename ... Args>
    void add_thread(F&& f, Args&& ... args)
    {
        _threads.emplace_back([f, args...] mutable {
            Guard guard;
            guard(std::move(f), std::move(args)...);
        });
    }

    template<typename F, typename ... Args>
    void add_threads(size_t n, F f, Args ... args)
    {
        for (size_t i = 0; i < n; ++i)
        {
            add_thread(f, args...);
        }
    }

    void join_all()
    {
        for (auto&& thread : _threads)
        {
            thread.join();
        }
    }

private:
    std::vector<std::thread> _threads;
};

}
