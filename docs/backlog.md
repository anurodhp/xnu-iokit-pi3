Resolved items moved to `docs/historic/backlog.md` — see there for fix history
(most recently the third archive pass, 2026-08-30: P0 items 1/2/4/5, and
items d7/d8/d9/0a/0b plus item d11's session-2-through-5 investigation trail).

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
   had no serialization at all (no `IOCommandGate`, no workloop, no mutex);
   fixed with `fTransferLock`, a plain `IOLock` held for the whole
   command-issue-through-completion span. **Verification caveat**: nothing
   in this project issues genuinely concurrent I/O, so the QEMU boot test
   confirms no regression, not that the interleaving race is exercised.
   Full writeup: `docs/historic/backlog.md`'s "P0, items 1/2/4/5" section.

2. **[FIXED 2026-08-27, QEMU-verified]** No error recovery anywhere in
   `BCM2837SDHCI` — every error path was `IOLog(); return false;`. Fixed
   with `resetCmdLine()`/`resetDatLine()` (SDHCI spec 3.7.1, matching
   Linux's `sdhci_reset()` split), wired into every real error/timeout
   path. **Verification caveat**: QEMU's SD model injects no real CRC/
   timeout/card-removal errors, so this is verified only as "the reset
   paths compile and don't regress a clean boot". Full writeup:
   `docs/historic/backlog.md`'s "P0, items 1/2/4/5" section.

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
   rather than before. Fixed by reordering `start()` and adding real
   `IODelay()`s. Only changes ordering/idle delays, not signal rate/timing,
   so unlike item 3 there's no way a wrong guess here makes real hardware
   worse. Full writeup: `docs/historic/backlog.md`'s "P0, items 1/2/4/5"
   section.

5. **[FIXED 2026-08-27, code correct but genuinely unverifiable from
   QEMU — exactly as this item originally predicted.]** No memory barrier
   in `BCM2837InterruptController::enableVector()`/`disableVectorHard()`
   before the next `IRQ_PENDING` read; fixed with
   `__asm__ volatile ("dsb sy" ::: "memory")` after each register write.
   All MMIO is strongly ordered under QEMU/TCG regardless of the barrier.
   Full writeup (including a correction to this item's own original "no
   barriers anywhere" claim): `docs/historic/backlog.md`'s "P0, items
   1/2/4/5" section.

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

**On items 1, 2, 4, 5 above**: all four were rebuilt together and verified
with one real QEMU boot — real login, real reads against both mounted
partitions (`disk0s1` root / `disk0s2` boot), zero panics, `BCM2837SDHCI`
log confirming a real hardware interrupt and correct card identification.
That confirms no regression from the four changes together; it does not
and cannot exercise the specific fault conditions items 1/2/5 exist to
guard against, since QEMU reproduces none of them. Full text archived to
`docs/historic/backlog.md`'s "P0, items 1/2/4/5" section.

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

d1. **RESOLVED 2026-08-29/30 (static-linking cleanup, Phases 1a–1d,
    QEMU-verified).** `libdispatch.dylib` no longer statically bundles
    its dependency stack — it links the real sibling dylibs directly.
    `mach_task_self_`/`g_zone`/`errno` each now have exactly one real
    owner project-wide (errno's own fix, moving it fully into
    `libsystem_kernel.dylib`, landed 2026-08-30 and was verified via a
    full real QEMU boot — see `docs/static-linking-cleanup-plan.md`'s
    2026-08-30 session section for the full trail, including one real
    regression that boot test caught and fixed). Full detail on the
    original bug archived in `docs/historic/backlog.md`.

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
    script's own comments for exactly which).

    **Partially resolved 2026-08-30**: `_pthread_key_init_np`'s
    `libc_locale_for_stdio.a` copy (`pthread_tsd_shim.o`) is still real
    and still linked into `libsystem_malloc.dylib` (malloc sits *below*
    `libsystem_pthread.dylib` in the real dependency order and cannot
    link the real dylib back for this), but its 5 public POSIX names are
    now narrowed to private-extern via `ld -r -keep_private_externs
    -unexported_symbol`, so it can no longer shadow the real
    `libsystem_pthread.dylib` copy for any other image — see
    `docs/static-linking-cleanup-plan.md`'s 2026-08-30 session section.
    The `mach_portUser.o`/`vm_mapUser.o` MIG-regen duplication (second
    bullet) is also resolved the same session: `libsystem_kernel.dylib`
    now runs its own real `mig` regen of `mach_port.defs` and owns the
    whole `__kernelrpc_mach_port_*` set outright, and both `libxpc.dylib`
    and `libiokituser.dylib` dropped their private copies. Still open:
    picking one canonical provider for the `_vm_allocate` family
    specifically (`libmalloc.a` vs `dyld_core.a`'s `vm_mapUser.o`).

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
    starting count of 153.** Real, unmodified Apple source throughout
    (`third_party/swift-corelibs-foundation` — Apple's own actively-
    maintained CF fork, NOT the 2014-dated `apple-oss-distributions/CF`;
    real ICU 66.1 via a real two-stage host+arm64 cross build; real
    libxml2-34.9). 94 real CF source files build clean into
    `libCoreFoundation.a`. The single biggest gap-closing fix of the whole
    effort was `libSystem.B.dylib`'s missing `-Wl,-reexport_library` (27
    groups in one rebuild). Full port/build narrative archived to
    `docs/historic/backlog.md` — the session-by-session story in its
    2026-08-27 "item d7" section, the current-state detail in its "Third
    archive pass, 2026-08-30" section.

    **Real, confirmed compile-time exclusions (4, structural, not build
    gaps)**: `CFUserNotification.c` (real GUI alert API, no GUI on this
    target); `CFPreferences.c`/`CFXMLPreferencesDomain.c` (**CLOSED
    2026-08-28** — both now compile and link for real, three
    `TARGET_OS_OSX`/`TARGET_OS_MAC` guard mismatches, see
    `docs/daemon-roadmap.md`'s "Milestone 2 CLOSED, 2026-08-28"; whether
    `CFPreferencesCopyAppValue` et al are *behaviorally* correct against a
    real on-disk preferences file is separately unverified);
    `CFURLSessionInterface.c` (real libcurl → closed-source Secure
    Transport for TLS, plus BSD socket wrappers this userland lacks — real
    prerequisite order: socket syscall wrappers, then a TLS backend
    decision, then a `build_libcurl.sh` off `build_libxml2.sh`'s template);
    `CFWindowsUtilities.c` (Windows-only).

    **Remaining real gaps in the dynamic trial link, 42 groups — still
    open** (full per-symbol list archived): `_scalbn`/`_getpwuid_r`/
    `_getsectbynamefromheader_64`/`_getsegbyname` (real functions, not
    confirmed built anywhere in this project's stack, not investigated);
    the `___chk_*` `_FORTIFY_SOURCE` hardening family (no shim source
    located); an os_log/dispatch bucket (`__os_log_*`/`_os_log_create`/
    `_os_log_type_enabled`/`__dispatch_queue_attr_concurrent`/
    `__mh_dylib_header`, confirmed absent from both `libdispatch.dylib`
    and `libSystem.B.dylib`); `_asl_*` (6 symbols — real and pinned as
    `syslog-385`, just not yet wired into CF's own trial link); the
    `CFPreferences`-family symbols tracked by the exclusion above; and
    `sysdir_*` (confirmed never open-sourced by Apple anywhere — full
    `Libc` git history plus an org-wide GitHub code search).

d8. **Static-archive-vs-dylib convention debt — RESOLVED 2026-08-28
    (explicit user directive: option (a), convention NOT narrowed) and
    2026-08-29 (all 11 "genuine conversion candidate" archives converted
    to real dylibs, every real consumer relinked, not just left as unused
    parallel output).** Filed 2026-08-26 after five libraries written
    *after* the per-library-dylib convention landed all shipped as `.a`
    anyway. Real per-library dylibs now: `libresolv`, `libsystem_notify`
    (replaces libnotify.a + libnotify_client.a), `libbsm`, `libncurses`,
    `libcopyfile`, `libremovefile`, `libiconv`, `libsystem_m`,
    `libopenpam` (base library only), `libxpc`, `libiokituser`.

    **Standing rule this produced, still in force**: do not add a new
    static archive for anything that is a real, standalone, nameable
    library — build a real `.dylib` even if today's only consumer is
    static. Per-consumer curated-extraction bundles (`cf_libc_extras.a`,
    `launchd_libc_extras.a`, `syslogd_libc_extras.a`), per-daemon object
    archives, and `libmach_ipc.a` stay `.a` by design; `tools/init_binary`/
    `pwtool` (run before/without dyld) and `build_dyld_link.sh` (dyld
    can't depend on the dylibs it loads) are the real exceptions.

    Full inventory, the real duplicate-`_res` bug the conversion closed at
    its architectural root, and the two new gaps it surfaced (`mach_msg()`
    now living for real in `libsystem_kernel.dylib`; a real
    `libsystem_pthread_dylib_bootstrap_compat.c` constructor) are archived
    verbatim to `docs/historic/backlog.md`'s "Third archive pass,
    2026-08-30".

    **Still open, carried forward into item d12** — do not re-derive here:
    `libCoreFoundation.a` → `.dylib` (deliberately deferred, own future
    session — real load-time global state/constructors);
    `build_libsystem_c.sh`'s narrow curated `ar x` extraction from
    `libm.a`/`libremovefile.a`/`libopenpam.a` for ICU's sake (a real
    circular-build-order constraint, not just style — real follow-up is to
    reexport `libsystem_m.dylib`/`libopenpam.dylib` through
    `libSystem.B.dylib`, then drop the extraction);
    `libdispatch_min.a`/`libdispatch_real.a` likely dead since
    `libdispatch.dylib` became real, never actually checked for remaining
    consumers.

d9. **`build_simple_cmds.sh` converted from fully-static to real dynamic
    linking — RESOLVED 2026-08-28: 76 of 76 commands build, and the
    ld64-609 crash that blocked 32 of them is fixed for real.** This is
    the first real dyld-loaded consumer item d8 was waiting for, and
    confirms d8's own premise: a real dynamic consumer needs zero
    `-Wl,-alias` flags of its own (the ~60-entry `LINK_ALIASES` saga is
    gone), because `libSystem.B.dylib` already bakes the same real
    UNIX2003/INODE64/DARWIN_EXTSN aliases into its own build. Root cause
    of the 32 failures was a real, narrow `ld64-609` bug (`Assertion
    failed: (_mode == modeFinalAddress)` under `-Wl,-fixup_chains`), not
    this project's object code — fixed by passing `-B"$NEWLD_BINDIR"` (the
    daily-driver Xcode's much newer `ld`) on the final per-command link
    only, still compiling with Xcode 12's own `clang`/SDK throughout.

    **Two load-bearing findings from this item are standing rules, kept
    here rather than archived:**
    - **`-Wl,-alias` inside a dylib only lands in that dylib's real
      `LC_DYLD_EXPORTS_TRIE` when the alias's BASE symbol is genuinely
      local to that same link** — confirm with `xcrun dyld_info -exports`,
      **not** `nm` (nm lists the alias regardless, which is actively
      misleading). Add each alias at the specific sub-dylib where its base
      is local, never at the umbrella `libSystem.B.dylib` level for a
      reexported base.
    - **`tools/init_binary/inject_into_sd_image.sh` must be re-run after
      every `build_libsystem_c.sh`/`build_libsystem.sh` rebuild**, not
      just after `build_simple_cmds.sh`, or booted binaries resolve dylib
      imports against stale on-image exports (a real QEMU crash,
      `Symbol not found: _fdopen$UNIX2003`, was caught exactly this way).
      Related: any sub-dylib whose real, non-aliased symbols a consumer's
      own compiled code calls directly must ALSO be linked directly on
      that consumer's own command line — ld64 does not resolve a
      multi-level reexport chain through `libSystem.B.dylib` alone for
      this toolchain.

    Full bisection trail, the four real ports that closed the remaining 21
    commands (`table.c`/`none.c`; `ldpart.c`/`getcwd.c`/`findfp.c`;
    `collate.c`'s real `__LIBC__` compile bug plus `collcmp.c`; `date`'s
    own alias case), and both real regressions the follow-up QEMU boot
    surfaced (`cat`'s dead aliases over a linker behavior divergence;
    `ed`/`diff`/`less`'s missing `_pipe` bridge — a dormant regression
    from earlier in this same conversion, invisible until something
    actually launched under real eager chained-fixups binding, fixed via
    `tar_pipe_compat.c`) are archived verbatim to
    `docs/historic/backlog.md`'s "Third archive pass, 2026-08-30".

    **Still unverified**: `ed`'s own file-write behavior under a real
    interactive session (as opposed to this pass's scripted/piped test).
    This item is scoped to "does it link and boot," not full feature
    parity — e.g. `date`'s real clock-setting path is still stubbed (item
    13 below).

0a. **[DONE 2026-08-28] Real AES core wired: FreeBSD rijndael, not
    PureDarwin.** Filed 2026-08-21 as a hard prerequisite for Phase 7b
    (`sshd`); closed by vendoring public-domain `sys/crypto/rijndael/`
    (Rijmen/Bosselaers/Barreto — the AES designers themselves) into
    `libkern/crypto/freebsd/rijndael-alg-fst.c`, plus a corecrypto-shaped
    adapter (`libkern/crypto/corecrypto_aes_register.c`) implementing real
    `ccmode_ecb`/`ccmode_cbc`/`ccmode_ctr` against it and calling
    `register_crypto_functions()` with only the AES fields filled.
    FIPS-197/SP 800-38A known-answer vectors run at boot *through the real
    ccmode interfaces* (not the raw cipher core) and `panic()` on failure.
    `tcp_tfo_init`'s TCP Fast Open path now works for real instead of
    degrading. PureDarwin/corecrypto (scoped 2026-08-27) was investigated
    and deliberately NOT used — mixed-license AES core, AES-128-only, no
    CTR.

    Two real bugs found and fixed while wiring this up, both from real
    QEMU panics, neither guessed: the registration call had to move into
    `kernel_bootstrap_thread()` immediately before `machine_lockdown()`
    (writing `g_crypto_funcs`, a `SECURITY_READ_ONLY_LATE` global, from
    `bsd_init()` faults for real); and `corecrypto_rand.c`'s
    `cc_rand_generate()` needed a field-specific null check, since it
    guarded only on `if (g_crypto_funcs)` and then unconditionally called
    a deliberately-NULL `ccrng_fn` — root-caused via a real A/B test.
    Full writeup (the five-times-bitten `g_crypto_funcs` caller table,
    context sizing against `AES_CBC_CTX_MAX_SIZE`, the PureDarwin
    license/coverage analysis) archived to `docs/historic/backlog.md`'s
    "Third archive pass, 2026-08-30".

    **Still open, filed separately as P1 item 3 above**: six other
    `g_crypto_funcs->` call sites have no null check at all.

0b. **[DONE 2026-08-27] `ccsha256_di` wired to real corecrypto**,
    replacing the from-scratch `corecrypto_sha2.c` for code-signing/exec
    hashing — the real digest core was already sitting unused at
    `third_party/xnu-7195/osfmk/corecrypto/` and already compiled into the
    kernel for the PRNG's own use; only the SHA-256 call sites hadn't been
    pointed at it. QEMU-verified (`xnu-7195` commit `ceb6d419c`).
    **[DONE 2026-08-28]** SHA-384/512 and MD5 are off the from-scratch
    path too: no real corecrypto body exists for either in this checkout
    (confirmed absent, only headers), so per the Apple→FreeBSD→custom
    source priority both now use FreeBSD's real source (`sha512c.c`'s
    compress core behind this project's own buffering/padding wrapper;
    `md5c.c` wholesale, byte-identical `MD5_CTX` layout). Both
    QEMU-verified in the same boot that verified item 0a's AES work. Full
    writeups archived to `docs/historic/backlog.md` (its 2026-08-27 "item
    0b" section and the "Third archive pass, 2026-08-30" section).

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

d10. **Real, project-wide finding, 2026-08-29 — `-Wl,-U` does not safely
    defer an unreached symbol under this project's `-Wl,-fixup_chains`
    (chained fixups) convention, and this has NOT been re-verified for
    every consumer yet.** Found resuming launchd's own real QEMU boot
    after item d8's static-to-dylib pass: with chained fixups, real dyld
    resolves every bound `__got` entry at real image-load time, before
    any code runs — not lazily on first call. A flat-namespace `-Wl,-U`
    symbol that's genuinely unresolvable ANYWHERE in the loaded process
    aborts the whole process load (`symbol not found in flat namespace
    '_X'`, exit reason namespace 6 subcode 0x9) the instant that dylib is
    loaded, regardless of whether the referencing code path ever
    executes. This never mattered for this project's static
    (`-nostdlib -static`, no dyld) consumers, and never mattered for a
    dylib nobody had actually booted into a real process yet — **launchd
    is the first real, fully-dynamic process complex enough to load
    `libresolv`/`libxpc`/`libbsm`/`libsystem_notify`/`libdispatch` all at
    once and actually hit this.** `gap_audit.sh`'s static analysis does
    **not** catch this class of bug (it only checks whether a symbol is
    defined *somewhere* in the process's linked images, not whether real
    chained-fixups binding order would actually succeed) — only a real
    boot finds it, one `symbol not found` at a time.

    **~20 real symbols across `libxpc`/`libresolv`/`libbsm`/
    `libsystem_notify` were found+closed this way for launchd
    specifically** (real syscalls where a wrapper was genuinely missing,
    real honest "service not available" stand-ins where a whole
    subsystem doesn't exist here — mDNSResponder, protocol/service
    databases; real sibling-dylib links where a symbol already existed
    elsewhere) — see `tools/userland_staging/libxpc_csops_syscall.c`,
    `mach_error_string_minimal_compat.c`, `libresolv_dnssd_compat.c`,
    `libresolv_dns_internal_compat.c`, `libresolv_more_socket_syscalls.c`,
    `libresolv_netdb_compat.c`, `libsystem_notify_misc_compat.c`, and
    each one's own build script (`build_libxpc_dylib.sh`,
    `build_libresolv_dylib.sh`, `build_bsm_dylib.sh`,
    `build_libsystem_notify.sh`) for the full real, individually-cited
    trail — not re-summarized here.

    **What's NOT yet done, real remaining work, not to be lost**: those
    fixes closed exactly the symbols launchd's own particular dylib load
    set needed. A real re-derivation (`xcrun dyld_info -fixups
    <dylib>.dylib | grep '<flat-namespace>'`) on 2026-08-29 shows these
    dylibs still carry real, unverified flat-namespace deferrals that
    **only currently "work" because launchd.macho happens to provide the
    matching symbol locally** (via its own static portion or another
    linked dylib) — a **different** real consumer of the same dylib
    (`syslogd.macho`, `launchctl.macho`, `build_corefoundation.sh`'s
    trial link, any of `build_simple_cmds.sh`'s 76 commands) has **not**
    been re-verified and may hit the identical class of boot failure the
    moment it's actually run, not just linked:
    - `libresolv.dylib`: `___darwin_check_fd_set_overflow` (real,
      genuinely safe — see its own citation, real Apple header already
      marks it `weak_import`), `_kevent`/`_kqueue`/`_pipe` (real, resolve
      via `libxpc.dylib`/`libdispatch.dylib` being loaded alongside —
      true for any consumer that also loads one of those two, not
      guaranteed for one that doesn't).
    - `libbsm.dylib`: `_getegid`/`_getgid`/`_getgrgid`/`_getpwuid`/
      `_syslog` — all resolve today only via launchd's own local BSM/
      passwd-compat wrappers (`liblaunchd_core.a`).
    - `libsystem_notify.dylib`: `__xpc_bool_true`/`_bootstrap_look_up2`/
      `_xpc_dictionary_create`/`_xpc_dictionary_set_string`/
      `_xpc_dictionary_set_uint64`/`_xpc_release`/`_pipe` — all resolve
      today only via `libxpc.dylib` being loaded in the same process.
    - `libdispatch.dylib`: the full 11-symbol `_kernelrpc_mach_port_*`
      family, `__mach_errors`, `_nanosleep`, `_thread_get_special_reply_port`
      — all resolve today only via launchd's own local
      `liblaunchd_core.a`/`mach_port_gap_closures2.o` (a narrower, only-
      3-of-11-symbol local copy that happens to be enough because
      `libxpc_syscalls2.a`'s `mach_portUser.o` is *also* force-loaded
      into `liblaunchd_core.a` for other reasons, covering the rest).
      `build_libdispatch_dylib.sh` itself already documents this exact
      reliance in its own `UNDEF_SYMS_ELSEWHERE_IN_PROCESS` comment
      (written 2026-08-27, before this was ever actually exercised by a
      real launchd boot) — confirmed still accurate, not yet closed at
      the architectural root the way `libxpc`/`libresolv`/`libbsm`/
      `libsystem_notify` were.
    - `libncurses.dylib`: `_clock_get_time`/`_host_get_clock_service`/
      `_mach_host_self`/`_semaphore_create` — not yet investigated
      against any real consumer at all.
    - `libopenpam.dylib`: `___ldpart_free_extra`/`___part_load_locale`/
      `_alarm`/`_connect$UNIX2003`/`_getgroups`/`_initgroups`/
      `_send$UNIX2003`/`_setegid`/`_seteuid`/`_sigaction`/
      `_writev$UNIX2003` — not yet investigated against any real
      consumer at all.

    **Real next step for whoever picks this up**: before trusting
    `syslogd.macho`/`launchctl.macho`/any `build_simple_cmds.sh` command
    that loads one of the dylibs above, run a real QEMU boot/exec of it
    specifically (not just a clean link) — the exact same
    `tools/boot_test.py --expect-fail 'panic\('` + read-the-panic-string
    method that closed all ~20 launchd-specific gaps above. Don't assume
    a clean `ld` link or a passing `gap_audit.sh` means it will actually
    run.

d11. **RESOLVED 2026-08-29 (session 5, main-repo commit `76ff8cd`;
    diagnostics removed in `a1678ab`). Full write-up:
    `docs/launchd-bug6-postmortem.md`.**
    **Root cause: `_FORTIFY_SOURCE` self-recursion in
    `third_party/Libc/secure/*_chk.c`, a USERLAND BUILD-FLAG bug — not a
    kernel context-switch bug, not QEMU.** Apple builds that whole
    directory with fortify OFF because each `__X_chk()` falls through to
    plain `X()` on the "unbounded size" path, and `<stdio.h>`/
    `<string.h>`'s fortify inlines rewrite that call back into
    `__builtin___X_chk()` — itself. Unconditionally: the rewritten call
    passes `__darwin_obsz(dest)` == `(size_t)-1` for any pointer whose
    size isn't statically known (always true of a `_chk` function's own
    parameter), and "unbounded" is exactly the branch that calls the
    plain function. `___vsprintf_chk` was emitting
    `mov x2, #-0x1; bl ___vsprintf_chk`, blowing launchd's main-thread
    stack into its guard page on the first real `launchd_syslog()` call.
    Fix: `-D_FORTIFY_SOURCE=0` on every `Libc/secure/*_chk.c` compile in
    `build_launchd_core.sh`/`build_corefoundation.sh`/`build_libxml2.sh`/
    `build_libmalloc.sh`/`build_dyld_core.sh`, matching what
    `build_libdispatch_dylib.sh`/`build_libxpc_syscalls2.sh` already did
    (their `CFLAGS_SECURE` arrays) — which is precisely why *their* `_chk`
    functions were fine and these five scripts' were not. Swept every
    built artifact for the same class: `___memmove_chk` (launchd.macho +
    libxml2.dylib) and `___stpncpy_chk` (libxml2.dylib) were identical
    latent landmines; post-fix sweep finds zero self-recursing `_chk`
    functions anywhere.

    **Two decisive clues were sitting in already-captured logs, needing
    no new boots — worth internalizing for next time:**
    (1) `handle_user_abort`'s `codes[0]` was `0x2` =
    `KERN_PROTECTION_FAILURE`, i.e. `vm_fault()` FOUND a VM entry and
    refused the write => **guard page => stack exhaustion**. It was
    read as "page never mapped" (that would be `0x1`,
    `KERN_INVALID_ADDRESS`), and the whole kernel-side `SP_EL0` theory
    was built on that misreading. (2) `lr - pc` was exactly `0x50` in
    all five captured boots across five different kernel builds — the
    return address sits inside the same function faulting in its own
    prologue, the signature of direct self-recursion. **Decode fault
    result codes exactly, and check `lr` vs `pc` for self-recursion,
    before theorising about the scheduler.**

    Verified statically (`llvm-objdump`: `___vsprintf_chk` now calls
    `_vsprintf`/`_vsnprintf`) and by real boot
    (`qemu/launchd_boot_test74.log`): the `IOKITDIAG18` fault is gone (0
    occurrences; present in every prior boot), launchd mounts devfs,
    finishes its mutex work, and successfully spawns its **third** thread
    (task reports 3 threads, was 2 — the `update_thread` `pthread_create`
    that used to trigger the crash).

    **Follow-on #1 — the silent `exit(1)` — ALSO RESOLVED 2026-08-30**
    (commits `b01de02`, `ebfd181`, PureDarwin `2f0ef34`). The `pid 1
    exited ... (signal 0, exit 1)` with zero console output was TWO more
    duplicate-static-copy bugs (the 4th and 5th of that class): a TSD
    base clobber in `pthread_tsd_shim.c` and two private
    `__pthread_ptr_munge_token`s signing one shared `_main_thread`
    (launchd statically linked a second full copy of libpthread next to
    the real `libsystem_pthread.dylib`). Full write-up:
    **`docs/launchd-pid1-postmortem.md`**, which also covers the
    2026-08-30 `fork()`/atfork work below. The kernel-side
    `IOKITDIAG15`/`17`/`18` instrumentation referenced by the older text
    here has since been stripped (`a1678ab`, xnu `54de1e85c`); the
    `TEMP DIAGNOSTIC: devfs_mount ...` prints still in
    `bsd/miscfs/devfs/devfs_vfsops.c` on `bcm2837-bringup` are a
    separate leftover, tracked as its own P3 item below.

    **Follow-on #2 — forked children — PARTIALLY CLOSED 2026-08-30.**
    launchd as PID 1 now reaches `launchd_runtime()` and really forks
    daemon children. Those children used to die with a real
    `EXC_CORPSE_NOTIFY` (`qemu/launchd_boot_test95.log`, pids 3 and 4)
    because this port's `fork()` did almost none of what real
    `libSystem_atfork_child()` does. `fork()` now implements the real
    `libSystem_atfork_prepare/parent/child` triple (see
    `tools/userland_staging/build_launchd_syscalls.sh`'s `fork_compat.c`
    and `docs/launchd-pid1-postmortem.md`); the corpses are gone
    (`qemu/launchd_boot_test109_clean.log`) and children now get through
    `_mach_fork_child` → `_pthread_atfork_child` → `_malloc_fork_child`
    → `dispatch_atfork_child`. They still do not reach `exec`. The
    remaining known walls are filed individually as **d13** (dyld atfork),
    **d14** (`_libc_initializer` tier) and **d15** (residual atfork
    handlers) below — do not treat this item as the tracker for them.

    Historical context (sessions 2–5) archived, not deleted: the full
    investigation trail — the TPIDRRO_EL0 cross-image-confusion
    hypothesis, the third and fourth duplicate-static-copy bugs found in
    `mig_reply_port.o` and a duplicate `pthread_create`, the kernel-side
    `IOKITDIAG15`/`16`/`17`/`18` kprintf bisection down into
    `thread_block(ipc_mqueue_receive_continue)`, the raw
    `handle_user_abort()` fault-register capture that finally pointed at
    the MAIN thread, the `SP_EL0` cross-contamination theory, and the
    `Switch_context` bracketing attempt that caused its own separate crash
    and had to be reverted — is archived verbatim to
    `docs/historic/backlog.md`'s "Third archive pass, 2026-08-30".
    **Note its "second thread"/`TPIDRRO_EL0`/`SP_EL0` framings are all now
    known wrong**; read `docs/launchd-bug6-postmortem.md` and
    `docs/launchd-pid1-postmortem.md` first, plus project memory
    `project_xnu_rpi3_launchd_pid1_2026-08-28_session2.md` /
    `project_xnu_rpi3_static_to_dylib_sweep_2026-08-29.md`.

    Two lessons from that trail are standing rules and are kept here:
    decode fault result codes exactly and check `lr` vs `pc` for
    self-recursion *before* theorising about the scheduler; and
    `kprintf()` is not safe to call from inside `machine_switch_context()`
    on a brand-new thread's first-ever resume (a `task != kernel_task`
    guard is not sufficient).

d12. **Missing/incomplete library work, explicitly tracked so it isn't
    lost (cross-referenced from item d8, not duplicated in full here)**:
    - `libCoreFoundation.a` → `.dylib`: deliberately deferred, own future
      session (real load-time global state/constructors, largest/
      riskiest remaining conversion) — see item d8's own "RESOLVED
      2026-08-29" note.
    - Real per-module PAM `.dylib`s: out of scope, this project has no
      `dlopen` — `libopenpam.dylib` (base library only) is as far as
      this converts; documented, not attempted further.
    - `build_libsystem_c.sh`'s own narrow curated `ar x` extraction from
      `libm.a`/`libremovefile.a`/`libopenpam.a` (acos/asin/sqrt/fmod/
      qsort/heapsort/time/gettimeofday/etc, needed by ICU): real,
      deliberately deferred residual — lower risk than a full-archive
      duplicate-global-state bug (mostly stateless math/qsort/time
      functions), but still real static-linking debt. Real follow-up
      path already scoped in item d8's own note: reexport
      `libsystem_m.dylib`/`libopenpam.dylib` through `libSystem.B.dylib`
      (matching the pattern that already closed the equivalent
      `libresolv`/`libcopyfile`/`libremovefile` need for ICU), then drop
      the curated extraction entirely.
    - `libdispatch_min.a`/`libdispatch_real.a`: still not investigated —
      likely dead now that `libdispatch.dylib` is real (Phase 6.6), but
      nobody has actually checked for remaining real consumers before
      proposing deletion. Still open.
    - `tools/userland_staging/build_libpthread.sh`'s own
      `pthread_smoketest.macho` link is currently broken (real,
      pre-existing, found 2026-08-29 while rebuilding this library for
      unrelated reasons — not caused by that session's own changes):
      fails with undefined `_kdebug_trace`/`_kill`/`_kill$UNIX2003`/
      `_mprotect$UNIX2003`/`_sleep$UNIX2003`/`_write$UNIX2003` (real
      `-Wl,-alias`/raw-syscall gaps in that one smoketest's own link
      line, same class of gap as this file's other items, just not yet
      triaged). The real `libpthread.a` archive itself still builds and
      is correct; only its own standalone smoketest executable fails to
      link. Not investigated further this pass.

d13. **Real dyld atfork hooks exist and are built, but hang every forked
    child — currently NOT called (filed 2026-08-30).** Real Apple
    `libSystem_atfork_child()` calls `_dyld_fork_child()` (and
    `_dyld_atfork_prepare()`/`_dyld_atfork_parent()` on the other two
    sides). All three are real, present in `third_party/dyld`, and really
    exported by this project's `libdyld.dylib` (`nm -gU`). This project's
    `fork()` (`tools/userland_staging/build_launchd_syscalls.sh`'s
    `fork_compat.c`) deliberately does **not** call them, because doing so
    is a measured regression: with them in, every forked launchd child
    reached `_dyld_fork_child()` and hung there forever — no crash, no
    corpse, just a stuck process (`qemu/launchd_boot_test107_atexit_fix.log`:
    the console markers for `_mach_fork_child`, `_pthread_atfork_child`
    and `_malloc_fork_child` all printed for all three children, the
    marker immediately after `_dyld_fork_child()` never did). With them
    removed the same children get all the way through
    `dispatch_atfork_child()` (`qemu/launchd_boot_test108_nodyld.log`).

    **Not diagnosed — this is the lead, not the answer.** Do not write it
    up as root-caused until it is actually confirmed:
    `RuntimeState::takeLockBeforeFork()`
    (`third_party/dyld/dyld/DyldRuntimeState.cpp`) takes `loadersLock`,
    `notifiersLock` and `tlvInfosLock` unconditionally, while the matching
    `resetLockInForkChild()` only resets them when
    `libSystemHelpers->version() >= 2`; and the reset itself calls
    libplatform's `os_unfair_recursive_lock_unlock_forked_child()`
    (`third_party/libplatform/src/os/lock.c`), which by design
    `__LIBPLATFORM_CLIENT_CRASH__`es with "Lock was not held" when the
    lock does not look held. Either half of that mismatch strands a
    forked child. **First thing to check is this port's real
    `LibSystemHelpers::version()`** (`third_party/dyld/libdyld/
    LibSystemHelpers.cpp`), then whether `gDyld.apis` and `gProcessInfo`
    are what dyld expects in a forked child here. Note `build_dyld_core.sh`
    builds with `-DBUILDING_DYLD=1` (so those bodies are live) while
    `build_libdyld.sh` uses `-DBUILDING_DYLD=0` (glue only).

    Leaving them out is not a regression against the pre-2026-08-30 state:
    this port's `fork()` never called any dyld atfork hook at all before.

d14. **`Libc/sys/_libc_init.c` — real Apple's `_libc_initializer()` — is
    never built, and nothing calls the eight real initializers it runs
    (filed 2026-08-30, one of them narrowly worked around).** Real Apple
    runs, once per image, at libSystem init time:
    `_program_vars_init`, `_libc_fork_init`, `__confstr_init`,
    `__atexit_init`, `_init_clock_port`, `__chk_init`, `__xlocale_init`,
    `__guard_setup`, `_subsystem_init`. This project has never built that
    file — several of the supporting Libc sources it needs
    (`gen/nanosleep.c` for `_init_clock_port`, `stdlib/FreeBSD/getenv.c`'s
    lock half, `_subsystem_init`'s `apple[]` plumbing) are not in this
    port's libc tier at all — so every one of those steps silently does
    not happen.

    **This is not theoretical; it already produced one real, totally
    silent boot hang.** `atexit()` hung forever in every binary linking
    `liblaunchd_syscalls.a`'s `atexit.o`. Real `atexit_register()`
    (`Libc/stdlib/FreeBSD/atexit.c`) opens with `struct atexit *p =
    __atexit; assert(p);`, and `__atexit` is a file-static that only
    `__atexit_init()` ever sets — from `_libc_initializer()` and nowhere
    else. So `__atexit` stayed `NULL`, the assert fired, and on this port
    `assert()`'s `abort()` is `kill(getpid(), SIGABRT)` with no real
    signal delivery yet, so instead of dying the process spins forever —
    silently, with launchd's `stderr` on `/dev/null`. It went unnoticed
    for months only because launchd's one boot-path `atexit()` call site
    (`ipc_server_init()`'s `atexit(ipc_clean_up)`) was unreachable: that
    function bailed out several statements earlier at
    `mkdir(LAUNCHD_SOCK_PREFIX)` returning `EROFS` while the root
    filesystem was still read-only. Fixing that ordering made the call
    reachable and turned the boot into a silent hang.

    **What was done**: `__atexit_init()` alone is now called from a real
    image constructor in `tools/userland_staging/
    launchd_syscall_compat_headers/isthreaded_compat.c` (that file,
    because `__atexit_init` is `__private_extern__` so only a same-image
    object can reach it, and because `__isthreaded` is referenced by
    `atexit.c` itself so the linker pulls that object out of the archive
    exactly when `atexit.o` is pulled and never otherwise). It is
    idempotent — `__atexit_init()` is one `os_alloc_once()` call.
    Verified by real boot (`qemu/launchd_boot_test107_atexit_fix.log`,
    `..._109_clean.log`).

    **What is still open**: the other seven initializers, for every image
    in this project, not just launchd. `third_party-patches/Libc.patch`
    already carries a second one-off of the same shape — a ctor calling
    `__xlocale_init()` for `__locale_key` — which is the same bug class
    solved the same ad-hoc way (see the `### d11` locale section below).
    Two one-offs is the point at which this should stop being handled
    per-symbol: either build `_libc_init.c` for real (closing
    `_init_clock_port`/`__environ_lock_fork_child`/`_subsystem_init`'s own
    gaps on the way, which also unblocks `_libc_fork_child()` in d15) or
    write one deliberate, documented per-image init constructor that runs
    exactly the steps this port really has. **Whoever touches this should
    also sweep for other Libc globals with the same "only
    `_libc_initializer()` ever sets it" shape** — there is no reason to
    believe `__atexit` and `__locale_key` are the only two.

d15. **Real atfork handlers still missing from this port's
    `libSystem_atfork_child()` (filed 2026-08-30).** `fork()` now runs the
    real Libsystem-1292.50.1 sequence, but four real handlers in it are
    genuinely absent here and two more are present-but-unreachable. Full
    per-symbol writeup lives in `fork_compat.c`'s own header comment in
    `build_launchd_syscalls.sh`; summarized so it is findable:
    - `_libc_fork_child` — the source exists
      (`third_party/Libc/sys/_libc_fork_child.c`) but two of its three
      callees do not: `_init_clock_port` (`Libc/gen/nanosleep.c`) and
      `__environ_lock_fork_child` (`Libc/stdlib/FreeBSD/getenv.c`) are
      absent from the built `libsystem_c.dylib` (`_arc4_fork_child` is
      there, as a non-exported local). Blocked on the same missing libc
      tier as **d14**, and must be compiled *into* `libsystem_c.dylib` to
      see its own private callees.
    - `xpc_atfork_prepare`/`_parent`/`_child` — PureDarwin's libxpc
      reimplementation has no fork handlers at all (zero hits for
      `xpc_atfork` anywhere in `third_party/PureDarwin`). A real gap in
      that source, not an omission here. Relevant: `bootstrap_init()`,
      which is exactly where the forked children used to die, is libxpc
      code.
    - `_notify_fork_child` — real and built
      (`libsystem_notify.dylib` exports it), but `launchd.macho` does not
      link `libsystem_notify.dylib` and `libSystem.B.dylib` does not
      reexport it, so calling it means a real link-line change to every
      consumer of `liblaunchd_syscalls.a`. Wire it up when something in
      launchd actually uses libnotify across a fork.
    - `cc_atfork_*` (corecrypto), `_asl_fork_child`,
      `_libcoreservices_fork_child`, `_libtrace_fork_child`,
      `_libSC_info_fork_*` — those subsystems do not exist in this port at
      all.

    **Concrete lead for the current child wall.** With dyld's trio omitted
    (**d13**), forked children get through `dispatch_atfork_child()` and
    then stop at the last step, `_pthread_atfork_child_handlers()`
    (`qemu/launchd_boot_test108_nodyld.log`). That step is not a no-op
    here: `nm -m launchd.macho` shows real `___cf_atfork_prepare` and
    `___cf_atfork_child` in the image, i.e. **CoreFoundation really does
    register `pthread_atfork()` handlers in this process**, and
    `_pthread_atfork_child_handlers()` is what invokes them in the child.
    So the next thing to instrument is CF's own atfork child handler, not
    libpthread's iteration loop. Note also that `pthread_atfork()` handler
    *registration* is what `globals->pthread_atfork_lock` protects, and
    `_pthread_atfork_prepare_handlers()`/`_pthread_atfork_parent_handlers()`
    take/release it around the fork — verify that pairing holds in this
    port before assuming the handler body itself is at fault.

d16. **`_malloc_fork_child()` only reinitializes launchd's own private
    heap, because `launchd.macho` has two heaps (filed 2026-08-30).**
    `nm -m launchd.macho` shows `_malloc`, `_free`, `_calloc`, `_g_zone`
    and `_create_scalable_zone` all defined **locally** in the executable,
    while it also loads `libSystem.B.dylib` (which reexports
    `libsystem_malloc.dylib` with its own separate `g_zone`). So
    `libdispatch`/CF/libc++ allocate from one zone and launchd's own code
    from another, and the new `_malloc_fork_child()` call in `fork()`
    binds to launchd's local copy and reinitializes only that zone. This
    is exactly the already-documented Phase 2b condition in
    `docs/static-linking-cleanup-plan.md` ("`launchd.macho` is the
    highest-risk one: it defines `_malloc`, `_free`, ... locally despite
    loading `libSystem.B.dylib`") — closing Phase 2b fixes this hook for
    free. Deliberately no `dlsym(RTLD_NEXT)` workaround was added to paper
    over it.

d17. **`tools/userland_staging/build_launchctl.sh` does not link — real,
    pre-existing, found 2026-08-30.** Confirmed pre-existing by rebuilding
    it with every one of that session's changes stashed: same failure.
    The `launchctl.macho` that had been sitting in `libc_build/` was a
    stale artifact from before `libCoreFoundation.a` grew the call sites
    that break the link, i.e. it was **not reproducible from source** —
    a direct violation of this repo's "every built artifact needs a
    reproducing script" rule, and the reason the breakage went unnoticed.
    (The script `rm -f`s its output before linking, so the stale copy is
    gone now.)
    - **Fixed**: undefined `___chkstk_darwin` (referenced by CF's
      `CFAttributedString.o`/`CFBinaryPList.o`). `libsystem_c.dylib` must
      be listed *explicitly* on the link line, not left to
      `libSystem.B.dylib`'s `LC_REEXPORT_DYLIB` of it — ld64 cannot follow
      a reexport by install-name when `/usr/lib/system/libsystem_c.dylib`
      does not exist in the on-disk SDK/sysroot, which it does not here.
      `build_launchd.sh` already documents this at length above its own
      link line; `build_launchctl.sh` had simply never had the same fix.
    - **Still open**: undefined `_lchflags` (`launchctl.c`'s
      `empty_dir()`) and `_sysctlnametomib`
      (`limitloadtohardware_iterator()`). Real Apple source for both is
      already pinned — `third_party/Libc/emulated/lchflags.c` and
      `third_party/Libc/gen/FreeBSD/sysctlnametomib.c` — but each pulls
      its own further real dependencies (`lchflags` needs `chflags` +
      `setattrlist`; `sysctlnametomib` needs the raw `__sysctl` trap,
      which this stack does not currently generate — only the public
      `sysctl` exists). Expect a short cascade, same shape as every other
      gap-closure pass here.

d18. **Possible duplicate `_getenv` inside `libsystem_c.dylib` (filed
    2026-08-30, unverified).** `nm -a libc_build/system/libsystem_c.dylib`
    reports **two** `_getenv` symbols — one external (`T`, `0x21984`) and
    one local (`t`, `0x2fc48`). Two implementations of `getenv` in one
    image is the same duplicate-static-copy shape that produced bug #6 and
    the duplicate-`pthread_create` bug, and `getenv`/`setenv` share
    process-global `environ` state, so it is exactly the risky kind. Not
    investigated at all — noticed in passing while checking that image for
    `__environ_lock_fork_child` (see **d15**). Confirm with
    `tools/userland_staging/dup_symbol_audit.sh` and find which two
    objects define it before assuming it is benign.


---

**Note on duplicate IDs**: the two `### d10`/`### d11` headed sections
below predate the numbered `d10.`/`d11.` items in the "Userland library
debt" list above and are *different* items with the same short IDs. Source
comments referring to "item d10"/"item d11" may mean either — the
`libSystem.B.dylib`/Libm-gap and locale/`strftime_l` references mean the
`###` sections below; the chained-fixups and launchd-bug-#6 references mean
the numbered items above. Both are kept resolvable deliberately.

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

5. **`bsd/miscfs/devfs/devfs_vfsops.c`'s `TEMP DIAGNOSTIC: devfs_mount ...`
    prints are still live, and are committed on `bcm2837-bringup`** — not
    a dirty working tree, so `git status` in `third_party/xnu-7195` looks
    clean and hides them. They fire on every `devfs` mount and are all
    over every recent boot log (e.g.
    `qemu/launchd_boot_test109_clean.log`), which is exactly the noise P3
    item 3 above describes. They are a leftover from the launchd PID-1
    devfs bring-up, whose real work is long since done. Same treatment as
    item 3's msdosfs/sdhci prints: gate behind a default-off flag or drop.
    Noticed 2026-08-30 while reading launchd boot logs; the surrounding
    launchd bug #6 kernel instrumentation (`IOKITDIAG15`/`17`/`18`) was
    already correctly stripped in `a1678ab`/xnu `54de1e85c`, this one was
    simply missed.
