# HFS+ kernel integration notes

**Update 2026-08-10**: the four diffs below were applied (already present
in `third_party/xnu-7195` by the time of this update), and a real 2-partition
FAT32+HFS+ SD card was built and boot-tested end to end in QEMU -
`hfs_mountroot()` now genuinely succeeds as root. See
`docs/roadmap.md` Phase 3.5b for the full writeup (partition-scheme wiring,
a real `IOMatchCategory` collision bug found+fixed, and an unrelated
pre-existing `config/Private.arm64.exports` build-system bug found+fixed
along the way). The rest of this document is the original research pass,
kept for history.

Written by a background research/implementation pass, parallel to the main
thread's FAT32/msdosfs work. Scope: investigate whether HFS+ kernel support
is realistic to add to `third_party/xnu-7195` (branch `bcm2837-bringup`),
and implement it if so. **Answer: yes, it was realistic, and it's done** -
all 36 real apple-oss-distributions/hfs source files plus one new shim file
are vendored into `third_party/xnu-7195/bsd/hfs/` and compile clean (zero
errors) against this exact kernel's real headers, using the real captured
build flags from a prior full kernel build. It was NOT a small job (HFS+ is
much bigger than mockfs or msdosfs, and needed real architectural
adaptation, not just a header copy-paste), but every blocker encountered had
a real, traceable resolution. **Nothing in `config/MASTER.arm64.bcm2837`,
`config/MASTER`, or `bsd/conf/files` has been touched** - see "Diffs to
apply" below for the exact, small changes the main thread (or a follow-up
session) needs to make in those shared files to actually wire this in and
boot-test it.

## TL;DR for the impatient

- Vendored: `third_party/xnu-7195/bsd/hfs/` (new directory, 37 files: 36 real
  Apple source files + 1 new shim, `hfs_vfsinit.c`).
- Source: `apple-oss-distributions/hfs` tag `hfs-556.100.11`, cloned to
  `third_party/hfs` (sibling of `third_party/xnu-7195`, its own git repo,
  matching the `third_party/IOStorageFamily` convention - not a submodule,
  not nested inside `xnu-7195/`).
- Architecture finding: HFS+ has been kext-only (not an in-tree
  `FILESYS_DEV` built-in like mockfs/msdosfs) for a very long time - there
  is no `hfs` entry anywhere in this tree's `config/MASTER*` or
  `bsd/conf/files`, not even a disabled/legacy one. Real hfs.kext registers
  itself with `vfs_fsadd()` (a real, always-present, always-worked xnu KPI -
  `bsd/vfs/kpi_vfs.c`) from an `IOService::start()`. This project has no
  kext-loading infrastructure and doesn't need any: `hfs_vfsinit.c` is a
  ~15-line, non-IOKit, plain-C replacement for that `start()` that calls the
  exact same `vfs_fsadd()`, invoked from one new call site this doc
  specifies (not applied) rather than from a kext load.
- All 37 files compile with **zero errors** via a standalone
  `-fsyntax-only` clang invocation using this project's real, previously-
  captured `DEVELOPMENT_ARM64_BCM2837` build flags (see "How this was
  verified"). Only ordinary warnings (`-Wsign-conversion` etc., same class
  already present throughout this codebase, build uses `BUILD_WERROR=0`).
- Three small, clearly-scoped diffs are needed in shared files to actually
  wire this in - written out below, not applied, so the main thread can
  apply them without a merge conflict.
- No boot test was attempted (out of scope for this pass - no
  `newfs_hfs`-created HFS+ image exists yet anyway; the main thread's FAT32
  image work is the nearer-term path to an actual bootable root).

## Version/vintage decision

Target: `xnu-7195.81.3` (this tree's real tag, confirmed via
`git log -1 --format='%H %ci' xnu-7195.81.3` -> commit dated
`2021-10-06 05:41:47 +0000`).

`apple-oss-distributions` commit/tag dates are **not** reliable for vintage
matching in general (confirmed again this session, see below), so this
needed real corroboration, not a date lookup:

- `opensource.apple.com`'s release-by-OS-version listing is JS-rendered
  (confirmed again this session - static fetch of `/releases.json` and
  guessed `/release/macos-11xx.html` URLs both 404) and archive.org isn't
  fetchable from this tool, so that path was a dead end.
