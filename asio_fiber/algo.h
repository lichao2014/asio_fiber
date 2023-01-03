#pragma once

#include <memory>

#include "boost/asio/io_context.hpp"
#include "boost/fiber/algo/algorithm.hpp"
#include "boost/fiber/scheduler.hpp"
#include "boost/assert.hpp"

namespace asio_fiber
{

class Algorithm : public boost::fibers::algo::algorithm
{
public:
    explicit Algorithm(const std::shared_ptr<boost::asio::io_context>& io_ctx) noexcept : _io_ctx(io_ctx){}

    void awakened(boost::fibers::context* fctx) noexcept override
    {
        BOOST_ASSERT(fctx != nullptr);
        BOOST_ASSERT(!fctx->ready_is_linked());
        fctx->ready_link(_worker_queue);
    }

    boost::fibers::context* pick_next() noexcept override
    {
        if (!_worker_queue.empty())
        {
            auto fctx = &(_worker_queue.front());
            _worker_queue.pop_front();
            return fctx;
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
    std::shared_ptr<boost::asio::io_context> _io_ctx;
    boost::fibers::scheduler::ready_queue_type _worker_queue;
};

}
