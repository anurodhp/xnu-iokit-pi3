/* Real, confirmed dead include (for dyld/DyldDelegates.cpp): launchd's
 * real public header -- launchd itself was never open-sourced, see this
 * project's already-documented fact -- but DyldDelegates.cpp calls zero
 * vproc_* symbols anywhere in that file (grepped in full).
 *
 * libdyld/LibSystemHelpers.cpp DOES call one real symbol from this
 * header, vproc_swap_integer(), inside isLaunchdOwned() -- real,
 * long-documented, stable Apple SPI (types/constant unchanged across
 * many macOS releases despite launchd itself never being open-sourced).
 * This port has no launchd at all, so the real, correct, honest answer
 * to "is this process launchd-managed" is always no -- see
 * libdyld_vproc_compat.c's real stub implementing exactly that, not
 * faking real launchd IPC. */
#ifndef _COMPAT_VPROC_PRIV_H_
#define _COMPAT_VPROC_PRIV_H_
#include <stdint.h>
typedef struct vproc_s *vproc_t;
typedef struct vproc_err_s *vproc_err_t;
typedef unsigned int vproc_gsk_t;
#define VPROC_GSK_IS_MANAGED ((vproc_gsk_t)26)
#ifdef __cplusplus
extern "C"
#endif
vproc_err_t vproc_swap_integer(vproc_t vp, vproc_gsk_t key, int64_t *inval, int64_t *outval);
#endif
