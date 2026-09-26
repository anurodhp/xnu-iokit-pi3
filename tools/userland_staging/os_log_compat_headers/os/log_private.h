/* Compat stand-in for Apple's private <os/log_private.h>, the os_log
 * "pack" API that real SystemConfiguration's SCD.c logging path uses.
 *
 * ---- THIS FILE IS THE SINGLE SOURCE OF TRUTH. DO NOT COPY IT. ----
 *
 * It is installed verbatim into libc_build/compat_include/os/ by BOTH
 * build_bootp_dhcp_engine.sh and build_libsystemconfiguration_client.sh
 * (each `cp`s it and then `cmp`s the result). It used to be a `cat >`
 * heredoc inside each of those two scripts instead -- two independent
 * copies of the same declarations, writing the same path, with neither
 * script depending on the other and build_configd.sh deliberately
 * REUSING that directory rather than regenerating it. Whichever script
 * ran last silently won.
 *
 * They really did diverge, twice over (DAR-263, 2026-09-07):
 *
 *   1. `olp_wall_time` was a plain `uint64_t` in one copy and a
 *      timespec-shaped struct in the other, which put `olp_reserved` at
 *      offset 16 in one view and 24 in the other -- an 8-byte struct
 *      layout skew across translation units in the same link. That is a
 *      real out-of-bounds stack write, not a cosmetic difference:
 *      os_log_pack_decl() below declares the caller's storage from this
 *      struct, and sc_log_pack_compat.c's os_log_pack_fill() writes into
 *      it sized by ITS view. A TU compiled against the 88-byte view
 *      writing into 80-byte caller storage smashes 8 bytes of the
 *      caller's frame.
 *   2. Only one copy declared os_log_with_args() (and included
 *      <stdarg.h> for it), so which declarations existed at all depended
 *      on script ordering.
 *
 * Both are the same underlying hazard -- duplicated declarations kept in
 * sync by a comment asking future editors to keep them byte-identical --
 * so the duplication itself was removed rather than re-synchronized.
 *
 * The timespec shape is the correct one: real SCD.c's __SC_log_send()
 * takes the address of both pack->olp_wall_time.tv_sec and .tv_nsec
 * (confirmed by a real compile error against the uint64_t shape, not
 * guessed). Only the fields real pinned call sites actually touch are
 * modeled -- this is deliberately not a reimplementation of Apple's
 * real, never-open-sourced, ABI-undocumented firehose pack wire format.
 *
 * DAR-267 (2026-09-17): os_log_pack_size() below used to be declared as a
 * real, ordinary variadic C function -- `size_t os_log_pack_size(const
 * char *format, ...)`. SCPrivate.h's own SC_log() macro (the real,
 * unmodified Apple macro, ~line 497) expands every call into
 *     size_t __pack_size = os_log_pack_size(__format, ##__VA_ARGS__);
 *     ...
 *     os_log_pack_fill(__pack, __pack_size, errno, __format, ##__VA_ARGS__);
 * i.e. it textually passes the SAME __VA_ARGS__ -- which for a `%@`
 * conversion is a real, arbitrary, potentially side-effecting C
 * expression (e.g. eventmon.c:834's own
 * `CFBundleGetIdentifier(bundle)`) -- to TWO call sites. Real Apple's own
 * macro (os/log.h, not open-sourced, but documented Clang behavior) gets
 * away with this because its size call is `__builtin_os_log_format_size`,
 * a CLANG COMPILER BUILTIN: Clang computes the encoded-buffer size from
 * the format string's conversions and each argument's STATIC TYPE alone
 * and does not emit code to evaluate the argument expressions for that
 * call at all. A real, ordinary C function has no such exemption -- the
 * C standard requires every argument expression at an ordinary call site
 * to be evaluated -- so this compat shim's use of a real variadic
 * function for os_log_pack_size() silently downgraded "compute a size"
 * into "evaluate every %@ argument's side effects a second time, then
 * throw the value away." Since this implementation's os_log_pack_size()
 * body ignored its arguments outright and returned a fixed
 * `sizeof(struct os_log_pack_s)` regardless (see the definition this
 * header used to share with os_log_pack_fill() in sc_log_pack_compat.c),
 * the second evaluation was pure waste at best -- and for any call site
 * whose %@ argument is a non-idempotent expression (a fresh
 * CFStringCreateWithFormat(), a lock/unlock pair via
 * CFBundleGetInfoDictionary() as in eventmon.c:834's
 * `SC_log(LOG_DEBUG, "  bundle ID = %@", CFBundleGetIdentifier(bundle))`,
 * etc) it silently doubled real work and added a full extra call chain
 * (lock, dictionary lookup, unlock) on every SC_log() call site with an
 * object argument, right on the same path DAR-266/DAR-267 found crashing
 * with a near-NULL CFBundleRef. Not proven (no dynamic/QEMU trace) to be
 * THE exact fault mechanism, but a real, independently-valid Apple-parity
 * bug in its own right, so fixed here regardless: os_log_pack_size is now
 * a macro that discards its arguments at the PREPROCESSOR level (they are
 * never even handed to the compiler as an expression, so they cannot be
 * evaluated) and calls a real, argument-free os_log_pack_size_impl()
 * instead -- restoring the "compute a size without evaluating the
 * message arguments" behavior real Apple's builtin gives for free, while
 * leaving SCPrivate.h (real, unmodified Apple source) untouched. The one
 * real evaluation of each %@ argument that must still happen (to
 * actually format the message) is the existing os_log_pack_fill() call --
 * unchanged, still evaluates its arguments exactly once, exactly as
 * before.
 */
