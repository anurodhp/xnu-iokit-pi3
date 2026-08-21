/* Real ls(1) (third_party/file_cmds/ls/ls.c's traverse()) calls
 * acl_get_link_np() unconditionally on every file (not just under -e) to
 * decide the long-listing mode-suffix character ('+' vs ' '). Real
 * Apple's acl_get_link_np()/acl_get_file() (Libc/posix1e/acl_file.c) go
 * through statx_np()/lstatx_np() (Libc/sys/statx_np.c) into a
 * filesec_t -- a real, but *removed-from-the-public-SDK* abstraction:
 * confirmed absent both from this project's vendored Libc checkout
 * (grepped: only Libc/gen/filesec.c's *implementation* is vendored, no
 * declaring header) and from Xcode 12's iPhoneOS/MacOSX SDKs (grepped
 * both real sys/acl.h headers directly: zero filesec_t/FILESEC_*
 * mentions in either -- Apple dropped this from public headers, it's
 * not something this checkout is merely missing by omission). Rather
 * than reconstruct a private struct layout from guesswork (a real
 * fabrication risk, not an engineering shortcut), this is an honest
 * "no ACL support wired yet" stub for exactly the two real acl(3) entry
 * points ls(1) calls into this path.
 *
 * NULL is acl(3)'s own documented "no ACL / unsupported" return value,
 * not a fabricated empty ACL -- and ls.c's own real upstream code
 * already treats a NULL return exactly as "this file has no ACL"
 * (mode_suffix stays ' ', or becomes '@' if real xattrs are present via
 * the separate, fully-real listxattr()/getxattr() path already wired
 * for cp/sed via libcopyfile.a). No real functionality is faked: this
 * target's HFS+/msdosfs mounts don't have a wired kernel<->userland ACL
 * display path yet either way (see docs/backlog.md's tracked follow-up
 * for actually reconstructing/vendoring a real filesec_t layout and
 * wiring the real kernel stat64_extended/lstat64_extended/
 * fstat64_extended syscalls (#341-343, confirmed present + NO_SYSCALL_STUB
 * in bsd/kern/syscalls.master, real kernel-side vn_stat(..., &fsec, ...)
 * implementation already present in bsd/vfs/vfs_syscalls.c) that would
 * back a real, non-stub acl_get_link_np()).
 *
 * The rest of the real ACL API (acl_get_entry/acl_get_tag_type/
 * acl_get_qualifier/acl_get_perm_np/acl_get_flagset_np/acl_get_permset/
 * acl_get_flag_np/acl_free/acl_dup/acl_init) is real, unmodified Apple
 * source (acl.c/acl_entry.c/acl_perm.c/acl_flag.c) with zero filesec_t
 * dependency -- compiled in as-is, not stubbed, and still needed at
 * link time for print.c's printacl() (dead code today since
 * acl_get_link_np() here always returns NULL, but the symbols must
 * still resolve).
 */

#include <sys/types.h>
#include <sys/acl.h>
#include <errno.h>

acl_t
acl_get_link_np(const char *path, acl_type_t type)
{
	(void)path;
	(void)type;
	errno = ENOTSUP;
	return (acl_t)0;
}

acl_t
acl_get_file(const char *path, acl_type_t type)
{
	(void)path;
	(void)type;
	errno = ENOTSUP;
	return (acl_t)0;
}
