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

2. **`SMSC9514Ethernet` has several real logic bugs.** Status as of
    2026-08-22 (was written when the class had no personality and was
    entirely unreachable — that's no longer true, see below):
    - ~~The PHY-reset loop doesn't check its own timeout flag before
      continuing as if it succeeded.~~ FIXED 2026-08-22 (`resetChip()`
      now has a `phyCleared` flag matching the lite-reset loop's own
      pattern, logs+fails instead of continuing on timeout).
    - ~~`mdioRead`/`mdioWrite` are never called from anywhere — the PHY
      is never initialized/auto-negotiated, yet `enable()`
      unconditionally sets `fLinkUp = true`.~~ FIXED 2026-08-22:
      `resetChip()` now does a real BMCR auto-negotiation restart, and
      `updateLinkStatus()` (polled via a real `IOTimerEventSource`,
      started/stopped in `enable()`/`disable()`) reads BMSR/ANAR/ANLPAR
      and reports real link state + negotiated speed/duplex through
      `setLinkStatus()`.
    - ~~`attachInterface()` is never called from `start()`, so no
      `IONetworkInterface` can ever be created — matches the "no
      personality, unreachable" state above.~~ FIXED 2026-08-21
      (`attachInterface()` in `start()`) + 2026-08-22 (the class is now
      manually constructed/attached/started by `BCM2837DWC2::start()` on
      real hardware, and its `IONetworkInterface` is now actually wired
      to BSD/`ifconfig` via a hand-instantiated `IONetworkStack` — see
      `iokit/BCM2837DWC2/BCM2837DWC2.cpp`).
    - `smsc95xx_reset()`-equivalent code does blind register writes to
      `HW_CFG`/`PM_CTRL`/`MAC_CR` where Linux does read-modify-write,
      clobbering unrelated bits (`BIR`/`RXDOFF`/`MEF`, full-duplex/
      auto-speed defaults). Still open.
    - **[RESOLVED 2026-08-25]** ~~`getPacketFilters()` advertises
      multicast filtering while `enablePacketFilter()` is a no-op — will
      break IPv6 ND.~~ Root cause was one layer up from where this was
      filed: `IOEthernetController`'s own base-class
      `enablePacketFilter()`/`disablePacketFilter()`
      (`IOEthernetController.cpp`) already correctly dispatch
      `kIOPacketFilterMulticast`/`kIOPacketFilterPromiscuous` to the
      virtual hooks `setMulticastMode()`/`setPromiscuousMode()` — this
      driver's own no-op override of `enablePacketFilter()`/
      `disablePacketFilter()` was *shadowing* that real logic entirely,
      not failing to extend it. Fix removed the shadowing overrides and
      implemented `setMulticastMode()`/`setPromiscuousMode()` (real
      MAC_CR MCPAS/PRMS read-modify-write) and `setMulticastList()`
      (real success — MCPAS already passes all multicast whenever the
      filter is active, a correct superset of any hash-table filter,
      which is unimplemented). Also fixed `getPacketFilters()` ignoring
      `group`, which meant it falsely advertised WOL filter support when
      `publishProperties()` queries `gIOEthernetWakeOnLANFilterGroup`.
      Build-verified (kext + full kernel); unverified on real hardware.
    - **[RESOLVED 2026-08-25]** ~~`fInterface` is read without a
      retain/lock from a USB-completion context that can race
      `disable()` setting it to NULL — UAF shape. Still open; also
      still moot in practice since nothing drives the bulk IN endpoint
      into `inputPacketAvailable()` yet.~~ RX is now wired
      (`fRxTimer`/`pollReceive()`, see below), so this is no longer
      moot — but it turned out not to be a real race either: RX runs
      from an `IOTimerEventSource` on the *same gated `IOWorkLoop`* as
      `outputPacket()`'s output queue and `disable()`'s framework calls,
      not an independent USB-completion thread (there is no async
      completion path anywhere in `BCM2837DWC2` — see its own "one
      transfer at a time, no queueing" limitation), so all of these are
      already serialized against each other. The original concern was
      written against a hypothetical async-completion design this
      driver doesn't have.
    - `frameLen & 0x7FF` truncates silently with no length guard. Still
      open.
    - **[RESOLVED 2026-08-22]** ~~New as of 2026-08-22, not yet verified~~
      — real hardware confirmed: `en0` attaches, shows link up in
      `ifconfig`, and responds to ICMP echo (ping) over a real Ethernet
      link. BSD/`ifconfig` registration and link-state/auto-negotiation
      logic are now verified end-to-end on silicon, not just build+QEMU-
      regression-tested. No DHCP/address assignment yet — that's Phase 6c.
    - **2026-08-25, real-hardware report, fixes unverified on hardware
      yet**: user reported real Pi 3 networking not working — board's
      Ethernet activity LEDs never lit, the MAC address looked like a
      placeholder, and inbound traffic never worked at all (self-ping
      only; nothing external, despite outbound frames visibly reaching
      the wire). Root-caused from source: `inputPacketAvailable()` was
      real but **nothing ever called it** — no code drove the bulk IN
      endpoint (exactly the "moot" gap noted above), so TX worked and
      RX was completely dead, matching the symptom. Fixed same day: RX
      poll timer wired up (`fRxTimer`/`pollReceive()`) and MAC address
      now attempted via a real EEPROM read before falling back to the
      locally-administered placeholder (see the new P2 item below on
      why the placeholder may still be what's seen on this board).
      Board LEDs explicitly out of scope per user — nothing in this
      driver ever touched LED/GPIO config, so if they're software-
      controlled at all it's a separate, not-yet-investigated gap.
      Full kernel build verified; **not yet booted on real hardware**.

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

d6. **CLOSED 2026-08-26 — every real file in `launchd/` now compiles
    clean.** `tools/userland_staging/build_launchd_recon.sh` (real
    `-fsyntax-only` reconnaissance harness, not yet a real build script
    that produces `.o`/link output) is the reproducer — see its own
    header comment for the complete real-fix writeup. All 8 real daemon
    source files (`pd_launchd_boot.c`, `kill2.c`, `pd_sandbox_check.c`,
    `pd_console_login.c`, `pd_launchd_plist.c`, `log.c`, `ipc.c`,
    `launchd.c`) plus `core.c` (12,162 real lines, launchd's actual
    job-management engine) compile with zero errors. Real fixes:
    `spawn_private.h`/`libinfo.h` include-path gaps; the `union
    sockaddr_in_4_6`/`struct sockaddr_dl` incomplete-type errors (same
    "SDK's own same-named older header wins" bug class as
    `mach/mach_vm.h`/`mach/mach_time.h`); `third_party/Libinfo`'s real
    `lookup.subproj/aliasdb.h` (a separately-pinned repo, reused for a
    header PureDarwin's own bundled libinfo/ is missing); a real,
    embedded-only `LAUNCH_JOBKEY_DEFAULTS` macro missing from the
    vendored `launch.h`; a real `sandbox_spawnattrs` compat extension
    matching this project's own already-established "always allow, no
    Sandbox.kext" precedent; and — the interesting one — a real,
    from-scratch `ld2xpc()`/`xpc2ld()` (`tools/userland_staging/
    ld2xpc_compat.c`), CONFIRMED via a fresh clone of the real,
    historically-open-sourced `apple-oss-distributions/launchd` tag
    `launchd-842.92.1` (the last launchd Apple ever published source
    for) — not just this project's own PureDarwin checkout — that these
    two `launch_data_t`<->`xpc_object_t` converters are called but never
    defined in EITHER real repository; they must come from a private,
    closed-source part of Apple's real libxpc neither tree ships. Real,
    from-scratch recursive implementation (last resort, only after
    confirming no real Apple source exists anywhere), mapping between
    the two real, already-working type systems.

    **UPDATE, same day**: `build_launchd_core.sh` (new) promoted the
    recon harness into a real build script — compiles all 9 real daemon
    files (`runtime.c`, 1512 lines, was mistakenly omitted from the
    first pass — real fix needed too: this project's pinned SDK's own
    `<bsm/libbsm.h>` marks `audit_token_to_au32()`
    `API_UNAVAILABLE(ios,...)` at the resolved-symbol level, a real hard
    error neither `__asm` nor C-identifier renaming can bypass —
    resolved via a redirect header, `launchd_compat_headers/bsm/
    libbsm.h`, pointing at the real, unrestricted upstream BSM repo's
    own copy instead) plus all 7 real SERVER-side MIG files
    (`jobServer.c` etc, `job_forwardServer.c` needing one extra
    `-include core.h`) into `liblaunchd_core.a` (20 objects).
    `build_launchd_syscalls.sh` (new) then closed a large real BSD-
    syscall/libc gap (`chown`/`fork`/`posix_spawn`/`wait4`/`uuid_clear`/
    `getopt`/etc, ~35 real functions) into `liblaunchd_syscalls.a` (46
    objects). A real, honest trial link (`-Wl,-force_load` — a plain
    link against `launchd.c`'s own real `main()` was found to not
    actually pull in `core.c`/`ipc.c` at all, since `main()` only
    reaches them indirectly through dispatch/XPC handlers, the same
    "empty main proves nothing" lesson recurring in a subtler form) is
    down to **~110 genuinely-undefined-anywhere symbols, from 266** at
    the start of this pass. Remaining, categorized (none fixed yet):
    - **CoreFoundation** (~14 symbols: `CFArrayGetCount`,
      `CFPropertyListCreateFromXMLData`, `CFDictionaryApplyFunction`,
      etc) — the same real structural wall already documented for
      IOKitUser (`project_xnu_rpi3_iokituser_cf_wall` in memory): no CF
      port exists on this target. `pd_launchd_plist.c` itself already
      avoids CF (compiles clean), but `core.c`'s own real plist-parsing
      path uses it directly.
    - **pthread QoS/attr API** (~20 symbols) — already tracked as this
      section's own item **d3**, unchanged by this pass.
    - **Mach voucher / host-special-port routines** (~10 symbols:
      `host_reboot`, `host_set_special_port`, `mach_voucher_attr_command`,
      etc) — already tracked as item **d5**.
    - **BSM audit-record construction** (`au_open`/`au_write`/`au_to_*`,
      ~10 symbols) — real source exists (`third_party/BSM/bsm/lib/
      bsm_audit.c`/`bsm_token.c`) but needs a real private header
      (`struct au_token`/`au_record_t`/`AU_ARG64_TOKEN`/`ADD_U_CHAR`
      etc) this project's pinned BSM repo doesn't ship — only `xnu-7195`'s
      kernel-side `bsd/bsm/audit_internal.h` came close, and pulling
      that in surfaced further real gaps (`HEADER_SIZE`/`TRAILER_SIZE`/
      `be32enc`/`be64enc`). Deprioritized as a peripheral, non-critical
      logging feature, not core job-management.
    - **`os_log`/`__mach_errors`** — already-documented, genuinely
      unbuildable gaps (Phase 6.8's own libdispatch/libxpc sections).
    - **A long tail of smaller items**: `job_mig_look_up2_forward`/
      `job_mig_parent_forward` (real functions, not yet located —
      possibly a genuinely-unimplemented job-forwarding feature in this
      exact PureDarwin branch, same class of gap as `ld2xpc`/`xpc2ld`,
      not yet checked against the real `launchd-842.92.1` clone the way
      those were); `runtime_ktrace`/`runtime_ktrace0` (called by
      `runtime.c` itself but still undefined — likely gated behind a
      macro this project doesn't define, not yet root-caused); `exec.c`/
      `signal.c` (need a real internal "private name" alias convention,
      e.g. `_execve` distinct from public `execve`, not yet ported);
      `waitpid.c` (a real K&R-style declaration trips this project's
      clang flags); `atexit.c`/`glob.c` (each need one more real private
      header not yet cross-referenced); `initgroups` (`NO_SYSCALL_STUB`,
      no real dedicated wrapper source located yet).

    **Concrete next steps, in priority order**: (1) ~~decide whether to
    invest in a real CF port now~~ — DONE, see item **d7**; (2) close the
    pthread QoS/attr gap (item d3) since it's shared with the still-open
    `libdispatch.dylib` gap too, so fixing it once helps both; (3) the
    smaller one-off items above are all real, bounded, and individually
    quick once picked up.

    **UPDATE 2026-08-26: CoreFoundation wired in for real, verified via
    the actual launchd trial link (not a synthetic CF-only probe).**
    First checked the real question this raised: does launchd's actual
    CF usage need ICU? Checked `pd_launchd_plist.c` (the only real
    launchd file that touches CF) line by line — its full CF surface is
    `CFGetTypeID`/`CFStringGetTypeID`/`CFStringGetCString`/
    `CFDictionaryGetTypeID`/`ApplyFunction`/`CFArrayGetTypeID`/
    `GetCount`/`GetValueAtIndex`/`CFBooleanGetTypeID`/`GetValue`/
    `CFNumberGetTypeID`/`GetValue`/`CFPropertyListCreateFromXMLData`/
    `CFRelease`/`kCFAllocatorNull`. **None of it touches CFLocale,
    CFCharacterSet, collation, or CFNumberFormatter** — the only real
    ICU-dependent parts of this CF port (item d7). Also corrected a
    stale assumption in `build_corefoundation.sh`'s own scope comment:
    `CFPropertyListCreateFromXMLData` was documented as "explicitly not
    included," but it already works — its real XML parser
    (`_CFPropertyListCreateFromUTF8Data`) is self-contained inside
    `CFPropertyList.c` itself, no separate `CFXMLTree`/libxml2
    dependency needed. **So: no, ICU is not on launchd's critical path.**
    One real, non-ICU caveat: `CFStringGetCString`'s fast path (a plain
    `memmove`) only covers strings already stored 8-bit/ASCII-compatible
    internally; non-ASCII plist string values would hit the still-
    missing `__CFStringEncodeByteStream` slow path. Real launchd plist
    content (labels, paths, program arguments) is essentially always
    ASCII in practice, so this doesn't block the realistic case, but
    it's a real, tracked gap (see item d7), not eliminated.

    Re-ran `build_launchd_core.sh`'s own real trial link (now updated to
    a real DYNAMIC link — dropped `-static`, dropped `libdispatch_real.a`
    for `libdispatch.dylib`, same reason as item d7's own harness fix —
    and wired in `libCoreFoundation.a` + `cf_libc_extras.a`). Also added
    a new, permanent `launchd_libc_extras.a` selective-extraction step
    (same pattern as d7's `cf_libc_extras.a`) closing 15 more real,
    already-built-elsewhere BSD libc symbols: `chdir`/`access`/`strstr`/
    `sleep`/`sigprocmask`/`sigaction`/`getifaddrs`/`freeifaddrs`/
    `arc4random_uniform`/`bind`/`getentropy`/`uuid_copy`/`uuid_is_null`/
    `fegetenv`/`fesetenv`/`fstatat`. (`ctime_r`/`localtime_r` via
    `libopenpam.a`'s `localtime.o` and `setlogmask` via its
    `oldsyslog.o` were tried and reverted the same way item d7 reverted
    `localtime.o`/`libproc.o`/`strtoul.o` — real, deep, not-locally-
    satisfied dependency chains for one function each, a net loss.)

    **Real, current count: 167 undefined symbols** (down from the
    pre-CF ~110 count's own honest baseline, but that number wasn't
    apples-to-apples — CF's ~14 direct symbols were simply unresolved
    before, now they're real and working, but exposed ~150 of CF's *own*
    internal gaps as a side effect of `CFRuntime.c`'s monolithic
    class-registration init path, see item d7). Categorized, **critical
    path vs backlog per explicit user instruction (2026-08-26: "continue
    on the critical path to get launchd done, everything else goes to
    backlog")**:
    - **BACKLOG (CF/ICU scope, item d7's own territory, not launchd-
      specific)**: all remaining `CF*`/`kCF*`/`u_*`/`__CF*` symbols —
      CFCharacterSet/CFUniChar/CFLocale/CFNumberFormatter (ICU),
      CFReadStream/CFWriteStream (needs kqueue/FD), CFBundle (tried,
      reverted — 30-for-3 bad trade), CFBurstTrie, CFURLCreateDataAnd
      PropertiesFromResource, CFTimeZoneGetSecondsFromGMT,
      CFCopySearchPathForDirectoriesInDomains, the StringEncodings-
      internal family (`CFStringEncodeByteStream`/`DecodeByteStream3`/
      `EncodingByteLengthForCharacters`/`GetConverter`/
      `ConvertIANACharSetNameToEncoding`/`UnicodeToBytes`,
      `CFStringGetFileSystemRepresentation`/`GetMaximumSizeOf...`,
      `CFStringScanDouble`/`Integer`, `CFSetCharToUniCharFunc`,
      `CFStrConvertBytesToUnicode`, `CFCharToUniCharTable`,
      `CFCreateOldStylePropertyListOrStringsFile`, `__CFGetSlash`) — not
      needed by launchd's real plist-parsing CF usage (verified above).
    - **BACKLOG (already-tracked)**: pthread QoS/attr (item d3);
      Mach voucher/host-special-port routines (item d5) — this pass
      found more of the same real category (`host_reboot`/
      `host_set_exception_ports`/`_special_port`/`_UNDServer`,
      `mach_port_get_attributes`/`get_set_status`/`move_member`/
      `request_notification`/`set_attributes`/`set_mscount`,
      `mig_allocate`, `pid_for_task`) — extend d5's scope to cover
      these too, same real MIG-client-routine-regeneration fix; BSM
      audit-record construction (`au_*`, `audit_session_*`,
      `getaudit_addr`/`setaudit_addr`) — already deprioritized as
      peripheral logging; `job_mig_look_up2_forward`/`_parent_forward`,
      `runtime_ktrace`/`runtime_ktrace0` — already-tracked long tail.
    - **BACKLOG (`libSystem.B.dylib` export-parity gap, item d7's own
      already-documented architecture debt)**: `__dispatch_queue_attr_
      concurrent`, `__os_alloc_once*`, `__os_log_*`, `__platform_bzero`,
      `__pthread_bsdthread_init`/`_main_thread_postfork_init`/
      `_clear_qos_tsd`, `malloc_zone_*`, `malloc_default_zone`/
      `_good_size`, `mach_vm_region`, `__NSGetArgc`, `fmod`/`modf`/`pow`,
      `uuid_generate_random`/`_time`, `pthread_getugid_np`/
      `_is_threaded_np`/`_threadid_np`, `strtoul_l`, `getegid`.
    - **BACKLOG (never open-sourced, item d7's own finding)**: ASL
      (`asl_open`/`_close`/`_free`/`_new`/`_send`/`_set`).
    - **CLOSED 2026-08-26, same day, second real pass on the "new, not
      yet triaged" batch above — 11 more symbols, 167 → 156**: real,
      individually-verified fixes for every one of these turned out to
      be small and mechanical once actually attempted, not deep rabbit
      holes:
      - `mkdirat`/`getsockopt`: plain, non-`NO_SYSCALL_STUB`
        `create-syscalls.pl` raw-trap entries this project just hadn't
        listed yet — one-line addition each.
      - `getpeereid`/`getlogin`: both real `NO_SYSCALL_STUB` entries
        Apple implements as hand-written wrappers, not raw traps
        (`getpeereid` via `getsockopt(LOCAL_PEERCRED)`,
        `getlogin` via a raw `__getlogin` trap this project's own
        `create-syscalls.pl` step CAN still generate despite
        `NO_SYSCALL_STUB` — it just skips the convenience-wrapper name,
        not the trap itself). The old `_getpeereid` entry in the
        `create-syscalls.pl` loop was actually silently failing the
        whole time (swallowed by the loop's own `2>/dev/null || true`)
        since `getpeereid` was never a raw trap in the first place — a
        real, pre-existing bug in this script, now fixed with the real
        `third_party/Libc/gen/FreeBSD/getpeereid.c`/`getlogin.c`.
      - `execl`/`execv`, `signal`, `waitpid`: real, unmodified
        `third_party/Libc/gen/FreeBSD/{exec,signal,waitpid}.c`. Each
        needed one real, small, previously-undiagnosed fix:
        `exec.c`/`signal.c` call internal "private name" aliases
        (`_execve`/`_write`/`_sigaction`) that real Apple's own build
        generates via a linker alias/export-map step this project
        doesn't have — closed via a small, real, thin-forwarding compat
        file (`launchd_syscall_compat_headers/private_names_compat.c` +
        `private_names_decls.h`), not a from-scratch implementation.
        `waitpid.c`'s real issue was never "K&R syntax" as first
        suspected — it was `__weak_reference(sym,alias)` (meant to
        alias real `__waitpid` to public `waitpid`) being a genuine
        no-op in this project's `fbsdcompat/sys/cdefs.h`, AND not
        defined by the real SDK's own `sys/cdefs.h` at all, so clang
        parsed the bare macro-call lines as invalid K&R declarations.
        Fixed via `-D'__weak_reference(sym,alias)='` (makes the real
        no-op explicit) + a curated public `waitpid()` wrapper (same
        forwarding-only pattern as this script's existing
        `wait4_compat.c`).
      - `atexit`: real `stdlib/FreeBSD/atexit.c`. Needed a real,
        genuinely-undefined-anywhere `__isthreaded` global (Apple's
        skip-locking-when-single-threaded optimization flag) — closed
        via `isthreaded_compat.c`, hardcoded to 1/always-threaded,
        always correct since this project's pthread support is real,
        just not maximally optimized. Also needed a new tracked
        `os/alloc_once_private.h` (same class of per-consumer-tracked-
        copy as `mach_vm.h`'s own precedent — the untracked, gitignored
        `libc_build/compat_include/` already has one for a different
        key this consumer doesn't use).
      - `glob`, `sys_siglist`, `qsort`: real `gen/FreeBSD/glob.c` needed
        a chain of real headers (`sys/syslimits.h` for
        `COLL_WEIGHTS_MAX`, `setlocale.h`, `malloc_private.h`, a new
        tracked `System/machine/cpu_capabilities.h` forward) plus
        `-D__LIBC__=1` (a real Apple-internal build macro gating
        `collate.h`'s own declarations — without it,
        `__collate_range_cmp` has no visible prototype) plus its own
        real dependency `locale/FreeBSD/collcmp.c`. `sys_siglist`
        (`gen/FreeBSD/siglist.c`) and `qsort` (selectively extracted
        from `libremovefile.a`, same pattern as `cf_libc_extras.a`)
        needed no fixes at all, just wiring in.
      - **Not fixed, real gap confirmed, left for a future pass**:
        `initgroups` — genuinely `NO_SYSCALL_STUB` with no dedicated
        wrapper source anywhere in this project's pinned repos (checked
        again this pass, same conclusion as before).
      All real fixes are now permanent steps in `build_launchd_syscalls.sh`
      (61 objects, up from 46) and `build_launchd_core.sh`'s
      `launchd_libc_extras.a` (14 objects, up from 5) — see either
      script's own header comment for the full per-symbol writeup.
    - **BACKLOG (new this pass, real but not yet triaged as critical-
      path-or-not)**: `mktime` (real source exists but only inside
      `stdtime/FreeBSD/localtime.c`, the same expensive file already
      reverted twice for `localtime_r`/`ctime_r` — tried again implicitly
      via this pass's other fixes, still not worth its own real,
      deep, not-locally-satisfied dependency chain just for one
      function); `memorystatus_control`/`setiopolicy_np` (real
      macOS-specific resource-management syscalls, need real raw-trap
      generation, not yet attempted); `xpc_copy` (a REAL gap, not just
      unbuilt — confirmed via source inspection that `libxpc/xpc_misc.c`
      has `xpc_copy_description`/`_description_level`/
      `_entitlement_for_token` but no plain `xpc_copy(xpc_object_t)`
      deep-copy function at all; needs the same "confirm via historical
      `launchd-842.92.1` clone" treatment `ld2xpc`/`xpc2ld` got before
      concluding it needs a from-scratch implementation);
      `__logname`/`_mutex` (getlogin/logname caching — `getlogin()`
      itself is now real, this is a separate caching layer some code
      calls directly); `__sprintf_chk`; `__udivti3` (a compiler-rt
      128-bit-division builtin — checked `libclang_rt.ios.a`, doesn't
      provide it for this target, real gap, small and unlikely to
      matter much); `__mach_errors` (a data table backing
      `mach_error_string`, which already works — this looks like a
      separate, real, currently-unused symbol some code references
      directly); `_localtime`/`_ctime_r`/`_localtime_r` (all live in the
      same reverted `localtime.c`, same as `mktime` above).
    Every one of these was spot-checked against the current curated
    stack (`nm -g` across all `.a` files) before being called a real
    gap, not assumed.

    **UPDATE, same day, real 5th fix on `mktime`/`localtime`/
    `localtime_r`/`ctime_r`**: unblocked for real by looking at how
    PureDarwin's own CMake build gets past this exact wall — it depends
    on a real `libc_features` step that runs `third_party/Libc`'s own
    real `xcodescripts/generate_features.pl` against a real per-
    platform `Makefile.inc`. Both pieces were already pinned in this
    project's own Libc checkout, just never run. Real, self-contained
    regen via a new tracked `libc_features_platform/Platforms/iphoneos/
    Makefile.inc` (one documented change from the real pinned copy:
    `FEATURE_TIMEZONE_CHANGE_NOTIFICATION` disabled, since enabling it
    needs the real `notify(3)` client API this project has only built
    the MIG server side of) + `-fno-stack-check` (avoids needing a
    from-scratch, safety-critical `__chkstk_darwin` ABI routine with no
    real source anywhere in this project) + `libsystem_pthread.dylib`
    linked directly (localtime.c calls the internal
    `_pthread_rwlock_*` variants, outside `libSystem.B.dylib`'s own
    limited re-export list — the same already-documented export-parity
    gap, not a new one). `asctime`/`asctime_r` confirmed to hit the
    identical deep `xlocale.c` chain already reverted for `strtoul_l` —
    left deferred, not re-chased. **Real gap count now 109.**

    **FINAL STATUS, end of the 2026-08-26 critical-path pass: 183 → 109
    undefined symbols** (a 40% reduction, every single fix real and
    individually trial-link-verified — no fabricated stubs beyond the
    honestly-scoped compat forwards documented throughout this section
    and item d7). Real, closed this pass: CF wired in for real (item
    d7); 15+11+6 real syscall/libc gaps via selective `ar x` extraction
    and real curated wrappers (`execl`/`execv`/`signal`/`waitpid`/
    `atexit`/`glob`/`getpeereid`/`getlogin`/`mkdirat`/`getsockopt`/
    `sys_siglist`/`qsort`/`chdir`/`access`/`strstr`/`sleep`/
    `sigprocmask`/`sigaction`/`getifaddrs`/`freeifaddrs`/
    `arc4random_uniform`/`bind`/`getentropy`/`uuid_copy`/
    `uuid_is_null`/`fegetenv`/`fesetenv`/`fstatat`/`getaudit_addr`/
    `setaudit_addr`/`memorystatus_control`); real Mach host/port admin
    routines (`host_reboot`/`host_set_*`, `mach_port_set_mscount`/
    `get_set_status`/`move_member`/`request_notification`/
    `get_attributes`/`set_attributes`, `pid_for_task`, `mig_allocate`);
    pthread thread-ops (`pthread_detach`/`kill`/`setschedparam`/
    `threadid_np`/`is_threaded_np`/`getugid_np`), honestly scoped to
    what's reachable given this project's own pre-existing
    pthread_create() gap (no real per-thread targetable handle); real
    malloc zone API (`malloc_zone_*`, CFAllocator's real default-
    allocator implementation); real math (`fmod`/`modf`/`pow`, no Libm
    anywhere in this project); `mach_vm_region`, `uuid_generate_random`/
    `_time`, `_NSGetArgc`, `getegid`; real `ktrace.c` wiring
    (`runtime_ktrace`/`0`, root-caused as "never compiled" not "gated
    behind a macro"); a real, from-scratch `xpc_copy()` (confirmed
    genuine gap, same class as `ld2xpc`/`xpc2ld`); real `__sprintf_chk`/
    `__vsprintf_chk`/`__mach_errors`/`__udivti3`. Two real, pre-existing
    bugs found and fixed as a side effect: `getpeereid`'s
    `create-syscalls.pl` entry was silently failing forever (swallowed
    by the loop's own `2>/dev/null || true`); a real "SDK's own older
    header wins" conflict for `mach_traps.h`/`libkern/OSReturn.h`/
    `IOKit/IOReturn.h` (same bug class as `mach_vm.h`/`spawn.h` before
    them).

    **What's left, honestly categorized — no unexplored territory,
    every item below was individually investigated this pass, not
    assumed to be hard**:
    - **Genuine scope walls (large, separate undertakings, not bugs)**:
      real ICU (CFCharacterSet/CFUniChar/CFLocale/CFNumberFormatter/
      StringEncodings-internal family, ~50 symbols) — item d7's own
      long-standing finding; real ASL client library (`asl_*`, 6
      symbols) — confirmed never open-sourced by Apple, not just
      unattempted.
    - **Deprioritized, real, peripheral** (already-tracked, unchanged
      this pass): BSM audit-record construction (`au_*`/
      `audit_session_*`, ~10 symbols) — needs a private header this
      project's pinned BSM repo doesn't ship.
    - **`libSystem.B.dylib` export-parity gap** (item d7's own already-
      documented architecture debt, not launchd-specific): `os_log`
      family, `__dispatch_queue_attr_concurrent`,
      `__pthread_bsdthread_init`/`_main_thread_postfork_init`/
      `_clear_qos_tsd`, `__platform_bzero`, `__os_alloc_once*`,
      `__CFGetSlash`, `CFReadStream`/`CFWriteStream`+
      `kCFStreamPropertyDataWritten`, `CFBundle*`, `CFBurstTrie*`,
      `CFCopySearchPathForDirectoriesInDomains`,
      `CFURLCreateDataAndPropertiesFromResource`,
      `CFTimeZoneGetSecondsFromGMT` — real, unported CF subsystems
      (`CFBundle` specifically tried+reverted, 30-for-3 bad trade).
    - **CLOSED, same day**: `mktime`/`localtime`/`localtime_r`/
      `ctime_r` — unblocked via `generate_features.pl` (see this
      section's own "UPDATE, same day" entry above). `asctime`/
      `asctime_r` remain real, deferred gaps (see below).
    - **Real, individually tried-and-reverted this pass (deep
      dependency chains, not worth the trade)**: `asctime`/
      `asctime_r` — real source exists but needs `strftime_l()`, which
      pulls the same deep `xlocale.c` locale-loading-machinery chain as
      `strtoul_l` below; `strtoul_l` — real `__c_locale` turned out to
      need the same chain, even via a filtered `ld -r -exported_symbol`
      extraction.
    - **Real, no source located anywhere in this project's pinned
      repos**: `initgroups` (confirmed `NO_SYSCALL_STUB` with no
      dedicated wrapper file, checked multiple times); `setiopolicy_np`
      (not in `syscalls.master` at all, no real Libc wrapper found).
    - **Real, genuinely unimplemented feature in this exact PureDarwin
      branch** (not a build gap): `job_mig_look_up2_forward`/
      `job_mig_parent_forward` — investigated this pass: real
      `job_mig_look_up2`/`job_mig_parent` (non-forward) are fully
      implemented in `core.c`; the `_forward` variants are
      `simpleroutine`s meant to forward a lookup to a different
      job-domain (real Apple's actual per-user/multi-instance launchd
      architecture) and reply out-of-band via a MIG reply-client
      function this project's own `job_reply.defs` regen doesn't
      provide (`job_replyUser.c` has `job_mig_send_signal_reply`/
      `_log_drain_reply`/`_spawn2_reply`, not `_look_up2_reply`/
      `_parent_reply`) — this project has no multi-instance-launchd
      concept at all (root-bootstrap only), so real forwarding was
      never going to have anywhere to forward TO; implementing a
      correct "no parent to forward to" reply needs the right reply-
      client function identified first, not yet done.

    **Bottom line**: launchd is not a zero-gap link (that requires a
    real ICU port and a real ASL port, both separate, large
    undertakings this project has never attempted) — but the critical-
    path work is exhausted for this pass. Everything remaining is a
    documented, categorized, previously-investigated item, not
    unexplored territory.

d7. **Real CoreFoundation port started 2026-08-26 — real minimal subset
    compiles + archives clean; real trial link has ~150 honestly-
    categorized remaining gaps.** `tools/userland_staging/
    build_corefoundation.sh` (new) — see its own HONEST SCOPE STATEMENT
    for the full writeup. Real, unmodified source from
    `third_party/swift-corelibs-foundation` (Apple's own, actively-
    maintained CF fork; pinned in `setup_third_party.sh` at
    `swift-5.4-DEVELOPMENT-SNAPSHOT-2021-02-12-a`), NOT
    `apple-oss-distributions/CF` — that repo's last real tag
    (`CF-855.17`) is dated 2014-08-15 and CF isn't part of the real
    `macos-112` distribution-macOS manifest at all (both confirmed via
    real clones). This resolves the "try Apple's real source first"
    question for CF specifically: it works, so the GNUstep fallback the
    user authorized as a contingency was never needed.

    17 real files compile clean (Base/Collections/NumberDate/String/
    Parsing subprojs — Array/Dictionary/Data/BasicHash/Set/Bag/
    BinaryHeap/BitVector/Storage/Tree/Number/Boolean/String/
    PropertyList/BinaryPList/Base/Runtime/Utilities/UUID/Platform/
    KnownLocations/SortFunctions), archived into `libCoreFoundation.a`.
    Real fixes along the way: a genuine upstream `bool`/`Boolean`
    prototype-conflict bug in `CFPriv.h` (patched via
    `third_party-patches/swift-corelibs-foundation.patch`, applied by
    `setup_third_party.sh` — same real-patch convention as `bash.patch`
    etc); a genuine upstream `TARGET_OS_OSX`/`TARGET_OS_MAC` guard
    mismatch around `__CFTSDInitialize` (iOS-target-only bug, forward-
    declared via a compat header rather than patched); `DEPLOYMENT_RUNTIME_C=1`
    + `INCLUDE_OBJC=1` (matches `CoreFoundation/build.py`'s own real,
    non-Swift-runtime build path — the CMake default assumes a Swift
    runtime this project doesn't have); the pinned iPhoneOS SDK's real
    `<mach/mach_vm.h>` being `#error unsupported` (same gap
    `build_libmalloc.sh` already found — reused + extended its compat
    header); `OS_LOG_SUBSYSTEM_RUNTIME_ISSUES` missing from this SDK's
    older `<os/log.h>`.

    **The bigger structural finding**: `CFRuntime.c` has one monolithic
    static class-registration table referencing every real CF type by
    extern symbol, so even a "minimal subset" needs *something* to
    satisfy symbols for types it doesn't build. Real, safe (never
    dereferenced unless that specific type is instantiated, which
    nothing in this project's call graph does) zero-initialized
    `CFRuntimeClass` stub descriptors for ~26 unbuilt types (RunLoop/
    Stream/Socket/MachPort/URL family/Locale/Calendar/TimeZone/Date
    family/Formatters/CharacterSet/AttributedString/Bundle/PlugIn/
    Preferences/Error) plus a handful of related init-hook/toll-free-
    bridging no-ops now live in `cf_compat_headers/cf_class_stubs.c`
    (fully documented per-symbol in that file). One of these
    (`_CFCompareStringsWithLocale`) got a real ordinal-UTF16-compare
    fallback instead of a stub, since it's real, reachable logic, not
    dead code.

    **Real trial link, ~150 symbols remaining, categorized** (none
    fixed yet — this is the honest state after the above):
    - **Real ICU dependency** (`_u_getIntPropertyValue`/
      `_u_hasBinaryProperty` + the entire `CFUniChar*`/
      `CFCharacterSet*` family + `CFStringUtilities.c`'s own
      `_CFCompareStringsWithLocale` needing `<unicode/ucol.h>`) — this
      project has no ICU port; out of scope until/unless one exists.
      Real impact: CFString's non-ASCII case-mapping/decomposition/
      collation and CFCharacterSet are unavailable, but basic
      CFStringGetCString/CFStringCreateWithCString-class ASCII usage is
      unaffected.
    - **Real, unported CF subsystems** referenced by API (not just the
      class table already stubbed above): `CFLocaleCopyCurrent`/
      `GetIdentifier`/`GetValue`/`GetTypeID` (real ICU dependency,
      confirmed via trial compile: `CFLocale.c` needs `<unicode/uloc.h>`,
      same wall as `CFStringUtilities.c`), `CFNumberFormatter*` (8
      functions, real ICU dependency too), `CFRead`/`CFWriteStream*` (6
      functions, needs real kqueue/FD integration), `CFBundleCreate`/
      `CopyLocalizedString`/`GetBundleWithIdentifier` — **tried and
      reverted 2026-08-26**: real `PlugIn.subproj/CFBundle.c` (1738
      lines) also compiled clean with zero errors, but a real trial link
      showed it drags in ~30 new symbols (real `dlfcn`/DYLD-introspection
      bundle-loading machinery — `_CFBundleDlfcnLoadBundle`/
      `_LoadFramework`/`_CheckLoaded`/`_Preflight`/`_Unload`,
      `_CFBundleDYLDCopyLoadedImagePaths*`, `_CFBundleGrokBinaryType`,
      real path-manipulation helpers from a different, not-yet-built
      file — `__CFGetFileProperties`/`_GetPathProperties`/
      `_CreateContentsOfDirectory`/`_LengthAfterDeletingLastPathComponent`
      etc) to close just 3 real symbols — a **30-for-3 trade**, much
      worse than `CFURL.c`'s own 6-for-3 trade, and reverted for the
      same reason `localtime.o`/`libproc.o`/`strtoul.o` were: real
      bundle/plugin discovery is genuinely out of proportion to
      launchd/IOKitUser's actual need (reading one plist's already-
      known-path contents, not real bundle resource lookup). Left
      stubbed;
      `CFCopySearchPathForDirectoriesInDomains`, `CFBurstTrie*` (4
      functions — a real CFPropertyList/CFBinaryPList performance
      helper, not yet located which subproj it lives in),
      `CFURLCreateDataAndPropertiesFromResource` (real old-style
      resource-fork API — CFURL's own basic path-creation functions are
      closed, see below), `CFTimeZoneGetSecondsFromGMT` (CFURL.c's own
      real dependency on the still-unported CFTimeZone).
      **CLOSED 2026-08-26: `CFError*`** — real `Error.subproj/CFError.c`
      (645 lines) compiled clean with zero extra work needed, added to
      `build_corefoundation.sh`'s `FILES` array, its class-table entry
      in `cf_class_stubs.c` removed in favor of the real one. Closed 6
      symbols (`CFErrorCreate`/`GetCode`/`GetDomain`/`CopyUserInfo`,
      `kCFErrorDomainCocoa`/`OSStatus`/`POSIX`,
      `kCFErrorDebugDescriptionKey`/`UnderlyingErrorKey`).
      **CLOSED 2026-08-26: `CFDate*`** — real
      `NumberDate.subproj/CFDate.c` (593 lines) also compiled clean with
      zero extra work, same treatment (class-table stub removed, the
      real file's own `__CFDateInitialize()` replaces the compat no-op
      that used to stand in for it). Closed 4 symbols (`CFDateCreate`/
      `GetAbsoluteTime`, `CFAbsoluteTimeGetCurrent`/`GetGregorianDate`,
      `CFGregorianDateGetAbsoluteTime`, `kCFAbsoluteTimeIntervalSince1970`).
      **CLOSED 2026-08-26: `CFURL*`'s core path-creation API** — real
      `URL.subproj/CFURL.c` (5413 lines, the biggest file added so far)
      also compiled clean with zero extra work, `__CFURLClass` stub
      removed (`__CFURLComponentsClass`, a genuinely separate real file
      not yet built, keeps its stub). Closed 3 real symbols
      (`CFURLCreateFromFileSystemRepresentation`/
      `CreateWithFileSystemPath`, `CFURLCopyFileSystemPath`) but honestly
      surfaced 6 new ones as real, previously-invisible transitive
      dependencies of CFURL's fuller feature set (`__CFGetSlash`,
      `__os_log_debug_impl`/`_default` [same os_log-export-gap category
      as `__os_log_error_impl`], `_CFStringEncodingUnicodeToBytes`,
      `_CFStringGetMaximumSizeOfFileSystemRepresentation`,
      `_CFTimeZoneGetSecondsFromGMT`) — a net **worse** raw symbol count
      (+3) for **more** real capability (the actually-useful path-
      creation functions now work for real; the newly-exposed gaps are
      in more peripheral CFURL functionality). Kept rather than reverted
      — the raw count is a progress signal, not the goal.
      Real gap count now **108** (dynamic-link count including the
      curated `cf_libc_extras.a`), starting from 153. Now 25 real/compat
      files in `build_corefoundation.sh`'s `FILES` array (24 real Apple
      source + 1 compat stub file).
    - **CONFIRMED out of scope 2026-08-26 (was "not yet checked")**:
      ASL legacy logging (`asl_open`/`_close`/`_free`/`_new`/`_send`/
      `_set`) — CFUtilities.c's own legacy console-logging fallback. The
      real full ASL client library is a separate apple-oss-distributions
      repo Apple never open-sourced as far as this project's
      `docs/apple-oss-catalog.md` inventory goes, and isn't pinned in
      `setup_third_party.sh`; `third_party/libplatform/src/simple/asl.c`
      only has the much smaller `_simple_asl_*` kernel/early-boot subset,
      a different real API, not a drop-in. Same "out of scope, no source
      to port" treatment as ICU, for a genuinely different reason (never
      open-sourced vs. real, tractable, just-not-attempted).
    - **CLOSED, verified 2026-08-26**: `dispatch_apply`/`_queue_create`/
      `_release`/`_once`, `os_log_create`/`_type_enabled`/
      `__os_log_error_impl`, `__os_alloc_once*`, `__chkstk_darwin`,
      `__stack_chk_fail`/`_guard`, `__strlcpy_chk`/`__vsnprintf_chk`,
      `__platform_bzero`, `__pthread_bsdthread_init`/
      `_main_thread_postfork_init`, `_NSGetExecutablePath`, `_dlopen`/
      `_dlsym`, `_kill`/`_mmap`/`_mprotect`/`_read`/`_write`/`_kdebug_trace`,
      `_flsl`, `_pthread_rwlock_rdlock`/`_unlock`, `_mach_error_string`,
      `_CFURLCreateWithFileSystemPathRelativeToBase` (30 symbols) were a
      **link-mode mismatch in the test harness itself**, not real gaps —
      confirmed by actually doing the real dynamic trial link this item
      called for: dropped `-static` (kept `-nostdlib`), linked
      `libCoreFoundation.a` against `libdispatch.dylib` +
      `libdyld.dylib` + `libSystem.B.dylib` directly (same real recipe
      `build_dyld_test_exec.sh` already established: plain dylib links
      on the command line, `-Wl,-fixup_chains`, no `-static`) — every
      one of these 30 resolved for real. Real gap count is **138, not
      153**. This also surfaced a second, smaller, already-documented
      finding: 15 *new* symbols (`fprintf`/`printf`/`getcwd`/`close`/
      `isdigit`/`isspace`/`strrchr`/`strnlen`/`memchr`/`asprintf`/
      `pthread_atfork`/`issetugid`/`fprintf_l`/`snprintf_l`/
      `strncasecmp_l`) appeared that weren't missing in the static link
      — `libSystem.B.dylib` doesn't yet export full parity with the
      curated static libc archives it was flattened from. This is
      CLAUDE.md's own already-documented "Static-vs-dylib architecture
      debt", not a new CF-specific problem — no action needed here
      beyond noting CF's own real future dynamic link (once wired into
      launchd/IOKitUser) will hit it too.
    - **Real `libSystem.B.dylib` export-parity gaps** (the *other* half
      of the already-documented architecture debt found while verifying
      the dynamic-link-mode item above): `close`/`getcwd`/`isdigit`/
      `isspace`/`strrchr`/`strnlen`/`memchr`/`asprintf`/`fprintf`/
      `_fprintf_l`/`printf`/`snprintf_l`/`strncasecmp_l`/`pthread_atfork`/
      `issetugid`/`__memmove_chk` are all real, ordinary libc functions
      this project's own static `libc_gen.a`/`libc_string.a`/
      `libc_stdio.a`/`libc_printf.a` already provide — they only show up
      as gaps in a *dynamic* link because `libSystem.B.dylib` (the
      flattened static-stack-as-one-dylib, per CLAUDE.md's own
      documented debt note) doesn't yet export them. Not a CF-specific
      problem and not fixed here; real fix is closing that dylib's own
      export-parity gap, tracked wherever that dylib's own build script
      lives (not yet cross-referenced from here).
    - **CLOSED 2026-08-26 (6 symbols)**: `getpwnam`/`getpwuid`/`getuid`/
      `geteuid`/`gethostname`/`writev`. Real, selective `ar x` extraction
      (same pattern `build_libdispatch_dylib.sh` already established) of
      5 small `.o` members — `copyfile_syscalls.o` (from `libcopyfile.a`,
      has both `getpwnam`+`getpwuid`), `getuid.o` (`libcopyfile.a`),
      `geteuid.o` (`libremovefile.a`), `gethostname.o` (`libresolv.a`),
      `date_compat.o` (`libopenpam.a`, has `writev`) — into a new
      `cf_libc_extras.a`, now a permanent `build_corefoundation.sh` step.
      Verified via real trial, not assumed: `localtime.o` (real home of
      `localtime_r`), `libproc.o` (`proc_pidinfo`), and `strtoul.o`
      (`strtoul_l`) were tried the same way and reverted — each pulls in
      its own deep, not-locally-satisfied dependency chain (pthread
      once/get|setspecific/rwlock_wrlock + open/close/read/fstat/access
      for `localtime.o`; `__proc_info`/`__thread_selfid`/
      `__process_policy` for `libproc.o`; locale globals for
      `strtoul.o`) that a real trial link confirmed is a **net loss** —
      more new gaps introduced than symbols closed. Left as real,
      documented remaining gaps (`localtime_r`, `proc_pidinfo`,
      `strtoul_l` below) rather than chasing diminishing returns. Also
      added the same curated `libc_string.a`/`libc_gen.a`/`libc_stdio.a`/
      `libc_printf.a`/`libc_locale_for_stdio.a` archives the static trial
      link already used to the dynamic one (they'd only ever been linked
      into the static trial link before — real, zero-pollution, already-
      proven-complete for these, and closed a further batch:
      `isdigit`/`isspace`/`strrchr`/`strnlen`/`memchr`/`asprintf`/
      `fprintf`/`_fprintf_l`/`printf`/`snprintf_l`/`strncasecmp_l`/
      `close`/`getcwd`/`issetugid`/`__memmove_chk`, 15 more symbols).
    - **Real, still-genuinely-missing-everywhere libc/Mach gaps**
      (present in neither the curated static archives now linked into
      this trial link nor `libSystem.B.dylib`, and not worth chasing
      further per the reverted attempts above): `getegid`/`localtime_r`/
      `proc_pidinfo`/`strtoul_l`/`pthread_getugid_np`/
      `pthread_is_threaded_np`/`pthread_threadid_np`/`mach_vm_region`
      (declared in `cf_compat_headers/mach/mach_vm.h`, but the real
      definition in `libmalloc.a`'s `mach_vm.c` needs checking — likely
      just needs exporting)/`malloc_zone_*` (6 functions)/
      `malloc_default_zone`/`malloc_good_size`/`uuid_generate_random`/
      `_time`/`_NSGetArgc`/`fmod`/`modf`/`pow`/
      `__dispatch_queue_attr_concurrent`/`__os_log_error_impl`/
      `pthread_atfork`. Not yet spot-checked against the full curated
      stack one more time — worth a quick pass before assuming any of
      these need new work.

    **Verified, not just assumed**: tried a real, narrow trial link of a
    tiny probe program calling only IOKitUser's specific documented
    need (`CFArrayCreate`/`GetCount`/`GetTypeID`/`GetValueAtIndex`,
    `CFBooleanGetTypeID`/`GetValue`, `CFDataCreateWithBytesNoCopy`,
    `CFDictionaryCreate`/`ApplyFunction`/`GetTypeID`, `CFGetTypeID`,
    `CFNumberGetTypeID`/`GetValue`, `CFRelease`) — both with and without
    `-force_load` — expecting it to link far cleaner than the full
    `core.c`/launchd-scale trial link above, since none of those calls
    touch CFString/CFPropertyList. It didn't: the undefined-symbol list
    came back nearly identical (~146 vs ~150 symbols). Root cause:
    `CFRuntime.c`'s own `__CFInitialize()` — unconditionally reached by
    any real `CFRuntimeCreateInstance()` call, i.e. any real CF object
    creation at all, `CFArrayCreate` included — itself calls into
    `_CFStringGetUserDefaultEncoding` (stubbed, fine) but real archive-
    member pull-in also drags in enough of `CFUtilities.c`/
    `CFPlatform.c`'s own real logging/ASL/time fallbacks to reproduce
    almost the whole gap list. **Correction to this project's own
    working assumption**: there is no "cheap partial CF" shortcut here —
    closing the real gap list above (or at least its non-ICU, non-
    dynamic-link-mode portion) is a prerequisite for ANY real CF usage
    in this project, not just CFString-heavy usage.

    **Concrete next steps, in priority order**: (1) ~~resolve the
    static-vs-dynamic trial-link harness gap~~ — DONE 2026-08-26; (2)
    ~~real `CFError.c` port~~ — DONE same day; (3) ~~selective `ar x`
    extraction of already-built-elsewhere libc symbols~~ — DONE same
    day (`cf_libc_extras.a`, 6 symbols, plus 15 more from linking the
    already-existing curated `libc_string.a`/etc into the dynamic trial
    link too). **Real gap count is now 111**, down from the original
    153. Remaining: (4) decide whether `CFPropertyListCreateFromXMLData`
    (needed by `launchd/core.c`, explicitly out of scope for this pass —
    only binary-plist parsing was included) is worth a follow-up pass,
    since XML plists are real launchd/LaunchDaemons input format on real
    Darwin; (5) the ICU wall (CFUniChar/CFCharacterSet/
    CFStringUtilities collation, CFLocale, CFNumberFormatter) is the one
    item on this list that's a genuine scope decision, not just
    unstarted work — real ICU is a large, separate port this project has
    never attempted; (6) `libSystem.B.dylib`'s own export-parity gap vs
    the curated static libc stack (found 2026-08-26, see above) — not
    CF-specific, but CF's real future dynamic link will hit it, so it's
    worth closing before CF actually gets wired into launchd/IOKitUser;
    (7) the remaining real, still-missing-everywhere libc/Mach symbols
    (`getegid`, `mach_vm_region`, `malloc_zone_*`, `uuid_generate_*`,
    etc, see above) — smaller and more bounded than the CF subsystem/ICU
    work, worth a quick pass; (8) once the gap list is as small as it's
    going to get without ICU/further subsystem ports, wire the working
    subset into IOKitUser (closing that project's own long-documented
    "IOKitUser needs CoreFoundation" wall) — the actual original reason
    this port started.

    **Real ICU port started 2026-08-26** (the wall named in item (5)
    above is no longer a scope decision — begun for real). First,
    corrected a stale assumption: `docs/apple-oss-catalog.md` had listed
    `ICU` under "not currently needed," but Apple did open-source it —
    confirmed via `git ls-remote apple-oss-distributions/ICU` and the
    `macos-112` manifest (`git ls-tree macos-112 -- ICU` == commit
    `f7b86563706f76d5267452815c6b6121cf4698b7` == tag `ICU-66109`), same
    method every other pin in this project uses. Pinned in
    `setup_third_party.sh`, cloned to `third_party/ICU` (325MB checkout:
    `icuSources/` is stock upstream ICU4C with its own real autotools
    build, plus `cldrFiles/` locale data and Apple's `ICU.xcodeproj` for
    their internal B&I pipeline, unused here).

    ICU4C's real cross-compile story (confirmed via `configure.ac`) is a
    two-stage `--with-cross-build=<dir>` mechanism: build ICU natively
    for the host first (to get working `genrb`/`gencnv`/`gennorm2`/etc.
    tools plus generated locale/data tables, since generating those
    requires actually running ICU code), then cross-configure the real
    target build pointing at that host build root. Categorically
    different from this project's usual from-scratch clang-per-file-loop
    template (`build_libc_regex.sh`) — reinventing autotools by hand for
    a project this size isn't worth it. `tools/userland_staging/
    build_icu_host.sh` is stage 1 (native host build only, never linked
    into anything that ships) — **DONE 2026-08-26**: real ICU 66.1 host
    build succeeded, producing `libicuuc`/`libicui18n`/`libicudata`/
    `libicuio` dylibs, all host tools, and a real packaged CLDR data
    library (`icudt66l`), with `config/icucross.mk` present and ready to
    be the `--with-cross-build` target for stage 2 (not yet written: the
    real arm64-apple-ios14.4 cross build using this project's usual
    Xcode-12 iPhoneOS SDK / `-ffreestanding` recipe for `CC`/`CXX`).

    Two real gaps found and fixed getting stage 1 green (both documented
    in that script's own header comment, and the first is also filed as
    `third_party-patches/ICU.patch`, applied via `setup_third_party.sh`
    like this project's other patches):
    - `common/Makefile.in` never listed `aaplbfct.cpp`
      (`AppleLanguageBreakFactory`) even though `localsvc.cpp`
      unconditionally references it and Apple's own `ICU.xcodeproj`
      builds both — a real upstream (Apple) build-system oversight, not
      a this-project issue. Plain `configure && make` hits an
      undefined-symbol link error without it.
    - Apple's fork flips ICU's normal upstream default of
      `U_SHOW_CPLUSPLUS_API` from 1 to 0 (deliberate: client code linking
      the shipped OS dylib must not depend on ICU's non-ABI-stable C++
      API) but ICU's own internal `.cpp` files are real C++-API consumers
      and need it back on to build the library itself at all — confirmed
      via `ICU.xcodeproj/project.pbxproj` that Apple's real internal
      build passes `U_SHOW_CPLUSPLUS_API=1`. Passed through `CPPFLAGS`,
      matching Apple's own recipe (not a source patch).
    - Also disabled `extra`/`tests`/`samples` in this host build
      (`--disable-extras --disable-tests --disable-samples`): the only
      real goal here is the `--with-cross-build` reference root, and
      `extra/uconv`'s sample CLI tool has its own real, separate
      `UChar`/`char16_t` pointer-type mismatch against any current host
      clang (an ICU-66-era-vs-modern-libc++ incompatibility, unrelated to
      this project) that isn't worth fixing for dead-to-this-project
      sample code.

    **Stage 2 (real arm64-apple-ios14.4 target cross build) DONE
    2026-08-26** — `tools/userland_staging/build_icu_target.sh`: real
    `libicuuc`/`libicui18n`/`libicuio`/`libicudata` dylibs, confirmed
    genuine arm64 Mach-O (`file`/`otool -l`), correctly linking
    `/usr/lib/libSystem.B.dylib` + `/usr/lib/libc++.1.dylib` (this
    project's own dylibs resolve those install names at actual link
    time, same as every other real per-library dylib here).
    `libicudata.66.1.dylib` is a real 29MB packaged CLDR/Unicode data
    library — built by having the HOST's own real `genrb`/`pkgdata`
    (stage 1's output, run natively on the build machine, per ICU4C's
    own `--with-cross-build` design) generate+package data for the
    target arch, never executing target-arch code during the build.
    Confirmed the two exact symbols item (5) above named as ICU's real
    wall are now real exports: `_u_hasBinaryProperty`/
    `_u_getIntPropertyValue` in `libicuuc`, `_ucol_open` in `libicui18n`.

    Three more real gaps found and fixed getting stage 2 green (all
    documented in `build_icu_target.sh`'s own header comment; none
    needed a source patch):
    - This project's usual `-ffreestanding` flag (every other
      `build_*.sh` here uses it) breaks ICU's `./configure` outright:
      per the C++ standard, `-ffreestanding` makes `main()` an ordinary,
      mangled function (`__Z4mainv`) instead of the special unmangled
      `_main` a linked executable's entry stub expects, so every one of
      configure's own full-link compiler-sanity checks (AC_PROG_CXX, the
      namespace-support probe, etc) fails with "_main ... referenced".
      Confirmed with a standalone repro isolating the flag. Fix: drop
      `-ffreestanding` (confirmed `-fno-builtin`/`-fno-stack-protector`
      alone don't cause this), since samples/tests/extras — the only
      real ICU components that ever link a full executable — are
      already disabled.
    - `common/aaplbfct.cpp` unconditionally `#include`s
      `<NSSystemDirectories.h>` (missing entirely from the iPhoneOS
      SDK), but its only real use is already dead code on this target
      (wrapped in the file's own `#if !TARGET_OS_EMBEDDED` / `#if 0`,
      and `TARGET_OS_EMBEDDED=1` confirmed for arm64-apple-ios14.4) —
      just the `#include` itself was left unguarded. Fixed by moving it
      inside the same `#if !TARGET_OS_EMBEDDED` guard its usage already
      has (second commit in `third_party-patches/ICU.patch`).
    - `--disable-tools` (needed to avoid `tools/pkgdata.cpp`'s real
      `system()` call, which the iPhoneOS SDK marks `__IOS_PROHIBITED` —
      a hard unavailable-on-iOS error, not a warning, for a build-time
      tool binary that would never run on this project's actual boot
      target) turned out to also disable the real `data` subdir
      entirely: `Makefile.in` ties `DATASUBDIR` to the same `TOOLS`
      autoconf conditional, even though `data/Makefile.in`'s own
      packaging rule already resolves through `--with-cross-build`'s
      `TOOLBINDIR` to the *host's* tools regardless of whether local
      target-arch tools get built. A real ICU build-system gap specific
      to combining `--disable-tools` with `--with-cross-build` (Apple
      never needed to hit this, since they don't build pkgdata for an
      iOS device target in their public source drops) — fixed by
      uncommenting `DATASUBDIR = data` in the *generated* top-level
      `Makefile` after configure (scoped to this build tree, not filed
      as a general `ICU.patch` change, since it's specific to that flag
      combination rather than a general upstream inconsistency).

    **CF wiring DONE 2026-08-27** — `build_corefoundation.sh` now links
    the real ICU dylibs and builds 21 more real, previously-stubbed CF
    source files: `CFUniChar.c`/`CFCharacterSet.c` (+ their real embedded
    Unicode data, `CFCharacterSetData.S`/`CFUniCharPropertyDatabase.S`
    wrapping `.incbin`-included `CharacterSets/CFCharacterSetBitmaps
    .bitmap`/`CFUniCharPropertyDatabase.data` — real Apple data already in
    this checkout, NOT ICU-generated), `CFStringUtilities.c` (real ICU
    collation) + `CFStringEncodings.c`, `CFLocale.c`/`CFLocaleIdentifier.c`
    /`CFLocaleKeys.c`/`CFNumberFormatter.c`, `CFCalendar.c`/
    `CFCalendar_Enumerate.c` + `CFTimeZone.c` (pulled in because
    `CFLocale.c` unconditionally calls `_CFCalendarCreateCoWWithIdentifier`
    and dereferences the result — a real crash risk if stubbed to NULL,
    unlike this file's other safe-because-unreachable stubs — and
    `CFCalendar.c` in turn unconditionally calls
    `CFTimeZoneCopyDefault`/`CFTimeZoneGetSecondsFromGMT`), plus
    `CFBigNumber.c`/`CFUnicodeDecomposition.c`/`CFStringScanner.c`/
    `CFBurstTrie.c`/`CFStringEncodingConverter.c`/`CFBuiltinConverters.c`/
    `CFStringEncodingDatabase.c`/`CFICUConverters.c`/`CFFileUtilities.c`/
    `CFOldStylePList.c` — each pulled in only after a real trial compile
    confirmed it, matching the same "just try it, it's often free"
    pattern CFError.c/CFDate.c/CFURL.c already established. All 21 files
    compiled clean or near-clean (`CFCalendar.c`'s only diagnostics are
    harmless `-Wswitch` "not all enum cases handled" warnings). Removed
    the now-superseded `cf_class_stubs.c` entries these real files
    replace: 5 `CF_STUB_CLASS` descriptors (CharacterSet/Locale/
    NumberFormatter/Calendar/TimeZone), `__CFCharacterSetInitialize`,
    `__kCFLocaleCollatorID`, `__CFLocaleGetNullLocale`/
    `__CFLocaleSetNullLocale`, `_CFStringGetUserDefaultEncoding`, and
    `_CFCompareStringsWithLocale` (the exact stub docs/backlog.md's own
    d7 entry above already named as the one needing real ICU).
    Real gap count for this specific trial link is now **83 symbol
    groups** (down from ~150 when ICU was entirely stubbed out), and
    every one left falls into an already-categorized bucket: the
    genuinely-out-of-scope subsystems this project has already decided
    not to port (`CFBundle*`/`CFPreferences*`/`CFDateFormatter*`/
    `CFDateComponents*`/`CFDateInterval*`/`CFReadStream*`/
    `CFWriteStream*`), confirmed-never-open-sourced ASL, the
    already-documented `libSystem.B.dylib` export-parity gap (`_abs`/
    `_bsearch`/`_qsort`/`_strstr`/`_scalbn`/`_llabs`/`_readlink`/
    `_setenv`/`_time`/`_getpwuid_r`/`_mkdir`/`_pwrite`/`_atol`/
    `_getsectbynamefromheader_64`/`_pthread_atfork`), the already-
    documented "still genuinely missing everywhere" list (`_getegid`/
    `_proc_pidinfo`/`_pthread_getugid_np`/`_pthread_is_threaded_np`/
    `_pthread_threadid_np`/`_uuid_generate_random`/`_uuid_generate_time`),
    a handful of non-ICU "platform" (built-in-table, not ICU-backed)
    string-encoding-converter symbols and one lone `_CFUniCharPrecomposeCharacter`
    not yet traced to their real source file, and a real but
    **newly-surfaced, not-yet-root-caused** `os_log`/`dispatch` gap
    (`__os_log_debug_impl`/`__os_log_default`/`__os_log_error_impl`/
    `_os_log_create`/`_os_log_type_enabled`/
    `__dispatch_queue_attr_concurrent`/`__mh_dylib_header`) — these were
    confirmed genuinely absent from both `libdispatch.dylib` and
    `libSystem.B.dylib` via `nm` (not the static-link-mode artifact the
    similarly-named symbols in this item's earlier "CLOSED 2026-08-26"
    entry turned out to be), so CFLocale/CFCalendar's new code paths
    exercise more of the real os_log API surface than earlier CF files
    did. Worth a dedicated follow-up pass, not chased further this
    session.

    Also found+fixed along the way: this session's own ICU stage 2
    `uameasureformat.o` fix (see above) had gone stale — patched
    `i18n/Makefile.in` mid-session but never re-ran
    `build_icu_target.sh` afterward, so the on-disk dylib didn't actually
    have the symbol until a real rebuild (third commit now in
    `third_party-patches/ICU.patch`, verified via a fresh-clone `git am`
    same as the other two). Real lesson: after editing anything under
    `third_party/ICU`, always rebuild both ICU stages before trusting
    the dylibs on disk are current — same class of staleness this port's
    "GNU Make self-regeneration" gotcha already documented, just at the
    level of remembering to invoke the build at all.

    **Full-sweep pass DONE 2026-08-27** — with ICU-dependent Locale/
    Calendar/CharacterSet in place, did a real "just try compiling it"
    pass (the same method that already proved CFError/CFDate/CFURL/etc
    free) across literally every remaining `.c` file in the
    swift-corelibs-foundation checkout, rather than trusting the
    original scope note's assumption that RunLoop/Stream/Socket/
    MachPort/PlugIn structurally needed new kqueue+Mach port
    infrastructure. That assumption was never actually verified and
    turned out to be wrong: **RunLoop.subproj (CFRunLoop.c/CFMachPort.c/
    CFMachPort_Lifetime.c/CFMessagePort.c/CFSocket.c), Stream.subproj
    (CFStream.c/CFSocketStream.c/CFConcreteStreams.c), and the entire
    PlugIn.subproj bundle family (CFBundle.c + all 10 of its
    CFBundle_*.c helper files + CFPlugIn.c) all compile clean** — this
    project's existing real syscall layer was already sufficient for
    the *compile* step (whether the real runtime *semantics* work is a
    separate, unverified question — this pass only proves compilation
    and linking, not correctness). Also newly added: the DateFormatter
    family (`CFDateFormatter.c`/`CFDateComponents.c`/`CFDateInterval.c`/
    `CFDateIntervalFormatter.c`, excluded from the original scope note
    specifically because Locale/Calendar didn't exist yet — they do
    now), `CFURLComponents.c`/`CFURLComponents_URIParser.c`/
    `CFURLAccess.c`, `CFAttributedString.c`, `CFRegularExpression.c`,
    `CFRunArray.c`, `CFStringTransform.c`, `CFPlatformConverters.c`,
    `CFUnicodePrecomposition.c`, `CFSystemDirectories.c`, and
    `CFApplicationPreferences.c`. **37 more real files, bringing the
    total to 94** (up from 57), every one compiling with zero errors
    except two needing a one-line real fix each (both compat-header
    additions, no source patches): `CFBundle_Binary.c`'s real dlfcn
    introspection call `dyld_image_path_containing_address` (added to
    the existing `cf_compat_headers/mach-o/dyld_priv.h` stub), and
    `CFSocket.c`'s `#include <libc.h>` (a real macOS-only umbrella
    header the iPhoneOS SDK never shipped — new
    `cf_compat_headers/libc_umbrella/libc.h` re-including just the
    specific standard headers it needs).

    Removed all now-superseded `cf_class_stubs.c` `CF_STUB_CLASS`
    entries except one — `__CFPreferencesDomainClass` is the only class
    stub left, since `CFPreferences.c` itself has a real, genuine,
    multi-part compile failure (see below) unlike everything else on
    the original stub list.

    **Five real, verified (not assumed) exclusions remain at this
    point** (see below — 2026-08-27's libxml2 pass closed one of them,
    `CFXMLInterface.c`, down to four), each a confirmed compile
    failure, not a scope guess:
    - `CFUserNotification.c` (AppServices.subproj): real GUI
      alert-dialog API, `kCFUserNotificationIconURLKey`/`SoundURLKey`/
      `LocalizationURLKey` are hard `unavailable: not available on iOS`
      SDK errors. No GUI on this project's boot target regardless.
    - `CFPreferences.c` / `CFXMLPreferencesDomain.c`
      (Preferences.subproj): real, multi-part gap — `gethostuuid`
      marked unavailable on iOS behind a `#if TARGET_OS_MAC` guard that
      doesn't actually exclude iOS (`TARGET_OS_MAC` means "any Apple
      OS," not "specifically macOS" — a real bug in this exact
      checkout, or at least a guard that doesn't do what it looks like
      it does), plus a missing CFUUID-family declaration and an
      undeclared `prefDir`; `CFXMLPreferencesDomain.c` separately hits
      "Dont know how to define sleep for this platform" in its own
      `#if` chain. Real preference-file storage
      (`CFPreferencesCopyAppValue` et al) stays unimplemented as a
      result — `CFApplicationPreferences.c` (built, clean) can't close
      that gap alone since it calls into `CFPreferences.c`'s own real
      functions.
    - `CFURLSessionInterface.c` (URL.subproj): real libcurl dependency
      (`curl/curl.h` not found). No `CFRuntimeClass` and nothing else
      in the checkout references its symbols (unlike
      `CFUserNotification.c` above, which needed a real stub) — a real
      libcurl port doesn't just close a compile gap, it's a
      **prerequisite for any real `NSURLSession`-style networking at
      all**, tracked as real future work below, not stubbed, since
      there's genuinely nothing to stub.
    - `CFWindowsUtilities.c` (Base.subproj): Windows-only, structurally
      inapplicable.

    **libcurl investigated 2026-08-27, real future-work item filed, not
    pursued this pass**: Apple did open-source it
    (`apple-oss-distributions/curl`, `macos-112` pins `curl-121.40.2`,
    confirmed via `git ls-remote`/manifest same as every other pin).
    Real, hard wall found before any build attempt: Apple's own real
    iOS/macOS build (`README.APPLE`) configures with `--with-darwinssl`
    (Secure Transport) for TLS — closed-source, this project has no
    equivalent, and per the standing crypto-scope decision (P2 item 0a)
    real crypto work here goes through corecrypto/AES, not
    OpenSSL/LibreSSL/etc, so swapping in a different TLS backend isn't
    in scope either. `--without-ssl` would let curl *compile* (real,
    standard, supported configure option), but real HTTP networking at
    runtime needs `socket`/`connect`/`bind`/`getaddrinfo`/etc — the
    exact same real BSD-socket-syscall gap already documented above for
    `CFSocket.c`/libxml2's own disabled `nanohttp.c`, which this project
    has no wrapper for anywhere yet. Net assessment: building curl now
    would only close one more *compile-time* file (mirroring
    libxml2/`CFXMLInterface.c`'s own "just needs the library to exist"
    situation), not enable any real functioning network client — a
    real, substantial, separate lift (implement real socket syscalls,
    then decide on a real TLS backend) for a much smaller immediate
    payoff than libxml2 had. **Real prerequisite order for whoever picks
    this up**: (1) real BSD socket syscall wrappers in userland libc
    (needed by `CFSocket.c`/`CFRunLoop.c` too, so likely worth doing
    once for both consumers, not curl-specific), (2) a real TLS backend
    decision (LibreSSL/BoringSSL/mbedTLS are real, portable,
    non-corecrypto options if a from-scratch TLS stack is ever wanted;
    `--without-ssl` HTTP-only curl is a smaller intermediate step that
    needs only (1)), (3) then a real `build_libcurl.sh` following
    `build_libxml2.sh`'s own template (check for a pregenerated
    `config.h` first, same as both ICU and libxml2 had).

    **Real gap count for this trial link is now 101 symbol groups**
    (up from 83, not down) — an honest tradeoff, not a regression: the
    newly-included RunLoop/Socket/MachPort/Bundle subsystems pull in
    their own real, previously-invisible transitive dependencies this
    project's userland stack doesn't expose yet anywhere (confirmed via
    `nm` against every existing archive, not assumed) — real Mach port
    syscalls (`mach_port_allocate`/`_construct`/`_destruct`/
    `_get_attributes`/`_insert_member`/`_extract_member`/`_type`), real
    BSD socket syscalls (`socket`/`bind`/`connect`/`listen`/`accept`/
    `recv`/`recvfrom`/`sendto`/`getsockname`/`getpeername`/
    `getsockopt`/`setsockopt`/`socketpair`), real Mach timer syscalls
    (`mk_timer_create`/`_arm`/`_cancel`/`_destroy`, CFRunLoop's timer
    sources), real `pthread_attr_*` thread-creation-attribute calls,
    `NXFindBestFatArch`/`NXGetLocalArchInfo` (CFBundle's fat-binary
    architecture detection), and `sysdir_start_search_path_enumeration`/
    `_get_next_search_path_enumeration` (CFBundle's real search-path
    API). None of these exist in any archive this project has already
    built (confirmed via `nm`, not assumed) — real follow-up work, not
    a quick link-order fix, and not chased further this session. Plus
    the previously-documented, still-not-root-caused os_log/dispatch
    gap and the already-categorized libSystem.B.dylib export-parity/
    still-missing-everywhere buckets.

    **libxml2 DONE 2026-08-27, closing `CFXMLInterface.c`** (one of the
    five exclusions above — down to four now). Investigated whether
    libxml2/curl were portable enough to close the last two real
    external-dependency exclusions; libxml2 turned out to be a real,
    easy win, curl a much bigger, murkier one (Apple's real iOS build
    uses closed-source Secure Transport for TLS, which this project
    doesn't have and isn't adding per the standing corecrypto/AES-only
    crypto scope decision — `--without-ssl` would compile but real
    networking needs socket syscalls this project doesn't have either,
    so curl wasn't pursued). Pinned `libxml2-34.9` (matches both the
    `macos-112` manifest and, confirmed via `LIBXML_DOTTED_VERSION`, the
    Xcode 12 iPhoneOS SDK's own bundled headers exactly — real upstream
    libxml2 2.9.4 either way). Unlike ICU, no need to run `configure` at
    all: Apple's checkout ships a real pregenerated `Pregenerated
    Files/include/config.h` (a real `configure` run's output, checked in
    for their own Xcode-based build).

    The earlier "conflicting types" compile failure blocking
    `CFXMLInterface.c` was never a real libxml2 dependency at all — a
    real, tiny, unrelated bug in this exact swift-corelibs-foundation
    checkout (`CFXMLInterface.h` declares 3 functions returning
    `Boolean`, `CFXMLInterface.c` defines them returning `bool`), fixed
    via a second commit in the existing `swift-corelibs-foundation
    .patch` (same category as that patch's first, already-existing
    fix). `tools/userland_staging/build_libxml2.sh` builds real libxml2
    as a per-library dylib (45 real source files, all compiling clean
    with Apple's real pregenerated headers) linked into
    `build_corefoundation.sh`'s dynamic trial link — introduced **zero
    new gaps** there, confirmed via a real trial link before and after.

    Real gaps found and fixed getting the dylib itself to link clean
    (all in `build_libxml2.sh`'s own header comment): disabled
    `LIBXML_HTTP_ENABLED`/`FTP_ENABLED` (real network transport code,
    `nanohttp.c`/`nanoftp.c` — needs `socket`/`connect`/`bind`/
    `getaddrinfo`/etc, the same real gap already documented for
    `CFSocket.c` above) and `LIBXML_ZLIB_ENABLED`/`HAVE_ZLIB_H` (real
    `gzopen`/`gzread`/etc compressed-I/O — this project's vendored
    zlib has no `gzFile` family) by editing the *copied* pregenerated
    headers, not Apple's checked-in files (same pattern
    `build_icu_target.sh`'s own `DATASUBDIR` fix already used); two
    more real `<mach-o/dyld_priv.h>` functions needed adding to the
    existing compat stub (`dyld_get_program_sdk_version` +
    `DYLD_MACOSX_VERSION_10_9`, a real, historically-stable constant
    confirmed against an older real `apple-oss-distributions/dyld` tag
    since this project's own pinned dyld-940 already replaced it);
    `Libc/secure/{memmove,strncat,strncpy,stpncpy}_chk.c` (already-
    established pattern, reused not reinvented); real `labs`/`rand_r`
    (trivial, zero-risk, hand-implemented) and a real from-scratch
    `log10()` (this project has never had any Libm — see
    `cf_compat_headers/libm_compat.c`'s own header for the same `fmod`/
    `pow` gap, reused unmodified here since libxml2's own real call
    sites match those functions' existing documented scope) using a
    real IEEE-754 exponent extraction + atanh-series mantissa
    approximation, verified against real libm to ~1e-10 accuracy with a
    standalone host-side test before being wired into the real build —
    catching and fixing one real, self-inflicted bug along the way
    (brace-initializing a union initializes its first member, not
    whichever member matches the initializer's type; the fix assigns
    `.bits` explicitly instead). `_time`/`_sched_yield` (real syscalls
    with no wrapper anywhere in this project yet) were deliberately
    deferred via the established per-symbol `-Wl,-U,<name>` mechanism
    rather than implemented, matching this port's overall "defer real
    syscall work, don't fake it" boundary. Also caught and fixed a real
    build-script bug of its own: the first dylib link attempt produced
    a "clean" but nearly-empty dylib (~33 symbols, none of libxml2's own
    API) because `-force_load` was missing — without it, ld64's normal
    archive semantics only pull `.a` members needed to satisfy an
    already-outstanding reference, and nothing else in that link
    actually called any real libxml2 function yet. Confirmed fixed via
    `nm -g` showing real exports (`xmlParseFile`, `xmlNewDoc`,
    `xmlDocGetRootElement`, etc, ~2000 total) after adding it.

    **Real syscall work started 2026-08-27 — 27 of the 100 remaining
    gap-groups closed** (100→73), all via real hand-written raw
    syscall/Mach-trap wrappers or verbatim-extracted real Apple
    functions, same established per-consumer-wrapper-file convention
    this project already uses (`ifconfig_extra_syscalls.c`/
    `libresolv_socket_syscalls.c`):
    - **BSD socket family (12 symbols) DONE**:
      `cf_socket_syscalls.c` — `socket`/`bind`/`listen`/`accept`/
      `connect`/`getsockname`/`getsockopt`/`setsockopt`/`socketpair`/
      `recvfrom`/`sendto`/`recv`, real syscall numbers cross-checked
      against `syscalls.master` (using the `_nocancel` trap numbers
      directly for `accept`/`connect`/`recvfrom`/`sendto`, matching
      real Apple's own `libsyscall/wrappers/legacy/*.c`, which do the
      same — this project has no real pthread cancellation to make the
      cancelable variant meaningfully different anyway).
    - **`mach_port_*` family (7 symbols) DONE**: `cf_mach_port_syscalls
      .c` — `mach_port_allocate`/`construct`/`destruct`/
      `extract_member`/`get_attributes`/`insert_member`/`type`,
      extracted verbatim from `libsyscall/mach/mach_port.c` (each
      citing its real origin line) rather than compiling that whole
      file — its other functions (`move_member`/`insert_right`/`guard`/
      `unguard`/`request_notification`/`mach_voucher_extract_attr_recipe`)
      pull in a genuinely different, unneeded MIG subsystem
      (`mach_voucher.defs`). Each function tries a real fast-path Mach
      trap first (`mach_port_traps.s`, 7 real trap numbers cross-checked
      against `syscall_sw.h`, confirmed backed by a real kernel
      implementation in `osfmk/ipc/mach_kernelrpc.c`) and only falls
      back to full MIG RPC on `MACH_SEND_INVALID_DEST` — since the real
      trap always succeeds on this project's real from-source kernel,
      that fallback is dead code in practice, so it's an honest,
      documented stub (`cf_mach_port_mig_fallback_stub.c`, returns
      `KERN_NOT_SUPPORTED`) rather than the real MIG marshaling, which
      would need this project's still-unbuilt real `mach_msg()` (item 7
      below — a real, substantial, already-tracked separate task, not
      something to pull in as a side effect of `mach_port_allocate`).
      Real fix once `mach_msg()` lands: replace the stub with the real
      `mig`-generated `mach_portUser.c` (already proven to compile
      clean against this project's headers during this investigation —
      same real recipe as `build_corefoundation.sh`'s own
      `mach_vm_region` precedent).
    - **`mk_timer_*` family (4 symbols) DONE**: `mk_timer_traps.s` —
      plain Mach traps, no MIG fallback at all (confirmed absent from
      real Apple source), real trap numbers `-91`/`-92`/`-93`/`-94`.
    - **`pthread_attr_*` (4 of 6 symbols) DONE**:
      `cf_pthread_attr_syscalls.c` — `pthread_attr_init`/`destroy`/
      `setdetachstate`/`setscope`, extracted verbatim from
      `libpthread/src/pthread.c` (pure struct-field manipulation, zero
      syscalls, no real dependency on that file's own excluded
      `pthread_create`-stack-allocation header conflict). Needed the
      real internal `pthread_attr_t` struct (named fields like `.sig`/
      `.detached`) from `libpthread/src/internal.h`, not the public
      SDK's opaque one — confirmed the hard way (first attempt using
      plain `<pthread.h>` failed with "no member named 'sig'"), not
      assumed. `pthread_attr_set_qos_class_np`/`qos_class_main` remain
      real gaps: both live in `libpthread/src/qos.c`, already
      documented by `build_libpthread.sh` as unreachable ("needs real
      `bsdthread_register`") — a real, already-diagnosed kernel-
      registration wall, not chased further here.

    **`_proc_pidinfo`/`_pthread_setname_np`/`NXFindBestFatArch`/
    `NXGetLocalArchInfo` CLOSED 2026-08-27** (real twofer + real easy
    win, 4 symbol groups, 73 -> 70 net — see below for the 1 new gap
    this surfaced): `_proc_pidinfo`/`_pthread_setname_np` both needed
    one real `__proc_info` raw syscall wrapper, generated fresh via
    `create-syscalls.pl` against xnu-7195's real `syscalls.master` (same
    recipe `build_libxpc_syscalls.sh` already used for this exact
    symbol) — `cf_libproc_syscalls.c` (real `proc_pidinfo`, extracted
    verbatim from `libsyscall/wrappers/libproc/libproc.c:100-110`) and
    `cf_pthread_setname_syscalls.c` (real `pthread_setname_np`,
    extracted verbatim from `libpthread/src/pthread.c:1130-1153`, same
    "whole file has an unrelated real header conflict, extract just this
    function" pattern as `cf_pthread_attr_syscalls.c`). The latter also
    needed real `_platform_bzero`/`_platform_strlcpy`
    (`third_party/libplatform/src/string/generic/{bzero,strlcpy}.c`),
    adapted rather than compiled as-is: each real file also defines its
    own generic `_platform_memset`/`_platform_memset_pattern4` under the
    same `#if !_PLATFORM_OPTIMIZED_*` this project's arm64 target takes,
    which duplicate-symbol-conflicts with `libmalloc.a`'s already-built
    copies — confirmed via a real trial compile, not assumed. `sys/
    proc_info.h`/`sys/event.h` are deliberately NOT included by either
    new file: pulling them drags in `struct kevent_qos_s`
    (`PRIVATE`-gated, stripped from this project's exported userspace
    headers) for proc_info flavors this extraction doesn't use — real
    Apple's own `pthread.c` takes the same raw-literal-call-number
    shortcut for this exact call, confirmed by reading it, not guessed.
    `NXFindBestFatArch`/`NXGetLocalArchInfo` (CFBundle's fat-binary
    detection) needed a new pin, real `cctools`, NOT part of the
    `macos-112` manifest at all (`cctools` ships with Xcode/the Command
    Line Tools, not the OS distribution — confirmed via a real
    `git show macos-112:.gitmodules`) — pinned instead to
    `cctools-973.0.1`, matched against this project's own pinned Xcode
    12's real `otool --version` output ("Apple Inc. version
    cctools-973.0.1"), not guessed by date proximity. Only
    `libmacho/arch.c` needed, real and unmodified, confirmed by a real
    trial compile to need nothing this project doesn't already have
    (`host_info`/`mach_host_self` from `libmach_ipc.a`,
    `mach_port_deallocate`/`mach_task_self_` from `libmalloc.a`) — zero
    new gaps from this one. See `setup_third_party.sh`'s own header
    comment and `build_corefoundation.sh`'s own per-file comments for
    the full detail.

    **`libSystem.B.dylib` real `-reexport_library` fix DONE 2026-08-27
    — 70 -> 42, the single biggest gap-closing change of this whole
    item d7 effort.** The `__pthread_ptr_munge_token` gap the twofer
    above surfaced turned out to be a symptom of something much bigger:
    `build_libsystem.sh`'s final `libSystem.B.dylib` link passed every
    real per-library sub-dylib (`libsystem_kernel`/`_platform`/`_blocks`/
    `_malloc`/`_pthread`/`_c`.dylib, `libc++.dylib`) as a plain linker
    input, never `-Wl,-reexport_library` — confirmed via a real `nm -gU`
    that `libSystem.B.dylib` exported only its own ~19 locally-compiled
    symbols and re-exported nothing from any sub-dylib at all (not even
    `_strlen`; CF's own dynamic trial link only ever resolved libc
    symbols because it separately links the curated static archives
    directly, alongside — not through — `libSystem.B.dylib`). Real
    macOS's own `libSystem.dylib` is itself exactly this kind of thin
    `LC_REEXPORT_DYLIB` umbrella (this project's own header comment on
    `build_libsystem.sh` already said so, just hadn't been carried
    through to the actual link command after the static-to-dylib
    migration split sub-libraries out). Fix: swap the 7 plain positional
    dylib args for `-Wl,-reexport_library,<path>` each. Verified for
    real, not assumed: a real ld64-609 smoke test first
    (`-reexport_library` correctly makes an umbrella's linker-level
    symbol resolution succeed for a symbol only defined in the
    re-exported sub-dylib, confirmed via a real failing-in-a-different-
    way link, not a guess about semantics), then the actual rebuild —
    `nm -u` on the new `libSystem.B.dylib` is BYTE-IDENTICAL to the old
    one (zero regressions, confirmed via a real diff), `otool -l` shows
    all 7 real `LC_REEXPORT_DYLIB` commands, and CF's own real dynamic
    trial link dropped from 70 to 43 in one rebuild — closing
    `_pthread_atfork`/`_pthread_getugid_np`/`_pthread_is_threaded_np`/
    `_pthread_threadid_np`/`_pthread_mutexattr_destroy`/
    `_pthread_attr_set_qos_class_np`/`_qos_class_main` (the whole
    pthread export-parity bucket at once) plus `_abs`/`_atol`/`_bsearch`/
    `_llabs`/`_mkdir`/`_pwrite`/`_qsort`/`_readlink`/`_setenv`/
    `_setrlimit`/`_strstr`/`_getegid`/`_getrlimit`/`_getpeername`/
    `_uuid_generate_random`/`_uuid_generate_time`/`___vsprintf_chk`/
    `___darwin_check_fd_set_overflow`/`_select$DARWIN_EXTSN`/`_time` — 27
    symbol groups in one real structural fix, confirming the "probably
    closes several export-parity entries at once" prediction. The one
    straggler, `__pthread_ptr_munge_token` itself, needed a second,
    separate real fix: a real trial (adding a genuine cross-object
    reference from `pthread_tsd.o`, then a real `-exported_symbols_list`
    wildcard override) both failed to change ld64's resolution of that
    *specific* common/tentative data symbol to LOCAL inside
    `libsystem_pthread.dylib`'s own link (confirmed via `nm`, still `s`
    not `S` either way) — a real, narrower ld64 default-visibility
    behavior for common symbols resolved entirely within one link unit,
    not an export-list problem. Real Apple never needed this symbol
    exported either (private `pthread.c` implementation detail, never a
    cross-image dependency on real macOS); this project's own new
    `pthread_setname_np` extraction was the first real consumer needing
    it externally. Fix: dropped the `_pthread_validate_signature(self)`
    corruption-check call from `cf_pthread_setname_syscalls.c` (not part
    of the actual rename logic) — same real "no corruption path exists
    on this project's simplified threading model, the defensive check
    that assumes one is safely skippable" pattern already established by
    `pthread_thread_ops_compat.c`. 43 -> 42.

    **`sysdir_start_search_path_enumeration`/
    `sysdir_get_next_search_path_enumeration` CONFIRMED never
    open-sourced 2026-08-27** (not merely "not yet investigated"): real
    `apple-oss-distributions/Libc`'s FULL git history (every tag through
    `Libc-1592.100.35`, not just the pinned `Libc-1439.40.11`) has no
    `gen/sysdir.c` and never has; no standalone `sysdir`/`libsysdir` repo
    exists in the org either (checked via `git ls-remote`); a real GitHub
    code search across the whole `apple-oss-distributions` org for
    `sysdir_start_search_path_enumeration` (via `gh api search/code`,
    not guessed) turns up only real CALLERS (`system_cmds`, `configd`) —
    zero definitions anywhere. Same category as `_asl_*` below: real,
    confirmed, out of scope, not a source-location problem to keep
    chasing.

    **Remaining real gaps, not yet closed (42 groups), roughly
    grouped** — the `libSystem.B.dylib` reexport fix above closed the
    ENTIRE previous "already-documented export-parity" bucket
    (`pthread_atfork`/`pthread_getugid_np`/`pthread_is_threaded_np`/
    `pthread_threadid_np`/`pthread_mutexattr_destroy`/
    `pthread_attr_set_qos_class_np`/`qos_class_main`/`abs`/`atol`/
    `bsearch`/`llabs`/`mkdir`/`pwrite`/`qsort`/`readlink`/`setenv`/
    `setrlimit`/`strstr`/`getegid`/`getrlimit`/`getpeername`/
    `uuid_generate_random`/`uuid_generate_time`/`__vsprintf_chk`/
    `__darwin_check_fd_set_overflow`/`select$DARWIN_EXTSN`/`time` — 27
    symbols, all real functions that were already built somewhere in
    this project's stack and just weren't reachable through
    `libSystem.B.dylib`) — what's left is qualitatively different, genuine
    "not built anywhere yet" or "never open-sourced" gaps, not export
    visibility:
    - `_scalbn`, `_getpwuid_r`, `_getsectbynamefromheader_64`,
      `_getsegbyname`: real functions, but NOT yet confirmed built
      anywhere in this project's curated stack at all (unlike the
      reexport-closed bucket above) — not yet investigated for real
      source location/build status this session.
    - `___chk_assert_no_overlap`/`___chk_fail_overflow`/`___chk_overlap`:
      the real `_FORTIFY_SOURCE`/`_chk`-family hardening routines
      `build_libxpc_syscalls.sh`'s own "STILL OPEN" note already flagged
      as needing "a real `_FORTIFY_SOURCE`-shim source, not yet
      located" — same gap, not yet found.
    - `__dispatch_queue_attr_concurrent`/`__mh_dylib_header`/
      `__os_log_debug_impl`/`__os_log_default`/`__os_log_error_impl`/
      `_os_log_create`/`_os_log_type_enabled`: the still-not-root-caused
      os_log/dispatch gap.
    - `_asl_*` (6 symbols): confirmed never-open-sourced, out of scope.
    - `___CFPreferencesAllocator`/`__CFPreferencesDomain*`/
      `_CFPreferencesCopyValue`/`_kCFPreferences*`/`_kCFLocale*`/
      `_kCFDateFormatter*`/`_kCFGregorianCalendar`: all real
      `CFPreferences.c`-family gaps, already documented above as a
      genuine, verified exclusion (real `gethostuuid`-unavailable-on-iOS
      bug), not re-litigated here.

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
    | `SHA256_Init` via `devfs_mount` | `0x30` | `ccsha256_di` | from-scratch SHA-2 (`corecrypto_sha2.c`), bypasses the table |
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
      existing from-scratch SHA-2/MD5 should be folded in at that point so
      there is one mechanism rather than two.

0b. **[DONE 2026-08-27] `ccsha256_di` wired to REAL corecrypto instead of
    the from-scratch `corecrypto_sha2.c`.** Found 2026-08-26, acted on
    2026-08-27. Turned out `register_crypto_functions()`/`g_crypto_funcs`
    was never the right mechanism for this one (see below) — the real
    fix was smaller: `SHA256_Init`/`_Update`/`_Final`
    (`libkern/crypto/corecrypto_sha2.c`) now call real
    `ccdigest_init`/`_update`/`_final` + `ccsha256_di()` directly, the
    same real corecrypto entry points `osfmk/prng/prng_random.c`'s
    `bootseed_init()` already used successfully. SHA-384/512 deliberately
    left on the from-scratch path (see below, unchanged). QEMU-verified:
    real login, real shell, real `/bin/ls`/`md5` exec (each real Mach-O
    exec runs `cs_hash_sha256` for code-signing), no panic — commit
    `ceb6d419c` in the `xnu-7195` checkout.

    **Correction to the original plan below**: `SHA256_Init` et al were
    never `g_crypto_funcs`-backed in the current tree (only an *earlier*,
    already-superseded revision of this file was) — `cs_hash_sha256`,
    `kern_exec.c`'s cdhash hashing, and `chunklist.c` all call
    `SHA256_Init`/`_Update`/`_Final` directly with zero table indirection.
    So `register_crypto_functions()` wiring, as originally scoped just
    below, would not actually have touched any of those real call sites —
    filed as a real, corrected finding, not a rewrite of the history below
    (kept for the real detective work it documents).

    The SHA256_Init row above says "from-scratch SHA-2, bypasses the table"
    on the assumption (from an earlier pass) that corecrypto's digest-core
    bodies were never open-sourced — that check only looked in
    `EXTERNAL_HEADERS/corecrypto/` (headers only, genuinely empty of
    bodies). Real, unmodified, APSL-licensed corecrypto digest-core source
    is actually sitting one directory over at
    `third_party/xnu-7195/osfmk/corecrypto/`: `ccdigest_init.c`,
    `ccdigest_update.c`, `ccdigest_final_64be.c`, `ccsha256_di.c`,
    `ccsha256_initial_state.c`, `ccsha256_K.c`, `ccsha256_ltc_compress.c`,
    `ccsha256_ltc_di.c` (plus `cchmac*.c`). These are marked `standard` in
    `osfmk/conf/files` (lines 338-354) — **already compiled into this
    exact kernel binary** — and `osfmk/prng/prng_random.c:128` already
    calls the real `ccdigest_init()` from this same file set for the
    kernel's own PRNG. Only a `register_crypto_functions()` boot-time call
    filling in `ccsha256_di` from this real source was ever missing, not
    the whole SHA-256 implementation.

    **Why this matters**: `cs_hash_sha256` (`bsd/kern/ubc_subr.c:185-191`,
    code-signing hash dispatch), cdhash/boot-session-UUID hashing
    (`bsd/kern/kern_exec.c:5607-5612`, runs on every real exec), and
    software-update chunk integrity verification (`bsd/kern/
    chunklist.c:249-257`) all currently run on the hand-rolled
    `corecrypto_sha2.c` instead of Apple's real, audited implementation
    that is already linked into this kernel and one `register_crypto_functions()`
    call away from being live.

    **Real scope for whoever picks this up**: wire `register_crypto_functions()`
    to populate `g_crypto_funcs->ccsha256_di` with the real `ccsha256_di()`
    from `osfmk/corecrypto/ccsha256_di.c`, rebuild, and boot-verify
    (code-signing/exec paths are hit on every boot, so a real QEMU boot
    test is a real, cheap verification here — this is a kernel change on a
    security-relevant path, run the project's adversarial-review-before-
    commit convention on it). SHA-384/SHA-512 real corecrypto bodies do
    **not** exist in this project's primary `xnu-7195` checkout (only in
    the separate, not-currently-built `third_party/xnu-latest`) — leave
    those on the from-scratch path for now, this item is SHA-256 only.
    MD5/SHA-1 real corecrypto bodies are confirmed absent from both
    checkouts (headers only, no bodies) — `corecrypto_md5.c`'s from-scratch
    implementation is a genuine last resort, not part of this item.

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

0. **DWC2 transfer completion is polled, not interrupt-driven** (added
    2026-08-21 with `iokit/BCM2837DWC2/`). `docs/usb-support-plan.md` task 2
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

5. **[RESOLVED 2026-08-20]** ~~No real POSIX regex (`regcomp`/`regexec`/
    `regfree`) built~~ — real, unmodified Apple TRE engine
    (`third_party/Libc/regex/TRE/lib/*.c` + `FreeBSD/regerror.c`) now
    builds clean into `libc_build/libc_regex.a` via the new, permanent
    `tools/userland_staging/build_libc_regex.sh`, and is linked into
    `bash.macho` (`build_bash.sh`), replacing `bash_syscalls.c`'s old
    `REG_BADPAT`/`REG_NOMATCH` stand-ins. QEMU-verified: real bash
    `[[ str =~ pattern ]]` performs genuine matches (both positive and
    negative cases confirmed correct). `rm -i`'s `rpmatch()` was
    deliberately left on its existing narrow C-locale-only replacement
    (`simple_cmds_rpmatch_compat.c`) — that file's own comment already
    establishes it's exactly equivalent real behavior in this project's
    always-C locale, not a degraded approximation, so there was nothing
    to gain by rewiring it. Still open: no `grep`/`sed`/`sort`/`nl`/
    `xargs`/`ed`-style command has been built yet — each is now a normal
    new-command port against a real, linkable engine, not blocked on a
    missing subsystem. See `docs/roadmap.md`'s Phase 4.6 and Current
    status.

6. **[RESOLVED 2026-08-20]** ~~`setattrlist(2)` never wired to
    userland~~ — real raw syscall wired in `tools/userland_staging/
    cp_mv_syscalls.c` (Block 1 of the 2026-08-20 command-sweep session),
    used for real by `cp -p`'s `lchmod`/`lchflags` path and `chmod`'s ACL
    machinery, QEMU-verified. `simple_cmds_lutimes_compat.c`'s
    `touch -h`-on-symlinks `ENOSYS` case could now be closed too (the
    real syscall it needs exists) but hasn't been revisited yet — small,
    low-priority follow-up. See `docs/roadmap.md`'s Phase 4.6 and
    `docs/historic/roadmap.md`'s "Twenty-fifth pass" Block 1 entry.

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

3. **`hexdump` prints only the final byte-count address line, never the
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

4. **`pathchk ""` (empty pathname) exits 0 instead of POSIX's specified
    "shall report an error"** -- real, unmodified upstream FreeBSD source,
    found during the same session's Block 3. Low practical impact, not
    investigated further.

5. **`rs -T` (transpose) dropped the first row of a small piped 2x3
    matrix in one observed test** (`printf '1 2 3\n4 5 6\n' | rs -T`
    printed only `4\n5\n6\n`) -- found during the same session's Block 2.
    Not confirmed as a port bug vs. a genuine regression: real upstream
    `rs`'s array-dimension auto-guessing heuristics, active whenever
    neither `-g` nor `-c` is given, are documented as sensitive to
    small/irregular input shapes. Not investigated further given `rs`'s
    low practical value.

6. **`ls -e`'s real ACL display (`acl_get_link_np()`) is an honest
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

7. **`date`'s network time-sync (`netsettime()`, `-n`-less clock-set)
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

8. **[FIXED 2026-08-21]** `tools/make_sd_image.sh`'s `SIZE_MB` default
    is now `1200` (was `500`, silently below the 1024 MiB SDHC/SDSC
    threshold -- see below for the real mechanism). Also added a real
    fail-fast check: the script now exits with a clear error if invoked
    with `SIZE_MB` at or below 1024, instead of silently building an
    image `BCM2837SDHCI` refuses much later at card-init time. 1200 (not
    the originally-considered 4096) deliberately keeps a wide safety
    margin above 1024 while minimizing attach/partition/detach time on
    every test iteration -- picked once the exact QEMU-side mechanism
    was confirmed (see below), not guessed.

    Original finding, kept for context: found while QEMU-testing the
    `ls`/`find`/`date`/`gzip` session: `qemu/hfs_root_test.img`
    regenerated at the buggy 500 MB default puts QEMU's emulated SD card
    into SDSC (CSD v1) mode -- confirmed exactly why by reading
    `third_party/qemu-zhuowei/hw/sd/sd.c`: `if (sd->size > 1 * GiB)` is
    literally what selects SDHC vs SDSC in the emulated card, based on
    the backing image file's own size, not any property of a real
    card. `BCM2837SDHCI` (`iokit/BCM2837SDHCI/BCM2837SDHCI.cpp`, ~line
    590-605) explicitly refuses SDSC cards outright (never implemented
    that CSD version's older capacity formula) -- exercised live as
    `no card identified at start()`, boot hangs forever at "Still
    waiting for root device". A real, reproducible, silent-failure-mode
    bug (produced a normal-looking image that only failed much later, at
    root-mount time) -- the new fail-fast check closes that class of
    mistake going forward, not just this one instance of it.

9. **`tools/init_binary/inject_into_sd_image.sh`'s `for cmd in ...` list
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

10. **This project's kext personalities never wire up
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

11. **[RESOLVED 2026-08-21, one real scope limit remains]** ~~`less`/
    `more` interactive behavior only partially verified~~ — a follow-up
    one-off timing-based pty test (RETURN/space/`q` sent on a fixed
    schedule, not depending on the shared harness's prompt-string
    matching, since `less`'s own real `(press RETURN)` prompt isn't in
    its `PROMPT_SUFFIXES` list) confirmed: RETURN correctly advances
    past the hardcopy-mode prompt and prints the file header, and `q`
    correctly exits back to the shell with a real exit code 0. `less`/
    `more`'s launch → degraded-mode-detection → quit lifecycle is real
    and QEMU-verified end to end. **Remaining real, honest scope limit,
    not a bug**: this project has no real termcap/terminfo *database*
    (only the proven "always report no entry" stub), so `less` always
    runs in real hardcopy mode on this target — never true full-screen
    cursor-addressing paging, and a genuine page-by-page scroll-through
    of multi-screen content was not separately exercised (only single
    RETURN + quit). Porting a real terminal-capability database is a
    distinct, larger, not-yet-scoped follow-up if full-screen paging is
    ever needed.

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
