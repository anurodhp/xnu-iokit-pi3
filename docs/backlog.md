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
