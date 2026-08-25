#ifndef _MACH_MSG_TRAP_DECL_COMPAT_H_
#define _MACH_MSG_TRAP_DECL_COMPAT_H_
/* Real userland prototypes, copied verbatim from
 * osfmk/mach/mach_traps.h's real userland trap-declaration block (lines
 * ~88-96 in this exact pinned checkout: mach_reply_port/
 * thread_get_special_reply_port/thread_self_trap/host_self_trap/
 * mach_msg_trap/mach_msg_overwrite_trap, all `#ifndef KERNEL #ifdef
 * PRIVATE`-guarded) -- same class of workaround this project's own
 * mach_reply_port_decl.h already documents for mach_reply_port() itself,
 * root-caused further here rather than just copied blind: with
 * `-isysroot <Xcode-12 iPhoneOS SDK> ... -idirafter $XNU/osfmk`, a plain
 * `#include <mach/mach_traps.h>` resolves against the SDK's OWN copy of
 * that header (angle-bracket system-header lookup checks the sysroot's
 * usr/include before any -idirafter path, which is deliberately
 * lowest-priority) -- NOT this project's real xnu-7195 source -- and the
 * SDK's public copy declares `task_self_trap()` but not
 * `host_self_trap()`/`thread_self_trap()`/`mach_reply_port()` at that
 * spot even with `-DPRIVATE=1` (confirmed via `clang -E`: only
 * `task_self_trap` survives preprocessing from that header under this
 * project's exact real build flags). Declaring just the two real
 * signatures actually used by this project's new Mach-IPC work
 * (mach_msg_real.c, mach_host_self_real.c) instead of fighting the SDK
 * header's own gating -- same fix shape as mach_reply_port_decl.h, kept
 * as a separate file since it covers different symbols. */
#include <mach/mach_types.h>
#include <mach/message.h>

extern mach_port_name_t host_self_trap(void);

extern mach_msg_return_t mach_msg_trap(
	mach_msg_header_t *msg,
	mach_msg_option_t option,
	mach_msg_size_t send_size,
	mach_msg_size_t rcv_size,
	mach_port_name_t rcv_name,
	mach_msg_timeout_t timeout,
	mach_port_name_t notify);

/* Real signature, copied verbatim from the same osfmk/mach/mach_traps.h
 * spot as mach_msg_trap() above (added 2026-08-25 for real dyld-940's
 * DyldAPIs.cpp, the first real consumer of mach_msg_overwrite() this
 * project has had -- see mach_msg_real.c's own header comment). */
extern mach_msg_return_t mach_msg_overwrite_trap(
	mach_msg_header_t *msg,
	mach_msg_option_t option,
	mach_msg_size_t send_size,
	mach_msg_size_t rcv_size,
	mach_port_name_t rcv_name,
	mach_msg_timeout_t timeout,
	mach_msg_priority_t priority,
	mach_msg_header_t *rcv_msg,
	mach_msg_size_t rcv_limit);

#endif