- `hfs`'s tag *tagger* dates (`git api .../git/tags/<sha>`) are recent
  (2023-2026) even for very old-numbered tags - clearly just re-tag/mirror
  timestamps, not release dates. Not usable.
- `hfs`'s tag *commit* dates cluster: everything up to `hfs-556.100.11` (and
  `xnu-7195.81.3` itself) shares the exact same `2021-10-06` bulk-import
  timestamp; `hfs-582.40.4` onward has real, distinct, later dates
  (`2022-02-07` onward). This means the apple-oss-distributions org's
  initial migration cutover happened between `hfs-556.100.11` and
  `hfs-582.40.4`, and everything before the cutover (`hfs-556.100.11` and
  earlier, `xnu-7195.81.3` included) already existed in Apple's own release
  history *before* Oct 2021 - i.e. `xnu-7195.81.3` (Big Sur, released within
  the Nov 2020 - Sep 2021 Big Sur cycle) and the `hfs-556.x` family are
  genuinely contemporaneous. This is corroborating evidence, not a precise
  date match.
- Independent corroboration: this session's own machine is real macOS (the
  environment this agent runs in is macOS, not just a Linux container) -
  `sw_vers`/`uname` show macOS 26.5.1 / `xnu-12377.121.6`, and its installed
  `/System/Library/Extensions/HFS.kext` version is `715.120.4` - which
  **exactly matches** the newest tag in the `apple-oss-distributions/hfs`
  tag list (`hfs-715.120.4`). That confirms the tag list itself is
  trustworthy/complete and up to date, and confirms the general HFS-version-
  number-increments-with-OS-version relationship is real and not coincidental,
  even though it's only a single anchor point (current OS), too far from
  Big Sur to interpolate precisely on its own.
- `hfs-556.41.1` / `hfs-556.60.1` / `hfs-556.100.11` are the three
  candidates spanning what's almost certainly the whole Big Sur (11.x) cycle
  (bracketed by `hfs-522.x` before it, matching Catalina/10.15, and
  `hfs-582.x`/`hfs-583.x` after it with real Monterey-2022-era dates).
  Diffing `hfs-556.41.1...hfs-556.100.11` shows only 12 files changed, small
  bug-fix-sized deltas (xattr rewrite, a CAS test, no header/API shape
  changes) - so which exact one of the three is picked barely matters for
  ABI/shape purposes.

**Picked `hfs-556.100.11`** (latest of the three, i.e. most bug-fixed while
still confidently pre-Monterey). **Confidence: high on the family
(hfs-556.x = Big Sur, not Catalina, not Monterey - multiple independent
signals agree), lower on which exact Big Sur point release within that
family** - flagged, not resolved further, since the compile-time diff
between the three candidates is negligible.

## Architecture finding: kext, not FILESYS_DEV builtin

Real, confirmed-in-this-exact-tree facts, all found by direct inspection
(not assumed):

1. `grep -rn hfs config/MASTER* bsd/conf/files` in `third_party/xnu-7195`
   returns **nothing** - no `hfs` token anywhere, not even disabled. Compare
   to mockfs, which has a real, live `options MOCKFS ... <mockfs>` line in
   `config/MASTER` (line 126) and a `FILESYS_DEV` bracket entry in
   `config/MASTER.arm64.bcm2837` (line 30).
2. The cloned `apple-oss-distributions/hfs` repo's own layout confirms why:
   its `core/` directory (the actual kernel VFS driver source) has its own
   `hfs.xcodeproj`/`kext.xcconfig`/`*.xcconfig`/`Info.plist` files - it is
   built as a **standalone kext** (`hfs.kext`), not as source meant to be
   dropped into an xnu build. `core/hfs_iokit.cpp` is a real IOKit
   `IOService` subclass (`com_apple_filesystems_hfs`) whose `start()` is
   what IOKit calls when the kext is matched/loaded.
3. Critically, `hfs_iokit.cpp::start()`'s *entire* VFS-registration job is
   one call: `vfs_fsadd(&vfe, &vfs_handle)`. `vfs_fsadd` (declared
   `bsd/sys/mount.h:874`, implemented `bsd/vfs/kpi_vfs.c:913`) is a real,
   complete, always-present, **not** disabled/stubbed KPI in this exact
   xnu-7195 tree - it's the general "register a filesystem with VFS at
   runtime" API any kext-shaped filesystem uses; it has zero IOKit-specific
   prerequisites of its own.
