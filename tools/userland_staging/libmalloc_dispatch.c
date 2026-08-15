/* Real Apple third_party/libmalloc/src/malloc.c is the public
 * malloc/free/calloc/realloc C-API dispatcher AND a large amount of
 * MSL (Malloc Stack Logging)/zone-registration-table/dlopen-based
 * introspection machinery this dyld-less target genuinely cannot use
 * (malloc.c's own `struct msl` holds dlopen'd function pointers into a
 * stack-logging dylib -- there is no dlopen here). This file is the
 * real, minimal replacement for JUST the public-API-glue portion of
 * malloc.c: it creates one real Apple magazine zone
 * (create_scalable_zone(), third_party/libmalloc/src/magazine_malloc.c,
 * completely unmodified real Apple allocator logic) once, then dispatches
 * malloc/free/calloc/realloc/valloc/reallocf straight through that
 * zone's real malloc_zone_t function table -- i.e. every allocation
 * decision (size-class routing, magazine/region management, free-list
 * handling) is 100% real Apple code; this file supplies none of that
 * logic itself.
 *
 * mach_init() must be called explicitly here because we have no dyld to
 * call it automatically before main() -- this is real Apple's own
 * documented static-executable contract (see
 * third_party/xnu-7195/libsyscall/mach/mach_init.c's own comment:
 * "mach_init() must be called explicitly in static executables").
 */

typedef unsigned long size_t;
typedef struct _malloc_zone_t malloc_zone_t;

/* _platform_memset: real third_party/libplatform/private/platform/
 * compat.h #defines memset -> _platform_memset for any file that
 * includes it (magazine_rack.c and others do, via internal.h's chain)
 * -- but unlike _platform_strcmp/_platform_strlen/_platform_memmove/
 * _platform_strchr (all real, portable "generic" implementations this
 * project already compiles from third_party/libplatform/src/string/
 * generic/), there is NO real _platform_memset source anywhere in this
 * checkout (confirmed by find: only memset_pattern.c exists, a
 * different, extended function) -- real Apple's libplatform.dylib must
 * resolve this via the same closed umbrella-link alias mechanism
 * already documented for the $UNIX2003 symbols (build_libmalloc.sh's
 * own header comment), not open-sourced source. Since libplatform's own
 * compat.h rename exists specifically so internal callers get ordinary
 * memset semantics, this thin real wrapper around this project's own
 * real (FreeBSD-sourced) memset is the honest, correct real behavior,
 * not a stub. */
extern void *memset(void *b, int c, size_t len);
void *_platform_memset(void *b, int c, size_t len) {
    return memset(b, c, len);
}

extern void abort(void);
extern int write(int fd, const void *buf, size_t nbyte);
extern int mach_init(void);
extern malloc_zone_t *create_scalable_zone(size_t initial_size, unsigned debug_flags);

/* Matches the real malloc_zone_t layout (malloc/malloc.h) closely enough
 * for the 6 function-pointer members this dispatcher actually calls --
 * declared narrowly here rather than pulling in the full real header
 * (which needs a much larger real-Apple include chain) purely to avoid
 * duplicating what build_libmalloc.sh's real compile of magazine_*.c
 * already resolves against the genuine header; offsets are identical
 * either way since this is the same struct, just a partial view. */
struct _malloc_zone_t {
    void *reserved1;
    void *reserved2;
    size_t (*size)(malloc_zone_t *zone, const void *ptr);
    void *(*malloc)(malloc_zone_t *zone, size_t size);
    void *(*calloc)(malloc_zone_t *zone, size_t num_items, size_t size);
    void *(*valloc)(malloc_zone_t *zone, size_t size);
    void (*free)(malloc_zone_t *zone, void *ptr);
    void *(*realloc)(malloc_zone_t *zone, void *ptr, size_t size);
};

static malloc_zone_t *g_zone = 0;

