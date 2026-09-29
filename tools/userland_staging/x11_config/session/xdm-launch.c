/*
 * /usr/X11/bin/xdm-launch: what launchd runs at boot
 * (org.puredarwin.x11-session.plist). It decides whether the graphical
 * login should start at all, then exec()s xdm in the foreground.
 *
 * Gates, each of which makes it exit 0 WITHOUT starting Xorg or xdm:
 *
 *  1. The kernel boot-args must ask for the HDMI framebuffer:
 *     "hdmi_console=<nonzero>". That is the arg osfmk/arm/arm_init.c:482-483
 *     parses with PE_parse_boot_argn("hdmi_console", ...) before it sets up
 *     video at all; without it there is no framebuffer for Xorg's
 *     puredarwingop driver, so Xorg would just fail. runvm_hdmi.sh
 *     (EXTRA_BOOT_ARGS) and the u-boot bootcmd (CONFIG_BOOTCOMMAND in
 *     third_party/u-boot configs/rpi3_xnu_defconfig) set it; plain
 *     ./runvm.sh does not. The string is read with sysctl kern.bootargs
 *     (third_party/xnu-7195/bsd/kern/kern_sysctl.c:2159-2173, which copies
 *     PE_boot_args()); the image ships no sysctl(8), hence this program
 *     rather than a shell script.
 *  2. /etc/x11-session.disabled must not exist (the pre-existing off switch).
 *
 * The plist's KeepAlive is {SuccessfulExit = false}, so a gated-off exit 0
 * is never restarted, while a nonzero exit (exec failure, or xdm dying
 * with a signal or nonzero status) is restarted every ThrottleInterval.
 * The console login is unaffected in every case.
 *
 * Before exec it also clears stale X0 lock/socket files a hard reset can
 * leave in /tmp (the old x11-session script did the same) and makes sure
 * /var/lib/xdm (xdm's authDir) and /var/run exist.
 *
 * Xorg still needs the USB mouse plugged in before boot (DAR-442).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define XDM "/usr/X11/bin/xdm"
#define XDM_CONFIG "/usr/X11/lib/X11/xdm/xdm-config"

static int hdmi_console_requested(void)
{
    char buf[1024];
    size_t len = sizeof(buf) - 1;
    char *p, *save = NULL;

    if (sysctlbyname("kern.bootargs", buf, &len, NULL, 0) != 0) {
        fprintf(stderr, "xdm-launch: sysctl kern.bootargs: %s\n", strerror(errno));
        return 0;
    }
    buf[len < sizeof(buf) ? len : sizeof(buf) - 1] = '\0';
    for (p = strtok_r(buf, " \t", &save); p != NULL; p = strtok_r(NULL, " \t", &save))
        if (strncmp(p, "hdmi_console=", 13) == 0 && atoi(p + 13) != 0)
            return 1;
    return 0;
}

int main(void)
{
    if (access("/etc/x11-session.disabled", F_OK) == 0) {
        fprintf(stderr, "xdm-launch: /etc/x11-session.disabled exists, not starting xdm\n");
        return 0;
    }
    if (!hdmi_console_requested()) {
        fprintf(stderr, "xdm-launch: no hdmi_console=1 in kern.bootargs, not starting xdm\n");
        return 0;
    }
    (void)unlink("/tmp/.X0-lock");
    (void)unlink("/tmp/.X11-unix/X0");
    (void)mkdir("/var/run", 0755);
    (void)mkdir("/var/lib", 0755);
    (void)mkdir("/var/lib/xdm", 0755);
    execl(XDM, "xdm", "-nodaemon", "-config", XDM_CONFIG, (char *)NULL);
    fprintf(stderr, "xdm-launch: exec %s: %s\n", XDM, strerror(errno));
    return 1;
}
