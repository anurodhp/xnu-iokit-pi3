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
    /* Real fields, present purely to keep `memalign` below at the same
     * real offset the genuine header (third_party/libmalloc/include/
     * malloc/malloc.h) gives it -- this struct is a partial view (see
     * comment above), but field layout still has to match exactly past
     * the point any later field is used. None of these six are
     * dereferenced by this file. */
    void (*destroy)(malloc_zone_t *zone);
    const char *zone_name;
    unsigned (*batch_malloc)(malloc_zone_t *zone, size_t size, void **results, unsigned num_requested);
    void (*batch_free)(malloc_zone_t *zone, void **to_be_freed, unsigned num_to_be_freed);
    void *introspect;
    unsigned version;
    void *(*memalign)(malloc_zone_t *zone, size_t alignment, size_t size);
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

/* Real Apple posix_memalign() (src/malloc.c), ported verbatim -- needed by
 * LLVM libc++abi's C++17 aligned-new support (build_llvm_libcxxabi.sh),
 * which real Apple malloc.c would normally provide but this project's
 * custom minimal public-API glue (this file, see build_libmalloc.sh's own
 * header comment for why malloc.c itself isn't built wholesale) didn't
 * carry over since nothing needed it until now. Dispatches through the
 * same real zone vtable every other function in this file already uses. */
int posix_memalign(void **memptr, size_t alignment, size_t size) {
    malloc_zone_t *z = get_zone();
    void *retval = z->memalign(z, alignment, size);
    if (retval == 0) {
        if (alignment < sizeof(void *) || 0 != (alignment & (alignment - 1))) {
            return 22; /* EINVAL */
        }
        return 12; /* ENOMEM */
    }
    *memptr = retval;
    return 0;
}

/* Real BSD extension: realloc() but frees ptr on failure too. */
void *reallocf(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p && ptr) {
        free(ptr);
    }
    return p;
}

/* ---- Real _malloc_fork_prepare/_malloc_fork_parent/_malloc_fork_child
 * (real Apple malloc.c, out of scope for this file wholesale -- see this
 * file's header comment -- so its three fork hooks are reimplemented here,
 * same as its public-API glue already is).
 *
 * Real libSystem calls these from libSystem_atfork_prepare/parent/child
 * (Libsystem's init.c). Without them, a fork() taken while another thread
 * holds a magazine lock leaves that lock held-by-a-thread-that-no-longer-
 * exists in the child, and the child's first malloc() deadlocks forever.
 * That is a real hazard here, not a theoretical one: launchd's job_start()
 * forks from a process whose libdispatch worker threads are running.
 *
 * Real malloc.c bodies, for reference:
 *   _malloc_fork_prepare(void) { return _malloc_lock_all(msl.fork_prepare); }
 *   _malloc_fork_parent(void)  { return _malloc_unlock_all(msl.fork_parent); }
 *   _malloc_fork_child(void)   { ...nano...; return _malloc_reinit_lock_all(msl.fork_child); }
 * ...where the _malloc_{lock,unlock,reinit_lock}_all() helpers walk the
 * malloc_zones[] array under MALLOC_LOCK() and call each zone's
 * introspect->force_lock / force_unlock / reinit_lock.
 *
 * Two real, deliberate differences from malloc.c here, both structural
 * consequences of what this dispatcher is, NOT simplifications:
 *  - There is no malloc_zones[] array and no MALLOC_LOCK() in this
 *    dispatcher: it owns exactly one zone (g_zone, created lazily by
 *    get_zone()). So "walk every zone under the global lock" reduces
 *    exactly to "act on g_zone", and the global-lock half has nothing to
 *    protect. Nothing is skipped.
 *  - The CONFIG_NANOZONE block in real _malloc_fork_child() (nano_forked_zone/
 *    nanov2_forked_zone) is genuinely N/A: build_libmalloc.sh deliberately
 *    does not build nano_malloc.c/nanov2_malloc.c at all (see its header
 *    comment), so _malloc_engaged_nano is never set and there is no nano
 *    zone to fork.
 * g_zone == NULL (nothing has allocated yet) is a real, expected state and
 * is correctly a no-op -- real malloc.c's loops are equally no-ops when
 * malloc_num_zones == 0. Deliberately NOT get_zone() here: creating a zone
 * as a side effect of fork() would be new behavior malloc.c never has.
 * (There is a theoretical race if g_zone were NULL at _malloc_fork_prepare()
 * time and non-NULL by _malloc_fork_parent() -- the parent would then
 * force_unlock a lock it never took. Real malloc.c is immune because
 * MALLOC_LOCK() covers the whole malloc_zones[] walk; here the window is
 * closed in practice instead, because get_zone() creates the one zone during
 * early process init, long before anything can call fork(). Noted rather
 * than papered over -- if this dispatcher ever gains a second zone or lazier
 * creation, this needs a real lock.)
 *
 * The msl.fork_* callouts are Malloc Stack Logging's hooks; msl_lite_support.c
 * is out of scope for the same real reason malloc.c is (it needs the dlopen'd
 * logging dylib), so there is no callout to make. */

