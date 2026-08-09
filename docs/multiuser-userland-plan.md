# Minimal multi-user userland: what's needed, what's open source

Written 2026-08-09. Scope: what it takes to get real distinct user accounts,
login/authentication, and enforced per-user permissions on top of this
project's XNU port (`third_party/xnu-7195`, Darwin 19.x-ish ABI, branch
`bcm2837-bringup`). This is a research/planning document, not an
implementation — nothing here has been built yet.

**Anchor version**: `xnu-6153.11.26` = Darwin 19.0.0 / macOS 10.15.0 GM,
already established in [[reference_apple_oss_distributions]] as this
project's version anchor for `apple-oss-distributions` lookups. Where a
tag's exact vintage couldn't be confirmed this pass, it's flagged below
rather than guessed — `apple-oss-distributions`' git commit dates are
**not reliable** for this: every tag checked on the `pam` repo (`pam-9`
through `pam-32.1`) shares the identical commit timestamp
(`2021-10-06T05:29:20Z`), meaning the whole tag history was bulk-imported
at once during Apple's opensource.apple.com → GitHub migration (~2021-2022,
per existing memory) — commit-date proximity to `xnu-6153.11.26`, the
technique the existing memory file recommends, does not actually
discriminate between these tags. Tag *numbers* still roughly track release
order within one project, just not against other projects' numbering.

---

## 0. Hard architectural constraint, independent of every component below

**FAT32 (the SD card's current, already-formatted filesystem — see
[[project_xnu_rpi3_userspace_libc_dyld]]) has no on-disk concept of Unix
ownership or permission bits at all.** Its metadata is limited to DOS
attribute flags (read-only, hidden, system, archive, directory, volume
label) — no uid, no gid, no mode. Every `msdosfs`-family driver (Darwin's
included) can only *synthesize* Unix-looking permissions at mount time via
fixed mount options (`mount_msdos`'s `-u`/`-g`/`-m`/`-M` flags: one uid,
one gid, one mode, applied uniformly to *every* file on the volume). There
is no way for two different files on a FAT32 volume to have different
owners or different permission bits — full stop, this is not a userland
software gap, it's the on-disk format.

**Consequence**: "real distinct user accounts with enforced per-user file
permissions" cannot be achieved for anything stored directly on the
current SD root, no matter how complete the userland below is. The kernel
VFS/credential layer would enforce permissions correctly (that part is
already standard, working xnu) — but every file would show the same
single owner/mode `mount_msdos` was configured with, so there's nothing
*to* enforce differently per file.

**Options, not mutually exclusive, not evaluated further here (out of
scope for this pass)**:
1. Accept the limitation for anything under the FAT32 root, but put
   genuinely per-user-permissioned state in a separate, real Unix
   filesystem — e.g. a disk image file living *on* the FAT32 card,
   loop-mounted as `hfs` or a simple custom filesystem, exactly the way
   `sdcard/` bootstraps today. This is probably the pragmatic path.
2. Reformat the SD card to a real Unix-permission-bearing filesystem
   (`hfs`, per [[reference_apple_oss_distributions]]) instead of FAT32 —
   loses the current u-boot/firmware FAT32 requirement's convenience,
   real architectural tradeoff, not investigated here.
3. Live with single-owner semantics and implement "multi-user" as
   process/session isolation (different login identities, different
   `$HOME` *paths*, `id`/`whoami` correctness) without real per-file
   permission enforcement. Weakest interpretation of "multi-user" but
   might be an acceptable phased first step.

**This should be resolved/chosen before investing heavily in the
components below** — it changes what "done" looks like for several of
them (e.g. `chpass`/`passwd` writing a real per-user home directory with
real permissions only matters once there's a filesystem that can hold
that).

---

## 1. User/group identity database

**Finding: flat-file-only multi-user auth, with zero `opendirectoryd`
dependency, is real and achievable.** `Libinfo` (already known-open, see
existing memory) has a pluggable lookup-module architecture in
`lookup.subproj/`, confirmed via the actual source tree
(`apple-oss-distributions/Libinfo`, `contents/lookup.subproj/`):