4. This project has zero kext-loading infrastructure (no kernel cache
   builder, no IOKit personality matching against a bundled `hfs.kext`, no
   code-signing bypass for third-party kexts) and building one would be a
   large, separate subsystem - genuinely not "straightforward." But it
   turns out to be unnecessary: `vfs_fsadd()` doesn't care who calls it or
   from where, as long as VFS itself is already up.

**Chosen approach**: `third_party/xnu-7195/bsd/hfs/hfs_vfsinit.c` (new file,
~180 lines, not from upstream) is a plain-C replacement for
`hfs_iokit.cpp::start()`/`stop()`: it builds the same `struct vfs_fsentry`
and calls the same `vfs_fsadd()`/`vfs_fsremove()`, `hfs_init_zones()`,
`hfs_sysctl_register()`/`_unregister()` - just from an ordinary function
(`hfs_vfsinit(void)`) instead of an `IOService::start()`. That function
needs exactly one call site, from kernel init, after VFS is up and before
root-mount is attempted (see "Diffs to apply" #4).

This file also replaces the three small IOKit-dependent helper functions
real `hfs_iokit.cpp` provides (`hfs_is_ejectable`, used for eject-
permission checks; `hfs_iterate_media_with_content`, used for Fusion-Drive/
external-journal-device pairing; `hfs_get_platform_serial_number`, used to
stamp a best-effort serial number into the journal info block) with trivial
stand-ins (not ejectable / no matching media / not supported) - all three
are soft-fail-safe by upstream's own design already (real upstream degrades
the same way when IOKit can't find a match), and none apply to a single
emulated-SD-card HFS+ volume with no `IOMedia` stack backing it.

It deliberately does **not** replicate `hfs_iokit.cpp`'s four AppleKeyStore-
backed key-wrapping functions (`hfs_unwrap_key`/`hfs_rewrap_key`/
`hfs_new_key`/`hfs_backup_key`) with a shim of AppleKeyStore's own
`aks_cred_t`/`aks_wrapped_key_t`/`aks_raw_key_t` types - see next section for
why that turned out to be the wrong shape to imitate at all.

## The CONFIG_PROTECT / AppleKeyStore situation

Real `hfs_iokit.h` additionally declares those four key-wrapping functions
using types from `<AppleKeyStore/AppleKeyStoreFSServices.h>` -
`aks_cred_t`/`aks_wrapped_key_t`/`aks_raw_key_t`. AppleKeyStore is a
separate, closed-source framework this project doesn't have and has no
Secure-Enclave-equivalent hardware to back anyway.

First assumption (wrong, corrected mid-pass): `hfs_cprotect.c`'s entire body
is one big `#if CONFIG_PROTECT ... #endif` (2755 of its 2783 lines), and
upstream's own `kext-config.h` (the project's `GCC_PREFIX_HEADER`) only sets
`CONFIG_PROTECT 1` on its `TARGET_OS_IPHONE`-real-hardware branch, not its
"OS X" branch - so the first draft of this vendoring's own
`bsd/hfs/hfs_config.h` (this project's from-scratch replacement for
`kext-config.h`, since xnu's built-in-filesystem convention has no per-
optional-group prefix-header mechanism - see next section) picked the "OS X"
branch and left `CONFIG_PROTECT` off, on the theory that this would compile
out the whole file and sidestep AppleKeyStore entirely.