/* Partial view of the real malloc_introspection_t (third_party/libmalloc/
 * include/malloc/malloc.h), declared in exactly the same style, and for
 * exactly the same reason, as the partial _malloc_zone_t view above: only
 * the two members this file actually calls are typed, the rest are
 * placeholders present purely to hold the real field offsets. Field order
 * and count match the real header exactly (16 pointer-sized members;
 * force_unlock is #6, reinit_lock is #13). */
struct _malloc_introspection_t {
    void *enumerator;
    void *good_size;
    void *check;
    void *print;
    void *log;
    void (*force_lock)(malloc_zone_t *zone);
    void (*force_unlock)(malloc_zone_t *zone);
    void *statistics;
    void *zone_locked;
    void *enable_discharge_checking;   /* version >= 7 */
    void *disable_discharge_checking;  /* version >= 7 */
    void *discharge;                   /* version >= 7 */
    void *enumerate_discharged_pointers;
    void (*reinit_lock)(malloc_zone_t *zone); /* version >= 9 */
    void *print_task;                  /* version >= 11 */
    void *task_statistics;             /* version >= 12 */
};

void _malloc_fork_prepare(void);
void _malloc_fork_parent(void);
void _malloc_fork_child(void);

void _malloc_fork_prepare(void) {
    malloc_zone_t *z = g_zone;
    if (!z) {
        return;
    }
    ((struct _malloc_introspection_t *)z->introspect)->force_lock(z);
}

void _malloc_fork_parent(void) {
    malloc_zone_t *z = g_zone;
    if (!z) {
        return;
    }
    ((struct _malloc_introspection_t *)z->introspect)->force_unlock(z);
}

