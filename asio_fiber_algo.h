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

        if (fctx->is_context(boost::fibers::type::main_context))
        {
            _main_fctx = fctx;
            _main_ctx_awakened = true;
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

        if (_main_ctx_awakened)
        {
            _main_ctx_awakened = false;
            return _main_fctx;
        }

        if (_io_run_in_main_ctx && !_io_main_ctx_resumed && _io_ctx->stopped())
        {
            _io_main_ctx_resumed = true;
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
    void io_yield() noexcept
    {
        _io_ctx->post([this] {
            auto fctx = boost::fibers::context::active();
            if (fctx->is_context(boost::fibers::type::main_context))
            {
                _main_fctx = fctx;
                _io_run_in_main_ctx = true;
                fctx->suspend();
            }
            else if (!fctx->is_context(boost::fibers::type::dispatcher_context))
            {
                fctx->yield();
            }
        });
    }

    using WorkGuard = boost::asio::executor_work_guard<
        boost::asio::associated_executor_t<boost::asio::io_context>>;

    std::shared_ptr<boost::asio::io_context> _io_ctx;
    WorkGuard _io_work_guard;
    boost::fibers::scheduler::ready_queue_type _worker_queue;
    boost::fibers::context* _main_fctx = nullptr;
    bool _io_run_in_main_ctx = false;
    bool _io_main_ctx_resumed = false;
    bool _main_ctx_awakened = false;
};

}
