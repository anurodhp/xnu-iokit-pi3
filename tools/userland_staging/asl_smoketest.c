/* QEMU smoketest for libsystem_asl.dylib + syslogd's real dbserver.c
 * (DAR-121). Built by build_libsystem_asl_dylib.sh, installed as
 * /bin/asl_smoketest by inject_into_sd_image.sh.
 *
 * 1. The exact sequence CoreFoundation's __CFLogCStringLegacy()
 *    (CFUtilities.c) uses: asl_open, asl_new, asl_set x2, asl_send,
 *    asl_free, asl_close.
 * 2. 64 asl_log() calls. _asl_server_message is a MIG simpleroutine with
 *    no timeout (aslcommon/asl_ipc.defs:51), so if nothing drains
 *    com.apple.system.logger this hangs after the port's queue fills
 *    (default qlimit 5) and "done" never prints.
 * 3. syslog(3), which real syslog-385 routes through the same ASL send
 *    path (libsystem_asl.tproj/src/syslog.c).
 *
 * All at ASL_LEVEL_NOTICE: asl.conf.ios's "? [<= Level notice] store"
 * and com.apple.system.log's "? [<= Level notice] file system.log" send
 * them to /var/log/system.log. Check with:
 *   asl_smoketest; grep DAR121 /var/log/system.log
 */
#include <asl.h>
#include <syslog.h>
#include <stdio.h>

int
main(void)
{
	int i;
	aslclient c = asl_open("asl_smoketest", "com.apple.console", 0);
	aslmsg m = asl_new(ASL_TYPE_MSG);

	printf("asl_smoketest: asl_open=%p asl_new=%p\n", (void *)c, (void *)m);
	asl_set(m, ASL_KEY_LEVEL, "5");
	asl_set(m, ASL_KEY_MSG, "DAR121 cf-sequence asl_send");
	printf("asl_smoketest: asl_send=%d\n", asl_send(c, m));
	asl_free(m);
	asl_close(c);

	for (i = 0; i < 64; i++)
		asl_log(NULL, NULL, ASL_LEVEL_NOTICE, "DAR121 asl_log %d", i);
	printf("asl_smoketest: 64 asl_log calls returned\n");

	syslog(LOG_NOTICE, "DAR121 syslog");
	printf("asl_smoketest: done\n");
	return 0;
}