That was wrong: **this kernel's real, already-captured build flags
(`BUILD/obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/.CFLAGS`, from this
project's own prior real build) already contain a bare `-DCONFIG_PROTECT`**,
unconditionally, for every file compiled into `bsd/`. That's a real,
project-wide setting (this tree's `config/MASTER.arm64.bcm2837` already
lists `config_protect` in its `FILESYS_BASE` bracket, independent of HFS)
that a per-file header like `hfs_config.h` cannot override. So
`hfs_cprotect.c`'s full body genuinely compiles, and the AppleKeyStore-type
question is real, not avoidable.

**Real fix found**: this exact xnu-7195 tree already has its own generic,
non-HFS-specific, non-AppleKeyStore content-protection interface -
`bsd/sys/cprotect.h`'s `cp_cred_t`/`cp_wrapped_key_t`/`cp_raw_key_t`/
`cp_key_class_t`, with `unwrapper_t`/`rewrapper_t`/`new_key_t`/
`backup_key_t` function-pointer typedefs - and it is **structurally
identical** to what `hfs_cprotect.c` expects from AppleKeyStore's
`aks_cred_t`/`aks_wrapped_key_t`/`aks_raw_key_t` (same fields: `key`,
`key_len`, `dp_class`; same credential fields: `inode`, `pid`, `uid`,
`key_revision`). This is xnu's own later generalization of the same
AppleKeyStore-era interface, already vendored, already compiled
(`BUILD/obj/.../vfs_cprotect.o` already exists from the prior build), fully
present in this tree.

So `hfs_cprotect.c` was mechanically adapted (not stubbed around) to use
`cp_cred_s`/`cp_wrapped_key_s`/`cp_raw_key_s`/`cp_cred_t` instead of
`aks_cred_s`/`aks_wrapped_key_s`/`aks_raw_key_s`/`aks_cred_t` throughout (a
handful of `sed`-driven renames, one struct-field-shape gap fixed by hand -
see "Fixes applied" below), and `hfs_iokit.h` declares the four functions
with the `cp_*` types. `hfs_vfsinit.c` implements all four as trivial
`return ENXIO;` stubs - the same fallback result upstream's own
`key_services()` helper already returns when no AppleKeyStore platform
function is registered, which callers already treat as a normal, handled
"content protection unavailable" case. There is no real key service on this
platform (no Secure Enclave / AppleKeyStore equivalent), so protected-class
files would fail cleanly; unprotected (class "none") files - which is
everything a from-scratch, non-macOS-formatted volume would use - are
unaffected.

## `hfs_config.h`: getting the platform-conditional branch right

`kext-config.h` (upstream's `GCC_PREFIX_HEADER`) branches on
`#if (TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR)` after
`#include <TargetConditionals.h>`. That header **does not exist anywhere in
this kernel's own (`-nostdinc`) include paths** - confirmed by `find` across
`EXTERNAL_HEADERS`, `BUILD/obj/EXPORT_HDRS`, and the SDK's kernel-only
include dir this build actually uses. Real xnu source already references
`TARGET_OS_OSX`/`TARGET_OS_IPHONE` without ever including
`TargetConditionals.h` (e.g. `bsd/sys/cdefs.h`, and upstream `hfs_vfsops.c`
itself, unmodified, already has several `#if TARGET_OS_OSX` blocks) -
relying on plain C's "undefined macro in `#if` evaluates to 0" rule (a
`-Wundef`-only warning; this build runs `BUILD_WERROR=0`). That is exactly
the right behavior for this project's real target anyway: an embedded,
non-macOS, non-simulator board (this project's own
`tools/build_xnu_7195.sh` uses `SDKROOT=iphoneos`/`PLATFORM=iPhoneOS` -
confirmed via the real captured build flags: `-DXNU_TARGET_OS_IOS
-DPLATFORM_iPhoneOS`).

So `bsd/hfs/hfs_config.h` (this vendoring's from-scratch replacement for
`kext-config.h`) deliberately does **not** try to define or fake
`TargetConditionals.h` - it just lets `TARGET_OS_OSX`/`TARGET_OS_IPHONE`
fall through as undefined/false everywhere, matching the rest of this
kernel. Only `HFS_COMPRESSION 1` and `FIFO 1` are set unconditionally (both
harmless/wanted regardless of platform branch). `QUOTA` and `NAMEDSTREAMS`
are deliberately left unset even though `kext-config.h` sets them on one
branch or the other: both are already real `MASTER`-config-driven macros
this kernel generates itself (`quota` opt tag in `config/MASTER`, currently
off for this platform; `namedstreams` token already in
`config/MASTER.arm64.bcm2837`'s `FILESYS_BASE` bracket, confirmed via the
real captured `.CFLAGS`) - redefining them would at best just warn (same
value) and at worst silently diverge from the rest of the kernel if that
bracket ever changes.

This choice has a second, load-bearing consequence, described next.

## The missing `mac_roman_to_unicode` family (real blocker, but avoided)

`apple-oss-distributions/hfs`'s `hfs_encodings/hfs_encodings.h` declares six
functions used as HFS-Standard-era Mac-Roman<->Unicode/UTF-8 fallback
converters: `mac_roman_to_unicode`, `unicode_to_mac_roman`,
`mac_roman_to_utf8`, `utf8_to_mac_roman`, `hfs_getencodingbias`,
`hfs_setencodingbias` (plus the converter-registry functions
`hfs_getconverter`/`hfs_addconverter`/`hfs_remconverter`/`hfs_relconverter`,
which are implemented, in `hfs_encodings/hfs_encodings.c`). **The six
conversion functions themselves are declared but never defined anywhere in
the public `apple-oss-distributions/hfs` repo at this tag** - confirmed via
`gh search code "mac_roman_to_unicode" --owner apple-oss-distributions`
across the *entire* org, which finds only the one declaration in
`hfs_encodings.h`, no implementation, in any repo. This is a real gap in
Apple's public source release, not something this vendoring pass
introduced.

Walking every call site's `#if`/`#endif` nesting (not just grepping flat,
which misses enclosing gates) shows all of them are reachable **only** from
two conditions:
- `#if CONFIG_HFS_STD` (legacy pre-1998 HFS Standard volume support - out of
  scope for this project; a fresh SD card only ever gets a modern HFS+
  volume) - covers `hfs_MountHFSVolume`, `promotekey`, `unicode_to_hfs`, the
  legacy-catalog-key fallback path in `hfs_catalog.c`, and every
  `hfs_getconverter`/`hfs_relconverter` call site in `hfs_vfsops.c`.
