/* Real Apple's firehose/private.h (the os_log/tracing ring-buffer
 * subsystem's own private API) is not vendored anywhere in this
 * project's pinned third_party/libdispatch checkout -- only
 * src/firehose's own .c/.h files (the ring-buffer/server IMPLEMENTATION, ~1600
 * lines) exists, not the public/private API surface libdispatch's own
 * voucher/queue code includes unconditionally from outside src/firehose/.
 *
 * This project deliberately does NOT port the real firehose engine: no
 * os_log daemon (`logd`) exists on this target for it to hand tracepoint
 * buffers to (see docs/daemon-roadmap.md's os_log scoping note), so a
 * real, working ring buffer would have nowhere to send its data anyway.
 * `-DOS_FIREHOSE_SPI=0`/`-DOS_VOUCHER_ACTIVITY_SPI=0` (see
 * build_libdispatch_real.sh) compile out every *functional* tracing call
 * site -- but a handful of these types are baked directly into
 * `voucher_internal.h`'s real `struct voucher_s`/`_voucher_mach_udata_s`
 * UNCONDITIONALLY (real Apple keeps the wire/struct layout stable
 * whether or not the tracing SPI is compiled in), so the types
 * themselves must still exist even though nothing here ever gives them
 * real tracing meaning.
 *
 * Confirmed via grep across all of third_party/libdispatch/src: exactly
 * these 10 real type names are referenced; every one is either a plain
 * uint64_t-sized handle (matching src/shims/mach.h's own portable
 * `typedef uint64_t firehose_activity_id_t;` for the !HAVE_MACH build)
 * or an opaque pointer nothing in the compiled subset of libdispatch
 * (queue/object/source/voucher/mach/io/semaphore/once/apply/time -- the
 * firehose ring-buffer implementation itself is NOT compiled) ever
 * dereferences the internals of.
 */
#ifndef __DISPATCH_COMPAT_FIREHOSE_PRIVATE_H__
#define __DISPATCH_COMPAT_FIREHOSE_PRIVATE_H__

#include <stdint.h>

typedef uint64_t firehose_activity_id_t;
typedef uint64_t firehose_tracepoint_id_t;
typedef uint16_t firehose_activity_flags_t;
typedef uint16_t firehose_activity_flags_precise_t;
typedef uint8_t firehose_stream_t;

typedef struct firehose_chunk_s *firehose_chunk_t;
typedef struct firehose_tracepoint_s *firehose_tracepoint_t;
typedef union firehose_buffer_u *firehose_buffer_t;
typedef struct firehose_buffer_header_s *firehose_buffer_header_t;

/* voucher_activity_hooks_t needs a REAL (not opaque) body: real
 * voucher.c's own always-compiled voucher_activity_initialize_4libtrace()
 * and its _voucher_libkernel_init() caller (lines ~750-830, 1145-1172 --
 * OUTSIDE the #if 0-disabled "voucher_activity_t" pragma-mark section
 * this project's libdispatch.patch also applies) dereference
 * `->vah_version`/`->vah_debug_channel_handler` directly. Only these 2
 * of the real (probably larger) Apple struct's fields are ever touched
 * outside the disabled block, confirmed via grep -- `vah_get_logd_port`/
 * `vah_metadata_init` are real fields too (referenced only inside the
 * disabled block) but harmless to omit since that code is never
 * type-checked. `_voucher_libtrace_hooks` (this struct's only real
 * instance, a global pointer) is never set to a non-NULL value anywhere
 * in this project's actual runtime -- nothing calls
 * voucher_activity_initialize_4libtrace() since no logd exists -- so the
 * exact field layout has no real ABI consumer to match, only these
 * accessor call sites need to compile.
 */
typedef struct voucher_activity_hooks_s {
	int vah_version;
	void (*vah_debug_channel_handler)(void *context, void *msg);
} *voucher_activity_hooks_t;

/* real, unconditionally-referenced (VOUCHER_USE_MACH_VOUCHER defaults
 * to 1 always, internal.h:1003) from event/event_kevent.c's memory-
 * pressure handler -- guarded at each real call site by
 * `if (_firehose_task_buffer)`, which is always NULL on this target
 * (nothing ever calls the real firehose init path, see voucher.c's own
 * #if 0-disabled block), so these never actually run; real no-op bodies
 * matching that dead-code reality, not a functional stand-in.
 */
#define FIREHOSE_BUFFER_BANK_FLAG_LOW_MEMORY 0x1u
static inline void firehose_buffer_clear_bank_flags(firehose_buffer_t fb, unsigned long flags) { (void)fb; (void)flags; }
static inline void firehose_buffer_set_bank_flags(firehose_buffer_t fb, unsigned long flags) { (void)fb; (void)flags; }

#endif /* __DISPATCH_COMPAT_FIREHOSE_PRIVATE_H__ */