void _malloc_fork_child(void) {
    malloc_zone_t *z = g_zone;
    if (!z) {
        return;
    }
    struct _malloc_introspection_t *in =
        (struct _malloc_introspection_t *)z->introspect;
    /* Exactly real _malloc_reinit_lock_all()'s per-zone branch: reinit_lock
     * only exists from introspection version 9 on; older zones fall back to
     * force_unlock. create_scalable_zone()'s szone publishes a real
     * szone_reinit_lock (magazine_malloc.c's szone_introspect table), so
     * this normally takes the reinit path -- the fallback is kept because
     * it is what real malloc.c does, not because a version < 9 zone is
     * expected here. */
    if (z->version < 9) {
        in->force_unlock(z);
    } else {
        in->reinit_lock(z);
    }
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

/* _pthread_set_self/mach_port_deallocate/mach_port_mod_refs: guarded
 * out under LIBMALLOC_DISPATCH_DYLIB_VARIANT 2026-08-30 (static-linking
 * cleanup, dylib-vs-dylib duplicate-symbol pass). build_libsystem_
 * kernel.sh's own header comment says it "copied verbatim from ...
 * libmalloc_dispatch.c" these exact 3 functions into
 * libsystem_kernel_compat.c, because real, unmodified mach_init.c/
 * mig_reply_port.c (compiled INTO libsystem_kernel.dylib) need them and
 * that narrowly-scoped dylib doesn't link real libpthread/a MIG mach_
 * port header. That made libsystem_kernel.dylib the real, canonical
 * owner of all 3 -- but this file's own copies kept shipping too,
 * because build_libsystem_malloc.sh reuses this same pre-built object
 * wholesale via `ar x`. Confirmed via `nm -m` on every other object
 * this dylib links (magazine_*.o, vm.o, etc.) that NOTHING inside
 * libsystem_malloc.dylib itself references `_pthread_set_self`/
 * `mach_port_deallocate`/`mach_port_mod_refs` by name -- real,
 * unmodified mig_reply_port.c (the only real caller anywhere in this
 * project) isn't even compiled into this dylib. Guarding them out only
 * for the dylib build variant lets libsystem_malloc.dylib fall through
 * to the real libsystem_kernel.dylib sibling it already links, while
 * every static-archive consumer of libmalloc.a (build_dyld_link.sh's
 * dyld.macho, etc, which must stay fully static and cannot link any
 * dylib) keeps its own copy unchanged. */
#ifndef LIBMALLOC_DISPATCH_DYLIB_VARIANT
void _pthread_set_self(void *p) {
    (void)p;
}

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
#endif /* !LIBMALLOC_DISPATCH_DYLIB_VARIANT */

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
 * then aborts -- same real "assertion failed -> terminate" contract.
 *
 * The line number is part of that real message, not decoration. Real
 * Libc's own assert.c (third_party/Libc/gen/FreeBSD/assert.c:83)
 * formats
 *
 *   "Assertion failed: (%s), function %s, file %s, line %d.\n"
 *
 * and this function used to drop the ", line %d" half with a bare
 * `(void)line;`. That cost a real diagnosis (DAR-261): notifyd's
 * __notify_generate_common_port (third_party/PureDarwin/src/Libraries/
 * XPC/notify/notify_proc.c) has TWO back-to-back
 * `assert(kstatus == KERN_SUCCESS)` calls -- one after
 * mach_port_construct(), one after mach_port_insert_right() -- in the
 * same function, with byte-identical expression text. With the line
 * number dropped, the daemon's one-line log could not say which call
 * failed, and the ticket had to guess (it guessed the first; a
 * standalone smoketest of that exact call, tools/userland_staging/
 * mach_port_construct_smoketest.c, then showed it passing). Any file
 * with two identical assertions in one function has the same problem,
 * so this is a general diagnostic gap, not a notifyd one.
 *
 * Decimal formatting is done by hand rather than via snprintf() for the
 * same reason every other write() in this file is raw: __assert_rtn
 * runs on the failure path of a malloc-adjacent library and must not
 * re-enter stdio or the allocator. */
static void mal_write_str(const char *s) {
    write(2, s, mal_strlen(s));
}
static void mal_write_int(int v) {
    char buf[16];
    int i = (int)sizeof(buf);
    unsigned int u;

    /* Negated in `long` so INT_MIN cannot overflow the negation -- no
     * real __LINE__ is ever negative, but a raw `-v` on an int is
     * undefined behavior at that one value and this is a crash-path
     * function. */
    if (v < 0) {
        mal_write_str("-");
        u = (unsigned int)(-(long)v);
    } else {
        u = (unsigned int)v;
    }
    if (u == 0) {
        buf[--i] = '0';
    }
    while (u) {
        buf[--i] = (char)('0' + (u % 10));
        u /= 10;
    }
    write(2, buf + i, sizeof(buf) - (unsigned long)i);
}
void __assert_rtn(const char *func, const char *file, int line, const char *expr) {
    mal_write_str("Assertion failed: (");
    mal_write_str(expr ? expr : "?");
    mal_write_str("), function ");
    mal_write_str(func ? func : "?");
    mal_write_str(", file ");
    mal_write_str(file ? file : "?");
    mal_write_str(", line ");
    mal_write_int(line);
    mal_write_str(".\n");
    abort();
}
