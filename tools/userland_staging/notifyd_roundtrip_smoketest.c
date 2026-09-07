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
 *
 * ---- Why this is more than three API calls (DAR-261 follow-up) ----
 *
 * The first version of this test printed only register/post/check, and
 * when it started failing with "check=1 at baseline, check=0 after the
 * post" that output could not say WHICH of three independent real
 * mechanisms had broken. notify_check() has two entirely separate
 * implementations inside real, unmodified notify_client.c:3456-3505 --
 *
 *   - NOTIFY_TYPE_MEMORY registrations read globals->shm_base[r->slot]
 *     directly. That mapping is real POSIX shared memory: notifyd does
 *     shm_open("apple.shm.notification_center", O_RDWR|O_CREAT) +
 *     mmap(MAP_SHARED) (notifyd.c:1161-1207) and bumps
 *     shared_memory_base[slot] on every post (notifyd.c:792/807/823);
 *     the client shm_open()s the same name O_RDONLY and mmap()s it
 *     (notify_client.c:836-851). No Mach IPC is involved in the check
 *     at all.
 *   - everything else does a real MIG round trip,
 *     _notify_server_check(), to the daemon.
 *
 * So a failing check can mean the registration never became
 * memory-backed, or the client's shared mapping is not really shared
 * with the daemon's, or the post never reached the daemon. Each step
 * below is labelled and each prints its real status code, so one run
 * names the layer instead of one boot per hypothesis.
 *
 * notify_peek() is the discriminator, and it is public API: its real
 * body (notify_client.c:3578-3588) returns
 * NOTIFY_STATUS_INVALID_REQUEST (6) for a registration that is not
 * memory-backed, and NOTIFY_STATUS_SHM_BASE_NULL (32, reported out as
 * NOTIFY_STATUS_FAILED == 1000000 by that function's own
 * IS_INTERNAL_ERROR remap) when it is memory-backed but the client's
 * mapping is missing -- otherwise it hands back the raw slot value, so
 * peeking either side of the post shows whether the daemon's increment
 * is visible through this process's own mapping.
 *
 * The set_state/get_state pair at the end is a deliberately
 * shm-independent control: it is a pure MIG round trip to the daemon
 * (_notify_server_set_state/_notify_server_get_state), so it passing
 * while the check fails localizes the fault to shared memory rather
 * than to Mach IPC or the bootstrap lookup.
 */
#include <stdio.h>
#include <unistd.h>

extern int notify_register_check(const char *name, int *out_token);
extern int notify_post(const char *name);
extern int notify_check(int token, int *check);
extern int notify_peek(int token, unsigned int *val);
extern int notify_cancel(int token);
extern int notify_set_state(int token, unsigned long long state);
extern int notify_get_state(int token, unsigned long long *state);

/* Real values from notify.h / notify_internal.h. */
#define NOTIFY_OK               0
#define NOTIFY_INVALID_REQUEST  6
#define NOTIFY_FAILED           1000000

static void peek(const char *when, int token)
{
    unsigned int val = 0;
    int status = notify_peek(token, &val);

    printf("notify_peek (%s): status=%d val=%u", when, status, val);
    if (status == NOTIFY_INVALID_REQUEST) {
        printf("  [registration is NOT shared-memory-backed;"
               " notify_check goes over MIG instead]");
    } else if (status == NOTIFY_FAILED) {
        printf("  [memory-backed, but this process has no shm mapping"
               " -- client shm_open/mmap of apple.shm.notification_center failed]");
    } else if (status == NOTIFY_OK) {
        printf("  [shared memory mapped and readable]");
    }
    printf("\n");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char *name = "com.iokit.notifyd.roundtrip";
    int token = -1;
    int check = 0;
    int status;
    int failures = 0;

    status = notify_register_check(name, &token);
    printf("notify_register_check(%s): status=%d token=%d\n", name, status, token);
    if (status != 0) {
        printf("notifyd_roundtrip_smoketest: FAIL (register_check status != 0)\n");
        return 1;
    }

    peek(" after register", token);

    /* First notify_check() after a fresh register_check() always reports
     * check=1 on real Darwin (the "since registration" baseline edge),
     * so drain it before the real test post below. */
    check = -1;
    status = notify_check(token, &check);
    printf("notify_check (baseline, before post): status=%d check=%d\n", status, check);

    /* A second check with nothing posted in between must report 0. If it
     * reports 1, the "changed since last look" bookkeeping is broken on
     * its own and the post below proves nothing either way. */
    check = -1;
    status = notify_check(token, &check);
    printf("notify_check (idle, still before post): status=%d check=%d"
           "  [expected check=0]\n", status, check);
    if (status != 0 || check != 0) {
        printf("  ^ FAIL: an unposted name reported a state change\n");
        failures++;
    }

    status = notify_post(name);
    printf("notify_post(%s): status=%d\n", name, status);
    if (status != 0) {
        printf("notifyd_roundtrip_smoketest: FAIL (post status != 0)\n");
        notify_cancel(token);
        return 1;
    }

    usleep(200000);

    peek(" after post", token);

    check = -1;
    status = notify_check(token, &check);
    printf("notify_check (after post): status=%d check=%d"
           "  [expected check=1]\n", status, check);
    if (status != 0 || check == 0) {
        printf("  ^ FAIL: the post did not reach this token\n");
        failures++;
    }

    /* Shm-independent control: a pure MIG round trip to the daemon. */
    status = notify_set_state(token, 0x00c0ffeeULL);
    printf("notify_set_state(0xc0ffee): status=%d\n", status);
    if (status == 0) {
        unsigned long long state = 0;
        status = notify_get_state(token, &state);
        /* Both halves fixed-width, so a passing run reads as
         * 0x0000000000c0ffee rather than 0x000c0ffee -- the latter looks
         * like a mismatch against the "expected" text right next to it. */
        printf("notify_get_state: status=%d state=0x%08x%08x"
               "  [expected 0x0000000000c0ffee]\n",
               status, (unsigned int)(state >> 32), (unsigned int)state);
        if (status != 0 || state != 0x00c0ffeeULL) {
            printf("  ^ FAIL: the daemon did not keep the state we set"
                   " (this leg is pure Mach IPC, no shared memory)\n");
            failures++;
        }
    } else {
        printf("  ^ FAIL: notify_set_state failed"
               " (this leg is pure Mach IPC, no shared memory)\n");
        failures++;
    }

    notify_cancel(token);

    if (failures == 0) {
        printf("notifyd_roundtrip_smoketest: ROUNDTRIP OK\n");
        return 0;
    }
    printf("notifyd_roundtrip_smoketest: FAIL (%d checks failed, see above)\n",
           failures);
    return 1;
}
