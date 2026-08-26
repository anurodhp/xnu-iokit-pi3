/* Real, curated mach_msg_server_once() -- verbatim body from
 * third_party/xnu-7195/libsyscall/mach/mach_msg.c (real Apple source),
 * needed by third_party/PureDarwin's libvproc.c
 * (vprocmgr_helper_maxmsgsz's real server loop). Curated out rather than
 * compiling the whole 806-line file: this project already has its own
 * curated mach_msg_send() (see mach_msg_send_compat.c) taken from the
 * same file, so compiling it whole would duplicate-define that symbol.
 *
 * The only real behavioral change from the original: the real body calls
 * voucher_mach_msg_adopt()/voucher_mach_msg_revert() around the demux
 * call (real source: third_party/libdispatch/src/voucher.c, a large file
 * with its own internal voucher-table/refcounting machinery this project
 * has not ported -- no real second real Mach voucher ever flows through
 * this target's single-thread server loop yet, see
 * docs/daemon-roadmap.md's Phase 6.8 section). Replaced with honest
 * local no-ops that always report "unchanged", which is the real,
 * correct behavior real Apple's own voucher_mach_msg_adopt() itself
 * reports whenever a message carries no voucher -- the common case here.
 */
#include <mach/mach_types.h>
#include <mach/message.h>
#include <mach/mig_errors.h>
#include <mach/vm_types.h>
#include <mach/vm_statistics.h>
#include <mach/vm_page_size.h>
#include <mach/kern_return.h>
#include <stdint.h>

typedef uint32_t voucher_mach_msg_state_t;
#define VOUCHER_MACH_MSG_STATE_UNCHANGED ((voucher_mach_msg_state_t)~0)

static voucher_mach_msg_state_t
local_voucher_mach_msg_adopt(mach_msg_header_t *msg)
{
	(void)msg;
	return VOUCHER_MACH_MSG_STATE_UNCHANGED;
}

static void
local_voucher_mach_msg_revert(voucher_mach_msg_state_t state)
{
	(void)state;
}

extern mach_port_t mach_task_self_;
extern mach_msg_return_t mach_msg(mach_msg_header_t *msg, mach_msg_option_t option,
    mach_msg_size_t send_size, mach_msg_size_t rcv_size, mach_port_name_t rcv_name,
    mach_msg_timeout_t timeout, mach_port_name_t notify);
extern void mach_msg_destroy(mach_msg_header_t *msg);
extern kern_return_t mach_port_deallocate(mach_port_name_t task, mach_port_name_t name);
extern kern_return_t vm_allocate(mach_port_name_t task, vm_address_t *address, vm_size_t size, int flags);
extern kern_return_t vm_deallocate(mach_port_name_t task, vm_address_t address, vm_size_t size);

static boolean_t
local_mach_msg_server_is_recoverable_send_error(kern_return_t kr)
{
	switch (kr) {
	case MACH_SEND_INVALID_DEST:
	case MACH_SEND_TIMED_OUT:
		return TRUE;
	default:
		return FALSE;
	}
}

static void
local_mach_msg_server_consume_unsent_message(mach_msg_header_t *hdr)
{
	if (MACH_MSGH_BITS_REMOTE(hdr->msgh_bits) == MACH_MSG_TYPE_MOVE_SEND_ONCE) {
		(void)mach_port_deallocate(mach_task_self_, hdr->msgh_remote_port);
		hdr->msgh_local_port = MACH_PORT_NULL;
	}
	mach_msg_destroy(hdr);
}

static kern_return_t
local_mach_msg_server_mig_return_code(mig_reply_error_t *reply)
{
	if (!(reply->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX) &&
	    reply->RetCode != KERN_SUCCESS && reply->RetCode != MIG_NO_REPLY) {
		return reply->RetCode;
	}
	return KERN_SUCCESS;
}

