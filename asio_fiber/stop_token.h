#pragma once

#include <type_traits>

#include "boost/intrusive/list.hpp"
#include "boost/type_traits.hpp"

namespace asio_fiber
{

enum class StopMode
{
    FORCE,
    SMOOTH
};

class StopToken
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>
{
public:
    virtual ~StopToken() = default;
    virtual bool stop(StopMode mode) = 0;
};

class StopSource
{
public:
    ~StopSource() { stop(StopMode::FORCE); }

    void stop(StopMode mode = StopMode::FORCE)
    {
        for (auto&& token : _tokens)
        {
            token.stop(mode);
        }

        _tokens.clear();
    }

    void add_token(StopToken& token) noexcept
    {
        _tokens.push_back(token);
    }
private:
    boost::intrusive::list<StopToken, boost::intrusive::constant_time_size<false>> _tokens;
};

template<typename T>
class StopTokenFunction : public StopToken
{
public:
    template<typename ... Args>
    StopTokenFunction(Args&& ... args) : _func(std::forward<Args>(args)...) {}

    bool stop(StopMode mode) override
    {
        return test_call(_func, mode);
    }
private:
    template<typename F>
    static auto test_call(F& f, StopMode mode)
        -> typename std::enable_if<std::is_same<bool, decltype(f(mode))>::value, bool>::type
    {
        return f(mode);
    }

    template<typename F>
    static auto test_call(F& f, StopMode mode)
        -> typename std::enable_if<!std::is_same<bool, decltype(f(mode))>::value, bool>::type
    {
        f(mode);
        return true;
    }

    template<typename F>
    static auto test_call(F& f, StopMode mode)
        -> typename std::enable_if<std::is_same<bool, decltype(f())>::value, bool>::type
    {
        return f();
    }

    template<typename F>
    static auto test_call(F& f, StopMode mode)
        -> typename std::enable_if<!std::is_same<bool, decltype(f())>::value, bool>::type
    {
        f();
        return true;
    }

    T _func;
};

template<typename F>
StopTokenFunction<typename std::decay<F>::type>
make_stop_token(F&& f)
{
    return { std::forward<F>(f) };
}

namespace detail
{
template<typename T>
struct Nothing : std::false_type
{
    static void execute(T& x) {}
};

template<typename T, typename = boost::void_t<>>
struct HasCancelHelper : Nothing<T> {};

template<typename T>
struct HasCancelHelper<T, boost::void_t<decltype(std::declval<T>().cancel())>> : std::true_type
{
    static void execute(T& x)
    {
        try
        {
            x.cancel();
        }
        catch (...) {}
    }
};

template<typename T>
struct HasCancelHelper<T, boost::void_t<decltype(std::declval<T>().next_layer().cancel())>> : std::true_type
{
    static void execute(T& x)
    {
        try
        {
            x.next_layer().cancel();
        }
        catch (...) {}
    }
};

template<typename T>
using HasCancel = HasCancelHelper<T>;

template<typename T, typename = boost::void_t<>>
struct HasStopHelper : Nothing<T> {};

template<typename T>
struct HasStopHelper<T, boost::void_t<decltype(std::declval<T>().stop())>> : std::true_type
{
    static void execute(T& x)
    {
        try
        {
            x.stop();
        }
        catch (...) {}
    }
};

template<typename T>
struct HasStopHelper<T, boost::void_t<decltype(std::declval<T>().next_layer().stop())>> : std::true_type
{
    static void execute(T& x)
    {
        try
        {
            x.next_layer().stop();
        }
        catch (...) {}
    }
};

template<typename T>
using HasStop = HasStopHelper<T>;

template<typename T, typename = boost::void_t<>>
struct HasCloseHelper : Nothing<T> {};

template<typename T>
struct HasCloseHelper<T, boost::void_t<decltype(std::declval<T>().next_layer().close())>> : std::true_type
{
    static void execute(T& x)
    {
        try
        {
            x.next_layer().close();
        }
        catch (...) {}
    }
};

template<typename T>
struct HasCloseHelper<T, boost::void_t<decltype(std::declval<T>().close())>> : std::true_type
{
    static void execute(T& x)
    {
        try
        {
            x.close();
        }
        catch (...) {}
    }
};


template<typename T>
using HasClose = HasCloseHelper<T>;

template<typename T, template<typename> class ... Traits>
using TestTraits = boost::disjunction<Traits<T>...>;
}

template<typename T>
struct StopTraits : detail::TestTraits<T, detail::HasCancel, detail::HasStop, detail::HasClose>
{
    void operator()(T& x)
    {
        detail::HasCancel<T>::execute(x);
        detail::HasStop<T>::execute(x);
        detail::HasClose<T>::execute(x);
    }
};
}
