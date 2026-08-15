/* Small, honestly-scoped compat shims needed to link Libc's locale
 * subsystem (third_party/Libc/locale/xlocale.c and friends) - NOT a
 * general-purpose allocator or lock implementation, see each section.
 */

typedef unsigned long size_t;

/* os_unfair_lock/OSAtomic*Barrier no-op stand-ins RETIRED 2026-08-14:
 * build_libplatform.sh now builds real Apple libplatform
 * (third_party/libplatform, os/lock.c + os/atomic.c +
 * atomics/arm64/OSAtomic.c) and links it into libc_locale.a's consumers
 * instead - see that script's header comment for the real TSD-owner-
 * bootstrap/Mach-trap/SDK-vintage work this took. This target being
 * provably single-threaded (tsd_bootstrap.c/pthread_tsd_shim.c) means
 * there's no real cross-thread contention to exercise, but the real
 * fast-path cmpxchg/TSD-owner logic (and the actual bug class a no-op
 * lock can never catch - e.g. an unpopulated lock-owner identity making
 * two different callers both see themselves as the current owner) is
 * now real code, not approximated.
 *
 * placeholder bump allocator (malloc/calloc/realloc/free/reallocf) ALSO
 * RETIRED 2026-08-14: build_libmalloc.sh now builds a real Apple
 * magazine-zone allocator (third_party/libmalloc, real
 * create_scalable_zone()/szone_malloc()/etc, unmodified) and links it
 * into every consumer that previously pulled the bump allocator from
 * this file (build_libc_locale.sh/build_libc_stdio.sh/build_libc_gen.sh/
 * build_libc_printf.sh all relink against libc_build/libmalloc.a
 * instead now) - see build_libmalloc.sh's own header comment for the
 * two real bugs (a _FORTIFY_SOURCE self-recursion, and a missing
 * pthread_tsd_shim_bootstrap() call) found and fixed getting real
 * magazine allocation actually working under QEMU. This file is kept
 * (rather than deleted) purely as the historical record of both
 * retirements; it now defines nothing.
 */
