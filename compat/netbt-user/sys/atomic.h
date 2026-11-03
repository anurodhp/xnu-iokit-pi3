/* NetBSD <sys/atomic.h>, the one call the netbt userland makes
 * (libbluetooth sdp_service.c: atomic_swap_uint). */
#ifndef _NETBT_USER_SYS_ATOMIC_H_
#define _NETBT_USER_SYS_ATOMIC_H_
#define atomic_swap_uint(p, v)  __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#define atomic_inc_uint_nv(p)   __atomic_add_fetch((p), 1, __ATOMIC_SEQ_CST)
#endif
