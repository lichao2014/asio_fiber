#pragma once

#include <memory>

#include "boost/asio/io_context.hpp"
#include "boost/asio/executor_work_guard.hpp"
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
        , _io_work_guard(boost::asio::make_work_guard(*io_ctx))
    {
        io_yield();
    }

    void awakened(boost::fibers::context* fctx) noexcept override
    {
        BOOST_ASSERT(fctx != nullptr);
        BOOST_ASSERT(!fctx->ready_is_linked());

        if (fctx == _io_fctx)
        {
            BOOST_ASSERT(_io_fctx_resumed);
            _io_fctx_awakened = true;
        }
        else
        {
            fctx->ready_link(_worker_queue);
        }
    }

    boost::fibers::context* pick_next() noexcept override
    {
        if (!_worker_queue.empty())
        {
            auto fctx = &(_worker_queue.front());
            _worker_queue.pop_front();
            return fctx;
        }

        if (_io_fctx_awakened)
        {
            _io_fctx_awakened = false;
            return _io_fctx;
        }

        if (!_io_fctx_resumed && _io_ctx->stopped())
        {
            _io_fctx_resumed = true;
            return _io_fctx;
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
    void io_yield() noexcept
    {
        _io_ctx->post([this] {
            auto fctx = boost::fibers::context::active();
            if (!fctx->is_context(boost::fibers::type::dispatcher_context))
            {
                _io_fctx = fctx;
                fctx->suspend();
            }
        });
    }

    using WorkGuard = boost::asio::executor_work_guard<
        boost::asio::associated_executor_t<boost::asio::io_context>>;

    std::shared_ptr<boost::asio::io_context> _io_ctx;
    WorkGuard _io_work_guard;
    boost::fibers::scheduler::ready_queue_type _worker_queue;
    boost::fibers::context* _io_fctx = nullptr;
    bool _io_fctx_resumed = false;
    bool _io_fctx_awakened = false;
};

}