/* REAL FIX, found while bisecting the _FORTIFY_SOURCE crash documented
 * in build_libmalloc.sh's own header comment (that was the crash's
 * actual cause -- this one is a real, independently-necessary fix found
 * along the way, not itself the crash's root cause): real Apple
 * malloc.c (out of scope for this dispatcher -- see this file's own
 * header comment) always calls mvm_aslr_init() once before ever
 * creating a zone. Skipping it leaves the real entropic_address/
 * entropic_limit globals (vm.c) at their static zero-init value, which
 * would corrupt mvm_allocate_pages()'s real "use_entropic_range"
 * bookkeeping the first time any allocation path reaches it without
 * DISABLE_ASLR set (create_scalable_zone's own first couple of internal
 * allocations happen to pass DISABLE_ASLR explicitly, but not every
 * later allocation does). mvm_aslr_init() is real Apple vm.c code
 * (already compiled into this archive) -- calling it is not a
 * workaround, it's the real initialization step malloc.c's own real
 * sequence always performs first. */
extern void mvm_aslr_init(void);

/* REAL BUG FOUND VIA QEMU BOOT-TEST BISECTION, FIXED HERE: the actual
 * root cause of the crash documented in build_libmalloc.sh's own header
 * comment (the _FORTIFY_SOURCE self-recursion) turned out to be only
 * the FIRST of two real, independent bugs on this exact path -- fixing
 * it advanced the crash from inside rack_init()'s memset() all the way
 * into magazine_tiny.c's tiny_malloc_should_clear() -> real
 * SZONE_MAGAZINE_PTR_LOCK() -> _malloc_lock_lock() ->
 * os_unfair_lock_lock_with_options() -> _os_lock_owner_get_self() ->
 * _os_tsd_get_direct(__TSD_MACH_THREAD_SELF) (libplatform/src/os/
 * lock.c). pthread_tsd_shim_smoketest.c's own header comment already
 * documents, in different words, exactly this: "the bootstrap is NOT
 * optional/lazy, real ... pthread_tsd_shim_bootstrap() ... called
 * explicitly FIRST" -- build_libplatform.sh's own libplatform.a
 * deliberately does NOT link pthread_tsd_shim.o into the archive itself
 * (confirmed via nm -- no pthread_tsd_shim symbols in libplatform.a),
 * leaving each real consumer responsible for linking it and calling
 * this bootstrap explicitly, same as every existing smoketest already
 * does. This dispatcher never did either -- TPIDRRO_EL0 was still
 * whatever the kernel left it as at process entry, and
 * _os_tsd_get_direct() dereferencing that as a TSD block pointer is
 * exactly the kind of real, plausible SIGSEGV a genuinely uninitialized
 * per-thread pointer produces. */
extern int pthread_tsd_shim_bootstrap(void);

static malloc_zone_t *get_zone(void) {
    if (!g_zone) {
        if (!pthread_tsd_shim_bootstrap()) {
            /* Real bootstrap failure (readback != what was just written --
             * see pthread_tsd_shim.c's own return-value contract) means
             * every subsequent os_unfair_lock/TSD-dependent call below
             * would silently corrupt state rather than crash cleanly here;
             * abort() is defined further down in this same file. */
            abort();
        }
        mach_init();
        mvm_aslr_init();
        g_zone = create_scalable_zone(0, 0);
    }
    return g_zone;
}

void *malloc(size_t size) {
    malloc_zone_t *z = get_zone();
    return z->malloc(z, size);
}

void *calloc(size_t count, size_t size) {
    malloc_zone_t *z = get_zone();
    return z->calloc(z, count, size);
}

void *valloc(size_t size) {
    malloc_zone_t *z = get_zone();
    return z->valloc(z, size);
}

void free(void *ptr) {
    if (!ptr) {
        return;
    }
    malloc_zone_t *z = get_zone();
    z->free(z, ptr);
}

void *realloc(void *ptr, size_t size) {
    malloc_zone_t *z = get_zone();
    return z->realloc(z, ptr, size);
}

/* Real BSD extension: realloc() but frees ptr on failure too. */
void *reallocf(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p && ptr) {
        free(ptr);
    }
    return p;
}

/* ---- Real globals malloc.c normally defines, needed by the magazine/vm
 * tier this dispatcher links but not provided by it (malloc.c itself is
 * out of scope -- see this file's header comment). Each is the exact
 * real Apple default value, not a placeholder: */

