//===------------------------- mutex_trimmed.cpp --------------------------===//
//
// iokit project: real, unmodified LLVM libc++ source
// (third_party/llvm-project/libcxx/src/mutex.cpp), trimmed to omit only
// __call_once() (already provided separately by
// libcxx_call_once_compat.cpp -- see that file's own header for why it
// was split out first) -- real gap found booting launchd as PID 1
// (docs/backlog.md item d10): ICU's own umutex.cpp needs real
// std::mutex::lock()/unlock(), not just std::condition_variable.
//
// mutex.o was previously excluded wholesale from build_libcxx_dylib.sh
// because it also bundles std::timed_mutex/std::recursive_timed_mutex,
// which need real pthread_cond_timedwait() (still missing). Read
// directly, though: timed_mutex/recursive_timed_mutex's own lock()/
// unlock() here only call the condition_variable's UNTIMED wait()/
// notify_one() (already real again, see condition_variable_trimmed.cpp)
// -- it's only the separate, inline, header-only try_lock_for()/
// try_lock_until() template methods (which call wait_until(), needing
// the still-missing __do_timed_wait) that actually need the timed path,
// and neither those nor plain std::mutex/std::recursive_mutex touch it
// at all. So this whole file compiles and links clean now, real and
// complete, not a further-trimmed subset -- __call_once() is the only
// real omission, purely to avoid a duplicate-symbol conflict with the
// already-existing compat file.
//===----------------------------------------------------------------------===//

#include "mutex"
#include "limits"
#include "system_error"
#include "include/atomic_support.h"
#include "__undef_macros"

#ifndef _LIBCPP_HAS_NO_THREADS
#if defined(__ELF__) && defined(_LIBCPP_LINK_PTHREAD_LIB)
#pragma comment(lib, "pthread")
#endif
#endif

_LIBCPP_BEGIN_NAMESPACE_STD
#ifndef _LIBCPP_HAS_NO_THREADS

const defer_lock_t  defer_lock{};
const try_to_lock_t try_to_lock{};
const adopt_lock_t  adopt_lock{};

// ~mutex is defined elsewhere

void
mutex::lock()
{
    int ec = __libcpp_mutex_lock(&__m_);
    if (ec)
        __throw_system_error(ec, "mutex lock failed");
}

bool
mutex::try_lock() _NOEXCEPT
{
    return __libcpp_mutex_trylock(&__m_);
}

void
mutex::unlock() _NOEXCEPT
{
    int ec = __libcpp_mutex_unlock(&__m_);
    (void)ec;
    _LIBCPP_ASSERT(ec == 0, "call to mutex::unlock failed");
}

// recursive_mutex

recursive_mutex::recursive_mutex()
{
    int ec = __libcpp_recursive_mutex_init(&__m_);
    if (ec)
        __throw_system_error(ec, "recursive_mutex constructor failed");
}

recursive_mutex::~recursive_mutex()
{
    int e = __libcpp_recursive_mutex_destroy(&__m_);
    (void)e;
    _LIBCPP_ASSERT(e == 0, "call to ~recursive_mutex() failed");
}

void
recursive_mutex::lock()
{
    int ec = __libcpp_recursive_mutex_lock(&__m_);
    if (ec)
        __throw_system_error(ec, "recursive_mutex lock failed");
}

void
recursive_mutex::unlock() _NOEXCEPT
{
    int e = __libcpp_recursive_mutex_unlock(&__m_);
    (void)e;
    _LIBCPP_ASSERT(e == 0, "call to recursive_mutex::unlock() failed");
}

bool
recursive_mutex::try_lock() _NOEXCEPT
{
    return __libcpp_recursive_mutex_trylock(&__m_);
}

// timed_mutex

timed_mutex::timed_mutex()
    : __locked_(false)
{
}

timed_mutex::~timed_mutex()
{
    lock_guard<mutex> _(__m_);
}

void
timed_mutex::lock()
{
    unique_lock<mutex> lk(__m_);
    while (__locked_)
        __cv_.wait(lk);
    __locked_ = true;
}

bool
timed_mutex::try_lock() _NOEXCEPT
{
    unique_lock<mutex> lk(__m_, try_to_lock);
    if (lk.owns_lock() && !__locked_)
    {
        __locked_ = true;
        return true;
    }
    return false;
}

void
timed_mutex::unlock() _NOEXCEPT
{
    lock_guard<mutex> _(__m_);
    __locked_ = false;
    __cv_.notify_one();
}

// recursive_timed_mutex

recursive_timed_mutex::recursive_timed_mutex()
    : __count_(0),
      __id_{}
{
}

recursive_timed_mutex::~recursive_timed_mutex()
{
    lock_guard<mutex> _(__m_);
}

void
recursive_timed_mutex::lock()
{
    __thread_id id = this_thread::get_id();
    unique_lock<mutex> lk(__m_);
    if (id ==__id_)
    {
        if (__count_ == numeric_limits<size_t>::max())
            __throw_system_error(EAGAIN, "recursive_timed_mutex lock limit reached");
        ++__count_;
        return;
    }
    while (__count_ != 0)
        __cv_.wait(lk);
    __count_ = 1;
    __id_ = id;
}

bool
recursive_timed_mutex::try_lock() _NOEXCEPT
{
    __thread_id id = this_thread::get_id();
    unique_lock<mutex> lk(__m_, try_to_lock);
    if (lk.owns_lock() && (__count_ == 0 || id == __id_))
    {
        if (__count_ == numeric_limits<size_t>::max())
            return false;
        ++__count_;
        __id_ = id;
        return true;
    }
    return false;
}

void
recursive_timed_mutex::unlock() _NOEXCEPT
{
    unique_lock<mutex> lk(__m_);
    if (--__count_ == 0)
    {
        __id_.__reset();
        lk.unlock();
        __cv_.notify_one();
    }
}

#endif // !_LIBCPP_HAS_NO_THREADS

_LIBCPP_END_NAMESPACE_STD
