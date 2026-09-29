# su/sudo: dyld environment hardening (DAR-340 C1) and setuid provisioning (H1)

## The hole (fixed)
This port has no AMFI, so `amfi_check_dyld_policy_self` in
`tools/userland_staging/build_dyld_core.sh` decides dyld's policy. It used to
return all-ones unconditionally, so dyld-940 honored `DYLD_INSERT_LIBRARIES`,
`DYLD_LIBRARY_PATH` etc. in setuid-root processes
(`third_party/dyld/dyld/DyldProcessConfig.cpp:491-497,841-845`; env pruning at
`:505-511` only runs for macOS platform binaries, ours are iOS-platform). Any
local user got root through `su`/`sudo`. Worse, the `issetugid` linked into
`dyld.macho` is the always-0 stub
(`libc_locale_env_stubs.c`), so the shim now issues the real trap (syscall 327,
`xnu-7195/bsd/kern/syscalls.master:497`).

Fix: setuid/setgid (real `issetugid()`) or a `__RESTRICT,__restrict` section
(`MachOFile.cpp:1339`) => policy output is only `ALLOW_AT_PATH`; every other
`allow*` flag is false, so all `DYLD_*` are ignored. `su.macho` and `sudo.macho`
also carry the `__RESTRICT` section (belt and braces).

## Regression procedure
1. Build: `build_dyld_core.sh && build_dyld_link.sh`, `build_dyld_inject_probe.sh`,
   `build_su.sh`, `build_sudo.sh`; make an image (`tools/make_sd_image.sh`).
2. Boot with `tools/boot_test.py` (never plain QEMU), log in as root.
3. Provision: `/usr/sbin/provision-setuid` (prints `ls -ln`; su/sudo must be
   `-rwsr-xr-x 0 0`, `/etc/sudoers` `-r--r----- 0 0`). Host-built images cannot
   chown to root (host user is uid 502, host `sudo` needs a password), so the
   image ships su/sudo as 0755 NON-setuid until this runs.
4. `/usr/sbin/dyld-inject-regression` -- exits 0 only if: the control (plain
   binary) DOES run the probe constructor; a setuid copy of `id` does NOT
   (issetugid path); real `su` and `sudo -V` do NOT; a non-setuid copy of `su`
   does NOT (`__RESTRICT` path). It prints SKIP (not PASS) if su/sudo are not
   setuid, because the result would be vacuous.
5. Manual spot check as the non-root user:
   `DYLD_INSERT_LIBRARIES=/usr/lib/dyld_inject_probe.dylib /usr/bin/su -` then
   `cat /tmp/dyld_inject_probe.out` must not exist / not contain `euid=0`.

## Other review items
* M1 `Defaults secure_path` is in the shipped sudoers.
* M2 `Defaults noexec` is NOT supported: `sudo_noexec.so` is not built, and
  `src/exec_common.c` `disable_execute()` just preloads that path; dyld ignores
  a missing inserted library in a non-restricted child, so the restriction is
  silently ineffective (fail-open). Do not rely on it.
* M3 `SHELL_IF_NO_ARGS`/`SHELL_SETS_HOME` removed (not upstream defaults,
  `configure.ac:1226-1246`).
* L8 the RELEASE kernel forces MNT_NOSUID; setuid-dependent tests only make
  sense on the development kernel (`kern_exec.c:5740`: setuid dropped on
  MNT_NOSUID).