- `file_module.c` — reads flat `/etc/passwd`, `/etc/group` (and the
  BSD-style `/etc/master.passwd`) directly. No daemon, no RPC.
- `ds_module.c` — talks to `opendirectoryd`/DirectoryService via XPC.
  **This is the one to not use** — everything else works without it.
- `cache_module.c` — optional caching layer over whichever backing
  module(s) are active.
- `search_module.c` — orchestrates module search order per-database
  (passwd/group/etc.), confirmed (via `grep` on the actual source) to
  read an `/etc/nsswitch.conf`-style config and to have a real default
  order array containing both `"ds"` and `"file"` as named sources
  (`search_module.c` lines ~983/986 in the checked revision) — i.e. this
  is genuinely the same mechanism classic BSD/Linux NSS config is, not
  something Apple removed. **Setting the search order to `file` only in
  `/etc/nsswitch.conf` (or whatever this vintage's equivalent config path
  is — verify exact filename/format against this Libinfo tag once pulled)
  is the whole fix.**

**Companion tools, also confirmed open** (`apple-oss-distributions/system_cmds`):
`pwd_mkdb` (builds the hashed passwd database from `/etc/master.passwd`,
the real BSD account-provisioning tool), `vipw`/`vifs` (safe editors for
the master files), `chpass`, `passwd`, `newgrp`. This is the complete
classic-BSD flat-file account toolchain, not a subset — matches how
FreeBSD itself does accounts natively (no daemon there either), so no
porting/semantic mismatch expected for this piece specifically.

**Dependency ordering**: `Libinfo` needs `Libc`/`libpthread`/etc.
underneath it (already-known libSystem-stack pull, see existing memory) —
nothing new here, just confirms Libinfo itself doesn't add new
prerequisites beyond what was already planned.

**Not yet checked / flagged**: the exact on-disk hash format
`file_module.c` expects in `/etc/master.passwd` (classic BSD DES `crypt()`
vs. something newer Apple moved to) — see §2's `crypt()` finding, likely
resolves this, but the two weren't cross-checked against each other in
this pass. Also not checked: whether `pwd_mkdb`'s hashed-db output format
(traditionally Berkeley DB) has a working Berkeley DB dependency in this
Darwin-19 vintage's `Libc` (`Libc/db/` exists per the tree listing above,
presumably fine, not independently confirmed).

## 2. Authentication / login

All confirmed open source, all in `apple-oss-distributions/system_cmds`
(source tree directly listed via `gh api repos/.../system_cmds/contents/`):

- **`login`** — real source present (`system_cmds/login`).
- **`getty`** — real source present (`system_cmds/getty`), the classic
  BSD TTY-session-spawning daemon `init`/`inittab`-equivalent would launch
  per serial line. Given this project's console is a real serial UART
  (see [[project_xnu_rpi3_status]]), `getty` on the console tty is the
  natural login-prompt mechanism once a PID-1/`init` exists to spawn it.
- **`crypt()`** — real source present, `Libc/gen/crypt.c` +
  `Libc/gen/crypt.3` (man page also shipped, confirming it's a real
  public API at this vintage, not deprecated-and-hidden).