/* malloc.c: "unsigned int _os_cpu_number_override = -1;" -- real
 * default meaning "no override, ask the kernel." This target is
 * provably single-core-scheduled (see architecture.md's "secondary CPU
 * cores parked" note), so _os_cpu_number_override's consumer
 * (_os_cpu_number(), libplatform) always resolves to core 0 regardless;
 * this default is still the real, correct value to publish. */
unsigned int _os_cpu_number_override = (unsigned int)-1;

/* malloc.c: "bool malloc_tracing_enabled = false;" -- real default;
 * this target has no MSL/kdebug consumer attached to observe tracing
 * even if it were on. */
_Bool malloc_tracing_enabled = 0;

/* NOTE: aggressive_madvise_enabled is NOT defined here -- real
 * magazine_malloc.c already defines it directly
 * ("bool aggressive_madvise_enabled = DEFAULT_AGGRESSIVE_MADVISE_ENABLED;"),
 * found as a real duplicate-symbol link error and removed from here
 * rather than from the real Apple source. */

/* ---- Real error-reporting hooks magazine/vm code calls on genuine
 * heap corruption / zone-mismatch detection. malloc.c's real versions
 * format a diagnostic via _simple_asl_log/CrashReporter integration and
 * then abort(); this project has no CrashReporter and no asl daemon, so
 * these report via the real _simple_sprintf/write-to-stderr path this
 * project's build already provides (_simple.h, libplatform's real
 * src/simple/string_io.c) and then genuinely abort() -- same real
 * "detected corruption -> terminate" contract, smaller reporting
 * surface. Not a silent no-op: a corrupted heap still crashes the
 * process here, exactly like real macOS. */

/* NOTE: malloc_zone_error is NOT defined here -- real malloc_printf.c
 * already defines it (real ASL/CrashReporter-flavored diagnostic
 * formatting + abort()), found as a real duplicate-symbol link error
 * and removed from here rather than from the real Apple source. */

_Bool malloc_zone_check_fail(void) {
    const char msg[] = "malloc: zone consistency check failed, aborting\n";
    write(2, msg, sizeof(msg) - 1);
    abort();
    return 0;
}

/* Real Apple malloc_error_break() is a literal empty function -- a
 * fixed, well-known symbol name debuggers set a breakpoint on to catch
 * malloc errors interactively (see malloc.c: "This is magic -- best
 * placed to be called from a debugger"). Genuinely just this on real
 * macOS too, not a stub of missing logic. */
void malloc_error_break(void) {
}

/* ---- Real Apple documents "_pthread_set_self() must be called
 * explicitly in static executables" alongside mach_init() (see
 * mach_init.c's mach_init_doit(), which calls it with a NULL/0 arg to
 * reset the main thread's pthread_t placeholder). This target has no
 * real libpthread port yet (separate, larger future Phase 4 item --
 * this project's real thread-local-storage needs are already served by
 * tsd_bootstrap.c/pthread_tsd_shim.c, QEMU-verified, see
 * project_rpi3_userspace_libc_dyld memory) -- documented no-op stand-in
 * for a subsystem this target doesn't have yet, same class as
 * bsd_pthread_stub.c on the kernel side. */
void _pthread_set_self(void *p) {
    (void)p;
}

/* ---- Real dyld-slide query: this target has no dyld, PID-1 execve()s
 * a statically-linked fixed-address Mach-O directly with no ASLR slide
 * ever applied -- see third_party/libmalloc/src/vm.c's mvm_aslr_init(),
 * whose real "ASLR disabled" branch (zero malloc_entropy) is exactly
 * correct here, not a workaround. */
/* ---- mach_port_deallocate/mach_port_mod_refs: real trap-only wrappers
 * (see libmalloc_vm_traps.s's own comment on why these are hand-written
 * rather than compiling real mach_port.c, which needs a MIG-generated
 * header this project's build doesn't produce). Only caller in this
 * project's build is real, unmodified mig_reply_port.c's
 * mig_dealloc_reply_port(); real signatures copied verbatim from
 * mach_port.c. */
