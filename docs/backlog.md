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

2. **`SMSC9514Ethernet` has several real logic bugs**, all currently
    unreachable since the class has no personality and is never
    instantiated (no urgency, but worth fixing before it's wired up):
    - `smsc95xx_reset()`-equivalent code does blind register writes to
      `HW_CFG`/`PM_CTRL`/`MAC_CR` where Linux does read-modify-write,
      clobbering unrelated bits (`BIR`/`RXDOFF`/`MEF`, full-duplex/
      auto-speed defaults).
    - The PHY-reset loop doesn't check its own timeout flag before
      continuing as if it succeeded.
    - `mdioRead`/`mdioWrite` are never called from anywhere — the PHY is
      never initialized/auto-negotiated, yet `enable()` unconditionally
      sets `fLinkUp = true`.
    - `attachInterface()` is never called from `start()`, so no
      `IONetworkInterface` can ever be created — matches the "no
      personality, unreachable" state above.
    - `getPacketFilters()` advertises multicast filtering while
      `enablePacketFilter()` is a no-op — will break IPv6 ND the moment a
      transport exists.
    - `fInterface` is read without a retain/lock from a USB-completion
      context that can race `disable()` setting it to NULL — UAF shape.
    - `frameLen & 0x7FF` truncates silently with no length guard.

---

## P2 — known gaps / incomplete work (not bugs, just unfinished)

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

3. **`build_kexts.sh` only standalone-compiles 3 of 8 driver sources**
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

5. **No real POSIX regex (`regcomp`/`regexec`/`regfree`) built.**
    `rm -i`'s `rpmatch()` currently uses a real but narrowly-scoped
    C-locale-only replacement (`simple_cmds_rpmatch_compat.c`) instead
    of pulling Apple's real regex implementation (likely already present
    in this checkout, not yet confirmed/built). Blocks real bash
    `[[ =~ ]]` and any future `grep`/`sed`-style command. See
    `docs/roadmap.md`'s Phase 4.6.

6. **`setattrlist(2)` never wired to userland**, even though the real
    kernel-side implementation already exists unmodified in this
    project's own `third_party/xnu-7195/bsd/vfs/vfs_attrlist.c` — purely
    a missing raw-syscall-stub-generation gap, the cheapest item in this
    batch to close. Currently worked around with an honest `ENOSYS` in
    `simple_cmds_lutimes_compat.c` (blocks `touch -h` on symlinks only).
    See `docs/roadmap.md`'s Phase 4.6.

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

3. **~30 `TEMP DIAGNOSTIC` `printf`/`IOLog` calls remain in HEAD**, several
    on hot paths (`bsd/msdosfs/msdosfs_vnops.c` on every read/blockmap,
    `iokit/Kernel/IOService.cpp` on every `registerService`/`startMatching`).
    At real mini-UART baud rates (115200) each line costs milliseconds of
    synchronous busy-wait — combined with P0 item 3's slow clock, this would
    make a working real-hardware boot look like a hang. Prune once no
    longer needed for active debugging.

4. **`BCM2837CPU::quiesceCPU()`/`haltCPU()` are silent no-ops** — correctly
    hedged as fine for now (single core, no sleep/shutdown ever attempted),
    but will need real implementations before any power-management work.