- **utmp/wtmp session accounting** — **not checked this pass, flagged**.
  Darwin is known to have diverged from classic BSD utmp at some point in
  its history (real macOS `who`/`w` behavior differs from FreeBSD's); the
  `adv_cmds` tree (confirmed open) has `last`/`finger`/`tabs`, and
  `shell_cmds` has `who`/`w`/`users`/`logname` — strongly suggesting *some*
  session-accounting mechanism exists and is open, but which one (classic
  `utmp.h`, or Darwin's own `utmpx`/`asl`-based replacement) needs a direct
  header/source check before relying on it. Not a blocker for a *minimal*
  multi-user setup (login can work without `who` being accurate), just
  flagged as unverified.

**Build order implication**: `login` needs `Libinfo` (§1, for the account
lookup) and `crypt()` (`Libc`, already in the planned pull) before it can
authenticate anyone; `getty` needs a real PID-1/`init` to spawn it per
line (see [[project_xnu_rpi3_next_phase_plan]] — the custom-init decision
already made, `getty` slots in as one of the things a real `init` execs,
same role `tools/init_binary/`'s hand-rolled shell currently occupies).

## 3. PAM

**Finding: Apple's own PAM is open source** — `apple-oss-distributions/pam`
exists, tagged `pam-9` through `pam-32.1`. **Exact Darwin-19-matching tag
not confirmed this pass** (see the commit-date caveat at the top of this
doc — the normal cross-reference technique doesn't work for this repo).
Flagged as an open question to resolve interactively: either pull a few
candidate tags and check embedded version strings/`CHANGES` files against
known Catalina PAM behavior, or just take the newest available (`pam-32.1`)
on the assumption PAM's module ABI is stable enough across a few years of
Darwin versions that exact matching isn't critical for a minimal setup
(unlike e.g. `xnu`/`Libc` ABI matching, which does matter more).

**Fallback if the Apple tag turns out unusable**: **OpenPAM (FreeBSD's PAM
implementation) is a real, viable drop-in** — worth noting explicitly
since Apple's own PAM is historically *based on* OpenPAM (shared lineage,
not just "similar"), so this isn't a risky cross-platform port the way
some other FreeBSD-sourced pieces might be; it's closer to reverting to
the upstream project Apple's version already derives from. Not
independently verified against this exact Darwin-19 ABI this pass — flag
as the fallback path, try Apple's own tag first.

**Scope note**: a genuinely *minimal* multi-user setup could plausibly
skip PAM entirely — `login` calling `crypt()`+`Libinfo` directly (classic
pre-PAM BSD style) is simpler and has fewer moving parts. PAM mainly
starts mattering once more than one authentication method or policy
(password aging, lockout, etc.) is wanted. Recommend treating PAM as a
**phase-2 nicety, not phase-1 blocker** — sequence it after a working
direct-auth `login` if minimizing time-to-working-multi-user is the goal.

## 4. Privilege/session tools

- **`su`** — confirmed open source, `apple-oss-distributions/shell_cmds/su`.
  Same account-lookup (`Libinfo`) and auth (`crypt()`, optionally PAM)
  dependencies as `login`, nothing new.
- **`sudo`** — **not in any `apple-oss-distributions` repo** (confirmed by
  its absence from both `shell_cmds` and `system_cmds` listings, and real
  macOS itself doesn't build sudo from Apple-owned source — it bundles the
  independent Todd Miller `sudo` project as a binary). **Flagging per the
  original instruction rather than chasing further**: porting real `sudo`
  is a separate, larger undertaking (its own source tree, its own
  `sudoers` parser, its own PAM integration) — plain `su` covers the
  "become another user" need for a minimal setup; `sudo` should be
  considered explicitly out of scope unless a future pass specifically
  wants it.

## 5. Shell + baseline utilities

Already substantially covered by existing memory
([[reference_apple_oss_distributions]]: `bash`, `shell_cmds`, `file_cmds`,
`text_cmds`, `adv_cmds`, `system_cmds`, `basic_cmds`, `developer_cmds`,
`misc_cmds`, `Csu`) — confirming/extending rather than redoing:

- Every repo named above was independently confirmed to exist and be
  reachable via `gh api repos/apple-oss-distributions/<name>` during this
  pass's spot-checks (`shell_cmds`, `system_cmds`, `adv_cmds`, `Libc` all
  directly listed above).
- `adv_cmds/ps`, `adv_cmds/last`, `system_cmds/w`-equivalent
  (`shell_cmds/w`) are the process/session-visibility tools a plausible
  multi-user system needs beyond the bare shell — all confirmed present.
- Nothing new found that changes the existing "real bash + real corequtils
  is achievable, sequenced after the dynamic-linker pull" conclusion.

## 6. Filesystem-permission userland support

Covered in §0 above as the headline finding — the kernel's own VFS
credential enforcement is standard, unmodified xnu and needs no userland
help *once there's a filesystem that carries per-file ownership at all*.
The actual gap is architectural (FAT32), not a missing userland tool.

One userland-level piece worth flagging while on this topic:
**`diskdev_cmds`** (already known-open per existing memory, for
`mount_hfs`/`newfs_hfs`/`fsck_hfs` + msdosfs equivalents) is also where
`chmod`/`chown`-adjacent *mount-time* configuration lives for FAT
(`mount_msdos`'s uid/gid/mask flags) — if Option 1 in §0 (real filesystem
in a loopback image) is chosen, `diskdev_cmds` covers both sides (FAT for
the outer card, real filesystem tooling for the inner image).

---

## Suggested build order

Numbered by dependency, not necessarily by calendar effort:

1. **Resolve §0 (the FAT32 constraint)** — decide the filesystem strategy
   before building anything whose value depends on it.
2. **libSystem stack** (already planned, see existing memory) — `dyld`,
   `Libc` (includes `crypt()`), `libpthread`, `libplatform`, `libdispatch`,
   `libclosure`. Prerequisite for everything below (all real dyld-linked
   binaries).
3. **`Libinfo`**, configured for `file`-only lookup (skip `ds_module.c`
   entirely, no `opendirectoryd` needed) — the actual "user database"
   deliverable.
4. **Account-provisioning tools**: `pwd_mkdb`, `vipw`, `vifs`, `chpass`,
   `passwd`, `newgrp` (`system_cmds`) — needed to actually *create* the
   flat-file account database `Libinfo` will read.
5. **Custom PID-1** (already planned/in-progress per
   [[project_xnu_rpi3_next_phase_plan]]) — needs to exist before `getty`
   has anything to be spawned by.
6. **`login` + `getty` + `crypt()`** — the actual login path. Direct
   `crypt()`+`Libinfo` auth first (skip PAM initially per §3's
   recommendation).
7. **`su`** — trivial once `login`'s auth path works, shares the same
   dependencies.
8. **Shell + coreutils** (`bash`, `shell_cmds`, etc.) — likely developed
   in parallel with the above, not strictly sequential.
9. **PAM** (Apple's own tag, or OpenPAM fallback) — phase-2, once a
   working direct-auth path already exists and PAM's added flexibility
   (multiple auth methods, policy) is actually wanted.
10. **`sudo`** — explicitly deferred, not scoped for a minimal setup.

---

## Flagged problems / open questions (gathered)

1. **`apple-oss-distributions` commit dates don't reliably indicate
   original release vintage** (bulk-migration artifact, confirmed on
   `pam`, likely affects other infrequently-touched repos too) — the
   existing memory's "cross-reference by commit date" technique needs a
   different method (embedded version strings, `CHANGES` files, or just
   tag-number-within-project + accept some slop) for repos where this
   applies. Worth re-checking whether the *already-pulled* `xnu`/`Libinfo`
   type repos are affected the same way before trusting any date-based
   claims made about them elsewhere.
2. **Exact Darwin-19-matching PAM tag not identified** — have the repo and
   tag list (`pam-9` .. `pam-32.1`), don't have confirmation of which one
   actually corresponds to Catalina-era Darwin 19. OpenPAM flagged as a
   viable fallback given Apple's PAM is historically OpenPAM-derived.
3. **utmp/wtmp session-accounting mechanism unverified** — `who`/`w`/`last`
   source exists and is open, but whether it's classic BSD `utmp.h` or a
   Darwin-specific replacement (and what that replacement needs) wasn't
   checked. Not a phase-1 blocker.
4. **`/etc/master.passwd` hash format vs. `crypt()`'s actual supported
   algorithms not cross-checked** — both pieces are confirmed to exist
   independently, but whether `file_module.c`'s expected on-disk hash
   format lines up with what `Libc/gen/crypt.c` produces by default
   wasn't verified end-to-end.
5. **`pwd_mkdb`'s Berkeley DB dependency** — `Libc/db/` exists in the
   tree listing, presumed sufficient, not independently confirmed to
   build/work standalone in this kernel-adjacent userland context.
6. **The FAT32 constraint's resolution (§0) is a genuine open decision**,
   not just a research gap — needs the user's input on which option (or a
   different one) to pursue, since it changes the shape of "done" for
   most of the rest of this plan.
