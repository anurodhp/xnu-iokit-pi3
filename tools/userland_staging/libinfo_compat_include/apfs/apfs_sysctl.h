/* Real Apple apfs/apfs_sysctl.h is a private-framework SPI header (part
 * of the apfs kext's userspace-facing sysctl surface) never published to
 * apple-oss-distributions -- confirmed absent from this checkout and
 * from every apple-oss-distributions repo this project has pinned (see
 * docs/apple-oss-catalog.md). lookup.subproj/file_module.c's own file
 * module (`#if (TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR)` branch, real
 * and taken for this project's arm64-apple-ios14.4 target, same as
 * build_copyfile.sh's own documented TARGET_OS_IPHONE precedent) uses
 * exactly one symbol from it: APFS_FSTAB_SYSCTL, the sysctlbyname(3)
 * name string for querying an APFS volume's real, kernel-resident fstab
 * override list.
 *
 * This project's XNU has no apfs kext at all (HFS+ is the only real
 * on-disk filesystem here, see architecture.md's storage-stack section)
 * -- so regardless of this string's exact byte value, sysctlbyname()
 * against it always fails safely with ENOENT on this project's real
 * kernel (no MIB node registered under any name), and file_module.c's
 * own caller (_fsi_get_apfs_fstab, gated behind that same real
 * TARGET_OS_IPHONE branch) already handles a failing sysctlbyname() as
 * "no APFS fstab entries," which is the real, honest answer here. The
 * value below is the name Apple's real apfs.kext registers on genuine
 * hardware (cross-referenced against public apfs/diskutil debugging
 * documentation, not independently verified against Apple's own
 * unpublished header) -- picked for source fidelity, not because its
 * exact spelling is load-bearing in this environment.
 *
 * struct edt_fstab: also real Apple SPI (from the same unpublished
 * header), also never dereferenced on this project's real kernel for
 * the same "no apfs kext" reason above -- _fsi_get_edt_fs() (file_module.c
 * ~line 1918) mallocs a buffer sized by sysctlbyname()'s returned size,
 * which is always 0 here (the call fails before ever writing through this
 * type), so this type's exact real field layout is not load-bearing
 * either. Field set/order given here matches file_module.c's own real,
 * unconditional usage 1:1 (fs_spec, fs_file, fs_vfstype, fs_mntops,
 * fs_type as a *string* -- not an int, confirmed by file_module.c's own
 * LI_ils_create() format string "L4488sssss44" encoding 5 consecutive
 * string fields between va/vb and the trailing freq/passno ints), which
 * is also exactly real BSD `struct fstab`'s own field layout
 * (third_party/Libc/include/fstab.h) -- Apple's real embedded-disktab
 * sysctl record mirrors the standard fstab(5) record shape, unsurprising
 * since both describe the same mount-table concept. */
#ifndef _APFS_SYSCTL_H_
#define _APFS_SYSCTL_H_

#define APFS_FSTAB_SYSCTL "apfs.fstab"

struct edt_fstab {
	char	*fs_spec;
	char	*fs_file;
	char	*fs_vfstype;
	char	*fs_mntops;
	char	*fs_type;
	int	fs_freq;
	int	fs_passno;
};

#endif /* _APFS_SYSCTL_H_ */
