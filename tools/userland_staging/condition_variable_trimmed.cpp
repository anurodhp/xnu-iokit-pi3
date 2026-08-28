//===-------------------- condition_variable_trimmed.cpp -----------------===//
//
// iokit project: real, unmodified LLVM libc++ source
// (third_party/llvm-project/libcxx/src/condition_variable.cpp), trimmed to
// exactly notify_one()/notify_all()/wait() -- the real gap found booting
// launchd as PID 1 (docs/backlog.md item d10): ICU's own umutex.cpp/
// unifiedcache.cpp call std::condition_variable::notify_all() and the
// untimed wait(unique_lock<mutex>&) overload only (confirmed via a real
// grep -- neither ever calls wait_for/wait_until), but the real upstream
// file's own condition_variable::__do_timed_wait() needs real
// pthread_cond_timedwait() with POSIX absolute-deadline semantics, which
// this project doesn't have yet (see build_libcxx_dylib.sh's own header
// comment -- only untimed pthread_cond_wait() and the non-POSIX relative-
// deadline pthread_cond_timedwait_relative_np() exist), so the whole real
// file was excluded wholesale rather than compiled with an unresolvable
// undefined symbol. notify_all_at_thread_exit() is also dropped here
// (needs __thread_local_data(), part of the separately-excluded <thread>
// support, same real gap class -- see that same header comment) -- ICU
// never calls it either.
//
// Real fix, not a stub: notify_one/notify_all/wait below are byte-for-byte
// the real upstream implementation, calling the real
// __libcpp_condvar_signal/broadcast/wait helpers (already inline in
// __threading_support, already proven working -- std::mutex itself
// already uses the same real pthread_cond/pthread_mutex primitives).
// __do_timed_wait/notify_all_at_thread_exit remain a real, deliberate,
// already-documented gap (build_libcxx_dylib.sh) -- a real
// pthread_cond_timedwait() port unblocks them, not attempted here.
//===----------------------------------------------------------------------===//

#include "__config"

#ifndef _LIBCPP_HAS_NO_THREADS

#include "condition_variable"
#include "system_error"
#include "__undef_macros"

_LIBCPP_BEGIN_NAMESPACE_STD

// ~condition_variable is defined elsewhere.

void
condition_variable::notify_one() _NOEXCEPT
{
    __libcpp_condvar_signal(&__cv_);
}

void
condition_variable::notify_all() _NOEXCEPT
{
    __libcpp_condvar_broadcast(&__cv_);
}

void
condition_variable::wait(unique_lock<mutex>& lk) _NOEXCEPT
{
    if (!lk.owns_lock())
        __throw_system_error(EPERM,
                                  "condition_variable::wait: mutex not locked");
    int ec = __libcpp_condvar_wait(&__cv_, lk.mutex()->native_handle());
    if (ec)
        __throw_system_error(ec, "condition_variable wait failed");
}

_LIBCPP_END_NAMESPACE_STD

#endif // !_LIBCPP_HAS_NO_THREADS
