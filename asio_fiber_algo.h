#pragma once

#include <memory>

#include "boost/asio/io_context.hpp"
#include "boost/fiber/algo/algorithm.hpp"
#include "boost/fiber/scheduler.hpp"
#include "boost/fiber/operations.hpp"
#include "boost/assert.hpp"

namespace asio_fiber
{

class Algorithm : public boost::fibers::algo::algorithm
{
public:
    explicit Algorithm(const std::shared_ptr<boost::asio::io_context>& io_ctx) noexcept
        : _io_ctx(io_ctx)
    {
        defer_yield();
    }

    void awakened(boost::fibers::context* fctx) noexcept override
    {
        BOOST_ASSERT(fctx != nullptr);
        BOOST_ASSERT(!fctx->ready_is_linked());

        if (fctx->is_context(boost::fibers::type::worker_context))
        {
            fctx->ready_link(_worker_queue);
        }
        else if (fctx->is_context(boost::fibers::type::main_context))
        {
            _main_fctx = fctx;
        }
        else if (fctx->is_context(boost::fibers::type::dispatcher_context))
        {
            _disp_fctx = fctx;
        }
    }

    boost::fibers::context* pick_next() noexcept override
    {
        if (!_worker_queue.empty())
        {
            auto ctx = &(_worker_queue.front());
            _worker_queue.pop_front();
            return ctx;
        }

        if (boost::fibers::context::active() != _disp_fctx)
        {
            return _disp_fctx;
        }

        if (!_is_main_exited && _io_ctx->stopped())
        {
            _is_main_exited = true;
            return _main_fctx;
        }

        return nullptr;
    }

    bool has_ready_fibers() const noexcept override
    {
        return !_worker_queue.empty();
    }

    void suspend_until(std::chrono::steady_clock::time_point const& abs_time) noexcept override
    {
        _io_ctx->run_one_until(abs_time);
    }

    void notify() noexcept override
    {
        _io_ctx->post([] {});
    }
private:
    void defer_yield() noexcept
    {
        _io_ctx->post([] {
            if (!boost::fibers::context::active()->is_context(boost::fibers::type::dispatcher_context))
            {
                boost::this_fiber::yield();
            }
        });
    }

    std::shared_ptr<boost::asio::io_context> _io_ctx;
    boost::fibers::scheduler::ready_queue_type _worker_queue;
    boost::fibers::context* _main_fctx = nullptr;
    boost::fibers::context* _disp_fctx = nullptr;
    bool _is_main_exited = false;
};

}
