/*
 * bootfs_uenv: set the kernel boot-args u-boot appends from uEnv.txt, from a
 * running Pi, by mounting the FAT32 BOOT partition (/dev/disk0s1) briefly,
 * rewriting uEnv.txt and unmounting it. No mount(8)/umount(8) exist on the
 * image, so this calls mount(2) for "msdos" directly, the same way
 * third_party/msdosfs/mount_msdos.tproj/mount_msdos.c:231-322 does:
 * struct msdosfs_args (msdosfsmount.h:257-268) with magic MSDOSFS_ARGSMAGIC
 * (:295), uid/gid/mask and MSDOSFSMNT_SECONDSWEST (:281).
 *
 *   bootfs_uenv 'fb_width=1280 fb_height=720'   -> uEnv.txt: extra_bootargs=...
 *   bootfs_uenv -show                           -> print the current uEnv.txt
 *
 * u-boot reads it at the next boot (anurodhp/u-boot configs/rpi3_xnu_defconfig);
 * see tools/make_realhw_sd_image.sh for the same file written on the Mac.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>

/* mount(2)/unmount(2) are exported by no dylib in this port, so issue the
 * syscalls directly: numbers 167 (mount) and 159 (unmount) from
 * third_party/xnu-7195/bsd/kern/syscalls.master:258,244; arm64 Darwin
 * convention: number in x16, args in x0-x3, `svc #0x80`, carry flag set on
 * error with the errno in x0. */
static long sys4(long n, long a0, long a1, long a2, long a3, int *err)
{
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x16 __asm__("x16") = n;
    unsigned long carry;
    __asm__ volatile ("svc #0x80\n\tcset %2, cs"
                      : "+r"(x0), "+r"(x1), "=r"(carry)
                      : "r"(x2), "r"(x3), "r"(x16) : "memory", "cc");
    *err = carry ? (int)x0 : 0;
    return carry ? -1 : x0;
}
static int my_mount(const char *type, const char *path, int flags, void *data)
{
    int e; long r = sys4(167, (long)type, (long)path, flags, (long)data, &e);
    if (r < 0) errno = e;
    return (int)r;
}
static int my_unmount(const char *path, int flags)
{
    int e; long r = sys4(159, (long)path, flags, 0, 0, &e);
    if (r < 0) errno = e;
    return (int)r;
}

struct msdosfs_args {
    char     *fspec;
    uid_t     uid;
    gid_t     gid;
    mode_t    mask;
    uint32_t  flags;
    uint32_t  magic;
    int32_t   secondsWest;
    uint8_t   label[64];
};
#define MSDOSFS_ARGSMAGIC   0xe4eff301
#define MSDOSFSMNT_SECONDSWEST 0x40

int main(int argc, char **argv)
{
    const char *dev = "/dev/disk0s1", *mnt = "/var/root/bootmnt";
    int show = (argc == 2 && !strcmp(argv[1], "-show"));
    struct msdosfs_args a;
    char path[256];
    int rc = 1;

    if (argc != 2) { fprintf(stderr, "usage: bootfs_uenv 'bootargs' | -show\n"); return 2; }
    mkdir(mnt, 0755);
    memset(&a, 0, sizeof a);
    a.fspec = (char *)dev;
    a.uid = 0; a.gid = 0; a.mask = 0755;
    a.magic = MSDOSFS_ARGSMAGIC;
    a.flags = MSDOSFSMNT_SECONDSWEST;
    if (my_mount("msdos", mnt, show ? MNT_RDONLY : 0, &a) < 0) {
        fprintf(stderr, "bootfs_uenv: mount %s on %s: %s\n", dev, mnt, strerror(errno));
        return 1;
    }
    snprintf(path, sizeof path, "%s/uEnv.txt", mnt);
    if (show) {
        FILE *f = fopen(path, "r");
        char buf[512];
        if (!f) printf("(no uEnv.txt)\n");
        else { while (fgets(buf, sizeof buf, f)) fputs(buf, stdout); fclose(f); }
        rc = 0;
    } else {
        FILE *f = fopen(path, "w");
        if (!f) fprintf(stderr, "bootfs_uenv: create %s: %s\n", path, strerror(errno));
        else {
            fprintf(f, "extra_bootargs=%s\n", argv[1]);
            if (fflush(f) == 0) rc = 0; /* sync() below flushes; fsync is not exported here */
            fclose(f);
            printf("bootfs_uenv: wrote %s: extra_bootargs=%s\n", path, argv[1]);
        }
    }
    sync();
    if (my_unmount(mnt, 0) < 0) {
        fprintf(stderr, "bootfs_uenv: unmount %s: %s\n", mnt, strerror(errno));
        rc = 1;
    }
    return rc;
}
