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
 */
#ifndef _OS_LOG_PRIVATE_H_COMPAT_
#define _OS_LOG_PRIVATE_H_COMPAT_
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
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
size_t os_log_pack_size(const char *format, ...);
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
