/* Real, curated mach_msg_send() -- verbatim body from
 * third_party/xnu-7195/libsyscall/mach/mach_msg.c (real Apple source),
 * curated out rather than compiling the whole file: that file's own
 * mach_msg_priority/mach_msg_qos_t section hits real, genuine errors
 * unrelated to anything this project needs (see build_libmach_ipc.sh's
 * own header comment, which already root-caused this for mach_msg()/
 * mach_msg_destroy() the same way). This project's real mach_msg()
 * (mach_msg_real.c, already in libc_build/libmach_ipc.a) is what this
 * function calls through to -- not reimplemented, just delegated to,
 * matching Apple's own real body exactly.
 */
#include <mach/mach_types.h>
#include <mach/message.h>

extern mach_msg_return_t mach_msg(mach_msg_header_t *msg, mach_msg_option_t option,
    mach_msg_size_t send_size, mach_msg_size_t rcv_size, mach_port_name_t rcv_name,
    mach_msg_timeout_t timeout, mach_port_name_t notify);

mach_msg_return_t
mach_msg_send(mach_msg_header_t *msg)
{
	return mach_msg(msg, MACH_SEND_MSG,
	    msg->msgh_size, 0, MACH_PORT_NULL,
	    MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
}
