# sudo port (DAR-340) -- first-run checklist

Drafted on Linux, then compiled and linked on a Mac (2026-09-28): 173 objects
compile clean, `sudo.macho` links against the current libc_build dylibs, and
`bind_target_audit.sh` / `dead_stub_audit.sh` / `weak_def_audit.sh` pass on it.
**Nothing has been booted; sudo has never run.** Steps below are what is left
(steps 1-5 were exercised once; re-run them for real in your own libc_build):

1. `./setup_third_party.sh` (clones `third_party/sudo` @ v1.9.5p2).
2. Build prerequisites as for `su` (`build_su.sh`'s dep list), then
   `tools/userland_staging/build_sudo.sh`.
3. Compile pass: the loop reports every failing file before stopping. Likely
   causes, in order: a wrong answer in `sudo_config_darwin.def` (each is tagged
   `[cfg]/[sdk]/[libc]`); a header missing from the iPhoneOS SDK
   (`utmpx.h`, `util.h`, `machine/endian.h`); a `-D`/`-I` from upstream's
   Makefiles that `build_sudo.sh` doesn't mirror yet. Drop `-w` when triaging.
4. Link pass: run `gap_audit.sh` on the failure list; extend
   `SUDO_EXTRA_SYSCALLS` (setuid family etc.) and the glue list, then bake the
   settled values into the script. `[libc]` entries in the `.def` that turn out
   not to be exported by `libsystem_c.dylib` should be **removed from the .def**
   so upstream's fallback compiles in (lib/util fallbacks are gated
   `#ifndef HAVE_X`).
5. `bind_target_audit.sh` (hard gate, already wired in) and, with no args,
   the full sweep; `weak_def_audit.sh` if any dylib symbol strength changed.
6. Boot: `inject_into_sd_image.sh` installs `/usr/bin/sudo` (4755) and a
   minimal `/etc/sudoers` (`root`, `%wheel`). Test `sudo -v`, `sudo id`,
   a bad password (3 tries), and a user missing from sudoers (must log via
   syslog and refuse).

Known open questions: `src/utmp.c` is replaced by the no-op
`sudo_utmp_compat.c` (no utmpx record for a sudo pty; see its header for the
real-Libc follow-up); `SUDOERS_LOG_CLIENT` is on because v1.9.5p2 does not
compile without it (see the .def); whether `-ffreestanding` interacts badly with sudo's `<stdbool.h>` fallback
(`include/compat/stdbool.h`); `crypt.c` from Libc (as `su` does) vs sudo's
expectations of `crypt(3)` returning NULL on failure.
