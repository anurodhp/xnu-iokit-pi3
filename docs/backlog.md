Resolved items moved to `docs/historic/backlog.md` — see there for fix history.

# Backlog

Open items found but not yet fixed, mostly surfaced by two adversarial
code reviews on 2026-08-13 (one on `BCM2837SDHCI`/the boot chain, one on
the rest of the custom IOKit kexts) plus a few longer-standing known gaps.
Organized by how much it matters, not by discovery order. File:line
references are as of commit `86b1653`; re-check before trusting them if
this list is stale.

---

## P0 — real-hardware risk (would plausibly hang, storm, or panic a real Pi)

1. **NOT ATTEMPTED this pass** - see the note after item 6 for why (real
   fix needs `IOCommandGate`/workloop integration, higher risk/effort than
   the rest of this batch).

   **`BCM2837SDHCI::doAsyncReadWrite()` has no serialization at all** —
   no `IOCommandGate`, no workloop, no mutex. Two concurrent requests
   (now plausible: the SD image has two mounted partitions, `disk0s1` +
   `disk0s2`) can interleave register writes and both consume from the
   same `fPendingIntStatus`, causing one to eat the other's completion —
   silent data corruption or a hang, not a clean error. Survives today
   only because nothing currently issues overlapping I/O.

2. **NOT ATTEMPTED this pass** - see the note after item 6.

   **No error recovery anywhere in `BCM2837SDHCI`.** Every error path is
   `IOLog(); return false;`. The SDHCI spec requires `SOFTWARE_RESET_CMD`/
   `SOFTWARE_RESET_DAT` after a command/data error and `CMD12` to abort an
   in-flight transfer; none are issued. After the first real CRC error,
   timeout, or card removal, `CMD_INHIBIT`/`DAT_INHIBIT` stay asserted and
   every subsequent command dies after a 1s busy-wait — the classic
   bring-up cliff QEMU's SD model never reproduces.

3. **NOT ATTEMPTED this pass** - see the note after item 6.

   **`BCM2837SDHCI`'s SD clock never leaves ~490 kHz / 1-bit.**
   `setClock()` always writes divisor `0xFF` regardless of the requested
   rate (explicitly `// TODO` for the data-phase case), and
   `kRegHostControl` is never written to widen the bus. QEMU has no timing
   cost; on real hardware this is roughly 60 KB/s — mounting HFS+ and
   exec'ing a binary at that rate will look indistinguishable from a hang.

4. **NOT ATTEMPTED this pass** - see the note after item 6.

   **No power-on ramp / 74-clock delay before `CMD0`**, and power is
   enabled *after* the clock in `start()` rather than before. Spec
   requires supply ramp + ≥1ms + 74 clocks first. Untested by QEMU.

5. **NOT ATTEMPTED this pass** - see the note after item 6.

    **No memory barriers anywhere in `iokit/`.** `grep -rn "dsb\|dmb\|isb\|OSSynchronizeIO"` across the whole driver tree returns nothing —
    the only ordering primitive present (`OSMemoryBarrier()` in both
    interrupt controllers) guards CPU-to-CPU ordering, not MMIO ordering.
    Likely benign if `IODeviceMemory` mappings land Device-nGnRnE (typically
    already ordered for same-peripheral accesses), but unverified in this
    tree, and QEMU/TCG makes all MMIO strongly ordered regardless of
    whether real hardware would. Cheapest place to add one: after the
    `Enable_IRQs`/`Disable_IRQs` register writes in
    `BCM2837InterruptController::enableVector()`/`disableVectorHard()`,
    before the next `IRQ_PENDING` read.

6. **GPIO pinmux for the SD slot is still only ever exercised in QEMU**,
    where it's a documented no-op (the emulated card is parented onto the
    bus regardless of GPFSEL). This session's GPIO34-39-to-input fix (for
    the WiFi-collision bug) is logically sound but genuinely untested on
    real silicon — first real-hardware boot is the only way to close this.

**Why items 1-5 weren't attempted this pass** (note: these numbers refer
to this same set of items by their position in this list — see
`docs/historic/backlog.md` for the original item numbers 4-7, 10 this note
was first written against): all real, all in `BCM2837SDHCI.cpp`'s hot I/O
path, and all meaningfully higher risk than the rest of this batch - #1
needs real `IOCommandGate`/workloop integration (an architectural change,
not a local fix), #2 needs a careful CMD12/reset state machine that could
interact with the just-verified interrupt-delivery path in subtle ways,
#3 changes real signal timing (clock divisor/bus width) which is exactly
the kind of change that's hardest to trust from QEMU alone since QEMU has
no timing cost to begin with, and #4 similarly touches power/clock
sequencing. Following this backlog's own explicit guidance ("go one at a
time, rebuild+boot-test after each, be willing to stop partway through and
report clearly rather than rushing all four") - after a productive pass
through several lower-risk items, this was the right place to stop and
hand off clearly rather than push into higher-risk territory in the same
sitting. #5 (memory barriers) is lower-risk but was deferred alongside
these since it's genuinely unverifiable from QEMU (which makes all MMIO
strongly ordered regardless) - no rebuild+boot-test could actually
confirm or refute it, so there was no way to close the verification loop
this pass even if the change itself is cheap.