#ifndef _OS_LOG_PRIVATE_H_COMPAT_
#define _OS_LOG_PRIVATE_H_COMPAT_
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <os/log.h>

struct os_log_pack_s {
	uint64_t olp_continuous_time;
	struct {
		uint64_t tv_sec;
		uint64_t tv_nsec;
	} olp_wall_time;
	/* 320 bytes, not Apple's 64: sc_log_pack_compat.c's
	 * os_log_pack_fill() composes the real message text into this space
	 * so os_log_pack_compose() can hand it back, instead of returning an
	 * empty string (which is what made configd's own log file 30 blank
	 * lines and left DAR-261/DAR-265 to be root-caused from ps output
	 * and disassembly). 320 covers the 256-byte buffer SCD.c's own
	 * __SC_log_send() passes to compose(), plus slack.
	 *
	 * Safe to resize precisely BECAUSE this file is now the only
	 * definition: os_log_pack_decl() declares the caller's storage from
	 * this same struct, every producer and consumer of a pack here is
	 * this project's own compat code, and the pack never crosses an ABI
	 * boundary. Resizing it while two copies existed is exactly what
	 * turned the layout skew above into a live stack smash. */
	uint8_t  olp_reserved[320];
};
typedef struct os_log_pack_s *os_log_pack_t;

/* Real Apple's version stack-allocates a size-byte pack buffer (a VLA --
 * SCPrivate.h's own SC_log() macro brackets its use in a "-Wvla"
 * pragma-disable, confirming this). A single fixed-size struct instance
 * is an honest substitute: `size` is always this header's own
 * os_log_pack_size() return value, i.e. sizeof(struct os_log_pack_s). */
#define os_log_pack_decl(name, sz) \
	struct os_log_pack_s name##_storage; \
	os_log_pack_t name = &name##_storage; \
	(void)(sz)

#ifdef __cplusplus
extern "C" {
#endif
void   os_log_with_args(os_log_t log, os_log_type_t type, const char *format,
                         va_list args, void *addr);
/* DAR-121: syslog-385's asl.c/syslog.c guard their os_log path with
 * this; libsystem_asl_compat.c answers false (no os_log backend here). */
bool   os_log_shim_enabled(void *addr);
/* DAR-267: real, argument-free implementation -- always returns
 * sizeof(struct os_log_pack_s), see this header's own top comment. The
 * macro below is what every real call site (SCPrivate.h's SC_log(),
 * Libc/os/assumes.h's __os_crash_fmt()) actually invokes; it discards
 * `format`/`...` textually so they are never evaluated for this call. */
size_t os_log_pack_size_impl(void);
#define os_log_pack_size(format, ...) os_log_pack_size_impl()
void   os_log_pack_fill(os_log_pack_t pack, size_t size, int saved_errno,
                         const char *format, ...);
void   os_log_pack_send(os_log_pack_t pack, os_log_t log, os_log_type_t type);
char  *os_log_pack_send_and_compose(os_log_pack_t pack, os_log_t log,
                                     os_log_type_t type, char *buf, size_t buf_size);
char  *os_log_pack_compose(os_log_pack_t pack, os_log_t log, os_log_type_t type,
                            char *buf, size_t buf_size);
#ifdef __cplusplus
}
#endif
#endif /* _OS_LOG_PRIVATE_H_COMPAT_ */