- `#if TARGET_OS_OSX` - covers `hfs_getencodingbias`/`hfs_setencodingbias`'s
  one call site in `hfs_vfsops.c`.

`CONFIG_HFS_STD` is correctly never set anywhere in this vendoring (not
wanted). `TARGET_OS_OSX` is correctly never set either (see previous
section - it's genuinely false for this project's real iOS-SDK-based
build). With both naturally false, **every call site for this missing
function family is compiled out** - confirmed by successfully compiling the
whole vendored tree (see "How this was verified"). `hfs.h`'s own
unconditional `#include "../hfs_encodings/hfs_encodings.h"` was repointed at
a locally-vendored, declarations-only copy (`bsd/hfs/hfs_encodings.h`) so
that include itself resolves; `hfs_encodings.c`/`hfs_encodinghint.c`
(the *implementations* of the functions that ARE real, e.g.
`hfs_pickencoding`) were deliberately **not** vendored at all, since nothing
in this configuration calls into them either (`hfs_pickencoding`'s own two
call sites are gated `#if TARGET_OS_OSX` and `#if CONFIG_HFS_STD`
respectively).

**This is a real, confirmed-missing-upstream gap, successfully avoided for
this project's actual configuration - not resolved in general.** If
`CONFIG_HFS_STD` or real-macOS-target (`TARGET_OS_OSX`) support is ever
wanted later, this becomes a real blocker requiring either: hand-writing a
correct Mac-Roman<->Unicode conversion table from scratch, sourcing one from
ICU/a different open-source project, or finding a different
apple-oss-distributions/hfs vintage (not checked - out of scope for this
pass) where these functions' definitions might actually be present.

## Other real compile-time fixes (all applied, all in `bsd/hfs/`)

Found via the actual standalone-compile verification described below, not
guessed:

1. **`ubc_create_upl` vs `ubc_create_upl_external`** (`hfs_readwrite.c`, 3
   call sites): `bsd/sys/ubc.h` declares plain `ubc_create_upl()` only
   `#ifndef XNU_KERNEL_PRIVATE` - i.e. only for callers building *outside*
   the xnu tree (a real external kext, using the public/kext-facing Kernel
   framework headers). This project's real build defines
   `-DXNU_KERNEL_PRIVATE` unconditionally for everything in `bsd/` (real,
   captured build flags), so in-tree code must call the always-declared
   `ubc_create_upl_external()` instead (identical signature). Renamed all
   three call sites.
