# fork()/waitpid()/execlp() canonical owner audit (DAR-292)

Canonical owner: `libsystem_c.dylib` exports `_fork`, `_waitpid`, `_wait4`,
`_execl/_execle/_execlp/_execv/_execvp/_execvP`, `_execve`
(`build_libsystem_c.sh`, `libsystem_c_fork_compat.c`: real atfork handlers +
real `_libc_fork_child()`). The raw `___fork` trap is `libsystem_kernel.dylib`'s
(it clears the dylib's own cached `__current_pid`). No shipped image other than
the exceptions below may define any of these.

Enforced by `tools/userland_staging/fork_owner_gate.sh` (run it over every built
image after touching any link recipe; pass image paths to check a subset).

## Where the ~92 private copies came from (measured with `nm -m`, 2026-09-30)

| Source | Symbols carried | Images |
|---|---|---|
| `cp_mv_syscalls.c` (`COMMON_EXTRA` in `build_simple_cmds.sh`; raw-svc `fork`/`waitpid`, no atfork, stale `__current_pid`) | `_fork` `_waitpid` | ~80 simple commands (cat, ls, sed, tar, ...), plus getty/vipw/passwd/pwd_mkdb (they compile it directly) |
| `Libc/gen/FreeBSD/exec.c` + `simple_cmds_execve_compat.c` + `sys__execve.o` (per-command extras, `LINK_COMMON`) | `_execl*` `_execv*` `_execve` | env, mv, nice, nohup, chroot, apply, xargs, find, diff, tar, getty, login, cron, crontab, vipw |
| `chpass_fork_compat.c` (raw-svc `fork`) plus archive members of `liblaunchd_syscalls.a` (`fork_compat.o`, `sys____fork.o`, `exec.o`, `wait4_compat.o`, `waitpid_compat.o`, `sys__execve.o`, `sys____wait4_nocancel.o`) pulled ahead of the dylib because archives precede dylibs on the link line | all | chpass, pw, su, sudo, pwd_mkdb, passwd, vipw, login, chronyd (`___fork`), launchctl, pd_console_login |
| `bash_syscalls.c` (`fork` with hand-zeroed `__current_pid`, `wait4`, `waitpid`, `waitpid$UNIX2003`) | | bash |
| `cron_extra_syscalls.c` (raw `fork`, `wait4`, `waitpid`) | | cron, crontab |
| `login_syscalls.c` (raw `fork` with hand-zeroed `__current_pid`) | | login |
| `sys_getpid_current_pid_only.o` + `-Wl,-u,__current_pid` (private pid cache that goes stale in a libsystem_c fork child) | | chronyd, launchctl |
| `launchd_syscall_obj/fork_compat.o` (dispatch/dyld/xpc-aware) | all | launchd.macho (kept, see below) |

## Fix

* `make_no_fork_archive.sh` derives `liblaunchd_syscalls_no_fork.a` (members
  defining the family dropped, detected with `nm`); every non-launchd consumer links it.
* Private definitions deleted from the `*_syscalls.c` shims; `vfork`/`wait` stay local
  (no shipped dylib exports them) and forward to the canonical `fork`/`wait4`.
* `-Wl,-alias,_waitpid,_waitpid$UNIX2003` removed everywhere (libsystem_c exports
  the `$UNIX2003` name; an alias whose base is not local is the ld64 bad-bind hazard).
  pw's `-Wl,-alias,_wait4,__wait4` became a real local forwarder (`pw_wait4_compat.c`).
* `common_*.o` objects in `build_simple_cmds.sh` are rebuilt when the source is newer
  (a stale `common_cp_mv_syscalls.o` kept the private copies alive).

## Exceptions

* `pwtool.macho`: fully static by design (DAR-314); defines none.
* `launchd.macho`: PID 1 keeps its private `fork_compat.o`. libsystem_c's fork cannot
  call `dispatch_atfork_*` / `_dyld_fork_child` (libdispatch is built after
  libsystem_c: ld64 cycle; a `-U` deferral aborts every non-dispatch binary), launchd needs
  them (real Libsystem init.c order is documented in `build_launchd_syscalls.sh`).
  Moving launchd needs libsystem_c to grow those hooks first (separate ticket).
* `kqueue_smoketest.macho`: `_execve` only (links `simple_obj/sys_*.o` wholesale; test-only).
* `vfork` (88 images): still a local forwarder to the canonical `fork()`; no dylib exports `_vfork`.