7. **Real, reproducible kernel PANIC (not a hang) from sustained fast
    console output — found 2026-08-16 while QEMU-verifying the real
    `yes` command (Phase 4 step 3, `docs/roadmap.md`).** `yes.macho`
    (real, unmodified Apple `shell_cmds/yes/yes.c` — a tight `puts("y")`
    loop, nothing unusual) reliably panics the kernel within seconds of
    starting: `"Processing of an interrupt (type = 2, handler address =
    0xfffffff00732d170, vector = 0) took 397932291 nanoseconds (timeout =
    354986666 ns)"` — the real `INTERRUPT_MASKED_DEBUG` watchdog (the
    same mechanism `cpu-factor=10` already tunes for ordinary QEMU/TCG
    jitter, per `CLAUDE.md`'s "Boot / test commands" section) firing for
    real, not a timing false-positive: ~400ms with interrupts
    provably masked is a genuine, large stall, not jitter. A single
    ordinary userspace process writing to its own stdout as fast as
    possible should never be able to mask interrupts for hundreds of
    milliseconds, on any real system — and the handler address resolves
    (via `nm` against this session's kernel build, not guessed) to
    `_rtclock_intr+0x0` exactly, i.e. the real-time-clock/timer
    interrupt's own handler, not the console/UART path as first
    suspected. The panic message's own semantics ("processing of an
    interrupt ... took N ns") means `rtclock_intr()` itself, once
    entered, took ~400ms to return — real per-tick scheduler/timer
    bookkeeping inside that handler is what's slow under this specific
    load, not something masking interrupts before it ran. Leading
    hypothesis, not confirmed: `yes`'s extremely tight `write()` loop
    (one syscall per line, no batching, real `puts()`+`fflush` per
    iteration) drives an unusually high trap/context-switch rate, and
    something per-tick inside `rtclock_intr`'s real scheduling
    accounting scales with that rate in a way this project has never
    exercised before (no prior Phase 4 command loops anywhere near this
    fast). Not root-caused or fixed this pass — flagging as P0 because
    it's a real userspace-triggerable kernel panic, not merely a
    slow/ugly path, and because real hardware's timer tick rate is not
    obviously immune to the same mechanism (unlike most of this
    section's QEMU-vs-real-hardware caveats, which lean on QEMU-specific
    timing slop).
    `yes.macho` still ships (the bug is not in `yes` itself, which is
    real, correct, unmodified source) but should not be treated as
    "verified safe to run" the way this project's other Phase 4 step 3
    commands are — anyone hitting this again should start from the exact
    panic string above and `nm`/`atos` the handler address against the
    current kernel build rather than re-diagnosing from scratch.

---

## P1 — real correctness bugs (not hardware-specific, will eventually bite)

1. **`tools/init_binary/init.c` (PID 1) has a few real rough edges**,
    lowest priority since it's bootstrap scaffolding Phase 4 will replace:
    - `read_line()`: a `sys_read` error/EOF makes `shell_main()`'s
      `if (n < 0) continue;` an unthrottled tight loop re-printing the
      prompt at 100% CPU on any console where input isn't polled (masked
      today by `serial=3` always being passed).
    - `init_main()`'s `sys_wait4()` return value is discarded, and there's
      no respawn-rate throttle — a child that dies instantly (e.g. `ECHILD`)
      would fork-loop unthrottled. Classic `init` rate-limits this.
    - The raw-syscall inline-asm wrappers (`syscall3`/`syscall4`/`sys_fork`/
      `sys_wait4`) don't mark the `is_error`/status output operand with an
      early-clobber (`&`) constraint. Currently safe (the compiler happens
      to consume inputs before the clobbered write), but fragile across a
      compiler upgrade — add `&` to be robust rather than lucky.
    - The boot banner still says `"booted via mockfs"` — stale since root
      moved to real HFS+ (`d7c6934d8`). Cosmetic, but it's the first thing
      a human sees.

2. **`SMSC9514Ethernet` has two real, confirmed-still-open logic bugs**
    (six other sub-items from this entry's original 2026-08-22 filing
    have since resolved one at a time — PHY-reset timeout handling,
    MDIO/auto-negotiation wiring, `attachInterface()` never called,
    packet-filter shadowing, a suspected `fInterface` UAF that turned
    out not to be a real race, real-hardware link-up/ping verification,
    and the 2026-08-25 RX-completely-dead report — full history archived
    to `docs/historic/backlog.md`):
    - `smsc95xx_reset()`-equivalent code does blind register writes to
      `HW_CFG`/`PM_CTRL`/`MAC_CR` where Linux does read-modify-write,
      clobbering unrelated bits (`BIR`/`RXDOFF`/`MEF`, full-duplex/
      auto-speed defaults). Confirmed still true 2026-08-27 (`resetChip()`
      still writes `kMacCrTxEn | kMacCrRxEn` directly, no RMW).
    - `frameLen & 0x7FF` truncates silently with no length guard.

---

## P2 — known gaps / incomplete work (not bugs, just unfinished)

### Userland library debt (filed 2026-08-26, from the real libdispatch port + dylib conversion)

d1. **`libdispatch.dylib` duplicates process-global state with its own
    sibling dylibs — real heap-corruption-class risk, same shape as the
    bug `build_libsystem_malloc.sh` already fixed once.** `tools/
    userland_staging/build_libdispatch_dylib.sh` builds a real
    `libdispatch.dylib` (`-nostdlib`, MH_DYLIB confirmed via `otool`) but
    statically bundles its CURRENT dependency stack
    (`libmach_ipc.a`/`libclosure.a`/`libmalloc.a`/`libplatform.a`/
    `libpthread.a`/`libc_gen.a`/`libc_string.a`/`libc_stdio.a`) rather
    than linking against this project's REAL sibling dylibs
    (`libsystem_kernel.dylib`/`libsystem_platform.dylib`/
    `libsystem_malloc.dylib`/`libsystem_pthread.dylib`/
    `libsystem_c.dylib`, all already built). That means any real process
    loading both `libdispatch.dylib` and one of those sibling dylibs
    gets two independent copies of `mach_task_self_`, `g_zone` (the live
    heap zone — see `build_libsystem_malloc.sh`'s own header comment for
    why that specific duplicate is real heap-corruption territory, not
    just "wrong value"), `errno`, etc. **Do not load this dylib alongside
    the real sibling dylibs in a multi-dylib process until this is
    fixed.** Real fix: relink against the sibling `.dylib`s (`-l`-style,
    not static bundling) instead of their static ancestors.

d2. **Real duplicate-symbol conflicts between archives built at
    different points in this project's history — surfaced while linking
    `libdispatch.dylib`, not yet de-duplicated for good.** Three
    confirmed, real cases (not fabricated exclusions — verified via `nm`
    before excluding each one):
    - `_pthread_key_init_np`: real in both `libpthread.a` (canonical)
      and `libc_locale_for_stdio.a`'s `pthread_tsd_shim.o` (an older
      compat copy from before `libpthread.a` had its own).
    - `_voucher_mach_msg_set` + `_mach_port_extract_right`/
      `_insert_right`/`_unguard`: real in both `libmach_ipc.a`'s own
      `mach_ipc_dead_stubs.o` (now fully redundant — all 4 of its
      symbols are superseded elsewhere, confirmed via `nm`) and the real
      voucher.c/MIG-generated bodies that superseded it.
    - `_vm_allocate`/`_deallocate`/`_map`/`_protect`/`_remap`/
      `_purgable_control`/`_read`, `_mach_port_mod_refs`/`_deallocate`:
      real in both `libmalloc.a`'s own `mach_vm.o`/
      `libmalloc_dispatch.o` and `dyld_core.a`'s `vm_mapUser.o`/
      `mach_portUser.o` (built for the dyld port, regenerated
      independently).
    `build_libdispatch_dylib.sh` works around all three via `ar x`
    exclusion or by simply not linking the conflicting object (see that
    script's own comments for exactly which). None of these are fixed
    at the source — a real followup pass should pick ONE canonical
    provider per symbol and delete the other, project-wide, not just in
    this one dylib's link line.

d3. **Standard POSIX/QoS pthread API entirely missing from this
    project's curated `libpthread.a`/`libsystem_pthread.dylib` — a real
    completeness gap, confirmed via `nm -g`, not assumed.** Surfaced by
    the real libdispatch port's own trial link
    (`tools/userland_staging/build_libdispatch_real.sh`'s own HONEST
    SCOPE STATEMENT has the full, current list). Two groups:
    - Standard, simple POSIX API with no real implementation anywhere
      in this project: `pthread_attr_init`/`_destroy`/`_setdetachstate`/
      `_setstacksize`/`_setschedparam`/`_setschedpolicy`/
      `_getschedparam`/`_getschedpolicy`/`_get_qos_class_np`/
      `_set_qos_class_np`, `pthread_exit`, `pthread_sigmask`,
      `pthread_setschedparam`, `pthread_get_stackaddr_np`,
      `qos_class_main`, `pthread_qos_max_parallelism`,
      `pthread_set_qos_class_self_np`.
    - Real QoS-override/workqueue-internals API, genuinely more
      involved (interacts with the kernel workqueue subsystem this
      project's `DISPATCH_USE_INTERNAL_WORKQUEUE=1` choice was
      specifically meant to avoid needing):
      `_pthread_qos_override_start_direct`/`_end_direct`,
      `_pthread_workqueue_override_start_direct`/`_reset`/
      `_start_direct_check_owner`, `_pthread_workloop_create`/
      `_destroy`, `_pthread_set_properties_self`,
      `_pthread_qos_class_encode`.
    Real Apple source for both groups should be in
    `third_party/libpthread` already (pinned repo) — this is a "port the
    missing files" task, not a "write from scratch" one; not yet
    attempted.

d4. **A handful of raw syscall traps this project hasn't generated
    stubs for yet, needed by the real libdispatch port.** `mmap`/
    `munmap` (raw trap objects exist -- `pthread___mmap.o`/
    `pthread___munmap.o` -- but only export the triple-underscore raw
    trap name, not the convenience wrapper libdispatch actually calls),
    `kevent_qos`, `clock_gettime_nsec_np`, `kpersona_get`,
    `thread_destruct_special_reply_port`. Small, mechanical, same
    create-syscalls.pl-based pattern already used throughout `tools/
    userland_staging/build_libxpc_syscalls*.sh`.

d5. **Real Mach voucher MIG routines not yet regenerated.**
    `mach_voucher_attr_command`/`_deallocate`/`_debug_info`/
    `_extract_attr_recipe`, `host_create_mach_voucher`,
    `host_get_special_port` -- real client routines from
    `mach_voucher.defs`/`host_priv.defs` (osfmk), same real mig-regen
    pattern already used for `job.defs`/`mach_port.defs`/
    `protocol.defs` elsewhere in this project; not yet attempted.

d6. **CLOSED 2026-08-26 (critical path) — launchd's full daemon +
    server-MIG source tree compiles and trial-links down to 109
    undefined symbols, from 266 at the start of this pass (a 59%
    reduction).** All 9 real daemon source files (`core.c` — 12,162
    lines, launchd's actual job-management engine — plus
    `pd_launchd_boot.c`/`kill2.c`/`pd_sandbox_check.c`/
    `pd_console_login.c`/`pd_launchd_plist.c`/`log.c`/`ipc.c`/
    `launchd.c`/`runtime.c`) and all 7 real SERVER-side MIG files
    compile clean and trial-link via `build_launchd_core.sh`/
    `build_launchd_syscalls.sh`/`launchd_libc_extras.a`. Real
    CoreFoundation is wired in (`libCoreFoundation.a`, see item d7) —
    confirmed launchd's actual CF usage (just `pd_launchd_plist.c`,
    plist parsing) doesn't need ICU. The full session-by-session fix
    history — 32+ syscall/libc gaps closed via selective `ar x`
    extraction and curated wrappers, real Mach host/port admin
    routines, `mktime`/`localtime`/`ctime_r` unblocked via a real
    `generate_features.pl` regen, a real from-scratch `ld2xpc()`/
    `xpc2ld()` confirmed genuinely absent from both the real
    `launchd-842.92.1` and PureDarwin trees, etc — is archived verbatim
    to `docs/historic/backlog.md`; this entry only tracks what's left.

    **What's left (109 symbols), categorized, every item individually
    investigated this pass — no unexplored territory**:
    - **Genuine scope walls** (separate, large undertakings, not bugs):
      real ICU (~50 symbols: CFCharacterSet/CFUniChar/CFLocale/
      CFNumberFormatter/StringEncodings-internal — since ported for CF
      generally, see item d7, but not yet re-verified against launchd's
      own trial link specifically); `asl_*` (6 symbols) — **CORRECTED
      2026-08-27, was wrong**: previously filed as "confirmed never
      open-sourced by Apple." That was true only in the narrow sense
      that no standalone `ASL` repo exists — the real ASL client library
      (`asl_open`/`asl_log`/`syslog()`, 14,555 real lines, Apple-
      licensed) is bundled inside `apple-oss-distributions/syslog`'s
      `libsystem_asl.tproj/src/` and is real and pinnable (pinned
      2026-08-27 as `syslog-385` for the `syslogd` port, see
      `docs/daemon-roadmap.md`'s Phase 6.7 syslogd section) — not yet
      wired into this project's CF/launchd trial links, but no longer a
      genuine scope wall, just unattempted.
    - **Deprioritized, real, peripheral**: BSM audit-record
      construction (`au_*`/`audit_session_*`, ~10 symbols) — needs a
      private header this project's pinned BSM repo doesn't ship.
    - **`libSystem.B.dylib` export-parity gap** (not launchd-specific,
      tracked under item d7) — mostly closed project-wide by the real
      `-reexport_library` fix (see d7); not yet re-verified against
      launchd's own trial link.
    - **Real, genuinely unimplemented feature in this exact PureDarwin
      branch** (not a build gap): `job_mig_look_up2_forward`/
      `job_mig_parent_forward` — real multi-instance-launchd job
      forwarding this project has no concept of (root-bootstrap only);
      needs the right MIG reply-client function identified first.
    - **Small, real, individually-scoped stragglers**: `asctime`/
      `asctime_r`/`strtoul_l` remain deferred (need the same deep
      `xlocale.c` locale-loading chain, tried+reverted twice);
      `xpc_copy` confirmed a real gap (no plain deep-copy function
      anywhere in either real repo, same class as `ld2xpc`);
      `initgroups`/`setiopolicy_np` confirmed to have no dedicated
      wrapper source anywhere in this project's pinned repos.

    **Bottom line**: launchd is not a zero-gap link (needs the real ICU
    port above, a separate, large undertaking — ASL is real and pinned
    now, just not yet wired in) — but the critical-path work is
    exhausted for this pass.

d7. **CoreFoundation + real ICU + real libxml2 ports DONE 2026-08-26/27
    — CF dynamic trial link down to 42 undefined-symbol groups, from a
    starting count of 153.** Real, unmodified Apple source throughout:
    `third_party/swift-corelibs-foundation` (Apple's own actively-
    maintained CF fork — NOT the 2014-dated `apple-oss-distributions/CF`,
    which isn't in the `macos-112` manifest at all), real ICU 66.1
    (`third_party/ICU`, real two-stage host+arm64-apple-ios14.4 cross
    build), real libxml2-34.9. 94 real CF source files build clean into
    `libCoreFoundation.a` (`build_corefoundation.sh`) — String/
    Collections/NumberDate/Parsing/RunLoop/Stream/PlugIn(Bundle)/
    DateFormatter/URL/AttributedString/RegularExpression, plus
    ICU-backed Locale/Calendar/CharacterSet. `libSystem.B.dylib`'s
    missing `-Wl,-reexport_library` (it was plain-linking its 7
    sub-dylibs, exporting almost nothing through the umbrella — real
    macOS's own `libSystem.dylib` is exactly this kind of thin reexport
    umbrella) turned out to be the single biggest gap-closing fix in
    this whole effort once found, closing 27 gaps in one rebuild. Full
    session-by-session build/port narrative (the ICU two-stage
    cross-build story, the CFBundle 30-for-3 trade that got reverted
    then un-reverted once the dependency turned out already-buildable,
    the CFError/CFDate/CFURL "just try compiling it" sweep, every
    individual syscall wrapper closed along the way, etc) archived
    verbatim to `docs/historic/backlog.md` — this entry only tracks
    current state.

    **Real, confirmed compile-time exclusions (4, not build gaps — each
    individually investigated and confirmed structural)**:
    - `CFUserNotification.c`: real GUI alert-dialog API, hard
      `unavailable: not available on iOS` SDK errors. No GUI on this
      target regardless.
    - `CFPreferences.c`/`CFXMLPreferencesDomain.c`: a real, multi-part
      gap — `gethostuuid` marked unavailable-on-iOS behind a
      `TARGET_OS_MAC` guard that doesn't actually exclude iOS (a real
      bug in this exact checkout), plus a missing CFUUID declaration and
      an undeclared `prefDir`. Real preference-file storage
      (`CFPreferencesCopyAppValue` et al) stays unimplemented.
    - `CFURLSessionInterface.c`: real libcurl dependency. Apple did
      open-source curl (`curl-121.40.2`, pinned per the manifest), but
      its real build needs closed-source Secure Transport for TLS (out
      of scope per the standing corecrypto/AES-only crypto decision,
      item 0a) — `--without-ssl` would compile, but real HTTP
      networking needs BSD socket syscalls this project's userland
      doesn't wrap yet either. **Real prerequisite order for whoever
      picks this up**: (1) BSD socket syscall wrappers in userland libc
      (shared need with `CFSocket.c`), (2) a real TLS backend decision,
      (3) a `build_libcurl.sh` off `build_libxml2.sh`'s own template.
    - `CFWindowsUtilities.c`: Windows-only, structurally inapplicable.

    **Remaining real gaps in the dynamic trial link, 42 groups** (the
    `-reexport_library` fix closed the entire previous 27-symbol
    export-parity bucket — what's left is qualitatively different,
    genuine "not built anywhere yet" or "never open-sourced" gaps):
    - `_scalbn`, `_getpwuid_r`, `_getsectbynamefromheader_64`,
      `_getsegbyname`: real functions, not yet confirmed built anywhere
      in this project's curated stack — not yet investigated.
    - `___chk_assert_no_overlap`/`___chk_fail_overflow`/`___chk_overlap`:
      real `_FORTIFY_SOURCE`/`_chk`-family hardening routines, no shim
      source located yet (same gap `build_libxpc_syscalls.sh` already
      flagged).
    - `__dispatch_queue_attr_concurrent`/`__mh_dylib_header`/
      `__os_log_debug_impl`/`__os_log_default`/`__os_log_error_impl`/
      `_os_log_create`/`_os_log_type_enabled`: a real, not-yet-root-
      caused os_log/dispatch gap — confirmed genuinely absent from both
      `libdispatch.dylib` and `libSystem.B.dylib` via `nm`.
    - `_asl_*` (6 symbols): **CORRECTED 2026-08-27** — not actually
      out of scope, see the parallel correction under item d6 above.
      The real ASL client library is bundled in
      `apple-oss-distributions/syslog`'s `libsystem_asl.tproj/src/`
      (pinned as `syslog-385`, see `docs/daemon-roadmap.md`'s Phase 6.7
      syslogd section) — genuine, just not yet wired into CF's own
      trial link.
    - `___CFPreferencesAllocator`/`__CFPreferencesDomain*`/
      `_CFPreferencesCopyValue`/`_kCFPreferences*`/`_kCFLocale*`/
      `_kCFDateFormatter*`/`_kCFGregorianCalendar`: real
      `CFPreferences.c`-family gaps, tracked by the exclusion above.
    - `sysdir_start_search_path_enumeration`/
      `sysdir_get_next_search_path_enumeration`: confirmed never
      open-sourced (checked real `Libc`'s full git history plus a
      GitHub code search across the whole `apple-oss-distributions`
      org — zero definitions anywhere, only real callers).

0a. **No AES implementation exists anywhere in this kernel — HARD
    PREREQUISITE for Phase 7b (`sshd`).** Filed explicitly 2026-08-21 so it
    is a known gate rather than the fourth surprise NULL dereference.

    `g_crypto_funcs` (`libkern/crypto/register_crypto.c`) is a table of
    function pointers that real macOS's closed-source corecrypto kext fills
    in via `register_crypto_functions()`. This kernel loads no kexts at all,
    so **nothing ever calls it and the table stays NULL forever**. Every
    consumer therefore faults as `NULL + <field offset>`. This has now bitten
    four separate times, each rediscovered the hard way from a fault address:

    | Caller | Offset | Field | Resolution so far |
    |---|---|---|---|
    | `SHA256_Init` via `devfs_mount` | `0x30` | `ccsha256_di` | now real corecrypto, see item 0b |
    | MD5 | `0x20` | `ccmd5_di` | from-scratch, bypasses the table |
    | `read_random` | n/a | `cckprng` | falls back to `read_erandom()` |
    | `tcp_init`→`tcp_tfo_init` | `0x78` | `ccaes_cbc_encrypt` | guarded; TCP Fast Open disabled |

    Every fix so far *routed around* the table rather than filling it, which
    is exactly why it keeps recurring. To decode the next one: the offset is
    the field index times 8 into `struct crypto_functions`
    (`libkern/libkern/crypto/register_crypto.h:113`).

    **Current state is safe but featureless.** `aes_encrypt_key()` now
    returns `aes_error` instead of dereferencing NULL, so further hits are a
    log line rather than a panic. Nothing that needs AES works.

    **What Phase 7b actually requires**, and why it is not just "write AES":
    - The AES block-cipher core (this is the part to take from an existing
      implementation, see below).
    - A corecrypto-shaped adapter: a `struct ccmode_cbc`
      (`EXTERNAL_HEADERS/corecrypto/ccmode_impl.h:63` — only 5 fields:
      `size`, `block_size`, `init`, `cbc`, `custom`) plus whatever other
      modes are needed, and a boot-time `register_crypto_functions()` call.
      No library provides this glue; it is project-specific either way.
    - The table can be filled **incrementally** — register AES, leave the
      rest NULL, and the guard above keeps unfilled slots non-fatal. The
      existing from-scratch MD5 should be folded in at that point so
      there is one mechanism rather than two (real SHA-256 already moved
      off this table entirely, see item 0b).

    **Do not hand-roll the AES core.** Confirmed 2026-08-21: `corecrypto` is
    **not** in apple-oss-distributions (searched, zero repos), so this
    project's "Apple's real source first" rule cannot be satisfied — the
    documented fallback is FreeBSD, whose `sys/crypto/rijndael/` is
    self-contained, already kernel-resident, and permissively licensed.
    xnu-7195 itself ships **no** AES core (only the corecrypto dispatch
    wrappers `corecrypto_aes.c`/`corecrypto_aesxts.c`, and
    `bsd/netinet6/esp_rijndael.c` which is IPsec glue over the same
    dispatch), so there is nothing in-tree to reuse.

    Risks to handle when doing it: verify against FIPS-197 known-answer
    vectors (a subtly wrong AES silently produces valid-looking wrong
    ciphertext — worse than none); table-based AES is cache-timing
    vulnerable, acceptable here but worth recording; and check whether the
    BCM2837's Cortex-A53 implements the optional ARMv8 Crypto Extensions
    (AESE/AESMC) before assuming a hardware path exists — believed absent on
    Pi 3, NOT verified.

    **Update 2026-08-27: `PureDarwin/corecrypto` investigated as a source
    for this, real findings, not yet acted on.** `src/kext/
    register_crypto.h` in that repo is byte-for-byte the same
    `struct crypto_functions` layout as this project's real
    `libkern/libkern/crypto/register_crypto.h` (verified by hand against
    every offset in the crash table above: `ccmd5_di`=0x20,
    `ccsha256_di`=0x30, `ccaes_cbc_encrypt`=0x78, all match) — real
    upstream precedent for the `register_crypto_functions()` adapter this
    item needs either way. Its AES core (`src/algorithms/aes128.c`/
    `aes_cbc.c`/`aes_ecb.c`) is a real, small, KAT-verified implementation
    adapted from `kokke/tiny-AES128-C` (public domain) — but AES-128
    ECB/CBC only, no CTR/AES-256/GCM/XTS, and real `sshd` (this item's
    actual reason for existing) defaults to `aes128-ctr`/`aes256-ctr`, so
    this alone would close the gate only partway. License provenance is
    also genuinely messy: `LICENSE.md` claims Apache-2.0 (mbedTLS-based),
    `README.md` says Botan + `rafirafi/pdcrypto`, the AES core's own
    header credits `tiny-AES128-C` — three different stated lineages, not
    a clean single license to adopt wholesale.

    **Decided direction (2026-08-27)**: use PureDarwin's `corecrypto` for
    now — its `register_crypto.h`-compatible interface/adapter shape in
    particular — but treat it as an interim source, not a final one:
    file a follow-up to revisit any part of it whose actual crypto logic
    (as opposed to the interface/struct plumbing) has unclear or mixed
    provenance, and prefer swapping just that logic for FreeBSD's
    `sys/crypto/rijndael/` (already the documented, cleanly-licensed
    fallback for the AES core specifically) where that's possible without
    changing the corecrypto-shaped interface itself. In short: PureDarwin
    for the adapter/struct shape (real, verified-matching ABI), FreeBSD
    for the actual cipher body wherever the two can be kept separate.
    Whoever picks this up should re-check this tradeoff before writing
    code — it was not implemented this session, only scoped.

0b. **[DONE 2026-08-27] `ccsha256_di` wired to real corecrypto, replacing
    the from-scratch `corecrypto_sha2.c` for code-signing/exec hashing.**
    `SHA256_Init`/`_Update`/`_Final` (`libkern/crypto/corecrypto_sha2.c`)
    now call real `ccdigest_init`/`_update`/`_final` + `ccsha256_di()`
    directly (the real digest-core source was already sitting unused at
    `third_party/xnu-7195/osfmk/corecrypto/` and already compiled into
    the kernel binary for the PRNG's own use — only the SHA-256 call
    sites hadn't been pointed at it). SHA-384/512 remain on the
    from-scratch path (no real corecrypto body for those exists in this
    project's `xnu-7195` checkout). QEMU-verified: real login, real
    shell, real `/bin/ls`/`md5` exec, no panic — commit `ceb6d419c` in
    the `xnu-7195` checkout. Full investigation (including a corrected
    finding that `register_crypto_functions()`/`g_crypto_funcs` was
    never actually the right mechanism for this call site) archived to
    `docs/historic/backlog.md`.

0. **DWC2 transfer completion is polled, not interrupt-driven** (added
    2026-08-21 with `iokit/BCM2837DWC2/`). `docs/historic/usb-support-plan.md` task 2
    called for mirroring `BCM2837SDHCI`'s interrupt-driven design; this
    first cut leaves global interrupts masked (`GAHBCFG.GLBLINTRMSK` clear)
    and polls `HCINT.CHHLTD`, exactly as its u-boot reference does. That was
    a deliberate sequencing call — doing first-ever register bring-up and
    interrupt plumbing in one change means debugging two unproven things at
    once, and `BCM2837SDHCI`'s interrupt work needed three passes and a
    device-tree root cause before a real interrupt was ever observed. The
    conversion is contained: `waitForBits()`/`waitForChannelHalted()` are the
    only places that spin, and `BCM2837SDHCI`'s hybrid
    `IOSimpleLock` + `IOLockSleepDeadline` pattern (real interrupt wins
    immediately, poll tick as a safety net that also proves which path
    fired) is the model to copy. Worth doing before any sustained Ethernet
    RX load, since polling a bulk-IN endpoint burns a core.

1. **Phase 3.5a still not done**: `tools/init_binary/init.c`'s `ls`/`cat`/
    `cd`/`pwd`/`mkdir` operate on a hardcoded in-memory fake tree, not real
    `open`/`read`/`getdirentries` syscalls against the actually-mounted
    root. Small, self-contained, no dependency on Phase 4.

2. **Phase 4's `Libc` build is unstarted** (`Csu`/crt0 and real syscall
    stubs are verified working — see `tools/userland_staging/` — but `Libc`
    itself, the actual C library, is not). Confirmed to be an Xcode-project
    build (`Libc.xcodeproj`, no plain Makefile) against a broken local
    `xcodebuild`; the real path is compiling a curated `.c` subset directly
    with `clang`, starting with string/mem functions (no syscall/TSD
    dependency) before `stdio`/`malloc`/locale. Expect the same
    header-skew-per-subsystem fight `IOStorageFamily`/`hfs` needed.

3. **`build_kexts.sh` only standalone-compiles some driver sources**
    (PARTIALLY ADDRESSED 2026-08-21: it now compiles every `.cpp` in each
    listed directory rather than only `<dir>/<dir>.cpp`, and `BCM2837DWC2`
    was added, so the four USB sources are covered. The `KEXTS` array is
    still a hand-maintained list, so the gap below stands for the rest.)
    Originally: only 3 of 8 driver sources
    (`BCM2837InterruptController`, `BCM2837SDHCI`, `SMSC9514Ethernet`) —
    `BCM2837CPU`, `BCM2837LocalIntc`, `BCM2837ArmIO`, `BCM2837Personalities`,
    `BCM2837PlatformExpert` are never syntax-checked outside a full kernel
    build, though all five are listed in `third_party/xnu-7195/iokit/conf/files`.
    Cheap to extend the `KEXTS` array (note `BCM2837Personalities.cpp`
    actually lives in `iokit/BCM2837ArmIO/`, not a same-named directory).

4. **`ls /` crash mentioned in old project memory needs re-verification.**
    An earlier session flagged `ls /` as crashing (real SIGSEGV-looking
    panic) after printing one entry. Recent boots in this session ran `ls`
    without incident, but that was likely still against the fake
    in-memory tree (item 1 above) rather than a real directory read — status
    unclear, re-check once 3.5a lands real syscalls.

5. **No `grep`/`sed`/`sort`/`nl`/`xargs`/`ed`-style command has been built
    yet.** Not blocked on a missing subsystem — the real POSIX regex engine
    (`libc_build/libc_regex.a`, Apple's own TRE port, `[RESOLVED
    2026-08-20]`, full writeup archived to `docs/historic/backlog.md`) has
    been linkable since 2026-08-20 and is already proven in real use
    (`bash`'s `[[ str =~ pattern ]]`, QEMU-verified). Each of these is now
    a normal new-command port against a real, linkable engine.

6. **`touch -h` (utimes-on-symlinks) still hits an honest `ENOSYS` stub**
    despite the real syscall it needs (`setattrlist(2)`, `[RESOLVED
    2026-08-20]`, full writeup archived to `docs/historic/backlog.md`)
    being wired and QEMU-verified since 2026-08-20 for `cp -p`/`chmod`'s
    ACL machinery. Small, low-priority follow-up — hasn't been revisited.

7. **No real Mach IPC (`mach_msg()` trap + MIG RPC) in userland**, real
    open-source kernel-side implementation already present in this
    project's `third_party/xnu-7195` (`osfmk/kern/ipc_*`/`clock.c`) but
    never exercised from a userland binary. Blocks real `ps` CPU%/RSS/VSZ
    columns (`ps_task_info_stub.c`) and real blocking sleep anywhere
    (`simple_cmds_nanosleep_compat.c` currently spin-waits, burning a
    full core instead of blocking — used by `tail -f`'s poll interval).
    NOT closed-source like Phase 7.5's pthread/corecrypto gaps — "not
    yet built," not "cannot be built." See `docs/roadmap.md`'s Phase 4.6
    for a smaller-scoped first real fix (hand-written trap wrappers for
    just `host_get_clock_service`/`clock_get_time`/`semaphore_*`, without
    needing full MIG-stub generation).

8. **`SMSC9514Ethernet`'s MAC address is a locally-administered
    placeholder (`02:00:00:00:00:01`) on any board where the LAN9514 has
    no EEPROM populated** — filed 2026-08-25 alongside that day's RX-poll
    fix. `start()` now genuinely tries a real EEPROM read first
    (`readEEPROM()`/`eepromWait()`, `E2P_CMD`/`E2P_DATA`) and only falls
    back to the placeholder if that read times out or looks
    uninitialized, but most Raspberry Pi boards ship this chip with no
    EEPROM at all, so the placeholder is likely still what real hardware
    reports — every such board presents the identical address, which
    will misbehave the moment two of them are on the same network
    segment (DHCP/ARP collisions). The Raspberry Pi Foundation's own
    convention doesn't use chip EEPROM either: it derives a real
    per-board MAC from the VideoCore's unique board serial (mailbox
    property tag "get board serial", low 3 bytes combined with the
    Foundation's OUI, e.g. `b8:27:eb:xx:xx:xx`) at firmware/u-boot time.
    This kernel has no VC-mailbox driver yet reachable from
    `SMSC9514Ethernet::start()` — would need to either read a value the
    bootloader already stashed somewhere (devicetree property?) or add a
    real mailbox "get board serial" call. Not started; scoped here for a
    follow-up session rather than attempted as part of the RX-poll fix.

9. **`hexdump` prints only the final byte-count address line, never the
    actual hex+ASCII data rows**, against a real non-empty file, for every
    format flag tried (plain/`-c`/`-C`) -- found and narrowed (not fully
    root-caused) during the 2026-08-20 command-sweep session's Block 3.
    Specific to `hexdump`'s own `freopen(argv, "r", stdin)` +
    `getc()`/`ungetc()` read idiom, not a general `freopen` bug (Block 2's
    `fold`/`expand`/`unexpand`, linking the identical real `freopen.c`,
    correctly read real file-argument content the same session). Most
    likely a latent bug in this project's ported `getc()`/`ungetc()`
    interaction with a freshly-`freopen`'d stream. Ships anyway (real,
    unmodified source, links, runs, doesn't crash) -- same tolerance as
    P0 item 7's `yes` kernel-panic bug. See `docs/historic/roadmap.md`'s
    "Twenty-fifth pass" Block 3 entry for the full evidence trail.

10. **`pathchk ""` (empty pathname) exits 0 instead of POSIX's specified
    "shall report an error"** -- real, unmodified upstream FreeBSD source,
    found during the same session's Block 3. Low practical impact, not
    investigated further.

11. **`rs -T` (transpose) dropped the first row of a small piped 2x3
    matrix in one observed test** (`printf '1 2 3\n4 5 6\n' | rs -T`
    printed only `4\n5\n6\n`) -- found during the same session's Block 2.
    Not confirmed as a port bug vs. a genuine regression: real upstream
    `rs`'s array-dimension auto-guessing heuristics, active whenever
    neither `-g` nor `-c` is given, are documented as sensitive to
    small/irregular input shapes. Not investigated further given `rs`'s
    low practical value.

12. **`ls -e`'s real ACL display (`acl_get_link_np()`) is an honest
    always-NULL stub** (`tools/userland_staging/ls_acl_compat.c`), not a
    real implementation -- found/scoped during the 2026-08-20 Tier-A
    command sweep (`ls`/`find`/`date`/`gzip`/`compress`). Real Apple
    `acl_get_link_np()` (`Libc/posix1e/acl_file.c`) goes through a real
    `filesec_t` abstraction (`Libc/gen/filesec.c`, implementation
    vendored) whose *declaring header* (`<sys/filesec.h>`) Apple removed
    from the public SDK entirely -- confirmed absent from both this
    project's vendored Libc checkout and Xcode 12's iPhoneOS/MacOSX SDKs
    directly (grepped both real `sys/acl.h` headers: zero `filesec_t`/
    `FILESEC_*` mentions in either), not merely unfound. The real kernel
    side is NOT missing -- `stat64_extended`/`lstat64_extended`/
    `fstat64_extended` (bsd/kern/syscalls.master #341-343, real,
    `NO_SYSCALL_STUB`, real `vn_stat(..., &fsec, ...)` kernel
    implementation already in `bsd/vfs/vfs_syscalls.c`) are proven live
    and callable via this same tier's raw-syscall pattern (see
    `cp_mv_syscalls.c`'s `lchown`/`sigaction`/`setattrlist` precedent) --
    only the userland-side struct layout is the gap. `find -acl`'s real
    `__APPLE__` path also depends on this same stub (same honest
    always-"no ACL" answer). Real follow-up: reconstruct or re-source a
    real `filesec_t` layout (possibly from an older/different Apple SDK
    vintage, or by reverse-engineering `Libc/gen/filesec.c`'s own real
    property-ID usage) and wire the three real syscalls above through
    it, then re-point `acl_get_link_np()`/`acl_get_file()` at the real
    path. Low urgency -- this target's HFS+/msdosfs mounts have no
    kernel-side ACL *storage* wired either, so a real userland
    implementation would correctly report "no ACL" for every file today
    regardless.

13. **`date`'s network time-sync (`netsettime()`, `-n`-less clock-set)
    and login-record write (`pututxline()`, also clock-set-only) are
    honest always-fail stubs** (`tools/userland_staging/date_compat.c`),
    found/scoped during the same 2026-08-20 Tier-A sweep. Neither
    affects `date`'s ordinary display/format path (QEMU-verified
    working) at all. `netsettime()`'s real implementation
    (`shell_cmds/date/netdate.c`) needs a real resolver+socket stack
    (Phase 6, unstarted) for the legacy `timed` UDP protocol, dead on
    every real modern system regardless. `pututxline()`'s real
    implementation (`Libc/gen/utmpx-darwin.c`, 1193 lines) needs a real
    Mach-RPC-to-ASL/utmpx-daemon path this project has no launchd/
    bootstrap-service registry to back (see CLAUDE.md's "no launchd"
    line) -- a whole additional unbuilt subsystem, not a narrow gap.
    `settimeofday(2)` itself IS real (wired as a raw `NO_SYSCALL_STUB`
    syscall in the same file), so `-d`/`-t` and an actual privileged
    clock-set still work for real, just without Apple's own wrapper's
    cosmetic `notify(3)`/ASL side effects. Not planned to be revisited
    before Phase 6/Phase 5.5's `syslogd` land for real.

14. **`tools/init_binary/inject_into_sd_image.sh`'s `for cmd in ...` list
    needs each new command name appended by hand** (documented in this
    repo's own working conventions but easy to forget) -- caught during
    this session's `find`/`date`/`gzip`/`compress` QEMU verification when
    each was initially silently absent from the injected image despite
    building cleanly (no error, the command just never got copied to
    `/bin`, "command not found" was the only symptom). Not a design
    flaw worth fixing structurally this pass (the list is deliberately
    explicit, matching the project's "no fabricated automation" bias),
    just a real, repeated friction point worth flagging for whoever adds
    the next command: verify the injected image actually contains the
    new binary (e.g. `strings`-check the mount, or just `ls /bin` at a
    real login prompt) before trusting a QEMU test's "command not found"
    absence to mean anything about the command itself.

15. **This project's kext personalities never wire up
    `kIOFDiskPartitionSchemeContentTable`** (`third_party/IOStorageFamily/
    IOFDiskPartitionScheme.cpp`'s `instantiateMediaObject()` looks this
    dictionary up to translate a raw MBR partition-type byte, e.g. real
    `0xAF`, into a content-hint string like `"Apple_HFS"` -- with no
    table wired, every partition's `Content` property is just the raw
    `"0xNN"` hex string instead). Only actually matters when XNU boots
    with no `rd=` boot-arg (real hardware's default, confirmed via a
    real device boot 2026-08-21 -- `iokit/bsddev/IOKitBSDInit.cpp` falls
    through to generic `IOProviderClass=IOMedia`/`Content=Apple_HFS`
    root matching in that case, which can never succeed without this
    table); every QEMU boot always passes `rd=disk0s2` (`runvm.sh`) and
    never hits this path, which is why it went unnoticed all session.
    Worked around for now by baking `setenv bootargs rd=disk0s2` into
    real hardware's `u-boot` autoboot (`third_party/u-boot`'s
    `configs/rpi3_xnu_defconfig`, see `sdcard/README.md`) rather than
    fixing the underlying table -- the real, more general fix (wiring an
    actual content-hint table into this project's hand-built
    `BCM2837Personalities.cpp`, matching how a real Apple platform
    expert would populate it) is still open for whoever wants boot to
    work without an explicit `rd=` in the future.

16. **`less`/`more`: no real termcap/terminfo database exists on this
    target**, so `less` always runs in real hardcopy mode, never true
    cursor-addressed full-screen paging — a real, honest scope limit, not
    a bug. The launch → degraded-mode-detection → quit lifecycle itself
    is real and QEMU-verified end to end (`[RESOLVED 2026-08-21]`, full
    verification writeup archived to `docs/historic/backlog.md`), and a
    genuine page-by-page scroll-through of multi-screen content was not
    separately exercised (only single RETURN + quit). Porting a real
    terminal-capability database is a distinct, larger, not-yet-scoped
    follow-up if full-screen paging is ever needed.

17. **Once `syslogd`/ASL land, switch real callers off the stderr-only
    `syslog(3)` fallback onto the real thing — but only via the legacy
    Unix-socket path first, not `libsystem_asl`'s real `asl_open()`
    client.** Filed 2026-08-27 alongside the `syslog-385` pin
    (`docs/daemon-roadmap.md`'s Phase 6.7) so this sequencing isn't lost:
    `tools/userland_staging/ifconfig_syslog_compat.c`'s `syslog()`/
    `openlog()`/`closelog()` stand-ins (also used by `cron`, see
    `build_cron.sh`'s own header comment) are correct and should stay
    exactly as-is until a real legacy-only `syslogd` (`bsd_in.c`-based,
    Unix-domain-socket `/var/run/syslog`, no Mach dependency) is actually
    running. Do **not** wire callers onto `libsystem_asl`'s real
    `asl_open()`/`syslog()` (`third_party/syslog/libsystem_asl.tproj/`)
    until the bootstrap-namespace registrar shim scoped for `notifyd`
    also exists — that client unconditionally calls
    `bootstrap_look_up2(bootstrap_port, ASL_SERVICE_NAME, ...)`
    (`asl.c:271`, confirmed no non-Mach fallback path exists anywhere in
    the client library), so wiring it in early would produce code that
    compiles clean but silently fails to log anything, exactly the kind
    of regression this project's own conventions exist to avoid.

---

## P3 — hygiene / cleanup

1. **Two headers overclaim a nonexistent IOKit matching-engine bug.**
    `iokit/BCM2837ArmIO/BCM2837ArmIO.h` and
    `iokit/BCM2837InterruptController/BCM2837InterruptController.h` both
    assert (one claims "empirically confirmed via instance-pointer-tagged
    logging") that `IOService::matchInternal()`'s `IONameMatch` handling
    over-matches personalities against sibling DT nodes. Tracing the real
    path (`IOPlatformDevice::compareName` → `IODTCompareNubName` →
    `CompareKey()`) shows an exact string match with no such defect. The
    actual bug in the commit that added these `probe()` guards
    (`c5fa3c6`) was a blind cast through the wrong vtable in
    `BCM2837ArmIO::start()` (since fixed, same commit) — a real, different
    bug that produced the same symptom. The `probe()` guards themselves are
    harmless belt-and-braces, worth keeping; just correct the stated
    reason in both header comments.

2. **Stray nested git repo at `iokit/.git/`** (own `config`/`HEAD`, no
    remote configured) inside a directory the parent repo tracks normally.
    Makes `git status` run from inside `iokit/` misleading/wrong. Probably
    a leftover from early scaffolding before the top-level repo existed —
    verify there's no uncommitted work in it, then remove it.

3. **`iokit/Kernel/IOService.cpp` still fires unconditional `TEMP
    DIAGNOSTIC` prints on every `registerService`/`startMatching`** (136
    lines in a single partial boot capture). At real mini-UART baud rates
    (115200) each line costs milliseconds of synchronous busy-wait —
    combined with P0 item 3's slow clock, this would make a working
    real-hardware boot look like a hang. Prune once no longer needed for
    active debugging.

    `bsd/msdosfs/msdosfs_vnops.c`'s `TEMP DIAGNOSTIC` read/blockmap prints
    and `third_party/qemu-zhuowei`'s `hw/sd/sdhci.c` per-command/per-block
    `DIAG` prints (the dominant source of console noise — 471 lines in the
    same capture, entirely a QEMU emulator-side trace, never runs on real
    hardware) were both gated behind a default-off flag **[FIXED
    2026-08-19]** (`xnu-7195` commit `89d131a6b`, `qemu-zhuowei` commit
    `05daaa2`, both local/unpushed) rather than pruned outright, so they're
    a one-line flip away if that SD path needs debugging again.

4. **`BCM2837CPU::quiesceCPU()`/`haltCPU()` are silent no-ops** — correctly
    hedged as fine for now (single core, no sleep/shutdown ever attempted),
    but will need real implementations before any power-management work.
