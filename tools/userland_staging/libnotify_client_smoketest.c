/* Real smoketest for libnotify_client.a -- see build_libnotify_client.sh's
 * own header comment for what this does and does not prove. Calls the
 * real public notify_register_check()/notify_post()/notify_cancel() API
 * (PureDarwin's real notify_client.c, not a stub), expecting each to
 * fail cleanly (no live notifyd/bootstrap registrar in this project yet)
 * rather than hang or crash. */
#include <stdint.h>
#include <unistd.h>

extern int notify_register_check(const char *name, int *out_token);
extern int notify_post(const char *name);
extern int notify_cancel(int token);

static void put(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    write(1, s, n);
}

int main(void) {
    int token = -1;
    int status = notify_register_check("com.iokit.syslogd.smoketest", &token);
    put("notify_register_check returned\n");
    (void)status;

    notify_post("com.iokit.syslogd.smoketest");
    put("notify_post returned\n");

    if (token >= 0) {
        notify_cancel(token);
        put("notify_cancel returned\n");
    }

    put("libnotify_client_smoketest: done\n");
    return 0;
}