2. **Missing `#include "hfs_cprotect.h"` in `hfs_readwrite.c`**: real
   upstream `hfs_readwrite.c` uses `cp_handle_vnop`/`CP_READ_ACCESS`/
   `CP_WRITE_ACCESS`/`cp_vnode_transcode`/`cp_get_root_major_vers`/
   `cp_get_default_level`/`cp_io_params_t`/`cp_io_params`/`off_rsrc_make`/
   `cp_handle_strategy`/`cp_fs_protected` completely unconditionally
   (not CONFIG_HFS_STD-gated), all declared in `hfs_cprotect.h` - but
   never includes that header, in the *real, unmodified upstream file* at
   this tag (confirmed by reading the pristine clone, not just this
   vendoring's copy). This looks like a genuine gap in the public release
   (some other internal-only header must pull it in for Apple's real build).
   Added the missing `#include "hfs_cprotect.h"`.
3. **`sysctlbyname()` used from in-kernel code** (`hfs_journal.c`,
   `hfs_iokit.cpp`'s original CPU-count heuristic): `sysctlbyname()` is only
   declared for `!KERNEL` builds in this tree's real `bsd/sys/sysctl.h` -
   real kexts get a separate kext-KPI-exported version this in-tree build
   doesn't have. `hfs_journal.c`'s one call (`sysctlbyname("hw.memsize",
   ...)`, used only to size the journal transaction buffer as a function of
   total RAM) was replaced with a direct read of `max_mem`
   (`osfmk/mach/vm_param.h`'s real xnu-internal global for exactly this
   value - confirmed as an established bsd-layer include, e.g.
   `bsd/kern/kern_sysctl.c` already includes it). The equivalent
   `sysctlbyname("hw.physicalcpu", ...)` heuristic from `hfs_iokit.cpp`
   (used only to scale a lookup-cache size) was simply dropped rather than
   ported when writing `hfs_vfsinit.c` - it only affects a cache-sizing
   tuning value, defaults are safe.
4. **`AKS_RAW_KEY_WRAPPEDKEY` -> `CP_RAW_KEY_WRAPPEDKEY`** (`hfs_cprotect.c`,
   2 call sites): same AppleKeyStore-vintage-name vs this-tree's-generic-name
   gap as the main `cp_*` rename (see previous section) - `cprotect.h`
   already defines `CP_RAW_KEY_WRAPPEDKEY` with the same value/meaning.
5. **`cp_cred_s` has no `volume_uuid` field** (`hfs_cprotect.c`,
   `cp_init_access()`): real AppleKeyStore-vintage `aks_cred_s` has a full
   128-bit `uuid_t volume_uuid`; this tree's generic `cp_cred_s` only has a
   plain `uint32_t volume` (a real, intentional shape difference from
   Apple's own later genericization, not a renaming-only gap). Changed the
   `uuid_copy(access->volume_uuid, hfs_uuid)` call to
   `memcpy(&access->volume, hfs_uuid, sizeof(access->volume))` (truncate
   into the available field rather than silently drop the value). Dead code
   in practice either way (see CONFIG_PROTECT section - the four key-service
   functions this feeds are permanent ENXIO stubs on this platform).
6. **`CP_ROOT_CRYPTOG1` undeclared** (`hfs_vfsutils.c`, on-disk "CP root"
   xattr flag check): genuinely missing from this tag's `hfs_cprotect.h`
   (no definition anywhere in the repo, unlike its sibling
   `CP_ROOT_XATTR_MIN_LEN` which is right there). **Defined locally as
   `0x00000001`, NOT sourced from any real Apple reference** - a
   placeholder using the obvious "first flag bit" convention, needed only to
   compile. `cp_crypto_generation` is inert either way on this platform (no
   real key service - see CONFIG_PROTECT section), but if this project is
   ever pointed at a real, content-protected macOS-formatted HFS+ volume,
   **this value should be re-verified before being trusted** to interpret
   real on-disk flags correctly. Flagged clearly in-code too
   (`hfs_cprotect.h`).

None of these required touching any file outside `third_party/xnu-7195/bsd/hfs/`.

## How this was verified

No full kernel build or QEMU boot test was run (out of scope for this pass,
and the main thread owns that build/boot loop concurrently on the same
output paths - see coordination note below). Instead, every vendored file
was compiled standalone with `-fsyntax-only`, using this exact kernel's
**real** `DEVELOPMENT_ARM64_BCM2837` build flags, captured from a `.CFLAGS`
file (`BUILD/obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/.CFLAGS`) that
this project's build tooling (`bsd/conf/Makefile.template`'s
`$(REPLACECONTENTS)` step) already wrote out during a genuine prior real
build of this tree - not reconstructed/guessed flags. This includes the
real `-nostdinc`, all real `-D` macros (`-DCONFIG_PROTECT`, `-DMOCKFS`,
`-DNAMEDSTREAMS`, etc - which is how the `QUOTA`-redefinition and
`-DCONFIG_PROTECT`-is-really-on findings above were actually discovered,
not assumed), and all real kernel `-I` include paths
(`BUILD/obj/EXPORT_HDRS/*`, `EXTERNAL_HEADERS`, the real iPhoneOS SDK's
kernel-only include dir).

**Result: all 37 files in `bsd/hfs/` (36 vendored `.c` files +
`hfs_vfsinit.c`) compile with zero errors.** Remaining diagnostics are
ordinary warnings (`-Wsign-conversion`, `-Wshorten-64-to-32`, etc - the same
classes already present throughout this codebase; this build runs with
`BUILD_WERROR=0`).

This does **not** prove the whole kernel links or boots - only that every
vendored translation unit is self-consistent against this tree's real
headers/ABI. Linking, the actual `vfs_fsadd()` runtime registration, and
mounting a real HFS+ volume are all unverified and are the natural next
steps once the main thread's build/boot loop is free, or in a follow-up
session.

## Diffs to apply (NOT applied - avoiding the main thread's shared files)

Four changes, all small, all outside `bsd/hfs/`:

### 1. `third_party/xnu-7195/config/MASTER` - declare the `<hfs>` opt tag

Add near the existing filesystem `options` lines (e.g. near line 126's
`options MOCKFS ... # <mockfs>`):

```
options		HFS		# HFS+ built-in filesystem	# <hfs>
```

This is what makes `bsd/conf/files`' `optional hfs` tag (see #3) resolve to
a real `-DHFS` compile flag - confirmed pattern: this build's config-bracket
mechanism uppercases the bracket token directly (`mockfs` -> `-DMOCKFS`,
`bindfs` -> `-DBINDFS`, `config_mnt_rootsnap` -> `-DCONFIG_MNT_ROOTSNAP` -
seen directly in this kernel's real captured `.CFLAGS`), so the bracket
token must be `hfs` (lowercase, see #2) and the opt-tag/macro is `HFS`
(uppercase) - **not** `CONFIG_HFS`.

### 2. `third_party/xnu-7195/config/MASTER.arm64.bcm2837` - enable it for this platform

Add `hfs` to the `FILESYS_DEV`/`FILESYS_DEBUG` bracket lists (mirroring how
`mockfs` is already there):

```
#  FILESYS_DEV =    [ FILESYS_BASE fdesc mockfs hfs ]
#  FILESYS_DEBUG =  [ FILESYS_BASE fdesc mockfs hfs ]
```

(Lines 30-31 currently read `[ FILESYS_BASE fdesc mockfs ]` for both.)

### 3. `third_party/xnu-7195/bsd/conf/files` - list the vendored source

Add, near the existing mockfs block (`bsd/miscfs/mockfs/mockfs_*.c optional
mockfs`):

```
bsd/hfs/BTree.c			optional hfs
bsd/hfs/BTreeAllocate.c		optional hfs
bsd/hfs/BTreeMiscOps.c			optional hfs
bsd/hfs/BTreeNodeOps.c			optional hfs
bsd/hfs/BTreeNodeReserve.c		optional hfs
bsd/hfs/BTreeScanner.c			optional hfs
bsd/hfs/BTreeTreeOps.c			optional hfs
bsd/hfs/BTreeWrapper.c			optional hfs
bsd/hfs/CatalogUtilities.c		optional hfs
bsd/hfs/FileExtentMapping.c		optional hfs
bsd/hfs/FileIDsServices.c		optional hfs
bsd/hfs/hfs_attrlist.c			optional hfs
bsd/hfs/hfs_btreeio.c			optional hfs
bsd/hfs/hfs_catalog.c			optional hfs
bsd/hfs/hfs_chash.c			optional hfs
bsd/hfs/hfs_cnode.c			optional hfs
bsd/hfs/hfs_cprotect.c			optional hfs
bsd/hfs/hfs_endian.c			optional hfs
bsd/hfs/hfs_extents.c			optional hfs
bsd/hfs/hfs_fsinfo.c			optional hfs
bsd/hfs/hfs_hotfiles.c			optional hfs
bsd/hfs/hfs_journal.c			optional hfs
bsd/hfs/hfs_link.c			optional hfs
bsd/hfs/hfs_lookup.c			optional hfs
bsd/hfs/hfs_notification.c		optional hfs
bsd/hfs/hfs_quota.c			optional hfs
bsd/hfs/hfs_readwrite.c		optional hfs
bsd/hfs/hfs_resize.c			optional hfs
bsd/hfs/hfs_search.c			optional hfs
bsd/hfs/hfs_vfsinit.c			optional hfs
bsd/hfs/hfs_vfsops.c			optional hfs
bsd/hfs/hfs_vfsutils.c			optional hfs
bsd/hfs/hfs_vnops.c			optional hfs
bsd/hfs/hfs_xattr.c			optional hfs
bsd/hfs/MacOSStubs.c			optional hfs
bsd/hfs/rangelist.c			optional hfs
bsd/hfs/UnicodeWrappers.c		optional hfs
bsd/hfs/VolumeAllocation.c		optional hfs
```

(37 lines - all 36 real Apple source files plus the new `hfs_vfsinit.c`
shim. Whitespace/column alignment above is illustrative; match the
surrounding file's actual tab convention when applying.)

### 4. `third_party/xnu-7195/bsd/kern/bsd_init.c` - the one new call site

`hfs_vfsinit()` uses the dynamic `vfs_fsadd()` KPI (see "Architecture
finding" above), not the static `vfstbllist[]`/`bsd/vfs/vfs_conf.c`
mechanism mockfs uses, so it needs exactly one direct call, made once VFS is
initialized and before root-mount is attempted. The natural spot is right
after the existing `vfsinit();` call (currently around line 816, inside
`bsd_init()`):

```c
	bsd_init_kprintf("calling vfsinit\n");
	vfsinit();
#if HFS
	{
		extern int hfs_vfsinit(void);
		int hfs_init_err = hfs_vfsinit();
		if (hfs_init_err) {
			printf("hfs_vfsinit failed: %d\n", hfs_init_err);
		}
	}
#endif
```

(`hfs_vfsinit()` returns `int`, 0 on success, matching `vfs_fsadd()`'s own
return convention - see `third_party/xnu-7195/bsd/hfs/hfs_vfsinit.c`.)

`bsd/vfs/vfs_conf.c` was **not** touched and does **not** need to be -
that's the static-table mechanism mockfs/msdosfs-style filesystems use;
`vfs_fsadd()` bypasses it entirely, which is the whole point of keeping
`hfs_vfsinit.c` close to upstream's own real (kext) registration shape
instead of rewriting HFS to conform to the older static-table style (a
bigger, more invasive change for no real benefit here).

## Coordination note

Per this task's instructions, none of `config/MASTER.arm64.bcm2837`,
`config/MASTER`, `bsd/conf/files`, or `bsd/kern/bsd_init.c` were edited by
this pass - only new files under `third_party/xnu-7195/bsd/hfs/` (and this
doc). `tools/build_xnu_7195.sh` was not run and no QEMU boot test was
attempted. The four diffs above are ready to apply whenever convenient -
they're small, additive, and don't touch anything the main thread's FAT32/
msdosfs work is likely also editing (msdosfs's own `FILESYS_DEV`/
`bsd/conf/files`/`config/MASTER` additions are separate list entries/lines,
not the same lines).

## Source locations

- `third_party/hfs/` - the vendor clone (`apple-oss-distributions/hfs`,
  checked out at tag `hfs-556.100.11`, sibling directory to
  `third_party/xnu-7195/`, its own independent git repo - not a submodule).
  Kept around for reference/future re-vendoring; not itself part of the
  kernel build.
- `third_party/xnu-7195/bsd/hfs/` - the actual vendored-in source (37
  files), ready to be wired in per the diffs above.
