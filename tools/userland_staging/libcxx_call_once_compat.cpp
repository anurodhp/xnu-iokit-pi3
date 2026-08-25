// Real gap found migrating libcxx.a into real libc++.dylib (migration
// step 5, docs/daemon-roadmap.md): real LLVM libcxx/src/mutex.cpp bundles
// __call_once() (used by std::call_once/std::once_flag, needed
// pervasively -- e.g. locale.cpp's own lazy facet initialization) in the
// SAME translation unit as std::timed_mutex/std::recursive_timed_mutex,
// which need real pthread_cond_timedwait() -- a real, separate gap this
// project doesn't have closed yet (see build_libcxx_dylib.sh's own
// header comment for the full writeup: only pthread_cond_wait(), not
// the timed variant, exists so far). __call_once() itself, read
// directly from the real source, needs only __libcpp_mutex_lock/
// __libcpp_condvar_wait/__libcpp_mutex_unlock/__libcpp_condvar_broadcast
// -- the UNTIMED primitives, already real and working -- so excluding
// the whole mutex.o over the timed-mutex gap would lose this real,
// widely-needed function for no real reason.
//
// This file is a real, verbatim copy of __call_once()'s function body
// from third_party/llvm-project/libcxx/src/mutex.cpp (llvmorg-11.1.0,
// this project's pinned tag) -- not reimplemented, not approximated --
// isolated into its own translation unit purely so it can be compiled
// and linked without dragging in std::timed_mutex's real, separate,
// still-open dependency. Compiled with the exact same real CXX_FLAGS
// build_llvm_libcxx.sh already establishes for every other libcxx
// source file (see build_libcxx_dylib.sh's own invocation), so it
// produces the identical object code real Apple's own build would.
#include "mutex"
#include "limits"
#include "system_error"
#include "include/atomic_support.h"
#include "__undef_macros"

_LIBCPP_BEGIN_NAMESPACE_STD
#ifndef _LIBCPP_HAS_NO_THREADS

_LIBCPP_SAFE_STATIC static __libcpp_mutex_t mut = _LIBCPP_MUTEX_INITIALIZER;
_LIBCPP_SAFE_STATIC static __libcpp_condvar_t cv = _LIBCPP_CONDVAR_INITIALIZER;

void __call_once(volatile once_flag::_State_type& flag, void* arg,
                 void (*func)(void*))
{
    __libcpp_mutex_lock(&mut);
    while (flag == 1)
        __libcpp_condvar_wait(&cv, &mut);
    if (flag == 0)
    {
#ifndef _LIBCPP_NO_EXCEPTIONS
        try
        {
#endif  // _LIBCPP_NO_EXCEPTIONS
            __libcpp_relaxed_store(&flag, once_flag::_State_type(1));
            __libcpp_mutex_unlock(&mut);
            func(arg);
            __libcpp_mutex_lock(&mut);
            __libcpp_atomic_store(&flag, ~once_flag::_State_type(0),
                                  _AO_Release);
            __libcpp_mutex_unlock(&mut);
            __libcpp_condvar_broadcast(&cv);
#ifndef _LIBCPP_NO_EXCEPTIONS
        }
        catch (...)
        {
            __libcpp_mutex_lock(&mut);
            __libcpp_relaxed_store(&flag, once_flag::_State_type(0));
            __libcpp_mutex_unlock(&mut);
            __libcpp_condvar_broadcast(&cv);
            throw;
        }
#endif  // _LIBCPP_NO_EXCEPTIONS
    }
    else
        __libcpp_mutex_unlock(&mut);
}

#endif // !_LIBCPP_HAS_NO_THREADS
_LIBCPP_END_NAMESPACE_STD
