/* Real smoketest for build_libinfo.sh (DAR-181): exercises real Apple
 * Libinfo's getpwnam(3)/getpwuid(3)/getgrnam(3)/getgrgid(3) plus the
 * real getpwent(3)/getgrent(3) full-enumeration path this ticket exists
 * to close (tools/libinfo_custom.c, this project's OWN working
 * flat-file resolver, has narrower by-name/by-id lookups plus its own
 * li_pw_at()/li_gr_at() index accessors, but never grew a POSIX-shaped
 * getpwent()/getgrent() iterator -- real Libinfo's file_module.c does).
 *
 * This binary is NOT executed on this host (arm64-apple-ios target,
 * blocked by this host's own hardened-runtime enforcement -- same
 * constraint tools/libinfo_custom/libinfo_custom.c's own header
 * documents for raw `svc` binaries). Real execution happens under QEMU
 * against this project's real /etc/master.passwd + /etc/group on the
 * SD image. Static verification here (link succeeds, real chained
 * fixups, otool/nm-clean) is this build's own host-side smoketest,
 * matching every other build_*_dylib.sh's own precedent (e.g.
 * build_libresolv_dylib.sh, which never executes its own dylib on-host
 * either).
 *
 * real, actual test flow once booted (see build_libinfo.sh's own
 * printed instructions):
 *   getpwnam("root")  -- expect uid 0, matches /etc/master.passwd
 *   getpwuid(0)       -- expect name "root"
 *   getgrnam("wheel") -- expect a real, defined gid (0 on this project)
 *   getgrgid(0)       -- expect name "wheel"
 *   setpwent()/getpwent() loop -- prints every real account on the
 *     image, the actual gap this ticket calls out (tools/libinfo_custom
 *     has no equivalent public iterator)
 *   setgrent()/getgrent() loop -- same, for groups
 */
#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>

static void
print_pw(const char *label, struct passwd *pw)
{
	if (pw == NULL) {
		printf("%s: NULL (not found)\n", label);
		return;
	}
	printf("%s: name=%s uid=%d gid=%d dir=%s shell=%s\n",
	    label, pw->pw_name, pw->pw_uid, pw->pw_gid, pw->pw_dir, pw->pw_shell);
}

static void
print_gr(const char *label, struct group *gr)
{
	if (gr == NULL) {
		printf("%s: NULL (not found)\n", label);
		return;
	}
	printf("%s: name=%s gid=%d\n", label, gr->gr_name, gr->gr_gid);
}

int
main(void)
{
	struct passwd *pw;
	struct group *gr;
	int n;

	printf("== libinfo smoketest (DAR-181) ==\n");

	pw = getpwnam("root");
	print_pw("getpwnam(root)", pw);

	pw = getpwuid(0);
	print_pw("getpwuid(0)", pw);

	gr = getgrnam("wheel");
	print_gr("getgrnam(wheel)", gr);

	gr = getgrgid(0);
	print_gr("getgrgid(0)", gr);

	printf("-- setpwent()/getpwent() full enumeration --\n");
	setpwent();
	n = 0;
	while ((pw = getpwent()) != NULL) {
		printf("  [%d] %s uid=%d gid=%d\n", n, pw->pw_name, pw->pw_uid, pw->pw_gid);
		n++;
		if (n > 64) { printf("  (stopping after 64, real loop guard)\n"); break; }
	}
	endpwent();
	printf("total passwd entries enumerated: %d\n", n);

	printf("-- setgrent()/getgrent() full enumeration --\n");
	setgrent();
	n = 0;
	while ((gr = getgrent()) != NULL) {
		printf("  [%d] %s gid=%d\n", n, gr->gr_name, gr->gr_gid);
		n++;
		if (n > 64) { printf("  (stopping after 64, real loop guard)\n"); break; }
	}
	endgrent();
	printf("total group entries enumerated: %d\n", n);

	printf("-- getaddrinfo() real DNS lookup (DAR-306, dns_module.c) --\n");
	{
		struct addrinfo hints, *res, *ai;
		char buf[INET6_ADDRSTRLEN];
		int rc;

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		rc = getaddrinfo("www.apple.com", NULL, &hints, &res);
		if (rc != 0) {
			printf("getaddrinfo(www.apple.com): FAILED: %s\n", gai_strerror(rc));
		} else {
			for (ai = res; ai != NULL; ai = ai->ai_next) {
				struct sockaddr_in *sin = (struct sockaddr_in *)(void *)ai->ai_addr;
				if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)) != NULL) {
					printf("getaddrinfo(www.apple.com): %s\n", buf);
				}
			}
			freeaddrinfo(res);
		}
	}

	printf("== done ==\n");
	return 0;
}
