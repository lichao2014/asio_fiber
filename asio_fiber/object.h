#pragma once

#include "asio_fiber/thread.h"

namespace asio_fiber
{

template<typename T>
class Object : public T, public StopToken
{
public:
    static_assert(StopTraits<T>::value, "T must has stop or cancel or close");

    template<typename ... Args>
    Object(Args&& ... args)
        : T(*ThreadContext::current(), std::forward<Args>(args)...)
    {
        ThreadContext::current()->_stop_source.add_token(*this);
    }

    ~Object()
    {
        if (this->is_linked())
        {
            do_stop();
        }
    }

    template<typename C = ThreadContext>
    C* get_thread_ctx() noexcept
    {
        return ThreadContext::current<C>();
    }

    bool stop(StopMode mode) override
    {
        do_stop();
        return true;
    }
private:
    void do_stop()
    {
        StopTraits<T>{}(static_cast<T&>(*this));
    }
};

}
