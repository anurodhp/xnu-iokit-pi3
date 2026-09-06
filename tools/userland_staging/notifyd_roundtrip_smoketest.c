/* Real end-to-end round trip against a real, running notifyd (DAR-167):
 * notify_register_check() a name, notify_post() it, then notify_check()
 * the same token and confirm the "did this change since I last looked"
 * flag really flipped -- the same real client sequence notifyutil(1)
 * itself uses, calling the exact same real, dynamically-linked
 * libsystem_notify.dylib/libSystem.B.dylib every other real client in
 * this project already links (this is a fresh, separate process image
 * from notifyd.macho -- no duplicate-state-engine concern, see
 * build_notifyd.sh's own header comment for why notifyd.macho itself
 * cannot link this same dylib).
 *
 * Real, unmodified public API only (Libnotify/notify.h): no private
 * headers, no assumptions about notifyd's internals -- if this program
 * gets real, correct answers, notify_register_*() genuinely resolved
 * bootstrap_look_up2() against real launchd's bootstrap namespace and
 * really talked MIG over real Mach IPC to the real, separately-running
 * notifyd.macho this ticket built.
 */
#include <stdio.h>
#include <unistd.h>

extern int notify_register_check(const char *name, int *out_token);
extern int notify_post(const char *name);
extern int notify_check(int token, int *check);
extern int notify_cancel(int token);

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char *name = "com.iokit.notifyd.roundtrip";
    int token = -1;
    int check = 0;
    int status;

    status = notify_register_check(name, &token);
    printf("notify_register_check(%s): status=%d token=%d\n", name, status, token);
    if (status != 0) {
        printf("notifyd_roundtrip_smoketest: FAIL (register_check status != 0)\n");
        return 1;
    }

    /* First notify_check() after a fresh register_check() always reports
     * check=1 on real Darwin (the "since registration" baseline edge),
     * so drain it before the real test post below. */
    check = -1;
    notify_check(token, &check);
    printf("notify_check (baseline, before post): check=%d\n", check);

    status = notify_post(name);
    printf("notify_post(%s): status=%d\n", name, status);
    if (status != 0) {
        printf("notifyd_roundtrip_smoketest: FAIL (post status != 0)\n");
        notify_cancel(token);
        return 1;
    }

    usleep(200000);

    check = -1;
    status = notify_check(token, &check);
    printf("notify_check (after post): status=%d check=%d\n", status, check);

    notify_cancel(token);

    if (status == 0 && check != 0) {
        printf("notifyd_roundtrip_smoketest: ROUNDTRIP OK\n");
        return 0;
    }
    printf("notifyd_roundtrip_smoketest: FAIL (no real state change observed)\n");
    return 1;
}
