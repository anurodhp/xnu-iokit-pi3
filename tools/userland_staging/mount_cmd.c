/*
 * mount(8) / umount(8) front-end, built by build_simple_cmds.sh.
 *
 * Custom code (source-priority last resort): Apple's real
 * diskdev_cmds/mount.tproj/mount.c includes <APFS/APFS.h>, <MediaKit/*.h>
 * and <IOKit/storage/IOMedia.h>, none of which exist in this tree, and
 * execs per-filesystem /sbin/mount_<fs> helpers; Apple's mount_msdos.c
 * needs CoreFoundation + IOKit for volume-label/encoding lookup. This
 * does only what's needed to drive the kernel's mount(2)/unmount(2):
 *
 *   mount                      list mounted filesystems (getfsstat)
 *   mount [-r] [-o opts] -t msdos|hfs dev dir
 *   umount [-f] dir
 *
 * The msdos argument block mirrors third_party/msdosfs/mount_msdos.tproj/
 * mount_msdos.c:231-322 (args.magic = MSDOSFS_ARGSMAGIC, args.fspec = dev,
 * mount("msdos", dir, flags, &args)) and the struct in
 * third_party/xnu-7195/bsd/msdosfs/msdosfsmount.h:257 (userland view,
 * which has the leading fspec the KERNEL view omits). hfs: mount_hfs.c's
 * struct hfs_mount_args is not reproduced; only the fspec-first layout
 * the kernel reads is passed.
 */
#include <sys/param.h>
#include <sys/mount.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

struct msdosfs_args_user {
	char		*fspec;
	uid_t		uid;
	gid_t		gid;
	mode_t		mask;
	uint32_t	flags;
	uint32_t	magic;
	int32_t		secondsWest;
	uint8_t		label[64];
};
#define MSDOSFS_ARGSMAGIC	0xe4eff301
#define MSDOSFSMNT_SECONDSWEST	0x40

struct hfs_args_user {
	char		*fspec;
};

static int is_umount;

static void
usage(void)
{
	if (is_umount)
		fprintf(stderr, "usage: umount [-f] dir\n");
	else
		fprintf(stderr, "usage: mount [-r] [-o opt[,opt]] -t msdos|hfs dev dir\n");
	exit(64);
}

static int
list_mounts(void)
{
	struct statfs *fs;
	int i, n = getfsstat(NULL, 0, MNT_NOWAIT);

	if (n < 0) {
		perror("getfsstat");
		return 1;
	}
	fs = malloc(n * sizeof(*fs));
	if (!fs)
		return 1;
	n = getfsstat(fs, n * sizeof(*fs), MNT_NOWAIT);
	if (n < 0) {
		perror("getfsstat");
		return 1;
	}
	for (i = 0; i < n; i++)
		printf("%s on %s (%s%s)\n", fs[i].f_mntfromname, fs[i].f_mntonname,
		    fs[i].f_fstypename, (fs[i].f_flags & MNT_RDONLY) ? ", read-only" : "");
	return 0;
}

int
main(int argc, char **argv)
{
	const char *type = NULL;
	int c, flags = 0;

	is_umount = (strstr(getprogname(), "umount") != NULL);

	if (is_umount) {
		while ((c = getopt(argc, argv, "f")) != -1) {
			if (c == 'f')
				flags |= MNT_FORCE;
			else
				usage();
		}
		argc -= optind;
		argv += optind;
		if (argc != 1)
			usage();
		if (unmount(argv[0], flags) < 0) {
			fprintf(stderr, "umount: %s: %s\n", argv[0], strerror(errno));
			return 1;
		}
		return 0;
	}

	while ((c = getopt(argc, argv, "rt:o:u")) != -1) {
		switch (c) {
		case 'r': flags |= MNT_RDONLY; break;
		case 'u': flags |= MNT_UPDATE; break;
		case 't': type = optarg; break;
		case 'o': {
			char *s = strdup(optarg), *p, *save;
			for (p = strtok_r(s, ",", &save); p; p = strtok_r(NULL, ",", &save)) {
				if (!strcmp(p, "ro") || !strcmp(p, "rdonly")) flags |= MNT_RDONLY;
				else if (!strcmp(p, "rw")) flags &= ~MNT_RDONLY;
				else if (!strcmp(p, "noexec")) flags |= MNT_NOEXEC;
				else if (!strcmp(p, "nosuid")) flags |= MNT_NOSUID;
				else if (!strcmp(p, "sync")) flags |= MNT_SYNCHRONOUS;
				else if (!strcmp(p, "update")) flags |= MNT_UPDATE;
				else { fprintf(stderr, "mount: unknown option %s\n", p); usage(); }
			}
			free(s);
			break;
		}
		default: usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0 && !type)
		return list_mounts();
	if (argc != 2 || !type)
		usage();

	if (!strcmp(type, "msdos") || !strcmp(type, "msdosfs")) {
		struct msdosfs_args_user a;
		memset(&a, 0, sizeof(a));
		a.fspec = argv[0];
		a.magic = MSDOSFS_ARGSMAGIC;
		a.mask = 0755;			/* ACCESSPERMS-ish default, see msdosfs_vfsops.c:253 */
		a.flags = MSDOSFSMNT_SECONDSWEST;
		if (mount("msdos", argv[1], flags, &a) < 0) {
			fprintf(stderr, "mount: %s on %s: %s\n", argv[0], argv[1], strerror(errno));
			return 1;
		}
	} else if (!strcmp(type, "hfs")) {
		struct hfs_args_user a = { argv[0] };
		if (mount("hfs", argv[1], flags, &a) < 0) {
			fprintf(stderr, "mount: %s on %s: %s\n", argv[0], argv[1], strerror(errno));
			return 1;
		}
	} else {
		fprintf(stderr, "mount: unsupported type %s\n", type);
		return 1;
	}
	return 0;
}
