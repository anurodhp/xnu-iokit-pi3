// Real STL smoketest for build_llvm_libcxx.sh: std::vector, std::string,
// std::sort, std::shared_ptr -- real container/algorithm code paths that
// don't depend on live stack unwinding, consistent with this project's
// llvm_libcxxabi_smoketest.cpp precedent (see that file's header comment
// and docs/daemon-roadmap.md's Phase 6.5 section for the still-open,
// separately-tracked throw/catch unwind gap). No libc available beyond
// raw syscalls -- same raw syscall pattern as crt0_smoketest.c /
// llvm_libcxxabi_smoketest.cpp.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// Real per-thread TPIDRRO_EL0 TSD-base install (pthread_main_thread_
// bootstrap.c) -- must run before any pthread_key_create()/
// pthread_setspecific() call. libc++abi's cxa_exception_storage.o pulls
// exactly those in for its own thread-local exception-globals storage
// the moment anything in this binary needs __cxa_get_globals (RTTI/
// dynamic_cast machinery does), so this isn't optional bootstrap
// ceremony -- skipping it is the same real crash class documented in
// pthread_tsd_platform_bootstrap.c's header comment.
extern "C" int pthread_main_thread_bootstrap(void);

static long syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}

static void write_str(const char *s, unsigned long len) {
    syscall3(4 /* SYS_write */, 1, (long)s, (long)len);
}

[[noreturn]] static void exit_now(int code) {
    syscall3(1 /* SYS_exit */, code, 0, 0);
    for (;;) { }
}

extern "C" int main(int argc, char **argv) {
    (void)argc; (void)argv;
    pthread_main_thread_bootstrap();

    std::vector<int> v{5, 3, 1, 4, 2};
    std::sort(v.begin(), v.end());
    if (v[0] != 1 || v[4] != 5) {
        static const char msg[] = "llvm_libcxx_smoketest: FAILED (vector/sort)\n";
        write_str(msg, sizeof(msg) - 1);
        exit_now(1);
    }

    std::string s = "hello";
    s += " world";
    if (s != "hello world") {
        static const char msg[] = "llvm_libcxx_smoketest: FAILED (string)\n";
        write_str(msg, sizeof(msg) - 1);
        exit_now(1);
    }

    auto sp = std::make_shared<int>(42);
    auto sp2 = sp;
    if (*sp2 != 42 || sp.use_count() != 2) {
        static const char msg[] = "llvm_libcxx_smoketest: FAILED (shared_ptr)\n";
        write_str(msg, sizeof(msg) - 1);
        exit_now(1);
    }

    static const char msg[] = "llvm_libcxx_smoketest: real vector/sort/string/shared_ptr OK\n";
    write_str(msg, sizeof(msg) - 1);
    exit_now(0);
}