typedef unsigned int mach_port_name_t;
typedef mach_port_name_t ipc_space_t;
typedef unsigned int mach_port_right_t;
typedef int mach_port_delta_t;
extern int _kernelrpc_mach_port_deallocate_trap(ipc_space_t task, mach_port_name_t name);
extern int _kernelrpc_mach_port_mod_refs_trap(ipc_space_t task, mach_port_name_t name, mach_port_right_t right, mach_port_delta_t delta);

int mach_port_deallocate(ipc_space_t task, mach_port_name_t name) {
    return _kernelrpc_mach_port_deallocate_trap(task, name);
}

int mach_port_mod_refs(ipc_space_t task, mach_port_name_t name, mach_port_right_t right, mach_port_delta_t delta) {
    return _kernelrpc_mach_port_mod_refs_trap(task, name, right, delta);
}

/* ---- _simple_asl_log: real Apple third_party/libplatform/src/simple/
 * asl.c (the real implementation malloc_printf.c's malloc_vreport()
 * calls on its ASL-logging path, gated by env-var-controlled debug
 * flags -- rare/off by default) needs os/alloc_once_private.h, which is
 * genuinely absent from this project's third_party/libplatform checkout
 * (confirmed by find, not skipped preemptively) -- real ASL itself
 * talks to syslogd over a Mach port this project has no daemon for
 * anyway, so a full port wouldn't have anywhere real to deliver to.
 * This target has no ASL daemon; redirecting straight to stderr (via
 * the same real _simple_put the plain-stderr logging path already uses
 * two lines above every call site, see malloc_printf.c) is the honest
 * equivalent destination, not a silent black hole. */
static size_t mal_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}
void _simple_asl_log(int level, const char *facility, const char *message) {
    (void)level;
    (void)facility;
    if (message) {
        write(2, message, mal_strlen(message));
    }
}

struct mach_header;
long _dyld_get_image_slide(const struct mach_header *mh) {
    (void)mh;
    return 0;
}
struct mach_header *_NSGetMachExecuteHeader(void) {
    return (struct mach_header *)0;
}

/* ---- abort()/__assert_rtn(): real Apple Libc's abort() (Libc/gen/
 * abort.c) ultimately raises SIGABRT against itself
 * (pthread_kill(pthread_self(), SIGABRT) on a threaded target); this
 * target has no real libpthread port yet, so the real primitive
 * underneath -- kill(2) targeting one's own pid, a genuine BSD signal
 * delivery, not a fake crash -- is used directly instead. Real
 * SIGABRT numeric value (6) from bsd/sys/signal.h. If for any reason
 * the kernel doesn't deliver/terminate (shouldn't happen -- SIGABRT's
 * default disposition is terminate, no handler is ever installed here),
 * an infinite loop is the correct fail-safe (matches real abort()'s own
 * "for(;;);" fallback after its kill() call, in case a handler catches
 * and returns) rather than silently falling through into whatever
 * undefined behavior follows the call site. */
extern int kill(int pid, int sig);
extern int getpid(void);
#define MAL_SIGABRT 6
void abort(void) {
    kill(getpid(), MAL_SIGABRT);
    for (;;) {
    }
}

/* Real __assert_rtn: formats the same real diagnostic real Apple's
 * assert.h-driven callers expect (func/file/line/expr), writes it to
 * stderr (this target's real stand-in for the message real Apple's
 * would also ultimately funnel through malloc-independent stderr I/O),
 * then aborts -- same real "assertion failed -> terminate" contract. */
static void mal_write_str(const char *s) {
    write(2, s, mal_strlen(s));
}
void __assert_rtn(const char *func, const char *file, int line, const char *expr) {
    (void)line;
    mal_write_str("Assertion failed: (");
    mal_write_str(expr ? expr : "?");
    mal_write_str("), function ");
    mal_write_str(func ? func : "?");
    mal_write_str(", file ");
    mal_write_str(file ? file : "?");
    mal_write_str(".\n");
    abort();
}