mach_msg_return_t
mach_msg_server_once(
	boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *),
	mach_msg_size_t max_size,
	mach_port_t rcv_name,
	mach_msg_options_t options)
{
	mig_reply_error_t *bufRequest, *bufReply;
	mach_msg_size_t request_size;
	mach_msg_size_t request_alloc;
	mach_msg_size_t trailer_alloc;
	mach_msg_size_t reply_alloc;
	mach_msg_return_t mr;
	kern_return_t kr;
	mach_port_t self = mach_task_self_;
	voucher_mach_msg_state_t old_state = VOUCHER_MACH_MSG_STATE_UNCHANGED;

	options &= ~(MACH_SEND_MSG | MACH_RCV_MSG | MACH_RCV_VOUCHER);

	trailer_alloc = REQUESTED_TRAILER_SIZE(options);
	request_alloc = (mach_msg_size_t)round_page(max_size + trailer_alloc);

	request_size = (options & MACH_RCV_LARGE) ?
	    request_alloc : max_size + trailer_alloc;

	reply_alloc = (mach_msg_size_t)round_page((options & MACH_SEND_TRAILER) ?
	    (max_size + MAX_TRAILER_SIZE) :
	    max_size);

	kr = vm_allocate(self,
	    (vm_address_t *)&bufReply,
	    reply_alloc,
	    VM_MAKE_TAG(VM_MEMORY_MACH_MSG) | TRUE);
	if (kr != KERN_SUCCESS) {
		return kr;
	}

	for (;;) {
		mach_msg_size_t new_request_alloc;

		kr = vm_allocate(self,
		    (vm_address_t *)&bufRequest,
		    request_alloc,
		    VM_MAKE_TAG(VM_MEMORY_MACH_MSG) | TRUE);
		if (kr != KERN_SUCCESS) {
			vm_deallocate(self,
			    (vm_address_t)bufReply,
			    reply_alloc);
			return kr;
		}

		mr = mach_msg(&bufRequest->Head, MACH_RCV_MSG | MACH_RCV_VOUCHER | options,
		    0, request_size, rcv_name,
		    MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

		if (!((mr == MACH_RCV_TOO_LARGE) && (options & MACH_RCV_LARGE))) {
			break;
		}

		new_request_alloc = (mach_msg_size_t)round_page(bufRequest->Head.msgh_size +
		    trailer_alloc);
		vm_deallocate(self,
		    (vm_address_t)bufRequest,
		    request_alloc);
		request_size = request_alloc = new_request_alloc;
	}

	if (mr == MACH_MSG_SUCCESS) {
		old_state = local_voucher_mach_msg_adopt(&bufRequest->Head);

		(void)(*demux)(&bufRequest->Head, &bufReply->Head);

		switch (local_mach_msg_server_mig_return_code(bufReply)) {
		case KERN_SUCCESS:
			break;
		case MIG_NO_REPLY:
			bufReply->Head.msgh_remote_port = MACH_PORT_NULL;
			break;
		default:
			bufRequest->Head.msgh_remote_port = MACH_PORT_NULL;
			mach_msg_destroy(&bufRequest->Head);
		}

		if (bufReply->Head.msgh_remote_port != MACH_PORT_NULL) {
			mr = mach_msg(&bufReply->Head,
			    (MACH_MSGH_BITS_REMOTE(bufReply->Head.msgh_bits) ==
			    MACH_MSG_TYPE_MOVE_SEND_ONCE) ?
			    MACH_SEND_MSG | options :
			    MACH_SEND_MSG | MACH_SEND_TIMEOUT | options,
			    bufReply->Head.msgh_size, 0, MACH_PORT_NULL,
			    MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

			if (local_mach_msg_server_is_recoverable_send_error(mr)) {
				local_mach_msg_server_consume_unsent_message(&bufReply->Head);
				mr = MACH_MSG_SUCCESS;
			}
		}
	}

	local_voucher_mach_msg_revert(old_state);

	(void)vm_deallocate(self,
	    (vm_address_t)bufRequest,
	    request_alloc);
	(void)vm_deallocate(self,
	    (vm_address_t)bufReply,
	    reply_alloc);
	return mr;
}
