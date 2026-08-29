Resolved items moved to `docs/historic/backlog.md` — see there for fix history.

# Backlog

Open items found but not yet fixed, mostly surfaced by two adversarial
code reviews on 2026-08-13 (one on `BCM2837SDHCI`/the boot chain, one on
the rest of the custom IOKit kexts) plus a few longer-standing known gaps.
Organized by how much it matters, not by discovery order. File:line
references are as of commit `d472f37`; re-check before trusting them if
this list is stale (this pointer itself is only updated when someone
actually re-verifies line numbers, not on every commit — treat it as "not
older than" rather than "current as of").

---

## P0 — real-hardware risk (would plausibly hang, storm, or panic a real Pi)

1. **[FIXED 2026-08-27, QEMU-verified]** `BCM2837SDHCI::doAsyncReadWrite()`
   had no serialization at all — no `IOCommandGate`, no workloop, no
   mutex. Two concurrent requests (plausible now that the SD image has
   two mounted partitions) could interleave register writes and corrupt
   `fPendingIntStatus`. Fixed with `fTransferLock` (a plain `IOLock`) held
   for the whole command-issue-through-completion span — sufficient
   because `doAsyncReadWrite()` is already synchronous under the hood
   (blocks on `waitForIntStatus32()` rather than completing from
   interrupt context). **Verification caveat**: nothing in this project
   currently issues genuinely concurrent I/O, so the QEMU boot test
   confirms no regression, not that the actual interleaving race is
   exercised. Full writeup (exact reasoning, `fTransferLock` placement)
   archived to `docs/historic/backlog.md`.

2. **[FIXED 2026-08-27, QEMU-verified]** No error recovery anywhere in
   `BCM2837SDHCI` — every error path was `IOLog(); return false;`. Fixed
   with `resetCmdLine()`/`resetDatLine()` (SDHCI spec 3.7.1
   `SOFTWARE_RESET_CMD`/`_DATA`, matching Linux's `sdhci_reset()` split),
   wired into every real error/timeout path in `sendCommand()`/
   `readBlocksPIO()`/`writeBlocksPIO()`. **Verification caveat**: QEMU's
   SD model still doesn't inject real CRC/timeout/card-removal errors, so
   the boot test confirms the reset paths compile and don't regress a
   clean boot, not that they trigger correctly under a real error. Full
   writeup archived to `docs/historic/backlog.md`.

3. **NOT FIXED — deliberately, after real investigation, not just
   deferred.** `BCM2837SDHCI`'s SD clock still never leaves ~490 kHz /
   1-bit; `setClock()` still always writes divisor `0xFF` (re-confirmed
   unchanged), and `kRegHostControl` is still never written. Investigated
   whether a real, low-risk fix was possible this pass (per this backlog
   entry's own instructions: write one if "the right fix is clear and
   low-risk", defer with a status note otherwise) and concluded it is
   **not** clear/low-risk here, so no code was changed: computing a
   correct data-phase divisor requires knowing the controller's real base
   clock frequency, and this SoC is `missing_caps=true` (the
   `CAPABILITIES` register's base-clock field is explicitly untrustworthy
   per the Linux `sdhci-iproc` quirk this driver already documents) — the
   real base clock is set by VideoCore firmware at boot and can vary by
   board/firmware config (commonly cited real-hardware values range
   ~41-50 MHz depending on `core_freq`), and this project has no
   VC-mailbox driver reachable from this kext to query it for real (see
   `tools/sdhci_baremetal/README.md`'s Finding, and backlog item 8's
   identical gap for `SMSC9514Ethernet`'s MAC address). Hardcoding a
   guessed base clock and computing a divisor from it risks programming a
   data-phase clock *faster* than the card/bus can actually handle on
   some real boards — active data corruption, strictly worse than today's
   always-conservative divisor `0xFF`, and exactly the kind of fabricated,
   unverifiable-from-QEMU guess this project's conventions exist to
   avoid. Same reasoning blocks widening the bus via `kRegHostControl`
   without also negotiating `ACMD6` with the card first (untested
   protocol addition, same class of real-hardware-only risk). Real fix
   needs a real VC-mailbox "get clock rate" query (or a bootloader-stashed
   DT `clock-frequency` property) wired to this driver first — not
   attempted, scoped here for whoever picks up the mailbox work.

4. **[FIXED 2026-08-27, QEMU-verified]** No power-on ramp / 74-clock delay
   before `CMD0`, and power was enabled *after* the clock in `start()`
   rather than before. Fixed by reordering `start()` to assert
   `kRegPowerControl` before `setClock()`, plus real `IODelay()`s for the
   ≥1ms power-on ramp (spec 6.4.1) and ≥74-clock identification delay.
   Only changes ordering/idle delays, not signal rate/timing, so unlike
   item 3 there's no way a wrong guess here makes things worse on real
   hardware. QEMU has no power/clock-dependent reset behavior to catch
   the old ordering being wrong, so this is verified only as "boots
   clean, no regression." Full writeup archived to
   `docs/historic/backlog.md`.

5. **[FIXED 2026-08-27, code correct but genuinely unverifiable from
   QEMU — exactly as this item originally predicted.]** No memory barrier
   in `BCM2837InterruptController::enableVector()`/`disableVectorHard()`
   before the next `IRQ_PENDING` read. Fixed with
   `__asm__ volatile ("dsb sy" ::: "memory")` after each register write,
   matching the barrier style `BCM2837DWC2.cpp` already uses. Genuinely
   unverifiable from QEMU/TCG (all MMIO is strongly ordered there
   regardless of the barrier's presence) — the boot test only confirms
   the added instruction doesn't itself break anything. Full writeup
   (including a correction to this item's own original "no barriers
   anywhere" claim) archived to `docs/historic/backlog.md`.

6. **Re-verified, not re-tested (inherently real-hardware-only).** GPIO
    pinmux for the SD slot is still only ever exercised in QEMU, where
    it remains a documented no-op (`tools/sdhci_baremetal/README.md`'s
    own from-scratch bare-metal harness independently confirms this
    QEMU model parents the card onto `sdhci` by default regardless of
    GPFSEL). Re-read `pinmuxSDSlot()` (`iokit/BCM2837SDHCI/BCM2837SDHCI.cpp`)
    end to end: the GPIO34-39-to-input fix for the WiFi-collision bug is
    still in place, unchanged, and the reasoning still holds (GPFSEL3
    bits for GPIO34-39 set to input `0b000` before GPFSEL4/5 claim
    GPIO48-53 as ALT3). Still genuinely untested on real silicon — no
    QEMU boot can close this, confirmed again this pass rather than
    assumed.

**On items 1, 2, 4, 5 above**: all four were rebuilt together
(`tools/build_kexts.sh` clean compile, then a full `tools/
build_xnu_7195.sh` kernel rebuild) and verified with one real QEMU boot
(`tools/boot_test.py --login --cmd 'ls /' --cmd 'cat /etc/hosts' --cmd
'ls /System'`, `--expect-fail 'panic\('`) — real boot, real login, real
reads against both mounted partitions (`disk0s1` root / `disk0s2` boot),
zero panics, `BCM2837SDHCI` log confirms a real hardware interrupt still
observed and the card still identifies correctly (RCA=0x4567, SDHC,
1200MB). This confirms no regression from any of the four changes
together; it does not and cannot exercise the specific fault conditions
items 1/2/5 exist to guard against (concurrent I/O, a real card/bus
error, or a real MMIO-ordering violation), since QEMU reproduces none of
those - noted individually above rather than glossed over.

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

    **UPDATE 2026-08-27: does NOT currently reproduce, after real,
    sustained re-verification — not closing this out, but downgrading
    the "reliably panics within seconds" claim, which is now stale.**
    Per this backlog's own "verify claims against source, not by date/
    name proximity" rule, re-ran the exact real repro (`yes.macho`, real
    unmodified source, at the console) against the CURRENT kernel build
    rather than assuming the 2026-08-16 finding still holds — it does
    not, at least not under this same QEMU config:
    - First re-derived the current handler address the same honest way
      this item asks for: `nm build/xnu-latest-build/out/
      kernel.development.bcm2837-7195.unstripped | grep _rtclock_intr`
      → `0xfffffff007335230` (shifted from the original
      `0x...732d170`, exactly as expected — confirms the address in
      this item's own text was already stale before this pass, matching
      the note right above this update).
    - `yes | head -c 2000000 > /dev/null` (real pipe, output never
      touches the console) ran clean, no panic — first real signal that
      console I/O specifically, not just high syscall/trap rate alone,
      is implicated (matches the original finding's own handler-address
      evidence pointing at `rtclock_intr`, but narrows what actually
      drives it).
    - `yes` run bare at the real console (exact original repro shape)
      for **100 real seconds**: no panic, `[boot_test] PASS`.
    - Added a real, cheap TEMP DIAGNOSTIC to `third_party/xnu-7195/
      osfmk/kern/timer_call.c`'s `timer_queue_expire_with_options()`
      (kept in place per this project's existing convention of leaving
      diagnostic instrumentation rather than reverting it — see this
      file's own P3#3 entry) to test the leading hypothesis directly:
      an unbounded/growing number of `timer_call` entries piling up on
      the per-CPU rtclock queue (each real, legitimate, but summing to
      hundreds of ms if there are enough of them) rather than genuinely
      unbounded per-entry cost — logs `queue->count` and the loop's real
      iteration count whenever either exceeds 32 in one call. Rebuilt
      the kernel with this in place and re-ran `yes` bare at the console
      for **210 real seconds** (25.5MB of real console output, `[boot_test]
      PASS`) — the diagnostic **never fired once**: the per-CPU timer
      queue never exceeded 32 pending entries at any point in 3.5
      minutes of the exact workload that used to panic within seconds.
      `BCM2837SDHCI` still shows a real hardware interrupt and a correctly
      identified card partway through this same boot, confirming items
      1/2/4/5's fixes above are intact in this build and this is a real,
      unmodified boot, not a degraded one.
    - **Working hypothesis for why, not fully confirmed**: this
      kernel has changed substantially since 2026-08-16 — most notably
      `xnu-7195` commit `d7c6934d8` ("Switch root from mockfs to real
      msdosfs, **fix console I/O plumbing for pid 1**"), which landed
      afterward for unrelated Phase 5 reasons but, per its own commit
      message, touched exactly the console I/O path this item's own
      panic evidence pointed at (`rtclock_intr` handler address, high
      syscall rate from console writes) before ever pointing at a
      confirmed root cause inside `rtclock_intr`/`timer_intr` itself.
      Also relevant: the original finding predates Phase 6b (INET
      enabled 2026-08-21) entirely, so today's per-CPU timer queue
      actually carries *more* background load (network timers) than it
      did during the original repro, yet still never panics — evidence
      against "more timer load" being the driver, consistent with the
      bug instead being specific to whatever the console I/O plumbing
      fix changed.
    - **Not claiming root-caused or intentionally fixed** — no code
      change was made to actually address this item (the TEMP
      DIAGNOSTIC is observation-only), and it's possible a different
      workload, a longer run, or real hardware's own timing could still
      trigger it. Downgrading from "reliably panics within seconds" to
      "not reproducible in 210 real QEMU seconds against the current
      build, despite a real, honest attempt" — a meaningfully different,
      weaker claim than this item's original text, which is why this
      update doesn't just delete the original finding. Whoever revisits
      this should re-run the exact repro above first before assuming
      either "still broken" or "fixed" — both the original finding and
      this update were real, evidenced observations at their respective
      times.

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

3. **Six `g_crypto_funcs->` call sites have zero null-checking at all**
   (filed 2026-08-28, found while closing `docs/backlog.md` item 0a's AES
   gate): `libkern/crypto/corecrypto_des.c`, `corecrypto_rsa.c`,
   `corecrypto_aesxts.c`, `corecrypto_chacha20poly1305.c`,
   `bsd/nfs/gss/ccrypto.c`, and `bsd/netinet/flow_divert.c` all
   dereference `g_crypto_funcs->somefield` directly — not even a bare
   `if (g_crypto_funcs)` guard, let alone a check of the specific field
   — because they were written assuming real macOS's corecrypto kext
   always fills every field atomically. Now that `g_crypto_funcs` is a
   real, non-NULL, *partially*-filled table (AES ecb/cbc/ctr only, see
   item 0a), every one of these is a live NULL-pointer function call the
   instant anything reaches DES, RSA, AES-XTS, ChaCha20-Poly1305, NFS
   GSS-API crypto, or flow-divert's HMAC-SHA1 token signing — the exact
   same crash class (`Kernel instruction fetch abort at pc 0x0`) already
   hit once and fixed in `corecrypto_rand.c`'s `cc_rand_generate()`
   during this same work (that one *did* have a bare
   `if (g_crypto_funcs)` guard, just not a field-specific one — these
   six don't even have that much).

   Not currently reached by anything exercised through a full QEMU boot
   to login (verified), so this is latent, not currently crashing — but
   it is a real, load-bearing landmine for whoever next touches DES/RSA/
   AES-XTS/ChaCha20-Poly1305/NFS-GSS/flow-divert. Fix is mechanical
   either way: add a field-specific null check (`corecrypto_aes.c`'s
   `aes_encrypt_key()` is the reference pattern) before wiring up real
   functionality that would exercise the call site for the first time,
   the same guard `corecrypto_rand.c` needed once its own gap got found.

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
    - `CFPreferences.c`/`CFXMLPreferencesDomain.c`: **CLOSED 2026-08-28,
      stale below** — both now compile and link for real (three real
      `TARGET_OS_OSX`/`TARGET_OS_MAC` guard mismatches, not the vaguer
      "gethostuuid unavailable" framing this entry used to have; full
      writeup in `docs/daemon-roadmap.md`'s "Milestone 2 CLOSED,
      2026-08-28" section). Whether `CFPreferencesCopyAppValue` et al are
      *behaviorally* correct against a real flat preferences file on disk
      is a separate, not-yet-QEMU-verified question this pass didn't
      address — but the exclusion itself (this bullet's original claim)
      no longer holds.
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

d8. **Every curated support library built since the real per-library-dylib
    convention was announced (`CLAUDE.md`'s "New project convention going
    forward: prefer real, per-library `.dylib`s over new static
    archives", Phase 6.5) has still shipped as a plain `.a` static
    archive, not a `.dylib` — a real, growing gap between stated
    convention and actual practice, not yet reconciled.** Confirmed via
    `ls tools/userland_staging/build_*.sh` + `file libc_build/*.a`:
    `build_libm.sh` (`libm.a`), `build_libnotify_client.sh`
    (`libnotify_client.a`), `build_libresolv.sh` (`libresolv.a`),
    `build_openpam.sh` (`libopenpam.a`), and `build_bsm.sh` (`libbsm.a`,
    2026-08-27/28) were all written and built *after* the convention
    landed, and all five chose `.a` anyway — each one's own header
    comment documents the same real reasoning (a curated support library
    linked into this project's still-dominant Csu-static `-nostdlib
    -static` consumers has no real dyld-based consumer to justify the
    extra dylib-linking complexity yet, unlike `libdyld.dylib`/
    `libsystem_*.dylib`, which exist specifically because real dyld
    structurally requires them). `libbsm.a` is the sharpest case: real
    Apple's own `bsm/lib/Makefile` *already* targets a dylib
    (`libbsm.dylib`, `-dynamiclib -install_name /usr/lib/libbsm.dylib`)
    — this project deliberately chose `.a` anyway, for consistency with
    the other four, not because the real upstream build system does.

    Not a correctness bug (every one of these `.a`s builds clean, links
    clean, and passes its own real smoketest) — filed because the
    convention exists precisely to stop this exact pattern from
    compounding (see `docs/daemon-roadmap.md`'s "Static-vs-dylib
    architecture debt" section for the same concern at the
    `libSystem.B.dylib`-flattening scale), and five real, independent
    instances of "wrote a new `.a` anyway" since the convention was
    announced is a real signal the convention isn't being followed by
    default, not just an occasional deliberate exception. Real follow-up,
    not attempted here: either (a) revisit each of these five as a real
    `.dylib` once/if a real dyld-loaded consumer actually needs one
    (lowest-risk — matches `build_libdyld.sh`'s own "link against real
    sibling dylibs, not their static ancestors" pattern, see item d1
    above for why that matters for correctness, not just style), or (b)
    make an explicit, documented call that `.a` is in fact still correct
    for small curated-support libraries with no standalone runtime role,
    and narrow the convention's own wording in `CLAUDE.md` to say so —
    but don't leave the stated convention and the actual practice
    silently diverging the way they are today.

    **RESOLVED 2026-08-28 — explicit user directive: option (a), no
    narrowing of the convention.** Real dyld-loaded consumers now exist
    for real (item d9 below: all 76 `build_simple_cmds.sh` commands,
    plus this project's real per-library `libsystem_*.dylib`/
    `libdispatch.dylib`/`libc++.dylib`/`libunwind.dylib`/`libxml2.dylib`
    conversions, plus this same day's launchd trial link now linking
    real dylibs throughout) — the "no real dyld-loaded consumer to
    justify it yet" reasoning every `.a`-choosing script above cited is
    no longer true project-wide, even for scripts that individually
    predate a consumer of their own. Standing rule going forward, not
    just for the five libraries above: **do not add a new static archive
    for anything that is a real, standalone, nameable library** (as
    opposed to a per-consumer curated-extraction bundle like
    `launchd_libc_extras.a`/`cf_libc_extras.a`/`syslogd_libc_extras.a` —
    those stay `.a` by design, they're not independent libraries) —
    build a real `.dylib` instead, even if today's only consumer is
    still static (mirrors `tools/init_binary/build.py`'s own real,
    narrow exception: PID-1 and `pwtool` stay fully static because they
    run before/without dyld by design — everything else doesn't get that
    exemption just because it hasn't been asked yet).

    **Full current static-archive inventory** (`ls libc_build/*.a` +
    `ls libc_build/system/*.dylib`, re-derived 2026-08-28, not copied
    from the five-library list above — that list is now a subset of
    this one):
    - **Already real dylibs, `.a` retired/superseded** (confirmed by
      reading each dylib's own build script, not guessed): libc_gen/
      libc_string/libc_stdio/libc_printf/libc_locale_for_stdio/
      libc_regex → bundled into `libsystem_c.dylib`; libcxx/libcxxabi/
      libc_locale (selectively) → `libc++.dylib`; libmalloc →
      `libsystem_malloc.dylib`; libplatform → `libsystem_platform.dylib`;
      libpthread → `libsystem_pthread.dylib`; libunwind →
      `libunwind.dylib`; libxml2 → `libxml2.dylib`. `libclosure.a` is a
      **partial** case: `libsystem_blocks.dylib` already builds the real
      Blocks runtime from source independently (confirmed via
      `build_libsystem_blocks.sh`), but `libclosure.a` itself is still a
      live, real dependency of `build_launchd_core.sh`'s own static-ish
      trial-link line — retire the `.a` only once that trial link (or
      its eventual real `build_launchd.sh`) links `libsystem_blocks.dylib`
      directly instead.
    - **Real, standalone libraries still static, genuine conversion
      candidates** (each has a real Apple `.dylib` counterpart on actual
      macOS, confirmed for the starred ones by reading that library's own
      real upstream `Makefile`/build config, not assumed for the rest):
      `libCoreFoundation.a`* (Phase 9, by far the largest and riskiest —
      real load-time global state/constructors, scope its own session,
      see `docs/daemon-roadmap.md`'s "Static-vs-dylib architecture debt"),
      `libbsm.a`* (real `bsm/lib/Makefile` already targets
      `libbsm.dylib`, see this item's own note above), `libresolv.a`,
      `libopenpam.a` (real PAM modules are themselves per-module
      dylib-shaped on macOS, not just the base library), `libnotify.a`/
      `libnotify_client.a`, `libncurses.a`, `libiconv.a`,
      `libcopyfile.a`, `libremovefile.a`, `libm.a`, `libxpc.a`/
      `libxpc_syscalls.a`/`libxpc_syscalls2.a` (real `libxpc.dylib`),
      `libiokituser.a` (real `IOKit.framework`, not a plain dylib, but
      the same "should be independently loadable" concern applies).
    - **Not real conversion candidates, stay `.a` on purpose** (this
      project's own internal glue, not independent Apple libraries):
      `cf_libc_extras.a`, `launchd_libc_extras.a`, `syslogd_libc_extras.a`,
      `libcxx_locale_for_stdio_scratch.a` (per-consumer curated-extraction
      bundles); `liblaunchd_core.a`/`liblaunchd_mig.a`/
      `liblaunchd_syscalls.a`/`libsyslogd.a` (per-daemon object archives
      feeding a real executable link, not a runtime-loadable library);
      `libmach_ipc.a` (this project's own MIG-regen compat layer, no real
      Apple equivalent to match).
    - **Possibly dead, not yet confirmed**: `libdispatch_min.a`/
      `libdispatch_real.a` — likely superseded now that
      `libdispatch.dylib` is real (Phase 6.6); check for remaining real
      consumers before deleting, don't assume. **Still open as of
      2026-08-29** — not investigated this pass either, carried forward.

    **RESOLVED 2026-08-29 — all 11 "genuine conversion candidate"
    archives above converted to real dylibs, and every real consumer
    relinked (not just left as unused parallel output).** User paused
    the launchd SIGSEGV investigation (bug #6, session 2) specifically
    to run this pass. `libCoreFoundation.a` deliberately still deferred
    (explicit user decision, own future session — unchanged from this
    item's own original reasoning). `libiokituser.a` converted to
    `libiokituser.dylib` (a plain dylib, not a `.framework` bundle — this
    project has no bundle/Info.plist packaging pipeline for kexts either,
    same established precedent).
    - Real per-library dylibs: `libresolv.dylib`, `libsystem_notify.dylib`
      (replaces libnotify.a + libnotify_client.a — real Apple ships these
      as one product), `libbsm.dylib`, `libncurses.dylib`,
      `libcopyfile.dylib`, `libremovefile.dylib`, `libiconv.dylib`,
      `libsystem_m.dylib` (real Apple name for what was libm.a),
      `libopenpam.dylib` (base library only — real per-module PAM dylibs
      need `dlopen`, which this project doesn't have; same documented gap
      class as OpenPAM's own module loader elsewhere in this project, not
      attempted), `libxpc.dylib` (replaces libxpc.a + libxpc_syscalls.a +
      libxpc_syscalls2.a as one real dylib), `libiokituser.dylib`.
    - Consumers relinked away from the static archives: `build_launchd.sh`
      (libbsm/libxpc — the launchd SIGSEGV diagnostics from the paused
      session were left completely untouched/uncommitted throughout),
      `build_syslogd.sh`, `build_launchctl.sh`, `build_simple_cmds.sh`
      (all 76 commands, re-verified 76/0 no regression), `build_corefoundation.sh`
      (non-CF library refs only — CF itself stays static per the scope
      decision above). `build_dyld_link.sh` deliberately left alone — a
      real, legitimate exception (dyld itself can't depend on the real
      dylibs it's the thing that loads), same class as `tools/init_binary`/
      `pwtool`.
    - Real duplicate-global-state bugs closed at the architectural root
      (same bug class as `mach_task_self_`/`g_zone`, not hypothetical):
      `build_libdyld.sh`/`build_libsystem.sh` now `-Wl,-reexport_library`
      (not statically bundle) `libresolv.dylib`/`libcopyfile.dylib`/
      `libremovefile.dylib`, closing three independently-uninitialized
      private copies of `_res` down to one shared copy.
    - Two new real gaps found+closed the same class of investigation this
      whole migration already established: `mach_msg()`/`mach_msg_trap()`
      now live for real in `libsystem_kernel.dylib` (previously bundled
      separately into launchd.macho's static portion AND
      libdispatch.dylib), and a real `libsystem_pthread_dylib_bootstrap_compat.c`
      constructor closes the same "nothing calls
      `pthread_main_thread_bootstrap()` for a real dyld-linked consumer"
      gap at the `libsystem_pthread.dylib` level that
      `libsystem_dyld_initializer_compat.c` already closed once for
      `libSystem.B.dylib`.
    - **Real, deliberately deferred residual, not silently dropped**:
      `build_libsystem_c.sh` still does narrow, curated `ar x` object
      extraction (not full-archive bundling) from `libm.a`/
      `libremovefile.a`/`libopenpam.a` for a handful of specific symbols
      ICU's own dylibs expect from "libSystem" (`acos`/`asin`/`sqrt`/
      `fmod`/`qsort`/`heapsort`/`time`/`gettimeofday`/etc). Lower risk
      than the `_res`-class bug above (mostly stateless math/qsort/time
      functions, not mutable process-global state) — and genuinely
      structurally different: `libsystem_m.dylib`/`libremovefile.dylib`/
      `libopenpam.dylib` all link AGAINST `libsystem_c.dylib`, so
      `libsystem_c.dylib` linking them back would be a real circular
      build-order dependency, not just a style choice. Real follow-up,
      not attempted here: reexport `libsystem_m.dylib`/`libopenpam.dylib`
      through `libSystem.B.dylib` (matching the umbrella-reexport pattern
      that already made ICU's `libresolv`/`libcopyfile`/`libremovefile`
      needs resolve without embedding), then drop the curated extraction
      entirely.

d9. **`build_simple_cmds.sh` converted from fully-static to real dynamic
    linking (2026-08-28) — RESOLVED: 76 of 76 commands now build, the
    ld64-609 crash below fixed for real.** See this item's own trailing
    "RESOLVED 2026-08-28" paragraph for the fix; the rest of this item
    (kept verbatim) is the real bisection trail that root-caused it.
    This is the first real
    dyld-loaded consumer d8 above was waiting for, and confirms d8's own
    premise: converting a real static consumer to real dynamic linking
    against `libSystem.B.dylib`/`libdyld.dylib` eliminates the entire
    `-Wl,-alias` workaround class this file used to need (the ~60-entry
    `LINK_ALIASES`/`LINK_ALIASES_TAR` saga, and the real ld64-609
    "Assertion failed: (target != NULL)" crash in `generateLinkEditInfo`
    that saga existed to work around) — `libSystem.B.dylib` already
    bakes the same real UNIX2003/INODE64/DARWIN_EXTSN aliases into its
    own build (`build_libsystem.sh`'s own tail), so a real dynamic
    consumer needs zero alias flags of its own for anything whose real
    base is locally resolvable at THAT dylib's own link time.

    Real, load-bearing finding from this conversion, not previously
    documented anywhere: **`-Wl,-alias` inside a dylib only lands in
    that dylib's real `LC_DYLD_EXPORTS_TRIE`** (confirmed via
    `xcrun dyld_info -exports`, **not** `nm` — `nm`'s plain symbol-table
    listing shows an alias-created indirect symbol regardless of whether
    it actually made the trie, which is actively misleading) **when the
    alias's BASE symbol is genuinely local to that same link.** Adding
    an alias for e.g. `_fputs$UNIX2003`/`_strerror$UNIX2003` to
    `libSystem.B.dylib`'s own build — where those bases are only
    reachable via `-reexport_library`, not local — silently produces an
    alias no real consumer can ever resolve (a real trial link fails
    with "symbol not found" despite `nm -gU` claiming the alias exists).
    The fix used throughout this conversion: add each alias at the
    specific sub-dylib (`build_libsystem_c.sh`, `build_libsystem.sh`,
    etc) where its base is genuinely local, never at the umbrella
    `libSystem.B.dylib` level for a reexported base. A real runtime
    regression from exactly this class of bug was caught live in QEMU
    (2026-08-28): `cat.macho` linked clean but crashed at real dyld load
    time (`dyld[3]: Symbol not found: _fdopen$UNIX2003`) because the
    private test SD image's `/usr/lib/system/libsystem_c.dylib` was
    stale — `tools/init_binary/inject_into_sd_image.sh` must be re-run
    after every `build_libsystem_c.sh`/`build_libsystem.sh` rebuild, not
    just after `build_simple_cmds.sh`, or booted binaries will resolve
    dylib imports against old exports. A second, related finding: **any
    sub-dylib whose real, non-aliased symbols a consumer's own compiled
    code calls directly must also be linked directly on that consumer's
    own command line** (`libsystem_pthread.dylib` for
    `pthread_main_thread_bootstrap()`, `libsystem_c.dylib` for the bulk
    of libc, `libsystem_malloc.dylib` for the private
    `_pthread_setspecific`/`_pthread_getspecific`/`_pthread_once`
    variants real libmalloc's own TSD-cache path needs) — ld64 does not
    resolve a multi-level reexport chain through `libSystem.B.dylib`
    alone for this toolchain, regardless of whether the symbol is
    alias-derived or a real, directly-defined export.

    **The 32 blocked commands** (`cp`, `mv`, `ls`, `grep`, `tar`, `diff`,
    `gzip`, `less`, `find`, `date`, `du`, `stat`, `chmod`, `chown`,
    `sed`, `xargs`, `nice`, `printf`, `echo`, `wc`, `cut`, `vis`, `rs`,
    `mknod`, `expand`, `unexpand`, `fold`, `hexdump`, `jot`, `nl`,
    `tail`, `touch`) all need real locale support this project's dropped
    `libc_locale_for_stdio.a` used to provide — at minimum
    `__locale_key`/`__global_locale` (real `ctype.h` xlocale macros;
    `libc_locale_globals_compat.c` already provides a narrow real
    reproduction of these two globals, same file used by
    `build_libdispatch_dylib.sh`), and for about 20 of them, deeper real
    `LC_TIME`/`LC_COLLATE` support (`ls`'s date formatting; `grep`/
    `find`/`diff`'s `[a-z]`-range `fnmatch()` matching). The real
    blocker: `libc_locale_globals_compat.c`'s `__global_locale` needs
    `&_DefaultRuneXLocale` — real `third_party/Libc/locale/FreeBSD/
    table.c`'s big built-in "C"-locale character-classification table —
    and linking that table into ANY of these 32 commands' already-
    substantial per-command object set reproduces a real, narrow
    ld64-609 internal crash: `Assertion failed: (_mode ==
    modeFinalAddress), function finalAddress` (a different assertion
    from the `generateLinkEditInfo` one d8/the alias saga hit — this one
    fires deep in `OutputFile::applyFixUps`, under `-Wl,-fixup_chains`
    specifically). Extensively, but so far unsuccessfully, bisected
    (2026-08-28) — real approaches tried, in order, each with a real,
    specific reason it didn't work:
    - Removing the one confirmed real cross-object static relocation
      (`__global_locale`'s static initializer took `&_DefaultRuneXLocale`
      directly; moved to a runtime constructor assignment instead) — no
      change, still crashes.
    - Source-level merging of `table.c`+`none.c`+`ldpart.c`+`collate.c`+
      `collcmp.c`+`findfp.c`+`getcwd.c` into one translation unit via
      `#include` (fewer separate link-time objects) — crashes MORE, not
      less (even locale-light commands started crashing once every
      command shared the same larger bundle).
    - Hand-copying the same real function bodies directly into `table.c`
      instead of `#include`-merging (avoids per-file `__FBSDID`/
      `sccsid`/license-comment overhead) — no reliable improvement over
      `#include`-merging at the same content size.
    - A real `ld -r` partial-link merge of the separately-compiled
      objects into one — still crashes, ruling out "too many separate
      *objects*" as the mechanism (it's total complexity/fixup count
      regardless of object boundaries).
    - Swapping `none.o` for an unrelated third object (`findfp.o`) at
      the same `{libc_locale_globals_compat, table}` pair — still
      crashes, ruling out anything `none.c`-specific.
    - Dropping the by-far-largest file (`collate.c`, 1046 real lines) in
      favor of just `collcmp.c`'s own tiny real `__collate_range_cmp()`
      (which only needs real `wcscoll_l()`, already a direct
      `libsystem_c.dylib` export, confirmed via `nm -gU`) — reduced but
      did not eliminate the crash.
    - **Results were not even consistently reproducible between a clean
      rebuild and an incremental one with identical source** — the same
      `{libc_locale_globals_compat, table}` pair that built 0/32 clean
      in one run reproduced 12/32 crashes in a supposedly-identical
      clean rebuild immediately after. This is the real reason bisection
      was abandoned rather than continued: a threshold that doesn't
      reproduce deterministically can't be reliably bisected against.

    Current state: `third_party/Libc/locale/FreeBSD/table.c` is back to
    fully pristine (`git checkout --`, no patches survive) and
    `build_simple_cmds.sh`'s `locale_extra_objs` is deliberately left
    empty — the 32 commands above simply don't build in this pass, a
    real, visible failure (`build_simple_cmds: 44 built, 32 failed`,
    stable across repeated clean rebuilds), not a silently-degraded one.
    Real follow-up, not attempted here: either find a real ld64-609
    workaround for this specific fixup-chains threshold (possibly
    version-specific to this project's pinned Xcode 12 toolchain — worth
    checking whether a newer `ld64`/`ld-prime` bundled with a later
    Xcode still on this machine, if any, reproduces it at all before
    assuming it's permanent), or restructure `_DefaultRuneXLocale`
    itself (e.g. lazily-populated instead of one giant static
    initializer) to reduce its own real fixup/atom footprint below
    whatever this toolchain's real threshold is.

    **RESOLVED 2026-08-28, same day, following the exact "worth
    checking" follow-up above.** Real, isolated repro (minimal
    `table.o`+`none.o`+`libc_locale_globals_compat.o`+trivial `main`,
    same `-Wl,-fixup_chains`, same dylib set) reproduced the identical
    `Assertion failed: (_mode == modeFinalAddress)` under Xcode 12's own
    `ld64-609`, then the *same* link command with `clang`'s `-B` flag
    pointed at the daily-driver Xcode's own much newer `ld` (this
    machine: Xcode 26.6, `ld` 1267) succeeded — confirmed not just
    "succeeds" but real load-command parity via `otool -l` against an
    already-QEMU-verified dynamic executable (`true.macho`): identical
    `LC_LOAD_DYLINKER`/`LC_MAIN`/`LC_DYLD_CHAINED_FIXUPS` shape. This is
    a real, narrow ld64-609 bug in the daily-driver Xcode's own
    predecessor, not anything about this project's object code —
    confirmed by two back-to-back clean rebuilds (`rm -rf
    libc_build/simple_obj`) both landing exactly `76 built, 0 failed`,
    where the crash used to reproduce non-deterministically per the
    bisection notes above.

    Fix, in `build_simple_cmds.sh`: `NEWLD_BINDIR` (`xcode-select -p`'s
    `Toolchains/XcodeDefault.xctoolchain/usr/bin`, fail-fast if missing)
    passed via `-B"$NEWLD_BINDIR"` on the final per-command link only —
    still compiling with Xcode 12's own `clang`/SDK throughout (the
    SDK/header-vintage invariant this project's other "must be Xcode
    12" rules are actually about is untouched; only ld64's own internal
    bug is being routed around). `table.c`/`none.c` joined
    `COMMON_EXTRA` (always linked, same "harmless if unreferenced"
    precedent as `err.c`/`getopt.c`), which resolved the crash and the
    original `___global_locale`/`___locale_key`/`__none_*` gaps
    together (44 → 55 built). Closing the remaining 21 needed three
    more real ports, each a genuine new finding, not just re-adding
    files the bisection above already tried:
    - `ldpart.c`/`getcwd.c`/`findfp.c`: compiled clean with zero
      changes, resolving `__ldpart_free_extra`/`__part_load_locale`
      (real `LC_TIME` via `__time_load_locale`, needed by every command
      already linking `strftime.c`/`timelocal.c`) and `___sfprelease`
      (`findfp.c`, needed by `freopen$UNIX2003`) — 55 → 71 built.
    - `collate.c` (real `LC_COLLATE`/`fnmatch()` range-matching support
      for `grep`/`find`/`diff`/`du`) has a genuine, narrow compile bug
      *unrelated to ld64-609*: its own header
      (`locale/FreeBSD/collate.h`) guards
      `__collate_mbstowcs`/`__collate_wcsdup`'s prototypes behind
      `#ifdef __LIBC__` (real Apple's own umbrella Libc build always
      defines this; no other file this project compiles ever needed
      to) — without it, collate.c's own internal forward-reference to
      `__collate_mbstowcs()` gets an implicit-int declaration that then
      conflicts with its real `wchar_t*`-returning definition later in
      the same file. Fixed with a `collate.c`-only `-D__LIBC__`
      override in the `COMMON_EXTRA` compile loop (scoped, not global —
      untested whether `__LIBC__` changes anything else this project
      compiles). `collcmp.c` (the actual home of `__collate_range_cmp`,
      confirmed via `grep`, not assumed to live in `collate.c`) added
      alongside it — 71 → 75 built.
    - `cat` **regressed** during this pass (real, caught immediately by
      the full-suite rerun, not silently missed): `cat_socket_stub.o`'s
      link-alias case aliased `_send`/`_writev` to base symbols
      `cat_socket_stub.c` never actually defined (only
      `socket()`/`connect()`/`shutdown()` — real `cat.c` never calls
      `send()`/`writev()`). Xcode 12's `ld64-609` silently tolerated
      aliasing an undefined base; the daily-driver Xcode's newer `ld`
      hard-fails on it (`symbol(s) not found ... in
      command-line-aliases-file`) — a real, confirmed behavioral
      divergence between the two linkers' `-alias` handling, not a
      regression from anything else in this pass. Fixed by dropping the
      two dead aliases from `cat_socket_stub.o`'s case entirely.
    - `date` was the last failure, and a *different* real gap: its own
      `date_compat.c` already ships honest `ENOSYS` `send()`/`writev()`
      stubs for `oldsyslog.c`'s `vsyslog()` path (predates this pass —
      real `netsettime()`/`pututxline()` network-daemon gaps this same
      file's header already documents), but no `case` ever added the
      matching `-Wl,-alias` flags for `date` specifically (only
      `cat_socket_stub.o`'s case did, and cat doesn't need them). Fixed
      by adding a `date`-only alias case — no new stub code needed, the
      base symbols already existed.

    **Second real regression found by the real QEMU boot-test that
    followed (2026-08-28, not caught by any of the above — `otool -l`/
    `gap_audit.sh` both passed clean on `ed`/`diff`/`less` first):**
    `diff /tmp/a /tmp/b` (the first of the 76 to actually launch and
    exercise this path — `diff` was one of the newly-unblocked 32, `ed`/
    `less` were already in the original 44) crashed immediately at
    process launch, before printing anything: `dyld[8]: Symbol not
    found: _pipe / Referenced from: /bin/diff / Expected in:
    /usr/lib/libSystem.B.dylib`. Root cause, confirmed via `nm -m` on
    all four real `pipe(2)` consumers (`ed`, `diff`, `less`, `tar`):
    real `ed_popen_system_compat.c` (`ed`/`less`) and real
    `filter_fork_posix.c`/GNU `diff.c` both call plain `pipe(fd)`,
    which needs a real `_pipe` symbol, but `ed_pipe_trap.o` (Apple's own
    real `libsyscall/custom/__pipe.s`, which all four already link)
    only exports the raw trap under `___pipe` (three underscores) —
    real Apple Libc needs a thin public-name forwarder in between,
    which `tar_pipe_compat.c` already provided (written earlier, for a
    *different* reason — see that file's own header) but only tar ever
    linked it. `ed`/`less` used to get this bridged for free via the
    old static-linking era's project-wide `-alias ___pipe _pipe`
    directive, which d9's own dynamic-link conversion earlier this same
    day removed project-wide (the whole obsoleted `LINK_ALIASES` stack)
    without anyone re-providing the bridge for these two specific
    commands — a real, dormant regression from *earlier in this same
    d9 conversion*, invisible until something actually launched under
    real chained-fixups dyld (which binds every referenced symbol
    eagerly at process start, unlike the old lazy-bind model — a call
    site being unreached at runtime doesn't save you). `diff` becoming
    linkable was what finally exercised it, but `ed`/`less` had already
    been silently broken the same way since d9's conversion, just never
    actually launched afterward to prove it (link-time `otool -l` /
    `gap_audit.sh` both pass regardless, since a `(from libSystem)`
    "safe two-level-namespace binding" classification only checks that
    *some* dylib in the process could plausibly satisfy the symbol at
    load, not that it actually does). Fixed by linking
    `tar_pipe_compat.c` into `ed`/`diff`/`less` too (that file's own
    header updated to reflect it's no longer tar-specific despite the
    name) rather than reintroducing the removed alias mechanism. Real
    QEMU re-verification after the fix: `ls`, `grep`, `date`, `diff`
    (real `1c1`/`<`/`>` output), `find`, `stat`, `fold` all confirmed
    working with no panic/assertion/dyld-symbol-not-found anywhere in
    the transcript; `ed`'s own file-write in that same session failed
    with an unrelated "No such file or directory" that traces to the
    test harness's own quoting through the pty, not a `pipe()`/dyld
    issue (ed's core buffer/file-write path doesn't call `pipe()` at
    all — only its `!`-shell-escape and `r !cmd`/`w !cmd` forms do),
    not chased further here.

    Real, still-open, deliberately out of scope for this pass: none
    known — all 76 commands build, and the two real regressions the
    QEMU pass surfaced (cat's dead aliases, ed/diff/less's missing
    `_pipe` bridge) are both fixed and re-verified. `docs/roadmap.md`'s
    own Phase 4/4.6 command tiers should be checked for whether any of
    them still list `date`/`ls`/etc as behaviorally-limited for a
    *different* reason (e.g. `date`'s real clock-setting path is still
    stubbed, see `date_compat.c`'s own header) — this item is scoped to
    "does it link and boot," not full feature parity. `ed`'s own
    file-write behavior under a real interactive session (as opposed to
    this pass's scripted/piped test) has not been separately verified.

0a. **[DONE 2026-08-28] Real AES core wired: FreeBSD rijndael, not
    PureDarwin.** Filed 2026-08-21 as a hard prerequisite for Phase 7b
    (`sshd`); closed by vendoring `sys/crypto/rijndael/` (public-domain,
    Rijmen/Bosselaers/Barreto — the AES designers themselves, cleaner
    than PureDarwin's mixed-license option scoped 2026-08-27 below and
    never acted on) into `libkern/crypto/freebsd/rijndael-alg-fst.c`,
    plus a new corecrypto-shaped adapter
    (`libkern/crypto/corecrypto_aes_register.c`) implementing real
    `ccmode_ecb`/`ccmode_cbc`/`ccmode_ctr` instances against it and
    calling `register_crypto_functions()` with only the AES fields
    filled (ecb/cbc/ctr encrypt+decrypt), everything else left NULL per
    the incremental-fill plan below.

    `g_crypto_funcs` (`libkern/crypto/register_crypto.c`) is a table of
    function pointers real macOS's closed-source corecrypto kext fills in
    via `register_crypto_functions()`; this kernel loads no kexts, so
    nothing had ever called it before this. Bitten five separate times
    now, each rediscovered the hard way from a fault address — the fifth
    (`cc_rand_generate`) found and fixed *by this same AES work*, not a
    surprise later:

    | Caller | Offset | Field | Resolution |
    |---|---|---|---|
    | `SHA256_Init` via `devfs_mount` | `0x30` | `ccsha256_di` | real corecrypto, item 0b |
    | MD5 | `0x20` | `ccmd5_di` | now real FreeBSD `md5c.c`, bypasses the table (see below) |
    | `read_random` | n/a | `cckprng` | falls back to `read_erandom()` |
    | `tcp_init`→`tcp_tfo_init` | `0x78` | `ccaes_cbc_encrypt` | **now real** — TFO auto-enables |
    | `cc_rand_generate` (`corecrypto_rand.c`) | n/a | `ccrng_fn` | **found by this work**, see below |

    **Two real bugs found and fixed while wiring this up, neither
    guessed — both from real QEMU panics:**
    1. `register_crypto_functions()` writes `g_crypto_funcs`, a
       `SECURITY_READ_ONLY_LATE` global — that section becomes
       hardware-write-protected at `machine_lockdown()`
       (`osfmk/kern/startup.c`). First attempt called the registration
       function from `bsd_init()` (which runs well after lockdown) and
       produced a real "Unexpected fault in kernel static region"
       (ESR=0x9600004f, a write-permission fault) at boot. Fixed by
       moving the call into `kernel_bootstrap_thread()`
       (`osfmk/kern/startup.c`), immediately before `machine_lockdown()`
       — the same place a real corecrypto kext's own registration would
       need to land relative to lockdown.
    2. Once AES was actually live, a *second*, unrelated real panic
       appeared inside `necp_init()` — a NULL-function-pointer jump
       (`Kernel instruction fetch abort at pc 0x0`). Root cause:
       `corecrypto_rand.c`'s `cc_rand_generate()` guards on
       `if (g_crypto_funcs)` only, then unconditionally calls
       `g_crypto_funcs->ccrng_fn(...)` — a field this table intentionally
       leaves NULL (no RNG registered here; `read_random()` has its own
       separate `read_erandom()` fallback). That guard was written
       assuming corecrypto's real all-or-nothing kext fill and had never
       been exercised, because `g_crypto_funcs` had literally never been
       non-NULL before in this project's history. Fixed with a
       field-specific null check
       (`g_crypto_funcs && g_crypto_funcs->ccrng_fn`), matching the
       pattern `corecrypto_aes.c` already uses. **Root-caused via a real
       A/B test**, not guessed: the exact same kernel with only the
       registration call disabled booted cleanly past this point, proving
       the crash was caused by AES becoming active, not a pre-existing
       gap.
    3. **Known, deliberately NOT fixed — now filed as its own bug, P1
       item 3**: six other `g_crypto_funcs->` call sites
       (`corecrypto_des.c`, `corecrypto_rsa.c`, `corecrypto_aesxts.c`,
       `corecrypto_chacha20poly1305.c`, `bsd/nfs/gss/ccrypto.c`,
       `bsd/netinet/flow_divert.c`) have **no null check at all**, not
       even the bare `if (g_crypto_funcs)` that `corecrypto_rand.c` had.
       Not reached by anything exercised up through a full boot to login
       (QEMU-verified), so not made worse by this change — but a live
       NULL-function-pointer call waiting for whoever next touches DES/
       RSA/AES-XTS/ChaCha20-Poly1305/NFS-GSS/flow-divert. Full writeup:
       P1 item 3 above.

    Context sizing: `libkern/libkern/crypto/aes.h`'s
    `AES_CBC_CTX_MAX_SIZE` (a hardcoded ~280-byte budget baked into the
    `aes_encrypt_ctx`/`aes_decrypt_ctx` typedefs used throughout the
    kernel) comfortably fits the new adapter's per-direction context
    (an `int` plus a 4×(Nr+1)-word key schedule, ≤244 bytes for AES-256)
    — confirmed for real by the AES-256 case in the boot-time self-test
    below, not just computed on paper.

    **FIPS-197/SP 800-38A self-test at boot, not just KAT-in-a-vacuum**:
    `xnu_rpi3_register_freebsd_aes()` runs AES-128/192/256 ECB
    (FIPS-197 Appendix C.1–C.3) and AES-128 CBC/CTR (SP 800-38A Appendix
    F.2.1/F.5.1) known-answer vectors through the real
    `ccmode_ecb`/`ccmode_cbc`/`ccmode_ctr` interfaces — not the raw
    cipher core directly — so a bug in the adapter glue fails loudly at
    boot (`panic()`) exactly like a bug in the vendored core would,
    before `register_crypto_functions()` is ever called.

    ARMv8 Crypto Extensions (AESE/AESMC) on BCM2837's Cortex-A53:
    believed absent, still not verified — irrelevant now since the
    software core is real and working, not a blocker.

    **PureDarwin/corecrypto (scoped 2026-08-27) was NOT used in the end.**
    That investigation found real, verified-matching `struct
    crypto_functions` ABI precedent but a mixed-license AES core
    (`LICENSE.md` says Apache-2.0/mbedTLS, `README.md` says Botan +
    `rafirafi/pdcrypto`, the code's own header credits `tiny-AES128-C` —
    three different stated lineages) and AES-128-only coverage (no
    CTR/AES-256). FreeBSD's `sys/crypto/rijndael/` turned out to need no
    interface borrowing at all — it's just a block cipher core
    (`rijndaelKeySetupEnc/Dec` + `rijndaelEncrypt/Decrypt`), and this
    project's own `struct ccmode_ecb/cbc/ctr` adapter (project-specific
    glue either way, per the original scoping above) covers ECB/CBC/CTR/
    AES-128/192/256 cleanly under one public-domain license. Vendoring
    details, pin, and per-file license notes: `setup_third_party.sh`'s
    `freebsd-src` block.

0b. **[DONE 2026-08-27] `ccsha256_di` wired to real corecrypto, replacing
    the from-scratch `corecrypto_sha2.c` for code-signing/exec hashing.**
    `SHA256_Init`/`_Update`/`_Final` (`libkern/crypto/corecrypto_sha2.c`)
    now call real `ccdigest_init`/`_update`/`_final` + `ccsha256_di()`
    directly (the real digest-core source was already sitting unused at
    `third_party/xnu-7195/osfmk/corecrypto/` and already compiled into
    the kernel binary for the PRNG's own use — only the SHA-256 call
    sites hadn't been pointed at it). QEMU-verified: real login, real
    shell, real `/bin/ls`/`md5` exec, no panic — commit `ceb6d419c` in
    the `xnu-7195` checkout. Full investigation (including a corrected
    finding that `register_crypto_functions()`/`g_crypto_funcs` was
    never actually the right mechanism for this call site) archived to
    `docs/historic/backlog.md`.

    **[DONE 2026-08-28] SHA-384/512 and MD5 also off the from-scratch
    path now**, alongside item 0a's AES work — no real corecrypto body
    exists for either in this checkout (confirmed absent, only headers),
    so per this project's Apple→FreeBSD→custom source priority: SHA-384/
    512's compress core (the real K[80] table and round function, not
    just constants copied by hand) is now FreeBSD's real 2-clause-BSD
    `sys/crypto/sha2/sha512c.c` (vendored to
    `libkern/crypto/freebsd/sha512_transform.c`, upstream's ARM64 ifunc
    hardware-accel path dropped — dead code on Cortex-A53 either way);
    `corecrypto_sha2.c` keeps its own buffering/padding wrapper since
    that has to match this project's `SHA512_CTX`/`SHA384_CTX` layout,
    not FreeBSD's differently-shaped one. MD5
    (`libkern/crypto/corecrypto_md5.c`, deleted) is now FreeBSD's real
    `sys/kern/md5c.c` (RSA Data Security's own 1991-2 reference
    implementation) at `libkern/crypto/freebsd/md5c.c`, K&R-style
    function definitions modernized to ANSI prototypes (mechanical,
    same bodies) — this project's own `MD5_CTX` layout
    (`state[4]`/`count[2]`/`buffer[64]`) is byte-identical to FreeBSD's,
    no adapter needed. Both QEMU-verified as part of the same boot that
    verified item 0a's AES work.

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

    **UPDATE 2026-08-27, same day**: `syslogd.macho` itself now builds
    and links completely clean (`tools/userland_staging/build_syslogd.sh`,
    real dynamic Mach-O) and PID-1 can fork+`execve()` it for real — but
    it is **not yet auto-spawned at boot**. The originally-found blocker
    (dyld crashing on `__dispatch_wait_for_enqueuer`, a real symbol
    `libdispatch.dylib` itself referenced but never exported) is fixed —
    `shims/yield.c` was compiled-but-never-included, a genuine missing-
    file bug, not an architecture gap; two more of the exact same class
    (`os/semaphore.c`, a narrow reproduction of `workgroup.c`'s TSD
    cleanup) were found and fixed the same way via iterative real QEMU
    boot-testing.

    **UPDATE 2026-08-27, later same day**: the pthread QoS-class/attr
    blocker (`__pthread_qos_class_encode` and ~30 real siblings) is now
    substantially closed too — `qos.c` turned out to compile clean
    against this project's real toolchain (the "unreachable per the
    kernel constraint" note in `build_libpthread.sh` predates Phase
    7.5's real kernel thread-creation port and is now stale for the pure
    encode/decode pieces); new, isolated `build_libpthread_qos.sh` +
    `libpthread_qos_compat.c`/`libpthread_attr_compat.c` close real
    `pthread_attr_*`, `pthread_sigmask`, `pthread_exit`,
    `pthread_workloop_create`/`destroy`, `clock_gettime_nsec_np`, and the
    `mach_voucher`/`host_priv` MIG-regenerated routines this needed along
    the way. Kept deliberately separate from the master `libpthread.a` to
    avoid regression risk to bash/ncurses/getty/syslogd. `pthread_get_
    stackaddr_np`/`pthread_setschedparam` stay honestly deferred (would
    need this project's from-scratch thread-bootstrap to match `pthread.c`'s
    real thread-list ABI, not yet verified safe to copy). Full writeup:
    `docs/daemon-roadmap.md`'s Phase 6.7 section.

    **UPDATE 2026-08-27, later still**: Step 2 (the sibling-dylib relink
    from this doc's own userland-library-debt section, `d1`) was applied
    to `libdispatch.dylib` specifically, ahead of the general cleanup:
    it now links for real against this project's own already-built
    `libsystem_c.dylib`/`libsystem_malloc.dylib`/`libsystem_pthread.dylib`
    (real `LC_LOAD_DYLIB` entries, confirmed via `otool -L`), closing 20
    of the ~54 remaining undefined symbols that a real `nm -gU` sweep
    found were already real, defined exports there. Two more closed by
    narrow, non-import fixes the same day: `gettimeofday` (real raw
    syscall #116 body) and `geteuid` (real single-object extraction from
    `syslogd_libc_extras.a`).

    **UPDATE 2026-08-27, later same day: all `libdispatch.dylib` symbol
    gaps closed — `syslogd.macho` reaches real execution.** Two more
    passes closed the remaining 32-symbol gap in full:
    - `3fab93c` closed 20 of the 32: real `kevent_qos`, `kpersona_get`,
      `pread`/`pwrite`, `sigsuspend`, `sleep`, `semaphore_timedwait`,
      `malloc_engaged_nano`/`memory_event_handler`,
      `mach_continuous_time`/`mach_get_times`,
      `mach_make_memory_entry_64`, all 7 `mach_port_*` traps/MIG-fallback
      wrappers, and `thread_destruct_special_reply_port`. Found and fixed
      a real bug along the way: `mach_ipc_dead_stubs.o`'s narrowly-scoped
      `KERN_FAILURE` `mach_port_insert_right`/`extract_right`/`unguard`
      stand-ins (meant only for one unreachable reply-validation path)
      would otherwise have been the only definition satisfying
      `syslogd`'s real dispatch/eventlink calls to those same names —
      replaced with real trap-first/MIG-fallback bodies. Added
      `tools/userland_staging/gap_audit.sh` (triages a built dylib/macho's
      undefined-symbol table against already-built archives and real
      Apple source without a QEMU boot per symbol; now a standing
      pre-boot step per `CLAUDE.md`).
    - `7629e79` closed the remaining 8: the `mach_eventlink_*` family (real
      `libsyscall/mach/mach_eventlink.c` BSD-syscall bodies for
      `signal`/`wait_until`/`signal_wait_until`, plus a fresh `mig` regen
      against `osfmk/mach/mach_eventlink.defs` for
      `create`/`destroy`/`associate`/`disassociate`) and
      `pthread_get_stackaddr_np` (a narrow extraction of `pthread.c`'s own
      function, skipping the internal cross-thread liveness check since
      child threads aren't inserted into `__pthread_head` on this
      project's from-scratch thread bootstrap — safe here specifically
      because `pthread_s` structs are never freed/reused).

    Verified via real QEMU boot: `syslogd.macho` now passes dyld's eager
    symbol binding completely (zero "symbol not found in flat namespace"
    aborts) and reaches real execution — where it then hit a **real
    runtime SIGSEGV (11)**, a different, later-stage failure than the old
    load-time SIGABRT (6).

    **UPDATE 2026-08-27, later still: that SIGSEGV fixed, `syslogd.macho`
    now runs to completion (`e819550`).** Two independent real bugs, both
    root-caused via the real crash (the first time anything in this
    project called plain `snprintf()`/`vsnprintf()` at runtime,
    `syslogd`'s `write_boot_log()`) and bisected with isolated static
    smoketests:
    1. This SDK defaults `_FORTIFY_SOURCE` to 2 for this iOS target.
       `third_party/Libc/secure/vsnprintf_chk.c`'s own internal
       `vsnprintf(...)` call was getting macro-rewritten back into
       `__vsnprintf_chk(...)` — infinite self-recursion, stack-guard-page
       SIGSEGV. Fixed with `-D_FORTIFY_SOURCE=0` on the two build scripts
       that compile `Libc/secure/*.c`
       (`build_libxpc_syscalls2.sh`/`build_libdispatch_dylib.sh`).
    2. Real `vsnprintf()` calls `__current_locale()`, which does a raw,
       unchecked TSD-array lookup using `__locale_key` — left at its
       static `-1` sentinel because nothing in this project ever called
       the real `__xlocale_init()` real Apple calls from
       `_libc_initializer()` (never ported here). Fixed with a real patch
       (`third_party-patches/Libc.patch`) adding a same-translation-unit
       constructor to `xlocale.c` itself, so every build script pulling in
       that object file links it for free;
       `libc_locale_globals_compat.c` (this project's own narrow private
       copy of the same two globals, used by `libdispatch.dylib`'s
       separate static link) got the identical fix.

    Verified end-to-end: `syslogd.macho` no longer crashes at all — it
    runs its full real startup (dispatch queue creation, boot-log writing
    with real `snprintf`, notify registration) and exits cleanly with the
    expected, already-documented "no real launchd" limitation (its
    `asl_open()`/`notify_register_*` calls need `bootstrap_look_up2()`
    against a real bootstrap namespace registrar this project doesn't
    have yet, see the notifyd scoping note above), not a memory-safety
    bug.

    **What's still actually open, confirmed by re-reading
    `tools/init_binary/init.c` directly rather than trusting the above as
    "done"**: `spawn_syslogd()` is defined (`init.c:1665`) but its call
    site is still commented out — grepping the file for
    `spawn_syslogd()` finds exactly one hit, inside that disabling
    comment block, no active call anywhere in `init_main()`'s loop.
    `syslogd` is **not** auto-spawned at boot despite every gap above
    being closed; nothing currently prevents re-enabling it (the
    documented "no real launchd" exit is a clean, non-fatal outcome, not
    a crash), it just hasn't been done yet. Note also: `init.c`'s own
    disabling comment block still enumerates the old "32 remaining
    symbols" list from before `3fab93c`/`7629e79` — that comment is now
    stale on gap count (not rewritten as part of this docs pass, since
    editing `init.c` itself is out of scope here); whoever re-enables the
    call site should refresh that comment too. Once re-enabled and QEMU-
    boot-confirmed, this item (switching real callers onto the legacy
    Unix-socket path) becomes actionable.

---

### d10: launchd PID-1 boot-path integration, in progress 2026-08-28

Real Milestone 4/5 work (docs/daemon-roadmap.md Phase 6.8): `/sbin/launchd`
on the SD image is now real, dynamically-linked `launchd.macho` (explicit
user choice: outright replace `tools/init_binary/`'s custom static PID-1,
not exec-into). Kernel needed zero changes -- `load_init_program()`
already hardcodes `/sbin/launchd`, stock unmodified XNU behavior.
`pd_launchd_load_daemons_dir()` (real PureDarwin source, previously
defined but never called anywhere upstream) is now wired into
`launchd.c`'s `main()`; `pd_launchd_boot()` now also remounts `/` rw
(same real kernel quirk `init.c`'s own `remount_root_rw()` already
documented). `pd_console_login.c` (real PureDarwin getty/login-equivalent
helper, built standalone as `/usr/libexec/pd-console-login`) had its
hardcoded `/bin/zsh` swapped for this project's real `/bin/bash` --
**note this drops password authentication entirely** (no login prompt,
auto-execs a root shell) versus `init.c`'s own real login/su support;
scoped as a known simplification to get first boot working, not a
permanent design choice -- revisit once launchd boots reliably.

Iterating real dyld "Symbol not found" aborts one at a time via real
QEMU boots (`_abs`/`_llabs` missing from `libSystem.B.dylib`'s real
export chain, then `_atol`/`_bsearch`/`_readlink`/`_getpeername`/
`_gethostuuid`/`_setiopolicy_np`/`_audit_session_self`/
`_audit_session_port`/`_audit_session_join`/`_asctime`/`_asctime_r`/
`_strtoll`/`_strtoll_l`/`_strtoul_l`) -- same shape as every other real
gap-closure pass in this project, see `tools/userland_staging/
launchd_audit_syscalls_compat.c` (new) for the honest-stub writeups on
the three `audit_session_*` routines (no real userland client source
exists anywhere in this project's pinned repos, only the kernel side).

**Real, not-yet-implemented follow-up surfaced by this pass**: this
project has no `launchctl` client at all. Real macOS's actual job-load
flow is `launchctl load <plist>` sending an IPC/XPC message to a running
launchd, not launchd reading `/Library/LaunchDaemons` itself at boot --
the `pd_launchd_load_daemons_dir()` wiring above is a real, working, but
simplified stand-in for that (loads once at boot, no dynamic
load/unload/list/start/stop after that). A real `launchctl` port is a
separate, sizeable follow-up (needs the real `vproc_mig_*`/job-control
MIG client side `libvproc.c` already has some of) -- filed here so it
isn't rediscovered from scratch; not blocking first boot.

### d11: real locale (xlocale) source exists and is pinned -- partially wired, `strftime_l` still a real wall

In response to a real question raised while closing d10's `asctime`/
`atol`/`strtoll_l`/`strtoul_l` gaps: **locale is not a "never
open-sourced" wall like ASL/corecrypto** -- real Apple/FreeBSD source is
already pinned at `third_party/Libc/locale/` (`xlocale.c`, `isctype.c`,
`FreeBSD/*.c`) and partially built into `libc_locale.a`
(`build_libc_locale.sh`): `isspace_l`/`toupper_l`/`__global_locale`/
`__locale_key`/`__current_locale()` (a header-inline function,
`xlocale_private.h`) all already compile and link real, confirmed by
`nm`. A real, already-committed patch (`third_party-patches/Libc.patch`)
fixed `__locale_key`'s init-order bug (added a real ctor calling
`__xlocale_init()`, since nothing in this project calls Apple's real
`_libc_initializer()`). **Good news, re-tested 2026-08-28**: with that
fix already in place, real `atol.c`/`strtoll.c`/`strtoul.c` (which all
route through `isspace_l`/`__current_locale()`) now compile and link
completely clean against `libc_locale.a` with zero changes needed --
the "tried and reverted" history below predates that fix landing.
`atol`/`strtoll`/`strtoll_l`/`strtoul`/`strtoul_l` are now real,
unmodified Apple/FreeBSD source in `libc_gen.a`, not a workaround.

**TEMPORARY FIX, revisit later -- what's still genuinely missing**:
`strftime_l` itself -- a large, locale-aware date/time formatter
(`stdtime/FreeBSD/strftime.c`) that `asctime_r()` calls internally just
to format a 4-digit year (a real implementation shortcut on FreeBSD's
part, not a functional need -- `asctime`'s output format is fixed by
the C standard regardless of locale). A prior pass tried pulling
`strftime_l` in for real and reverted it as a deep, disproportionate
dependency chain for what it bought at the time; unlike atol/strtoll/
strtoul above, this one is NOT yet re-tested against the current
`libc_locale.a` state, so it may or may not still be as deep a wall --
genuinely unknown, not re-attempted this pass. d10 worked around it for
`asctime`/`asctime_r` specifically with a small, honest,
locale-independent from-scratch implementation instead
(`launchd_audit_syscalls_compat.c`'s `asctime`/`asctime_r`/`put2`/
`wday_name`/`mon_name`) -- this is a real, deliberately temporary
stand-in, not a permanent design choice: it duplicates real FreeBSD
logic by hand instead of linking the real `asctime.c`, and should be
replaced with the real source the moment `strftime_l` is actually
ported (re-test it first -- it may already "just work" the same way
atol/strtoll did). `strftime_l`/full `strftime` itself remains a real,
open, not-yet-closed gap for any future caller that needs actual
locale-aware formatting (not just a fixed year digit
string) -- real source is right there, pinned, just needs the same
kind of dedicated pass CF's own locale work got.

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
