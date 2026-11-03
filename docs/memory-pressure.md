# Memory pressure (jetsam bands)

How this port decides which process dies when memory runs out, and why.

## The problem (2026-11-01)

On a real Pi 3 with Florence open, opening Settings' City popup ran the
system out of memory, and the desktop dropped back to the gsdm login window.
The serial console showed the kernel's memory killer (jetsam, xnu's
memorystatus) killing processes in this order:

```
memorystatus: killing process 103 [gdnc]    in high band IMPORTANT (18) ...
memorystatus: killing process 229 [login]   ...
... sshd, chronyd, syslogd, gsdm, audiod ...
memorystatus: killing process 236 [Xorg]    ...
```

Then launchd restarted every killed job. Florence and its WebKit processes,
which held the memory, were never touched.

## Why

- Jetsam kills the lowest priority band first
  (`bsd/sys/kern_memorystatus.h:40-66`).
- Every new process starts in band 18, `JETSAM_PRIORITY_DEFAULT`
  (`bsd/kern/kern_fork.c:1446-1447`). On macOS and iOS, launchd and the
  app-lifecycle daemons move processes to other bands; nothing here did. launchd
  does apply a job's `JetsamProperties` at spawn (`launchd core.c`, under
  `TARGET_OS_EMBEDDED`, which the `arm64-apple-ios` target sets), but no plist
  had one.
- Within a band, the process at the head of the list goes first, and processes
  are added at the tail (`bsd/kern/kern_memorystatus.c:2066`). Only band 10 is
  re-sorted by memory size (`:5642`). So in band 18 the **oldest** process died
  first: the daemons, gsdm and the X server, before any application.

## The bands now

| Band | Processes | Set by |
|---|---|---|
| 19, CRITICAL | every launchd daemon (configd, notifyd, syslogd, sshd, chronyd, audiod, gsdm, wifi, bluetooth, …) and the X server | `JetsamProperties` (`JetsamPriority` 19) added to each plist by `tools/init_binary/inject_into_sd_image.sh`; gsdm starts Xorg in 19 (`gsdm.c` `exec_in_band`) |
| 18, default | the desktop session: Xsession, Window Maker, Naples, Milan, gdnc, gpbs, and anything started from a shell | the kernel default |
| 10, FOREGROUND | applications: whatever you open from the Naples dock, Milan, Window Maker's menu, or `NSWorkspace` (`gopen`, `openFile:`) | the launchers, at exec |

Under pressure an application dies first, the largest first (band 10 is sorted
by size). Then the session, and the system last.

**Mechanism.** A forked child always starts in band 18, and exec keeps the
band. So a launcher sets the band at the exec, with `posix_spawn`'s
`POSIX_SPAWN_SETEXEC` and `posix_spawnattr_setjetsam_ext`. The kernel applies
that without a privilege check (`bsd/kern/kern_exec.c:3859-3884`); changing a
running process's band with `memorystatus_control` needs root.

- **gnustep-base** (fork, `iokit-port`): `NSTask -setJetsamPriority:`
  (`NSTask+GNUstepBase.h`). The attribute is built in the parent; the child
  execs through `posix_spawn` and falls back to `execve`.
- **gnustep-gui** (fork, `iokit-port`): `NSWorkspace _launchApplication:`
  launches applications in 10.
- **Naples, Milan**: their launchers set 10 on the app's `NSTask`.
- **Window Maker** (fork, `iokit-port`): `launch.c` spawns menu, dock and
  session-restore commands in 10; the `wmsetbg` helper keeps 18.
- **gsdm**: the X server in 19.

Helper processes an application starts (Florence's WebKit processes) are forked
by it, so they start in 18. They exit when the application is killed.

## Tools

- `jetsam_bands` (`/usr/sbin`, root): every process's band, in kill order. The
  first row is the next victim.
- `memhog` (`/usr/local/bin`): re-execs itself into band 10 and allocates until
  killed. It's a test aid.
- `tools/jetsam_qemu_test.sh` prints the bands after boot and with the desktop
  running. It opens Activity from the Naples dock and Preview through
  `NSWorkspace`, then runs `memhog` and lists who was killed and who survived.
  Result, 2026-11-01:

  ```
  memorystatus: killing process 150 [memhog] in high band FOREGROUND (10)
  ```

  That was the only kill. The daemons, Xvfb, Window Maker, Naples and Milan
  survived, and so did Activity and Preview (also in band 10, but smaller).

## Notes

- Every launchd daemon also has a 5 MB memory limit. That comes from launchd's
  embedded default daemon high-water mark (`launchd core.c`
  `DEFAULT_JETSAM_DAEMON_HIGHWATERMARK`), which is applied without the fatal
  flag. Going over it produces a resource report, not a kill; that is a likely
  source of the `EXC_CORPSE_NOTIFY` lines in the kernel log.
- Under QEMU, Naples takes longer than its 0.5 s long-click time to see a
  click's button-up, so a scripted click opens the dock icon's menu.
  `jetsam_qemu_test.sh` then clicks the menu's Open, which uses the same
  launch code.
