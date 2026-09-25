#!/bin/bash
# Builds Tier 1 of the "remaining commands" pass (docs/roadmap.md Phase 4
# step 3, "shell_cmds/file_cmds/text_cmds/adv_cmds/..."): the simplest,
# lowest-dependency real Apple commands, ordered deliberately by
# complexity (this file) before the heavier tiers that need real file
# I/O, passwd/group lookups, or environment plumbing (separate build
# scripts, one tier at a time, same pattern as bash/uname/ps).
#
# Every one of these is a single real, unmodified Apple/FreeBSD source
# file (third_party/shell_cmds/<name>/<name>.c) needing only what's
# already in libc_build/*.a from the bash/uname/ps tiers plus the same
# generic_main_wrapper.c TSD-bootstrap fix those three needed
# individually (see that file's own header for why this is mandatory,
# not optional -- skipping it is a real, reproducible SIGSEGV).
#
# CMD_LIST format: "name:srcdir:extra_source_files" (space-separated
# extra files, may be empty). Ordered simplest-first:
#   true/false   -- 2-line files, exit(0)/exit(1), no libc surface at all
#   yes          -- trivial printf loop
#   pwd          -- getcwd()-based, real err()/getopt()
#   basename/dirname -- call real Libc/gen/FreeBSD/{basename,dirname}.c
#     (already compiled into libc_gen.a by build_libc_gen.sh -- confirmed
#     via nm, not assumed) plus real err()/getopt()
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
XNU="../../third_party/xnu-7195"
PTHREAD="../../third_party/libpthread"
SHELL_CMDS="../../third_party/shell_cmds"
TEXT_CMDS="../../third_party/text_cmds"
TEXT_CMDS_BINTRANS="../../third_party/text_cmds_bintrans"
FILE_CMDS="../../third_party/file_cmds"
GNUDIFF="../../third_party/gnudiff/diffutils"
LIBARCHIVE_ROOT="../../third_party/libarchive"
LIBARCHIVE="$LIBARCHIVE_ROOT/libarchive/libarchive"
LIBARCHIVE_TAR="$LIBARCHIVE_ROOT/libarchive/tar"
LIBARCHIVE_FE="$LIBARCHIVE_ROOT/libarchive/libarchive_fe"
LESS="../../third_party/less/less"
LESSROOT="../../third_party/less"
COMMONCRYPTO="../../third_party/CommonCrypto"
OUT="libc_build"
mkdir -p "$OUT/simple_obj" "$OUT/compat_include"

# md5(1) (Phase 4.6 item 6): real, unmodified text_cmds/md5/md5.c and its
# real sibling commoncrypto.h (same directory, also unmodified) both
# `#include <CommonCrypto/CommonDigest.h>` / `<CommonCrypto/
# CommonDigestSPI.h>` -- real Apple headers, vendored verbatim here from
# third_party/CommonCrypto (tag CommonCrypto-60178.40.2, see
# setup_third_party.sh's own header comment for the macos-112 manifest
# confirmation). md5_commoncrypto_shim.c (this command's own CMD_LIST
# extra source, see its header) supplies the real function BODIES
# (CCDigestInit/Update/Final/OutputSize, Digest_End/Data/File) these
# headers only declare -- real corecrypto, which those functions would
# call through to on real macOS, is not open-sourced (confirmed, see
# md5_rfc1321_core.c's header).
mkdir -p "$OUT/commoncrypto_glue/CommonCrypto"
cp "$COMMONCRYPTO/include/CommonDigest.h" "$OUT/commoncrypto_glue/CommonCrypto/CommonDigest.h"
cp "$COMMONCRYPTO/include/Private/CommonDigestSPI.h" "$OUT/commoncrypto_glue/CommonCrypto/CommonDigestSPI.h"

# Stand-in headers for nohup.c's real vproc.h/vproc_priv.h (libvproc/
# launchd client API this project has no equivalent of at all -- no
# launchd-managed daemons, see architecture.md's userland section, same
# class of gap as build_copyfile.sh's xpc/private.h stand-in). Generated
# here (not checked in directly) since $OUT/compat_include is a build
# intermediate, matching every other tier's compat-header-generation
# precedent (e.g. build_copyfile.sh's content_protection.h forwarder).
cat > "$OUT/compat_include/vproc.h" <<'EOF'
#ifndef _VPROC_H_COMPAT_
#define _VPROC_H_COMPAT_
#endif
EOF
cat > "$OUT/compat_include/vproc_priv.h" <<'EOF'
/* Real declaration nohup.c actually calls; real (honest, not fabricated)
 * implementation is nohup_vproc_compat.c -- see that file's own header
 * comment for why NULL ("detach succeeded") is the correct, not just
 * convenient, answer on a target with no console-session concept at
 * all to detach from. */
#ifndef _VPROC_PRIV_H_COMPAT_
#define _VPROC_PRIV_H_COMPAT_
const char *_vprocmgr_detach_from_console(int flags);
#endif
EOF

# diff(1)'s real, unmodified lib/regex.c and src/{diff,analyze,context,
# io,util}.c all `#include <regex.h>` expecting gnulib's OWN GNU-syntax
# regex engine (struct re_pattern_buffer, re_compile_pattern() -- a
# different, older API from the POSIX regcomp()/regexec() this
# project's TRE-based $LIBC/include/regex.h provides for every other
# command) -- real Apple's own gnudiff.xcodeproj resolves `<regex.h>`
# to gnulib's own copy by putting lib/ ahead of the SDK's regex.h on its
# HEADER_SEARCH_PATHS; this build reproduces that by (a) putting diff's
# own cmd_cflags -I paths ahead of the shared $CFLAGS on the compile
# line (see this loop's own comment further down) and (b) generating
# lib/regex.h here, since the checked-out tree only has the *.hin
# autoconf input template (never actually configured for this
# checkout's own target) -- confirmed byte-for-byte template-only, zero
# "@VAR@" substitution markers present, so a straight copy is the real,
# correct "configure" output here, not a guess. Same story for
# fnmatch.h (exclude.c's `#include "fnmatch.h"` -- the real
# implementation used is still the project's own $LIBC/gen/FreeBSD/
# fnmatch.c via du_fnmatch_libc_wrapper.c, reused verbatim from the
# grep/du tiers; gnulib's own lib/fnmatch.c is deliberately NOT compiled,
# matching Apple's own diffutils.xcodeproj "diffutils" static-lib target
# source list, which omits it too).
mkdir -p "$OUT/gnudiff_glue"
if grep -q '@[A-Za-z_]*@' "$GNUDIFF/lib/regex.hin" 2>/dev/null; then
    echo "error: $GNUDIFF/lib/regex.hin has unexpected @VAR@ substitution markers" >&2
    exit 1
fi
cp "$GNUDIFF/lib/regex.hin" "$OUT/gnudiff_glue/regex.h"
if grep -q '@[A-Za-z_]*@' "$GNUDIFF/lib/fnmatch.hin" 2>/dev/null; then
    echo "error: $GNUDIFF/lib/fnmatch.hin has unexpected @VAR@ substitution markers" >&2
    exit 1
fi
cp "$GNUDIFF/lib/fnmatch.hin" "$OUT/gnudiff_glue/fnmatch.h"

# diff needs BOTH gnulib's own real lib/strftime.c (context.c's real
# nstrftime() call -- see this command's own CMD_LIST comment) AND the
# real system $LIBC/stdtime/FreeBSD/strftime.c (nstrftime's own
# internal locale-dependent-conversion fallback calls real system
# strftime()/strftime_l(), and $LIBC/stdtime/FreeBSD/asctime.c's real
# asctime_r() is itself implemented via strftime_l() -- both genuine,
# separate real dependencies, not redundant). The per-command build
# loop's own object-naming scheme (`${name}_$(basename $ef .c).o`)
# can't tell these two same-named strftime.c apart -- confirmed via the
# real, reproducible silent-clobber bug this caused before adding this
# copy (gnulib's own compiled object got overwritten on disk by the
# system one compiled right after it, since both produced
# libc_build/simple_obj/diff_strftime.o). A plain renamed copy of the
# real, unmodified system file sidesteps the collision without
# touching the shared build-loop logic other commands also use.
cp "$LIBC/stdtime/FreeBSD/strftime.c" "$OUT/gnudiff_glue/libc_real_strftime.c"

# less(1) needs BOTH its own real, unmodified less/signal.c (winch()/
# init_signals()/psignals()/the real `sigs` global -- SIGWINCH/SIGINT
# bookkeeping) AND the real system $LIBC/gen/FreeBSD/signal.c (real
# signal(3), lsystem.c's own real dependency) -- same real same-
# basename object-clobber bug diff's own strftime.c pair already hit
# and fixed the same way (a renamed copy, not a build-loop change).
mkdir -p "$OUT/less_glue"
cp "$LIBC/gen/FreeBSD/signal.c" "$OUT/less_glue/libc_real_signal.c"

# lib/c-stack.c's real segv_handler()/init_backtrace() call
# write()/_exit()/getpid()/kill() without including <unistd.h>/
# <signal.h> themselves (same real "relies on ambient TU context"
# pattern dirname.h's own missing size_t/strlen already showed --
# see this command's own CMD_LIST comment). Force-including the real
# <unistd.h>/<signal.h> themselves (rather than four narrow prototypes)
# was tried first and reverted: cp_mv_syscalls.c's own hand-rolled
# setattrlist() prototype (a real, deliberate NO_SYSCALL_STUB stand-in,
# see that file's own header) then collides with unistd.h's real
# declaration the moment unistd.h is force-included into every extra
# source file for this command, not just c-stack.c -- a real,
# reproducible build break, not a guess. Four narrow, real-signature-
# matching prototypes avoid pulling in unistd.h's ~150 other
# declarations entirely, so nothing else can collide.
cat > "$OUT/gnudiff_glue/diff_cstack_decls.h" <<'EOF'
#ifndef _DIFF_CSTACK_DECLS_H_COMPAT_
#define _DIFF_CSTACK_DECLS_H_COMPAT_
#include <sys/types.h>
extern ssize_t write(int fd, const void *buf, size_t nbyte);
extern void _exit(int status) __attribute__((noreturn));
extern pid_t getpid(void);
extern int kill(pid_t pid, int sig);
/* $LIBC/stdlib/FreeBSD/getopt_long.c's own real `#include <getopt.h>`
 * -- same angle-bracket include diff.c itself uses, so it also resolves
 * to gnulib's own lib/getopt.h via $OUT/cmd_pre_include (see the
 * "diff" cmd_cflags case's own comment) rather than this project's
 * usual $LIBC/include/getopt.h -- loses that header's own `optreset`
 * global (a real 4.4BSD extension gnulib's getopt.h has no equivalent
 * of at all, confirmed by reading it). The real getopt_long.c source
 * itself is unmodified; common_getopt.o ($LIBC/stdlib/FreeBSD/
 * getopt.c, always linked into every command) already defines the
 * real storage for this exact global, this just restores its
 * declaration.
 */
extern int optreset;
#endif
EOF

# tar(1) (Tier B, third_party/libarchive, pinned via setup_third_party.sh
# tag libarchive-83.40.4): real, unmodified Apple libarchive/bsdtar
# source needs its own "corrected" config.h -- the real, checked-in
# config.h reflects what Apple's OWN full-macOS-SDK build environment
# actually has (HAVE_BZLIB_H/HAVE_LZMA_H/HAVE_LIBXML2/HAVE_ICONV(_H)/
# HAVE_LOCALCHARSET_H/HAVE_READPASSPHRASE(_H)/HAVE_POSIX_SPAWNP all =1),
# none of which this project has ported (no bzip2/lzma/libxml2/iconv/
# posix_spawn anywhere in this checkout, confirmed by grep, not
# assumed). Real upstream libarchive is ALREADY designed to gracefully
# degrade when any of these are undefined (confirmed by reading
# archive_read_support_filter_bzip2.c's own real `#if !defined(HAVE_
# BZLIB_H)` fallback before relying on this project-wide) -- so
# "correcting" config.h to say what this environment actually has is
# the same real, honest signal a real ./configure run would have
# produced here, not a fabricated shortcut. HAVE_UTIMENSAT/HAVE_
# FUTIMENS are undef'd too for the same reason (archive_write_disk_
# posix.c's own real `#if defined(HAVE_UTIMENSAT) && defined(HAVE_
# FUTIMENS)` vs `#elif HAVE_UTIMES` fallback -- neither utimensat(2)
# nor futimens(2) exist in this xnu-7195 checkout's syscalls.master at
# all, confirmed, not guessed, so forcing the real, already-available
# utimes/futimes/lutimes fallback path is correct). A straight sed
# transform of the real, checked-in file (never modified in place),
# regenerated every build.
mkdir -p "$OUT/tar_glue"
sed -e 's/^#define HAVE_BZLIB_H 1/\/* #undef HAVE_BZLIB_H *\//' \
    -e 's/^#define HAVE_LZMA_H 1/\/* #undef HAVE_LZMA_H *\//' \
    -e 's/^#define HAVE_LIBXML2 1/\/* #undef HAVE_LIBXML2 *\//' \
    -e 's/^#define HAVE_ICONV 1/\/* #undef HAVE_ICONV *\//' \
    -e 's/^#define HAVE_ICONV_H 1/\/* #undef HAVE_ICONV_H *\//' \
    -e 's/^#define HAVE_LOCALCHARSET_H 1/\/* #undef HAVE_LOCALCHARSET_H *\//' \
    -e 's/^#define HAVE_READPASSPHRASE 1/\/* #undef HAVE_READPASSPHRASE *\//' \
    -e 's/^#define HAVE_READPASSPHRASE_H 1/\/* #undef HAVE_READPASSPHRASE_H *\//' \
    -e 's/^#define HAVE_POSIX_SPAWNP 1/\/* #undef HAVE_POSIX_SPAWNP *\//' \
    -e 's/^#define HAVE_UTIMENSAT 1/\/* #undef HAVE_UTIMENSAT *\//' \
    -e 's/^#define HAVE_FUTIMENS 1/\/* #undef HAVE_FUTIMENS *\//' \
    "$LIBARCHIVE_ROOT/config.h" > "$OUT/tar_glue/config.h"

# Real zlib convenience-macro/function restoration (deflateInit as a
# macro over the real deflateInit_, plus compressBound()/uncompress(),
# needed only by archive_write_disk_posix.c's real Apple-only HFS+
# "decmpfs" support) lives directly in $OUT/compat_include/zlib.h's own
# widening below (NOT a separate force-included header here -- that was
# tried first and reverted, see that widening's own comment for the real
# crc32 conflicting-types break it caused).

# Real Apple dirent.h privately renamed every DIR struct field to a
# double-underscore-prefixed name (__dd_fd/__dd_loc/etc, confirmed by
# reading third_party/Libc/include/dirent.h directly) but the real,
# unmodified FreeBSD opendir.c/readdir.c/closedir.c this project newly
# needs for tar(1) (archive_read_disk_posix.c's real directory-tree
# walk -- no prior tier needed real opendir/readdir/closedir at all,
# every earlier command used libremovefile.a's fts_* instead) still
# reference the plain FreeBSD names throughout -- same real field
# layout either way (confirmed field-for-field), so a narrow macro
# rename is a real, textually-scoped compatibility fix, not a
# behavior change.
cat > "$OUT/tar_glue/dirent_compat_macros.h" <<'EOF'
#ifndef _TAR_DIRENT_COMPAT_MACROS_H_
#define _TAR_DIRENT_COMPAT_MACROS_H_
#define dd_fd __dd_fd
#define dd_loc __dd_loc
#define dd_size __dd_size
#define dd_buf __dd_buf
#define dd_len __dd_len
#define dd_seek __dd_seek
#define dd_flags __dd_flags
#define dd_lock __dd_lock
#define dd_td __dd_td
/* Real, unmodified readdir.c's own `_GENERIC_DIRSIZ()` macro (its only
 * real dependency on third_party/Libc/fbsdcompat/_fbsd_compat_.h,
 * copied verbatim from there rather than force-including that whole
 * header project-wide) -- that header's own real `#define _sigaction
 * sigaction`-style BSD-namespace-hiding macros (meant to redirect
 * OTHER real FreeBSD source's internal `_foo()` calls to the public
 * `foo()` symbol) collide with cp_mv_syscalls.c's own, unrelated
 * `_sigaction()` helper function name -- a real, reproducible
 * redefinition error this narrower single-macro copy avoids
 * entirely while still giving readdir.c exactly what it needs. */
#define _GENERIC_DIRSIZ(dp) \
    (((unsigned long)&((struct dirent *)0)->d_name + (dp)->d_namlen+1 + 3) & ~3)
#endif
EOF

# grep's real, unmodified grep.h unconditionally `#include <zlib.h>` and
# `#include <bzlib.h>` (only fastmatch.h's include is guarded by
# WITHOUT_FASTMATCH; zlib/bzlib are not, even though file.c's actual
# BZ2_* usage IS guarded by WITHOUT_BZIP2 -- confirmed by reading
# grep.h/file.c directly, not assumed). With WITHOUT_BZIP2 defined (see
# grep's own CMD_LIST entry below), zero BZ2_*/BZFILE symbols are ever
# referenced, so bzlib.h can be a genuinely empty stand-in (same pattern
# as vproc.h above). zlib.h can't be empty -- file.c's FILE_GZIP path
# (grep's -Z flag / zgrep argv[0]) is NOT compile-time guarded upstream
# (no WITHOUT_GZIP knob exists), so gzdopen()/gzread() must be real,
# linkable symbols; this narrow public header just exposes the two of
# them grep.c/file.c actually call (gzFile as an opaque `void *`) --
# the real, non-fabricated implementation is gz_gzip_impl.c (genuine
# DEFLATE decompression via the real, unmodified zlib core vendored at
# third_party/u-boot/lib/zlib, see that file's own header comment and
# the zlib_glue generation step below for the full mechanism).
cat > "$OUT/compat_include/bzlib.h" <<'EOF'
#ifndef _BZLIB_H_COMPAT_
#define _BZLIB_H_COMPAT_
#endif
EOF
# Widened 2026-08-20 (gzip(1) tier): originally a narrow gzdopen()/
# gzread()-only stand-in (grep.c/file.c's only real call sites at the
# time) -- but $OUT/compat_include is on EVERY command's include path
# ahead of any command-specific -I (confirmed the hard way: gzip.c's own
# real, unmodified `#include <zlib.h>` silently resolved to this narrow
# stand-in instead of the fuller real one gzip's own cmd_cflags adds via
# -I $OUT/zlib_glue, since -I search order follows command-line order
# and CFLAGS's own -I always precedes any per-command cmd_cflags -I).
# Rather than leave that shadowing trap in place for the next command
# that needs more of real zlib.h than gzFile/gzdopen/gzread (gzip.c
# itself needs the full public API: z_stream, deflateInit2/deflate,
# Z_DEFLATED/Z_NULL/etc), this is now a copy of the exact same real,
# complete upstream zlib.h (third_party/u-boot/include/u-boot/zlib.h,
# u-boot's own vendored real zlib public header) $OUT/zlib_glue/zlib.h
# already forwards to -- real, not fabricated, and a strict superset of
# what grep.c/file.c's own narrower use needs, so this is a widening,
# not a behavior change, for every command that was already using it.
cp "../../third_party/u-boot/include/u-boot/zlib.h" "$OUT/compat_include/uboot_real_zlib_public.h"
cat > "$OUT/compat_include/zlib.h" <<'EOF'
#ifndef _ZLIB_H_COMPAT_PUBLIC_
#define _ZLIB_H_COMPAT_PUBLIC_
#include "uboot_real_zlib_public.h"
/* u-boot's own real zlib.h has zero gzFile/gzdopen()/gzread() (the
 * file-based convenience API) -- it only ever uses in-memory z_stream
 * calls itself, confirmed absent by grep, not omitted by mistake here.
 * grep.c/file.c's real -Z/.gz support needs exactly these two (see
 * gz_gzip_impl.c's own header for the real, non-fabricated
 * implementation); restored here after this header was widened from a
 * narrow gzFile-only stand-in to the full real public zlib.h (2026-08-20,
 * gzip(1) tier) so grep doesn't lose what it already had. */
typedef void *gzFile;
gzFile gzdopen(int fd, const char *mode);
int gzread(gzFile file, void *buf, unsigned len);
/* u-boot's own copy of real zlib.h keeps the real deflateInit_/
 * inflateInit_/inflateInit2_ convenience macros (it calls inflateInit2
 * itself) but trimmed the real deflateInit/deflateInit2 macros (it never
 * calls those, only declares+links the real deflateInit2_ function so a
 * full real deflate.c can be compiled in) -- gzip(1) needs the real
 * macro form (real call site: `deflateInit2(&z, level, Z_DEFLATED,
 * -MAX_WBITS, 8, strategy)`), so this restores the exact real macro
 * every other real zlib.h ships (see the same file's own real
 * deflateInit_/inflateInit2 macros a few lines up in
 * uboot_real_zlib_public.h for the identical pattern), not invented. */
#ifndef deflateInit2
#define deflateInit2(strm, level, method, windowBits, memLevel, strategy) \
	deflateInit2_((strm), (level), (method), (windowBits), (memLevel), \
	    (strategy), ZLIB_VERSION, (int)sizeof(z_stream))
#endif
/* Widened again 2026-08-21 (tar(1) tier): real, unmodified
 * archive_write_disk_posix.c (Apple-only HFS+ "decmpfs" support) calls
 * the real unversioned deflateInit() macro (same real "u-boot trimmed
 * it, restore the real upstream macro" story as deflateInit2 above) plus
 * compressBound()/uncompress() -- real zlib top-level convenience
 * wrappers u-boot's vendored core never included (only deflate/inflate,
 * confirmed absent from third_party/u-boot/lib/zlib by directory
 * listing). See tools/userland_staging/tar_zlib_convenience_compat.c's
 * own header for the real implementation (compressBound: zlib's own
 * well-known deterministic formula; uncompress: a faithful real
 * inflateInit_/inflate/inflateEnd wrapper, matching upstream zlib's own
 * actual implementation). Declared here (the widely-shared, already-
 * included-by-every-zlib-consumer header) rather than via a separate
 * force-included file, which was tried first and reverted: forcing a
 * second copy of this same real crc32()/deflateInit_()/etc block ahead
 * of gz_crc32_compat.c's own definition on tar's command line caused a
 * real, reproducible `uInt` vs `uLong` conflicting-types error for
 * crc32 (gz_crc32_compat.c has zero #includes of its own by design, so
 * it never saw this file's real crc32 prototype under grep/gzip -- only
 * an extra forced -include newly introduced it for tar).
 */
#ifndef deflateInit
#define deflateInit(strm, level) \
	deflateInit_((strm), (level), ZLIB_VERSION, (int)sizeof(z_stream))
#endif
unsigned long compressBound(unsigned long sourceLen);
int uncompress(unsigned char *dest, unsigned long *destLen,
    const unsigned char *source, unsigned long sourceLen);
#endif
EOF

# Real, unmodified upstream zlib core (third_party/u-boot/lib/zlib/*.c,
# u-boot's own vendored copy of real zlib -- used there for gzip'd
# kernel images) copied verbatim into a build-generated directory next
# to a small glue zlib.h, so that zutil.h's own `#include "zlib.h"`
# (quote form always resolves against the *including file's own
# directory* first, before any -I search path -- confirmed via a real,
# reproducible build failure otherwise, not assumed) picks up our glue
# instead of u-boot's own lib/zlib/zlib.h, which pulls in u-boot's whole
# <common.h> board-config/watchdog/malloc environment and doesn't build
# outside a real u-boot tree. The glue supplies exactly the two things
# that environment actually provides to these particular .c files (read
# by inspection, not guessed): WATCHDOG_RESET() (real u-boot builds with
# no watchdog configured also compile this to a no-op) and
# get_unaligned() (a generic unaligned 16-bit load -- reproduced exactly
# via memcpy, not approximated) -- plus trivial empty stand-ins for
# <hang.h> (only reachable from dead `#ifdef DEBUG` code, never defined
# here) and <linux/string.h> (real zutil.h includes it purely for the
# memcpy/memcmp/memset prototypes already visible via plain <string.h>).
# gz_gzip_impl.c (grep's own real gzdopen()/gzread(), see its header)
# and gz_crc32_compat.c (a real standalone CRC-32, since u-boot's own
# lib/crc32.c is itself too entangled with u-boot's board/EFI headers to
# vendor directly -- see that file's own comment) are the only
# consumers; -I "$OUT/zlib_glue" is added to the global CFLAGS below
# since no other command's real source has a file named zutil.h/
# inflate.h/etc to collide with.
mkdir -p "$OUT/zlib_glue/linux"
ZLIB_SRC="../../third_party/u-boot/lib/zlib"
ZLIB_REAL_HDR="../../third_party/u-boot/include/u-boot/zlib.h"
for f in inflate.c inftrees.c inffast.c zutil.c adler32.c zutil.h inftrees.h inflate.h inffast.h inffixed.h \
         deflate.c deflate.h trees.c trees.h; do
    cp "$ZLIB_SRC/$f" "$OUT/zlib_glue/$f"
done
cp "$ZLIB_REAL_HDR" "$OUT/zlib_glue/uboot_real_zlib.h"
# Real deflate.c's own `#include <u-boot/crc.h>` (its gzip-format wrap==2
# path's crc32() calls, real trailing-CRC support -- exactly what
# gzip(1)'s own compression path needs) -- narrow forwarding declaration
# matching gz_crc32_compat.c's already-real, already-proven standalone
# crc32() (same signature, same real byte-for-byte zlib-compatible CRC-32,
# see that file's own header), not u-boot's own lib/crc32.c (too
# board/EFI-entangled to vendor directly, same reasoning gz_crc32_compat.c's
# header already documents).
mkdir -p "$OUT/zlib_glue/u-boot"
# Signature deliberately matches u-boot's OWN real zlib.h's crc32
# declaration (`uInt crc32(uInt crc, ...)`, NOT standard real zlib's
# usual `uLong crc32(uLong crc, ...)`, confirmed by reading
# uboot_real_zlib.h directly -- u-boot trimmed the crc/return type from
# uLong to uInt in its own vendored copy) rather than gz_crc32_compat.c's
# own (standard-zlib-matching) `uLong` signature, to avoid a hard
# "conflicting types" redeclaration error against uboot_real_zlib.h in
# this same translation unit (deflate.c includes both). The resulting
# declared-uInt-vs-defined-uLong mismatch against gz_crc32_compat.c's
# real definition is real but harmless on this target's AAPCS64 ABI: a
# CRC-32 is always a <=32-bit value, and arguments/return values that
# fit in 32 bits occupy the same register (W-view of X0 etc) regardless
# of whether either side thinks it's a 32- or 64-bit type -- the exact
# same latent (and already QEMU-verified working) mismatch grep's own
# build already has for this identical symbol, not a new risk this pass
# introduces.
cat > "$OUT/zlib_glue/u-boot/crc.h" <<'EOF'
#ifndef _GZ_UBOOT_CRC_H_COMPAT_
#define _GZ_UBOOT_CRC_H_COMPAT_
#include <stdint.h>
typedef unsigned int uInt;
typedef unsigned char Bytef;
uInt crc32(uInt crc, const Bytef *buf, uInt len);
#endif
EOF
cat > "$OUT/zlib_glue/zlib.h" <<'EOF'
#ifndef _GZ_ZLIB_GLUE_H_
#define _GZ_ZLIB_GLUE_H_
#include <string.h>
#define WATCHDOG_RESET() ((void)0)
#define get_unaligned(p) __gz_get_unaligned_u16(p)
static __inline__ unsigned short __gz_get_unaligned_u16(const void *p) {
    unsigned short v;
    memcpy(&v, p, sizeof(v));
    return v;
}
#include "uboot_real_zlib.h"
#endif
EOF
cat > "$OUT/zlib_glue/hang.h" <<'EOF'
#ifndef _GZ_HANG_H_COMPAT_
#define _GZ_HANG_H_COMPAT_
#endif
EOF
cat > "$OUT/zlib_glue/linux/string.h" <<'EOF'
#include <string.h>
EOF

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

# Real, verified fix (2026-08-28) for a real ld64-609 internal crash
# ("Assertion failed: (_mode == modeFinalAddress), function finalAddress")
# that Xcode 12's own bundled linker hits once libc_locale_globals_compat.o
# (__global_locale/__locale_key) is linked together with
# third_party/Libc/locale/FreeBSD/table.c's _DefaultRuneXLocale -- see
# docs/backlog.md item d9 for the full bisection trail that root-caused it
# to ld64-609 itself, not this project's object code. Confirmed via an
# isolated repro (same objects, same -Wl,-fixup_chains, same dylib set)
# that swapping ONLY the linker for the daily-driver Xcode's much newer one
# makes the identical link succeed, producing byte-for-byte the same real
# load-command shape (LC_LOAD_DYLINKER/LC_MAIN/LC_DYLD_CHAINED_FIXUPS) as
# this project's already-QEMU-verified dynamic executables -- not a
# fabricated workaround. Still compiling with Xcode 12's clang/SDK
# (CLANG/SDK above are untouched); only the final link step's linker
# binary changes, via clang's -B override, which is why this doesn't
# conflict with this project's other "must use Xcode 12" invariants
# (those are about SDK/header vintage, not ld64's own internal bugs).
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi
for dep in libmalloc libplatform libclosure libpthread libc_gen libc_stdio libc_printf libc_locale libc_locale_for_stdio libmach_ipc; do
    if [ ! -f "$OUT/$dep.a" ]; then
        echo "error: $OUT/$dep.a not found -- run build_$dep.sh first" >&2
        exit 1
    fi
done
# cp/mv also need libcopyfile.a (real copyfile()/fcopyfile(), plus its
# already-built realpath/fchown/fchflags/fchmod members) and
# libremovefile.a (real public rename()/unlink()); both already built by
# earlier tiers (build_copyfile.sh, build_removefile.sh) for rm/the
# copyfile smoketest, reused here rather than rebuilt.
# Real gap, found 2026-09-13: grep's/df's own per-command extra_libs
# below also need libremovefile.dylib/libsystem_m.dylib (distinct from
# the static .a archives checked here) with no prerequisite check at
# all.
#
# 2026-09-14: now phrased with the literal "run build_X.sh first" idiom
# gen_build_order.py's parser recognizes (previously deliberately left
# unphrased -- a real BFS trace during an earlier session found that
# naming either dylib script this way bridged this script's own,
# already-accepted build_simple_cmds.sh <-> build_libpthread.sh cycle
# into ~30 otherwise-unrelated scripts, cycle-drop count 4 -> 46). That
# analysis predated the actual root cause: build_libsystem_c.sh had a
# STALE direct link against libremovefile.dylib (real reason: qsort.o's
# flsl() call -- fixed by compiling flsl.o locally on 2026-08-30, but
# the now-redundant dylib link was never removed), which is what made
# build_removefile_dylib.sh <-> build_libsystem_c.sh a real cycle and
# dragged everything reachable from it along for the ride. Removing that
# stale link (build_libsystem_c.sh, 2026-09-14) leaves only the correct,
# one-directional build_removefile_dylib.sh -> build_libsystem_c.sh
# edge, so naming the dylib scripts here no longer bridges into a large
# cycle -- reverify with gen_build_order.py's own cycle-drop count
# (baseline 1, the same removefile_dylib<->libsystem_c pair, now safely
# resolved in the correct direction) before assuming this still holds
# after further changes to either script.
if [ ! -f "$OUT/system/libremovefile.dylib" ]; then
    echo "error: $OUT/system/libremovefile.dylib not found -- run build_removefile_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_m.dylib" ]; then
    echo "error: $OUT/system/libsystem_m.dylib not found -- run build_libm_dylib.sh first" >&2
    exit 1
fi
# Real gap, found 2026-09-14: LINK_DYLIBS below (the base set every
# single command in this script's per-command loop links against, not
# just grep/df) also needs libdyld.dylib and libSystem.B.dylib, with no
# prerequisite check at all -- only ever surfaced by actually running
# this script this far in the real order (every prior attempt died
# earlier). Same reasoning as the removefile_dylib/libm_dylib checks
# just above: safe to name literally now that the libxpc_syscalls.sh
# <-> simple_cmds.sh edge that used to make this bridge into a large
# cycle is gone (that script no longer depends on this one at all,
# fixed 2026-09-14) and the libsystem_c.sh <-> libdyld.sh cycle is
# fixed too (moved the dyld-API reexport to libSystem.B.dylib) --
# reverify with gen_build_order.py's own cycle-drop count (baseline 0)
# before assuming this still holds after further changes.
if [ ! -f "$OUT/system/libdyld.dylib" ]; then
    echo "error: $OUT/system/libdyld.dylib not found -- run build_libdyld.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libSystem.B.dylib" ]; then
    echo "error: $OUT/system/libSystem.B.dylib not found -- run build_libsystem.sh first" >&2
    exit 1
fi
for dep in libcopyfile libremovefile; do
    if [ ! -f "$OUT/$dep.a" ]; then
        echo "error: $OUT/$dep.a not found -- run build_$dep.sh first" >&2
        exit 1
    fi
done

# DAR-204 (2026-09-04): sleep/tail's real MIG client stubs (clockUser.o/
# mach_hostUser.o/taskUser.o, via $OUT/libmach_ipc.a -- see the sleep|tail
# case below) each carry real Apple's own generated
# `if (voucher_mach_msg_set != NULL) voucher_mach_msg_set(&InP->Head);`
# guard before every mach_msg() send, so voucher_mach_msg_set is a real,
# LIVE call site here (unlike DAR-203's dead writev() case) -- called on
# every host_get_clock_service()/semaphore_create()/clock_get_time() a
# sleep(1)/tail -f poll makes. It currently resolves to
# mach_ipc_dead_stubs.o's local, honest `return FALSE` stand-in (this
# project has no real Mach voucher subsystem -- see that file's own
# header).
#
# Investigation (full detail in the DAR-204 Linear comment): the "real"
# dylib alternative, libsystem_kernel.dylib's own voucher_mach_msg_set
# (DAR-153's funcptr-indirection copy), is ALSO provably inert in this
# project's current state -- DAR-14's audit confirmed libdispatch.dylib
# here builds the EMPTY `_voucher_init(void) {}` variant, which never
# calls `__libkernel_voucher_init()`, so libsystem_kernel.dylib's own
# `_libkernel_voucher_functions` table never leaves its all-NULL default
# and its voucher_mach_msg_set() always returns 0/FALSE too -- relinking
# changes nothing observable, same "behaviorally unchanged" bar DAR-203
# used for date's fix.
#
# Narrowed here (same `ld -r -exported_symbol` scoping trick build_
# launchd.sh's own 2026-08-28 postmortem already established for this
# EXACT object file, just inverted): keep mach_port_insert_right/
# extract_right/unguard's existing local definitions exported/untouched
# (a similar-shaped duplicate, but out of THIS ticket's scope -- DAR-196
# owns that), hide only _voucher_mach_msg_set so it can no longer satisfy
# the MIG stubs' reference locally, forcing it to bind externally.
#
# Safe here specifically, NOT a blind copy of launchd's own choice to
# KEEP its local stub: launchd.macho AND launchctl.macho both link
# libdispatch.dylib DIRECTLY (confirmed via `otool -L`) and real,
# documented history (build_launchd.sh's own 2026-08-28 comment) shows
# removing their local stub really did resolve externally to
# libdispatch.dylib's OWN separate, considerably more complex voucher
# implementation instead of libsystem_kernel.dylib's, causing a real
# silent exit(1). sleep.macho/tail.macho's full transitive dylib closure
# ($LINK_DYLIBS: libsystem_c/libsystem_pthread/libsystem_malloc/
# libsystem_platform/libdyld/libcopyfile/libremovefile/libSystem.B, plus
# every one of THEIR OWN dependencies, recursively) contains
# libdispatch.dylib NOWHERE at all (confirmed via `otool -L` on all 8,
# recursively, not assumed) -- libsystem_kernel.dylib is the only
# possible resolution target for these two binaries.
if [ ! -f "$OUT/mach_ipc_dead_stubs.o" ]; then
    echo "error: $OUT/mach_ipc_dead_stubs.o not found -- run build_libmach_ipc.sh first" >&2
    exit 1
fi
"$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld" -r -arch arm64 \
    -exported_symbol _mach_port_insert_right \
    -exported_symbol _mach_port_extract_right \
    -exported_symbol _mach_port_unguard \
    "$OUT/mach_ipc_dead_stubs.o" -o "$OUT/mach_ipc_dead_stubs_no_voucher.o"
rm -f "$OUT/libmach_ipc_no_voucher_for_simple_cmds.a"
cp "$OUT/libmach_ipc.a" "$OUT/libmach_ipc_no_voucher_for_simple_cmds.a"
ar d "$OUT/libmach_ipc_no_voucher_for_simple_cmds.a" mach_ipc_dead_stubs.o
ar r "$OUT/libmach_ipc_no_voucher_for_simple_cmds.a" "$OUT/mach_ipc_dead_stubs_no_voucher.o"

# Real FreeBSD strstr.c, fetched fresh each run -- same reproducible
# curl-at-build-time pattern build_libc_string.sh already established
# for functions Apple's own checkout doesn't vendor open source for
# (confirmed via grep across all of third_party/Libc, not guessed: only
# strstr.3's man page ships, no implementation -- a real, if surprising,
# gap in Apple's own open-source drop). xargs is the first
# simple_cmds.sh command needing it; not folded into libc_string.a
# (which this script's binaries don't otherwise link at all) to avoid
# introducing that whole archive's duplicate-symbol surface into every
# other already-verified command.
mkdir -p "$OUT/freebsd_extra"
if [ ! -f "$OUT/freebsd_extra/strstr.c" ]; then
    curl -sf -o "$OUT/freebsd_extra/strstr.c" "https://raw.githubusercontent.com/freebsd/freebsd-src/main/lib/libc/string/strstr.c"
fi

# Real, unmodified FreeBSD humanize_number.c -- ls(1) -h needs it and
# nothing in this project's vendored Libc checkout has it (confirmed by
# grep: it's a libutil(3) function, and this checkout has no libutil.h
# or libutil source at all, apple-oss-distributions doesn't open-source
# it separately -- same "genuinely missing, not just unfound" gap
# strstr.c's own comment documents for a different function). Same
# reproducible curl-at-build-time pattern.
if [ ! -f "$OUT/freebsd_extra/humanize_number.c" ]; then
    curl -sf -o "$OUT/freebsd_extra/humanize_number.c" "https://raw.githubusercontent.com/freebsd/freebsd-src/main/lib/libutil/humanize_number.c"
fi

# Real, unmodified FreeBSD flsl.c -- diff(1)'s real, unmodified
# $LIBC/stdlib/FreeBSD/qsort.c (DEPTH() macro, its own real recursion-
# depth-limit-before-falling-back-to-heapsort logic) calls flsl(), which
# this checkout's Libc only ever declares (include/strings.h) and never
# implements (confirmed by grep across all of third_party/Libc, same
# "genuinely missing, not just unfound" gap strstr.c/humanize_number.c's
# own comments document -- apple-oss-distributions' Libc drop doesn't
# open-source this one either). Same reproducible curl-at-build-time
# pattern; a tiny, self-contained, dependency-free single function
# (LONG_WIDTH - __builtin_clzl(mask)).
if [ ! -f "$OUT/freebsd_extra/flsl.c" ]; then
    curl -sf -o "$OUT/freebsd_extra/flsl.c" "https://raw.githubusercontent.com/freebsd/freebsd-src/main/lib/libc/string/flsl.c"
fi

# ls(1)'s own real, unmodified print.c #includes <libutil.h> (only for
# strmode()'s declaration -- the real implementation is Libc/string/
# FreeBSD/strmode.c, already vendored -- and humanize_number()/HN_* --
# see above) and <System/sys/fsctl.h> (ls.c's IS_DATALESS macro only
# needs SF_DATALESS from real sys/stat.h, already visible, but the
# literal include path must still resolve). Neither the "System/"-
# prefixed path nor a real libutil.h exist anywhere in this checkout or
# in Xcode 12's iPhoneOS/MacOSX SDKs (grepped both directly: real
# macOS/iOS sys/acl.h headers have zero libutil.h coupling and Apple
# never shipped a public libutil.h on iOS at all) -- narrow forwarding/
# declaration shims, same convention as zlib.h's glue and
# mach/arm64/asm.h's forwarder elsewhere in this script.
mkdir -p "$OUT/compat_include/System/sys"
cat > "$OUT/compat_include/System/sys/fsctl.h" <<'EOF'
#ifndef _SYSTEM_SYS_FSCTL_H_COMPAT_
#define _SYSTEM_SYS_FSCTL_H_COMPAT_
#include <sys/fsctl.h>
#endif
EOF
cat > "$OUT/compat_include/libutil.h" <<'EOF'
#ifndef _LIBUTIL_H_COMPAT_LS_
#define _LIBUTIL_H_COMPAT_LS_
#include <sys/types.h>
#include <stdint.h>
void strmode(int mode, char *p);
int humanize_number(char *buf, size_t len, int64_t number,
    const char *suffix, int scale, int flags);
#define HN_DECIMAL        0x01
#define HN_NOSPACE        0x02
#define HN_B              0x04
#define HN_DIVISOR_1000   0x08
#define HN_IEC_PREFIXES   0x10
#define HN_GETSCALE       0x10
#define HN_AUTOSCALE      0x20
#endif
EOF

# $OUT/cmd_pre_include: an always-first -I directory, empty by default,
# that a per-command cmd_cflags case can populate (just before its own
# compile step, then it MUST be cleared at the top of the next command's
# iteration -- see the "clear any previous command's override" line
# below) to make a same-named header shadow one $CFLAGS's own later -I
# dirs would otherwise resolve to. diff's own real `<regex.h>` (gnulib's
# GNU-syntax regex engine, NOT this project's shared TRE/POSIX one) is
# the first real need for this -- see the gnudiff_glue generation
# comment above and this loop's own "diff" cmd_cflags case. Deliberately
# NOT done by simply reordering `$cmd_cflags $CFLAGS` on the compile
# line instead (tried first, reverted): that would also move every
# *other* command's existing cmd_cflags -I dirs ahead of $CFLAGS, and
# grep/gzip's own zlib_glue -I -- added for a completely different
# reason (zlib's inflate/deflate.c's own internal zutil.h, never meant
# to shadow anything) -- happens to contain its OWN internal zlib.h,
# which would then wrongly win over $OUT/compat_include/zlib.h's real,
# complete public API forwarder that grep.c/gzip.c's own `#include
# <zlib.h>` needs (confirmed by the real, reproducible build break this
# caused before reverting, not guessed).
mkdir -p "$OUT/cmd_pre_include"
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -I $OUT/cmd_pre_include \
    -I $LIBC/include -I $LIBC/locale -I $LIBC/locale/FreeBSD -I $LIBC/darwin -I $LIBC/gen \
    -I $LIBC/fbsdcompat \
    -I $LIBC/stdtime/FreeBSD \
    -I $OUT/compat_include \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall -I $XNU/libsyscall/wrappers \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I ../../third_party/removefile"

# Real err()/getopt() -- every tier-1 command needs at least one of the
# two; harmless to always link both (unused-but-linked, same tolerance
# this project's other tiers already take with e.g. libclosure/libmalloc
# stand-ins -- confirmed no duplicate-symbol conflicts, zero of these
# tier-1 sources define their own err/getopt). getopt.c's own exports
# (`_getopt`/`_optarg`/`_optind`/`_opterr`/`_optreset`) really are still
# genuinely absent from every LINK_DYLIBS sibling (re-confirmed via
# `xcrun dyld_info -exports`, 2026-09-04, DAR-196) -- this project's own
# libsystem_kernel.dylib is a narrow Mach/VM-focused port, not a full
# syscalls.master surface, and libsystem_c.dylib never picked up getopt
# at all. getopt.c stays fully linked, unnarrowed, for real.
#
# err.c is a DIFFERENT story now (DAR-196, 2026-09-04): the
# 2026-08-30 audit this comment used to cite is stale -- libsystem_c.dylib
# now genuinely exports 13 of err.c's 15 public symbols for real
# (`_errc`/`_errx`/`_verr`/`_verrc`/`_verrx`/`_vwarn`/`_vwarnc`/`_vwarnx`/
# `_warnc`/`_warnx`/`_err_set_exit`/`_err_set_exit_b`/`_err_set_file`,
# confirmed via `xcrun dyld_info -exports`), so common_err.o now gets the
# same private-extern-preserving narrow treatment as common_findfp.o
# below (see that block's own comment) rather than linking wholesale.
# The other 2 (`__err`/`__warn`, i.e. C `_err()`/`_warn()`) stay fully
# exported on purpose: real Apple's own err.c makes public `err()`/
# `warn()` a `__weak_reference(_err, err)`/`__weak_reference(_warn, warn)`
# alias of these two -- that macro is a no-op under this project's
# freestanding build, so `link_aliases` below (`-Wl,-alias,__err,_err
# -Wl,-alias,__warn,_warn`) synthesizes the same public names by hand,
# and a `-Wl,-alias` base must stay a real, locally-defined (not
# necessarily externally-VISIBLE, but definitely present) symbol in the
# same link -- see this project's own "-Wl,-alias nonlocal-base bug"
# finding. Genuinely no state-consistency risk from splitting these two
# families across two owners: a real sweep of every simple_cmds source
# file confirmed nothing here ever calls `err_set_exit()`/
# `err_set_file()` (the only two entry points whose behavior a caller
# could observe diverge between "this object's local `_e_err_file`/
# `_e_err_exit`" and "the dylib's own copy") -- chmod_acl.c only
# *mentions* err_set_exit() in a comment, never calls it.
#
# table.c/none.c added 2026-08-28 alongside the NEWLD_BINDIR fix above --
# real third_party/Libc/locale/FreeBSD/table.c (_DefaultRuneXLocale, the
# C-locale ctype table libc_locale_globals_compat.c's __global_locale
# points at) and its own real none.c dependency (the encoding-stub
# __none_mbrtowc/__none_wcrtomb/etc functions table.c's static
# initializer references). Still always linked -- but now NARROWED (see
# the `narrow_rune_locale.sh` call right after this loop): compiled under
# this file's own CFLAGS, table.c/none.c ALSO define
# `__CurrentRuneLocale`/`__DefaultRuneLocale`/`___mb_cur_max`/
# `___mb_sb_limit`, real PUBLIC process-wide locale globals that
# libsystem_c.dylib -- a real sibling on every command's link line --
# already exports for real. Before 2026-08-30 every one of the ~70
# commands here carried its OWN independently-mutable copy of "the
# current locale", the same duplicate-STATE bug class as
# mach_task_self_/g_zone (docs/static-linking-cleanup-plan.md). The
# private-extern internals these two files are actually needed for
# (`_DefaultRuneXLocale`/`__none_*`/whatever `___global_locale` cascades
# into) are untouched by the narrowing -- same treatment already applied
# to libiconv/libncurses/libcopyfile/libresolv/libopenpam.dylib.
#
# getcwd.c/simple_cmds_getenv_compat.c REMOVED from this list entirely
# 2026-08-30 (static-linking cleanup, Phase 2a) -- verified via `nm -m`
# that every symbol each one defines is BOTH fully external (no
# private-extern content requiring a local copy) AND already a real
# export of libsystem_c.dylib:
#   simple_cmds_getenv_compat.c -> its own separate getenv/setenv/
#                  unsetenv/putenv/_NSGetEnviron, each managing a SECOND,
#                  disconnected environ array -- real duplicate MUTABLE
#                  STATE, the same class of bug as the FILE table above,
#                  and arguably worse (a setenv() through this copy would
#                  be invisible to any real libsystem_c.dylib consumer,
#                  and vice versa). libsystem_c.dylib's real setenv.c
#                  family (build_libsystem_c.sh) covers every one of
#                  these for real, confirmed via nm -gUj.
#   getcwd.c's own real export (`_getcwd`) is likewise already a real
#                  libsystem_c.dylib export.
#
# collate.c/collcmp.c/ldpart.c stay -- a first pass (2026-08-30) tried
# dropping these too on the same "libsystem_c already covers their
# public exports" theory, but a real rebuild of all ~70 commands proved
# it wrong: several PER-COMMAND extra objects need symbols these files
# provide that libsystem_c.dylib does NOT export --
# `___part_load_locale`/`___ldpart_free_extra` (ldpart.c, needed by every
# command that links a `*_timelocal.o` extra: date/gzip/diff/tar/less)
# and `___collate_collating_symbol`/`___collate_equiv_class`/
# `___collate_range_cmp` (collate.c, needed by diff's fnmatch wrapper).
#
# findfp.c ALSO stays, for the same real reason, discovered the same
# way: 6 commands (tail/fold/expand/unexpand/hexdump/nl) link
# Libc/stdio/FreeBSD/freopen.c as a per-command extra, and real Apple's
# freopen() calls `__sfprelease()` -- which is `__private_extern__` in
# real findfp.c (confirmed via `nm -m`: libsystem_c.dylib's own copy is
# "non-external (was a private external)" too), so no image can reach
# another's copy. Unlike collate.c/ldpart.c, findfp.c's OTHER 9 symbols
# (___sF/___sfp/___sglue/___sdidinit/__cleanup/___sinit/___stdinp/
# ___stdoutp/___stderrp) really are fully public and really are
# libsystem_c.dylib's job -- so findfp.c gets the same narrowing
# treatment as table.c/none.c below, rather than being linked wholesale
# the way it was before this pass.
COMMON_EXTRA="$LIBC/gen/FreeBSD/err.c $LIBC/stdlib/FreeBSD/getopt.c simple_cmds_read_compat.c libc_locale_globals_compat.c simple_cmds_libsystem_c_gap_compat.c cp_mv_syscalls.c $LIBC/locale/FreeBSD/table.c $LIBC/locale/FreeBSD/none.c $LIBC/locale/FreeBSD/ldpart.c $LIBC/locale/FreeBSD/collate.c $LIBC/locale/FreeBSD/collcmp.c $LIBC/stdio/FreeBSD/findfp.c"
# cp_mv_syscalls.c promoted from a cp/mv-only CMD_LIST extra source to a
# common object (2026-08-28): its real lchown()/setattrlist()/waitpid()
# turned out to be needed by many more commands once the dynamic-link
# conversion's own real per-command undefined-symbol sweep ran (chown/
# chflags/chmod/find/tar/etc, not just cp/mv) -- same real syscalls
# (#364/#221/real fork+wait4), just needed broadly now. cp/mv's own
# CMD_LIST entries below no longer list cp_mv_syscalls.c as a per-command
# extra (would otherwise duplicate-symbol against this common copy).
for f in $COMMON_EXTRA; do
    base=$(basename "$f" .c)
    out="$OUT/simple_obj/common_${base}.o"
    if [ ! -f "$out" ]; then
        # collate.c only: real Libc/locale/FreeBSD/collate.h guards its
        # own __collate_mbstowcs/__collate_wcsdup/etc prototypes behind
        # `#ifdef __LIBC__` -- without it, collate.c's own internal
        # forward uses of those two functions get an implicit-int
        # declaration that conflicts with their real wchar_t*-returning
        # definitions later in the same file.
        f_flags=""
        case "$base" in
            collate) f_flags="-D__LIBC__" ;;
        esac
        "$CLANG" $CFLAGS $f_flags "$f" -o "$out"
    fi
done
# Narrow common_table.o/common_none.o's public rune-locale globals so
# they bind to libsystem_c.dylib instead of this project's ~70 commands
# each carrying their own copy (static-linking cleanup, Phase 2a,
# 2026-08-30) -- see narrow_rune_locale.sh's own header and COMMON_EXTRA's
# comment above for the full rationale. Idempotent: narrowing an
# already-narrowed object a second time is a correctness-preserving
# no-op (the symbols are already private), so this is safe to run every
# build even though common_table.o/common_none.o are cached across runs.
./narrow_rune_locale.sh "$OUT/simple_obj" common_

# Narrow common_findfp.o's 9 fully-public stdio FILE-table symbols the
# same way (static-linking cleanup, Phase 2a, 2026-08-30) -- see
# COMMON_EXTRA's own comment on findfp.c above. __sfprelease (private
# extern) and anything else findfp.c defines survive untouched via
# -keep_private_externs, same mechanism narrow_rune_locale.sh uses.
# _f_prealloc ADDED 2026-09-04 (DAR-196): a 10th fully-public findfp.c
# symbol, missed in the original 2026-08-30 pass -- real, genuine
# libsystem_c.dylib export (confirmed via `xcrun dyld_info -exports`),
# same narrowing treatment as the other 9.
FINDFP_OBJ="$OUT/simple_obj/common_findfp.o"
if [ -f "$FINDFP_OBJ" ]; then
    XCODE12_LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
    "$XCODE12_LD" -r -arch arm64 -keep_private_externs \
        -unexported_symbol ___sF -unexported_symbol ___sfp \
        -unexported_symbol ___sglue -unexported_symbol ___sdidinit \
        -unexported_symbol __cleanup -unexported_symbol ___sinit \
        -unexported_symbol ___stdinp -unexported_symbol ___stdoutp \
        -unexported_symbol ___stderrp -unexported_symbol _f_prealloc \
        "$FINDFP_OBJ" -o "$FINDFP_OBJ.narrowed"
    mv -f "$FINDFP_OBJ.narrowed" "$FINDFP_OBJ"
fi

# Narrow common_err.o's 13 fully-public, state-free err(3) family
# symbols the same way (DAR-196, 2026-09-04) -- see COMMON_EXTRA's own
# updated comment on err.c above for the full rationale (real libsystem_c
# .dylib exports, real sweep confirming no simple_cmds source calls the
# two state-setter entry points this could otherwise make inconsistent).
# __err/__warn are DELIBERATELY excluded from this list -- they must
# stay a real, present (if no longer externally-visible-to-other-images)
# local definition for link_aliases' `-Wl,-alias,__err,_err`/
# `-Wl,-alias,__warn,_warn` below to have a base to alias, since real
# `err()`/`warn()` are not real dylib exports here (this project's own
# libsystem_c.dylib build never picked up the bare, unversioned names --
# only their `_`-prefixed FreeBSD internals and the rest of the v*/*c/*x
# family).
COMMON_ERR_OBJ="$OUT/simple_obj/common_err.o"
if [ -f "$COMMON_ERR_OBJ" ]; then
    XCODE12_LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
    "$XCODE12_LD" -r -arch arm64 -keep_private_externs \
        -unexported_symbol _err_set_exit -unexported_symbol _err_set_exit_b \
        -unexported_symbol _err_set_file \
        -unexported_symbol _errc -unexported_symbol _errx \
        -unexported_symbol _verr -unexported_symbol _verrc -unexported_symbol _verrx \
        -unexported_symbol _vwarn -unexported_symbol _vwarnc -unexported_symbol _vwarnx \
        -unexported_symbol _warnc -unexported_symbol _warnx \
        "$COMMON_ERR_OBJ" -o "$COMMON_ERR_OBJ.narrowed"
    mv -f "$COMMON_ERR_OBJ.narrowed" "$COMMON_ERR_OBJ"
fi

# Narrow common_collate.o's 4 fully-public collate(3) lookup symbols the
# same way (DAR-196, 2026-09-04) -- real, genuine libsystem_c.dylib
# exports (confirmed via `xcrun dyld_info -exports`). The 3 symbols
# diff's du_fnmatch_libc_wrapper.c actually needs from this file
# (`___collate_collating_symbol`/`___collate_equiv_class`/
# `___collate_range_cmp`, see COMMON_EXTRA's own comment above) are
# UNAFFECTED: the first two are already `__private_extern__` in real
# FreeBSD collate.c (confirmed via `nm -m`, survive via
# -keep_private_externs same as __sfprelease), and the third is
# genuinely local-only (no dylib defines it at all) so was never a
# candidate for narrowing in the first place.
COMMON_COLLATE_OBJ="$OUT/simple_obj/common_collate.o"
if [ -f "$COMMON_COLLATE_OBJ" ]; then
    XCODE12_LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
    "$XCODE12_LD" -r -arch arm64 -keep_private_externs \
        -unexported_symbol ___collate_equiv_match \
        -unexported_symbol ___collate_load_error \
        -unexported_symbol ___collate_lookup \
        -unexported_symbol ___collate_lookup_l \
        "$COMMON_COLLATE_OBJ" -o "$COMMON_COLLATE_OBJ.narrowed"
    mv -f "$COMMON_COLLATE_OBJ.narrowed" "$COMMON_COLLATE_OBJ"
fi

# Real, unmodified find(1) getdate.y (third_party/shell_cmds/find/
# getdate.y) -- find's real -newerXY predicate can take either a
# reference file OR a literal natural-language date string
# ("2 days ago", "next monday", real ISO-ish timestamps, etc), and
# function.c's real c_newer() calls get_date() unconditionally for the
# literal-string case (confirmed by reading it, not assumed dead code --
# the symbol has to link even if a given test run never exercises that
# branch). This is upstream's own classic public-domain getdate.y
# grammar (same file traceable to the original Bellovin/Salz/Berets BSD
# getdate.y this project's other real, unmodified sources trace back
# to) -- genuinely different from, and not a substitute for, Apple's own
# real Libc/stdtime/getdate.c (that one implements the POSIX
# getdate(3) API instead: a $DATEMSK-template-file matcher returning
# struct tm*, incompatible signature and semantics, not what find.c
# calls). Real yacc/bison is a standard build-time code generator (this
# host has /usr/bin/bison, GNU Bison 2.3) -- running it over real,
# unmodified upstream grammar source at build time is the same real
# "run a real generator over real source" pattern this script already
# uses for the raw-syscall .s stubs above (create-syscalls.pl), not a
# fabrication. `bison -y` (POSIX yacc-compatible output naming) is the
# same invocation upstream's own Makefile uses for this exact file.
mkdir -p "$OUT/find_gen"
if [ ! -f "$OUT/find_gen/getdate.c" ]; then
    bison -y -o "$OUT/find_gen/getdate.c" "$SHELL_CMDS/find/getdate.y" \
        || { echo "error: bison generation of find's getdate.y failed" >&2; exit 1; }
fi

# Real sched_yield()/swtch_pri Mach trap: libpthread.a's real
# contended-rwlock slow path (__pthread_rwlock_lock_slow) needs it --
# same real object every prior tier (uname/ps/etc) already links.
if [ ! -f "$OUT/simple_sched_yield_trap.o" ]; then
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
        libunwind_sched_yield.s -o "$OUT/simple_sched_yield_trap.o"
fi
if [ ! -f "$OUT/simple_sched_yield.o" ]; then
    "$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/simple_sched_yield.o"
fi

# Real raw syscall stubs this project has never generated before, all
# via the same Perl generator bash/ps/uname's own SYS_STUB_NAMES passes
# already use. One generation pass, multiple .s outputs picked out of it
# -- real Apple asm, not hand-written. Generated-symbol names confirmed
# by generating and reading the real .s output, not guessed (real
# double/triple-underscore C-symbol-mangling conventions vary per
# syscall depending on whether the public Libc wrapper calls a private
# name -- e.g. `___getlogin`/`___mmap`/`___munmap` vs plain `_kevent`/
# `_kqueue`, which have no private wrapper layer at all).
# _readlinkat: tar(1)'s real archive_read_disk_posix.c (symlink-target
# reads during a real *at()-relative directory-tree walk). syscalls.master
# #473, not NO_SYSCALL_STUB, a plain generatable addition.
# _poll: tar(1)'s real filter_fork_posix.c (__archive_check_child()'s
# real HAVE_POLL path, waiting on the external-decompressor pipe).
# syscalls.master #230, plain cancelable poll, not NO_SYSCALL_STUB.
# _fsync: less(1)'s real ch.c (log-file (-o/-O) durability on close).
# syscalls.master #95, plain cancelable fsync, not NO_SYSCALL_STUB.
# _statfs deliberately NOT in this list -- see
# simple_cmds_statfs64_compat.c's header comment: the auto-generated
# stub for syscalls.master's plain "statfs" entry (#157) is a REAL but
# legacy 32-bit-struct syscall, wrong for this project's
# __DARWIN_64_BIT_INO_T=1 build (which expects "statfs" to mean
# "statfs$INODE64", backed by #345/statfs64's modern struct layout --
# same class of mismatch fstatfs() already had to work around). Hand-
# written correctly in simple_cmds_statfs64_compat.c instead, alongside a
# correct getfsstat() (also never in this list -- same file).
SYS_STUBS_NEEDED="___getlogin ___mmap ___munmap _kevent _kqueue _ftruncate _mkdir _utimes ___chmod _sigprocmask _fchmodat _fstatat64 _mkdirat _umask _access _getuid _execve _link _symlink _getegid _geteuid _getgid _getgroups _chflags _chown _mknod _mkfifo _futimes _readlink _pread _pwrite _pathconf _faccessat _getentropy _dup2 _getpriority _chroot _setgroups _setgid _setuid _chdir _fgetattrlist _fsetattrlist _dup _readlinkat _poll _fsync _getrusage"
if [ ! -f "$OUT/simple_getlogin_trap.o" ]; then
    mkdir -p "$OUT/sys_asm_compat/mach/arm64"
    cat > "$OUT/sys_asm_compat/mach/arm64/asm.h" <<'EOF'
/* SYS.h expects the installed-SDK-style path mach/arm64/asm.h; real
 * content is osfmk/arm64/asm.h in this checkout -- same forwarding-shim
 * pattern build_bash.sh's own copy of this file already uses. */
#include <arm64/asm.h>
EOF
    SYS_TMPGEN="$(mktemp -d)"
    trap 'rm -rf "$SYS_TMPGEN"' EXIT
    ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
        "$XNU/bsd/kern/syscalls.master" \
        "$XNU/libsyscall/custom" \
        "$XNU/libsyscall/Platforms" \
        iPhoneOS \
        "$SYS_TMPGEN" >/dev/null
    for s in $SYS_STUBS_NEEDED; do
        "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
            -I"$SYS_TMPGEN" -I"$OUT/sys_asm_compat" -I"$XNU/libsyscall/custom" -I"$XNU/libsyscall/mach" -I"$XNU/osfmk" \
            "$SYS_TMPGEN/${s}.s" -o "$OUT/simple_obj/sys_${s}.o" 2>"$OUT/simple_obj/sys_${s}.err" \
            || { echo "error: $s raw syscall stub failed:" >&2; cat "$OUT/simple_obj/sys_${s}.err" >&2; exit 1; }
    done
    rm -rf "$SYS_TMPGEN"
    trap - EXIT
    touch "$OUT/simple_getlogin_trap.o" # sentinel: this whole block already ran
fi

# Real pipe(2) for ed's popen()/pclose() (ed_popen_system_compat.c).
# syscalls.master marks pipe NO_SYSCALL_STUB (real kernel trap returns
# BOTH fds via x0/x1, not a single value -- a real special case a
# generic single-return-value stub generator can't produce), but unlike
# getrlimit/setattrlist's NO_SYSCALL_STUB gaps elsewhere in this project
# (hand-written from scratch), Apple ships a real, complete,
# already-correct hand-written arm64 implementation for this one right
# in libsyscall/custom/__pipe.s (confirmed by reading it: takes the
# caller's `int fd[2]` pointer in x0, does the raw syscall, stores the
# kernel's x0/x1 results into fd[0]/fd[1], returns 0/error via the same
# real cerror_nocancel convention every other raw syscall here uses) --
# compiled directly, real Apple asm, not hand-rolled.
if [ ! -f "$OUT/ed_pipe_trap.o" ]; then
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
        -I"$OUT/sys_asm_compat" -I"$XNU/libsyscall/custom" -I"$XNU/libsyscall/mach" -I"$XNU/osfmk" \
        "$XNU/libsyscall/custom/__pipe.s" -o "$OUT/ed_pipe_trap.o" 2>"$OUT/ed_pipe_trap.err" \
        || { echo "error: __pipe.s compile failed:" >&2; cat "$OUT/ed_pipe_trap.err" >&2; exit 1; }
fi

# Real AAPCS64 register-save/restore primitives for ed's setjmp/longjmp/
# sigsetjmp/siglongjmp (ed_setjmp_compat.c calls these; see both files'
# own header comments). Compiled as its own plain .s object here
# (rather than through the generic per-command CFLAGS extra-file loop
# below, which assumes C sources) since assembly needs neither this
# tier's C defines nor -include libc_build_prototypes.h.
if [ ! -f "$OUT/ed_setjmp_raw.o" ]; then
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
        ed_setjmp_raw.s -o "$OUT/ed_setjmp_raw.o"
fi

# Real sigaltstack(2) raw syscall for diff(1)'s real, unmodified
# lib/c-stack.c (`c_stack_action()`, called unconditionally at diff's
# own startup to install a SIGSEGV-on-alternate-stack handler for deep
# recursive comparisons). syscalls.master marks sigaltstack
# NO_SYSCALL_STUB (same category as pipe above -- a real case the
# generic create-syscalls.pl generator skips), but Apple ships a real,
# already-correct hand-written arm64 stub at
# libsyscall/custom/__sigaltstack.s (same __SYSCALL macro/cerror
# convention as every generated stub, just not auto-generated); compiled
# directly here rather than added to SYS_STUBS_NEEDED, matching the
# __pipe.s precedent immediately above.
if [ ! -f "$OUT/simple_obj/sys____sigaltstack.o" ]; then
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
        -I"$OUT/sys_asm_compat" -I"$XNU/libsyscall/custom" -I"$XNU/libsyscall/mach" -I"$XNU/osfmk" \
        "$XNU/libsyscall/custom/__sigaltstack.s" -o "$OUT/simple_obj/sys____sigaltstack.o" 2>"$OUT/simple_obj/sys____sigaltstack.err" \
        || { echo "error: __sigaltstack.s compile failed:" >&2; cat "$OUT/simple_obj/sys____sigaltstack.err" >&2; exit 1; }
fi

# Real, correct statfs()/getfsstat() -- see
# simple_cmds_statfs64_compat.c's own header comment for why these are
# hand-written here instead of via the generic SYS_STUBS_NEEDED batch
# above. Compiled once, linked into every command via LINK_COMMON (df/
# du/mv all reference statfs(); df's real getmntinfo.c is this project's
# first getfsstat() caller).
if [ ! -f "$OUT/simple_obj/statfs64_compat.o" ]; then
    "$CLANG" $CFLAGS simple_cmds_statfs64_compat.c -o "$OUT/simple_obj/statfs64_compat.o"
fi

# Narrow statfs64_compat.o's `_statfs` (DAR-196, 2026-09-04): real,
# genuine libcopyfile.dylib export -- confirmed via `xcrun dyld_info
# -exports` it exports BOTH `_statfs` AND `_statfs$INODE64` directly
# (build_copyfile_dylib.sh's own header confirms these are real,
# genuine BSD raw-syscall wrappers local to libcopyfile's own archive,
# exposed the same `-Wl,-alias` way this project's own link_aliases
# below used to do it -- same shape, not a different implementation).
# libcopyfile.dylib is reached from every command here transitively via
# libSystem.B.dylib's real LC_REEXPORT_DYLIB of it, already on every
# link line via $LINK_DYLIBS. `_getfsstat` stays local and public --
# genuinely absent from every shipped dylib (confirmed via `xcrun
# dyld_info -exports`), still this project's only real provider.
# link_aliases' own `-Wl,-alias,_statfs,_statfs$INODE64` entry is
# REMOVED alongside this (see below) -- no longer needed now that the
# real dylib exports both names directly.
STATFS64_OBJ="$OUT/simple_obj/statfs64_compat.o"
if [ -f "$STATFS64_OBJ" ]; then
    XCODE12_LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
    "$XCODE12_LD" -r -arch arm64 -keep_private_externs \
        -unexported_symbol _statfs \
        "$STATFS64_OBJ" -o "$STATFS64_OBJ.narrowed"
    mv -f "$STATFS64_OBJ.narrowed" "$STATFS64_OBJ"
fi

# sys__kevent.o/sys__kqueue.o/sys__getgroups.o REMOVED from this list
# 2026-09-02 (DAR-196): the header comment above this block ("every one
# of these real syscalls is genuinely absent from [libsystem_kernel.dylib]")
# was true when written (2026-08-28) but went stale -- `xcrun dyld_info
# -exports` on today's real libc_build/system/libsystem_kernel.dylib shows
# real, genuine `_kevent`/`_kqueue`/`_getgroups` exports (reached from
# every command here transitively via libSystem.B.dylib's real
# LC_REEXPORT_DYLIB of libsystem_kernel.dylib, already on every link line
# via $LINK_DYLIBS). All three are plain, single-syscall stubs generated
# by Apple's own create-syscalls.pl (SYS_STUBS_NEEDED loop above) -- no
# per-command special-casing to lose, so dropping this project's own
# private copies in favor of the real dylib exports is a straight
# same-code swap, not a behavior change. The .o files themselves are
# still generated above (unchanged) -- build_libdispatch_real.sh/
# build_libdispatch_dylib.sh/build_libnotify_client.sh/build_libxpc*.sh/
# build_syslogd.sh reference these exact paths directly and are out of
# DAR-196's scope (build_simple_cmds.sh's own ~70 binaries only).
#
# sys____chmod.o/sys__fstatat64.o/sys__ftruncate.o/sys__mkdir.o/
# sys__mkdirat.o/sys__access.o/sys__getuid.o/sys__getegid.o/
# sys__geteuid.o/sys__sigprocmask.o/sys__getentropy.o REMOVED from this
# list 2026-09-04 (DAR-196, same pattern as the kevent/kqueue/getgroups
# batch above): `xcrun dyld_info -exports` confirms libsystem_kernel
# .dylib/libsystem_c.dylib -- BOTH already directly on every command's
# own link line via $LINK_DYLIBS, not merely reached through a sibling's
# reexport -- genuinely, directly export `___chmod`/`_fstatat`/
# `_fstatat64`/`_ftruncate`/`_mkdir`/`_mkdirat`/`_access`/`_getuid`/
# `_getegid`/`_geteuid`/`_sigprocmask` for real. Same straight
# same-code swap, not a behavior change; verified via `dup_symbol_audit.sh
# --summary` (all 11 dropped out of the per-binary shadow list) and a
# real QEMU boot.
#
# sys__pread.o/sys__pwrite.o/sys__readlink.o/sys__symlink.o/
# sys__chflags.o REMOVED from this list 2026-09-04 (DAR-196, second pass
# same day): the previous pass deferred these because their real owners
# (libcopyfile.dylib/libremovefile.dylib) are reached only transitively,
# via libSystem.B.dylib's LC_REEXPORT_DYLIB, not a direct $LINK_DYLIBS
# entry -- `bind_target_audit.sh` (written exactly for this reexport-vs-
# plain-link distinction, see its own header) now confirms clean on a
# real full run, and build_copyfile_dylib.sh's own header confirms
# libcopyfile.dylib's `_pread`/`_pwrite`/`_statfs` (and, by the same
# real-Apple-raw-syscall reasoning, `_readlink`/`_symlink`) are
# themselves real, plain BSD raw-syscall wrappers local to that
# archive -- same shape as this project's own sys__*.o objects, not a
# different implementation. `_pread`/`_pwrite`'s local copies were also
# link_aliases' alias BASE for `-Wl,-alias,_pread,_pread$UNIX2003`/
# `-Wl,-alias,_pwrite,_pwrite$UNIX2003` -- REMOVED alongside these (see
# below), since libcopyfile.dylib already exports `_pread$UNIX2003`/
# `_pwrite$UNIX2003` directly, no local alias needed any more.
LINK_COMMON="$OUT/simple_obj/sys____getlogin.o \
    $OUT/simple_obj/sys__utimes.o \
    $OUT/simple_obj/sys__fchmodat.o $OUT/simple_obj/sys__umask.o \
    $OUT/simple_obj/sys__execve.o \
    $OUT/simple_obj/sys__link.o \
    $OUT/simple_obj/sys__getgid.o \
    $OUT/simple_obj/sys__chown.o $OUT/simple_obj/sys__mknod.o \
    $OUT/simple_obj/sys__mkfifo.o $OUT/simple_obj/sys__futimes.o \
    $OUT/simple_obj/sys__pathconf.o $OUT/simple_obj/sys__faccessat.o \
    $OUT/simple_obj/sys__dup2.o $OUT/simple_obj/sys__getpriority.o \
    $OUT/simple_obj/sys__chroot.o $OUT/simple_obj/sys__setgroups.o $OUT/simple_obj/sys__setgid.o $OUT/simple_obj/sys__setuid.o \
    $OUT/simple_obj/sys__chdir.o $OUT/simple_obj/statfs64_compat.o"
# REAL ARCHITECTURE CHANGE 2026-08-28: this file used to link every
# command fully static against this project's own libc_printf.a/
# libc_gen.a/libc_stdio.a/libc_locale_for_stdio.a/libpthread.a/
# libclosure.a/libmalloc.a/libplatform.a/cerror_stub.o, plus a ~60-entry
# -Wl,-alias workaround for ld64-609's real internal crash (Assertion
# failed: (target != NULL) in generateLinkEditInfo -- see git history
# around 2026-08-27 for the full saga this replaces: LINK_ALIASES/
# LINK_ALIASES_TAR/LINK_ALIASES_ERRWARN/_STDIO_EXTRA/_REGEX/_CPMV/_ATTR/
# _SIGSUSPEND/_SOCKET/_GETRLIMIT/_PIPE, one alias block per real
# UNIX2003/INODE64/DARWIN_EXTSN symbol-versioning gap that static linking
# forced this project to synthesize by hand). Now that real dyld-940
# boots and libSystem.B.dylib is real (Phase 6.5/6.7), that entire
# archive+alias stack is obsolete: libSystem.B.dylib already reexports
# real libsystem_c.dylib/libsystem_pthread.dylib/libsystem_malloc.dylib/
# libsystem_platform.dylib/libsystem_blocks.dylib (covering everything
# libc_printf.a/libc_gen.a/libc_stdio.a/libc_locale_for_stdio.a/
# libc_regex.a/libpthread.a/libclosure.a/libmalloc.a/libplatform.a used
# to provide -- confirmed via nm -gU on each dylib, not guessed) and
# already bakes in the exact same UNIX2003/INODE64/DARWIN_EXTSN alias set
# directly into its own build (build_libsystem.sh's own tail) -- clients
# linking against it need zero alias flags of their own, matching real
# Apple's own architecture instead of working around this project's
# former lack of one. Only the project's own real, hand-written BSD
# syscall wrapper objects above survive unchanged: libsystem_kernel.dylib
# turns out to be a narrow, Mach/VM-bootstrap-focused port (Phase 6.5
# step 1), NOT a full syscalls.master surface -- confirmed via nm -gU
# that every one of these real syscalls is genuinely absent from it, so
# this project's own separate, already-complete raw-syscall port here is
# still the only real provider of each. sys____mmap.o/sys____munmap.o
# and the mmap-wrapping common_mmap_compat.o were dropped from this list
# (unlike the syscalls above) because libSystem.B.dylib itself already
# exports plain _mmap/_munmap via its own real alias over
# libsystem_kernel.dylib's real mmap syscall -- keeping this project's
# own copies alongside would just be a redundant, divergent
# implementation of something now real upstream.
# libsystem_platform.dylib ADDED 2026-09-04 (DAR-13, real-Apple-parity
# fix): real single owner of _memcpy/_memset now (libsystem_c.dylib no
# longer defines them, see build_libsystem_c.sh's own updated header).
# This comment's own "clients need zero alias flags, libSystem.B.dylib
# already reexports everything" claim turned out not to hold for real
# LINK-TIME resolution here: a real trial link relying on libSystem.B.dylib's
# LC_REEXPORT_DYLIB of libsystem_platform.dylib alone failed with
# "_memcpy"/"_memset" undefined across all 76 commands (common_findfp.o's
# _moreglue, common_collate.o, etc) -- the exact same real install-name-
# vs-local-copy resolution hazard this project's own CLAUDE.md already
# documents for build_libdyld.sh's libresolv.9.dylib case (ld64 resolving
# an absolute /usr/lib/... install name against the HOST SDK's own real
# stub instead of this project's local libc_build/system/ copy, rather
# than a code bug in the reexport itself). Fix: list the real owning
# dylib directly, same pattern every other consumer in this project
# already uses for this exact reason.
# libcopyfile.dylib/libremovefile.dylib ADDED 2026-09-04 (DAR-196, second
# pass same day): same exact real install-name-vs-local-copy resolution
# hazard as libsystem_platform.dylib just above, hit for real trying to
# drop LINK_COMMON's local `_pread`/`_pwrite`/`_readlink`/`_symlink`/
# `_chflags`/`_statfs` in favor of libcopyfile.dylib/libremovefile.dylib's
# real exports (reached, in principle, transitively via libSystem.B
# .dylib's own LC_REEXPORT_DYLIB of both): a real trial link of df (the
# first command in CMD_LIST order that references `_statfs$INODE64`
# without also already listing libcopyfile.dylib in its own per-command
# extra_libs) failed with `_statfs$INODE64` undefined -- ld64 resolving
# libSystem.B.dylib's reexport of `/usr/lib/system/libcopyfile.dylib`
# against the HOST SDK's own real stub (which does not carry this
# project-specific symbol) instead of this project's local
# libc_build/system/ copy. Fix: list both real owning dylibs directly,
# same pattern as libsystem_platform.dylib above. Harmless where a
# handful of commands already list one or both again in their own
# per-command extra_libs (chmod/chown/mv/cp/tar/ed/etc, for their own
# unrelated needs like fchmod$UNIX2003/getattrlist) -- a dylib listed
# twice on one link line is a real, ordinary no-op, not a conflict.
# Real prerequisite check (2026-09-21) -- see build_ifconfig.sh's
# identically-structured block for why this is needed: without it, a
# from-scratch build hits a bare clang "no such file" pile instead of one
# clear message, and gen_build_order.py never learns this is a real
# dependency.
for d in libsystem_c libsystem_pthread libsystem_malloc libsystem_platform; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/system/libdyld.dylib" ]; then
    echo "error: $OUT/system/libdyld.dylib not found -- run build_libdyld.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libcopyfile.dylib" ]; then
    echo "error: $OUT/system/libcopyfile.dylib not found -- run build_copyfile_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libremovefile.dylib" ]; then
    echo "error: $OUT/system/libremovefile.dylib not found -- run build_removefile_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libSystem.B.dylib" ]; then
    echo "error: $OUT/system/libSystem.B.dylib not found -- run build_libsystem.sh first" >&2
    exit 1
fi
LINK_DYLIBS="$OUT/system/libsystem_c.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libcopyfile.dylib $OUT/system/libremovefile.dylib $OUT/system/libSystem.B.dylib"
# libsystem_malloc.dylib: real per-command need for the private
# ___pthread_setspecific/___pthread_getspecific/___pthread_once variants
# (one leading underscore in C source) real libmalloc's own per-thread
# zone-cache TSD path exports -- confirmed via nm -gU as the only real
# provider, same "must link every sub-dylib a real symbol is genuinely
# local to, transitive reexport-only doesn't resolve" finding as
# libsystem_pthread.dylib/libsystem_c.dylib above.
# libsystem_pthread.dylib must be linked directly, not just transitively
# via libSystem.B.dylib's own -reexport_library -- ld64 cannot resolve a
# two-level-namespace reexport chain through a dylib whose
# LC_REEXPORT_DYLIB install-name path (/usr/lib/system/
# libsystem_pthread.dylib) doesn't exist at that exact path on this
# build host (only a real target boot image has it there); giving ld64
# the real local .dylib file directly on the command line sidesteps that
# (confirmed via a real prototype trial link + real QEMU boot test,
# 2026-08-28, not guessed -- every one of these 76 commands needs
# generic_main_wrapper.c's real pthread_main_thread_bootstrap() call,
# which is why this dylib is in the always-linked common set rather than
# scoped like libm.a/libmach_ipc.a below).

# Bash array, one element per command: "name:srcfile:extra1|extra2|..."
# (srcfile is the FULL real source path -- not just relative to
# $SHELL_CMDS -- so this list can span multiple third_party/*_cmds
# top-level dirs, not just shell_cmds. extras are '|'-separated, not
# space-separated, so a multi-file extra list can't collide with the
# outer per-command split -- a real bug this script had at first: a
# space-separated top-level CMD_LIST string silently split a command's
# own multi-file "extra" field into bogus extra top-level entries,
# confirmed via a real, reproducible garbled-path link failure for
# hostname's two-file gethostname.c/sethostname.c extra list).
CMD_LIST=(
    "true:$SHELL_CMDS/true/true.c:"
    "false:$SHELL_CMDS/false/false.c:"
    "yes:$SHELL_CMDS/yes/yes.c:"
    "pwd:$SHELL_CMDS/pwd/pwd.c:"
    "basename:$SHELL_CMDS/basename/basename.c:"
    "dirname:$SHELL_CMDS/dirname/dirname.c:"
    "echo:$SHELL_CMDS/echo/echo.c:"
    "printf:$SHELL_CMDS/printf/printf.c:$LIBC/stdlib/FreeBSD/atoi.c"
    "hostname:$SHELL_CMDS/hostname/hostname.c:$LIBC/gen/FreeBSD/gethostname.c|$LIBC/gen/FreeBSD/sethostname.c"
    "logname:$SHELL_CMDS/logname/logname.c:$LIBC/gen/FreeBSD/getlogin.c"
    "cat:$TEXT_CMDS/cat/cat.c:cat_socket_stub.c"
    "wc:$TEXT_CMDS/wc/wc.c:"
    "head:$TEXT_CMDS/head/head.c:"
    "tail:$TEXT_CMDS/tail/tail.c:$TEXT_CMDS/tail/forward.c|$TEXT_CMDS/tail/reverse.c|$TEXT_CMDS/tail/misc.c|$TEXT_CMDS/tail/read.c|$LIBC/stdio/FreeBSD/_flock_stub.c|$LIBC/stdio/FreeBSD/freopen.c|$LIBC/stdlib/FreeBSD/strtoll.c|$LIBC/gen/FreeBSD/usleep.c|simple_cmds_nanosleep_compat.c|simple_cmds_mmap_compat.c"
    "mkdir:$FILE_CMDS/mkdir/mkdir.c:$XNU/libsyscall/wrappers/unix03/chmod.c|$LIBC/util/mkpath_np.c|$LIBC/gen/FreeBSD/setmode.c|simple_cmds_sigprocmask_compat.c"
    # DAR-142 (2026-09-15): touch -h's lutimes() symlink case is now
    # real, unmodified Libc/emulated/lutimes.c (was
    # simple_cmds_lutimes_compat.c, an honest ENOSYS stub for exactly
    # this one case). setattrlist(2) itself needs no new provider here:
    # COMMON_EXTRA (see this script's own definition above) already
    # compiles cp_mv_syscalls.c's real setattrlist(2) into every simple
    # command's link, touch included - confirmed the hard way via a
    # real duplicate-symbol link error from a first-pass, narrower
    # per-command extraction that turned out redundant.
    "touch:$FILE_CMDS/touch/touch.c:$XNU/libsyscall/wrappers/unix03/chmod.c|simple_cmds_time_compat.c|$LIBC/sys/gettimeofday.c|ps_commpage_gettimeofday_stub.c|$LIBC/stdtime/FreeBSD/localtime.c|$LIBC/stdtime/timezone_unix03.c|$LIBC/stdtime/FreeBSD/asctime.c|$LIBC/stdtime/FreeBSD/strftime.c|$LIBC/stdtime/FreeBSD/timelocal.c|$LIBC/emulated/lutimes.c"
    "rm:$FILE_CMDS/rm/rm.c:$LIBC/gen/get_compat.c|ps_passwd_compat.c|../libinfo_custom/libinfo_custom.c|simple_cmds_rpmatch_compat.c|simple_cmds_undelete_stub.c|$LIBC/gen/strtofflags.c"
    "which:$SHELL_CMDS/which/which.c:"
    "env:$SHELL_CMDS/env/env.c:$SHELL_CMDS/env/envopts.c|$LIBC/gen/FreeBSD/exec.c|simple_cmds_execve_compat.c"
    "sleep:$SHELL_CMDS/sleep/sleep.c:simple_cmds_nanosleep_pub_compat.c"
    "printenv:$SHELL_CMDS/printenv/printenv.c:"
    "cut:$TEXT_CMDS/cut/cut.c:"
    "uniq:$TEXT_CMDS/uniq/uniq.c:simple_cmds_catopen_compat.c"
    "ln:$FILE_CMDS/ln/ln.c:"
    "id:$SHELL_CMDS/id/id.c:ps_passwd_compat.c|../libinfo_custom/libinfo_custom.c|simple_cmds_getgrouplist_compat.c|$LIBC/gen/FreeBSD/getlogin.c"
    # cp/mv: real, unmodified file_cmds/cp + file_cmds/mv sources. Needed
    # new plumbing (this pass): six new plain raw syscalls generated the
    # normal way (chflags/chown/mknod/mkfifo/futimes/readlink -- see
    # SYS_STUBS_NEEDED above), three hand-written NO_SYSCALL_STUB raw
    # syscalls (lchown/sigaction/setattrlist -- cp_mv_syscalls.c, same
    # carry-flag pattern removefile_syscalls.c/copyfile_syscalls.c
    # already established), real Apple lchmod.c/lchflags.c (both call the
    # new setattrlist directly -- Phase 4.6 item 4's "cheapest item"
    # closed for real, not just for touch -h anymore), real
    # Libc/gen/FreeBSD/signal.c (cp's SIGINFO progress handler -- see
    # cp_mv_syscalls.c's sigaction header comment for why this target
    # honestly can't deliver it, but still links/runs the real syscall),
    # and real Libc/gen/get_compat.c (both cp.c/utils.c/mv.c compile
    # __APPLE__'s real get_compat.h path, not the non-Apple COMPAT_MODE=1
    # macro fallback -- confirmed by reading the #ifdef, not assumed).
    "cp:$FILE_CMDS/cp/cp.c:$FILE_CMDS/cp/utils.c|$LIBC/gen/get_compat.c|$LIBC/emulated/lchmod.c|$LIBC/emulated/lchflags.c|$LIBC/gen/FreeBSD/signal.c|simple_cmds_rpmatch_compat.c|simple_cmds_lutimes_compat.c|$XNU/libsyscall/wrappers/clonefile.c|$XNU/libsyscall/wrappers/unix03/chmod.c"
    "mv:$FILE_CMDS/mv/mv.c:$LIBC/gen/get_compat.c|simple_cmds_rpmatch_compat.c|$LIBC/gen/FreeBSD/exec.c|simple_cmds_execve_compat.c|mv_pwcache_compat.c|../libinfo_custom/libinfo_custom.c|$XNU/libsyscall/wrappers/unix03/chmod.c"
    # Block 2 (docs/roadmap.md Phase 4 step 3 "remaining commands" sweep):
    # nine pure-stdio text_cmds (zero new syscalls -- just real getopt/
    # err/warn, all already linked) plus four file_cmds reusing the raw
    # syscalls cp/mv's block just added (chflags/mkfifo/mknod) or
    # libremovefile.a's real rmdir. Deliberately excludes text_cmds' `nl`
    # and shell_cmds' `xargs` from this same survey pass -- both real-
    # source-confirmed to call regcomp/regexec (grepped, not guessed),
    # the Phase 4.6 item 3 POSIX-regex wall this sweep is scoped to stop
    # at, not attempt piecemeal.
    "rev:$TEXT_CMDS/rev/rev.c:"
    "fold:$TEXT_CMDS/fold/fold.c:$LIBC/stdlib/FreeBSD/atoi.c|$LIBC/stdio/FreeBSD/freopen.c|$LIBC/stdio/FreeBSD/_flock_stub.c"
    "paste:$TEXT_CMDS/paste/paste.c:"
    "expand:$TEXT_CMDS/expand/expand.c:$LIBC/stdio/FreeBSD/freopen.c|$LIBC/stdio/FreeBSD/_flock_stub.c"
    "unexpand:$TEXT_CMDS/unexpand/unexpand.c:$LIBC/stdio/FreeBSD/freopen.c|$LIBC/stdio/FreeBSD/_flock_stub.c"
    "comm:$TEXT_CMDS/comm/comm.c:"
    "colrm:$TEXT_CMDS/colrm/colrm.c:"
    "lam:$TEXT_CMDS/lam/lam.c:"
    "rs:$TEXT_CMDS/rs/rs.c:$LIBC/stdlib/FreeBSD/atoi.c"
    "rmdir:$FILE_CMDS/rmdir/rmdir.c:"
    "mkfifo:$FILE_CMDS/mkfifo/mkfifo.c:$LIBC/gen/get_compat.c|$LIBC/gen/FreeBSD/setmode.c|simple_cmds_sigprocmask_compat.c"
    "mknod:$FILE_CMDS/mknod/mknod.c:$LIBC/stdlib/FreeBSD/bsearch.c|$LIBC/stdlib/FreeBSD/strtoul.c"
    "chflags:$FILE_CMDS/chflags/chflags.c:$LIBC/gen/strtofflags.c|$LIBC/emulated/lchflags.c"
    # Block 3: seven shell_cmds (seq/jot/test/kill/tee/mktemp/hexdump) plus
    # four file_cmds (stat/cksum/pathchk/du) -- all real-source-confirmed
    # regex/ACL/mmap/Mach-IPC-free (grepped first, not guessed). mktemp
    # needs real Libc/stdio/FreeBSD/mktemp.c (mkstemp/mkdtemp); cksum
    # needs its own real crc.c/crc32.c/sum1.c/sum2.c/print.c siblings
    # (cksum.c dispatches between CRC/sum1/sum2/CRC32 algorithms by
    # argv[0]/-o, all four real implementations, not just one); hexdump
    # is a real 5-file program (hexdump.c/hexsyntax.c/conv.c/display.c/
    # parse.c -- od shares the same dir/od.1 but is a separate program
    # via odsyntax.c, not built here).
    "seq:$SHELL_CMDS/seq/seq.c:block3_last_resort_compat.c"
    "jot:$SHELL_CMDS/jot/jot.c:$LIBC/stdlib/FreeBSD/random.c|$LIBC/gen/FreeBSD/arc4random.c|$LIBC/stdlib/FreeBSD/atoi.c|$LIBC/sys/gettimeofday.c|ps_commpage_gettimeofday_stub.c|$LIBC/stdtime/FreeBSD/localtime.c|$LIBC/stdtime/timezone_unix03.c|$LIBC/stdtime/FreeBSD/asctime.c|$LIBC/stdtime/FreeBSD/strftime.c|$LIBC/stdtime/FreeBSD/timelocal.c"
    "test:$SHELL_CMDS/test/test.c:"
    "kill:$SHELL_CMDS/kill/kill.c:$LIBC/gen/FreeBSD/siglist.c"
    "tee:$SHELL_CMDS/tee/tee.c:$LIBC/gen/FreeBSD/signal.c"
    "mktemp:$SHELL_CMDS/mktemp/mktemp.c:$LIBC/stdio/FreeBSD/mktemp.c|$LIBC/gen/FreeBSD/arc4random.c|block3_last_resort_compat.c"
    "hexdump:$SHELL_CMDS/hexdump/hexdump.c:$SHELL_CMDS/hexdump/hexsyntax.c|$SHELL_CMDS/hexdump/conv.c|$SHELL_CMDS/hexdump/display.c|$SHELL_CMDS/hexdump/parse.c|$SHELL_CMDS/hexdump/odsyntax.c|$LIBC/stdlib/FreeBSD/atoi.c|$LIBC/stdlib/FreeBSD/strtoll.c|$LIBC/stdlib/FreeBSD/strtoul.c|$LIBC/stdio/FreeBSD/freopen.c|$LIBC/stdio/FreeBSD/_flock_stub.c|hexdump_bcmp_compat.c"
    "stat:$FILE_CMDS/stat/stat.c:$LIBC/gen/strtofflags.c|ps_passwd_compat.c|../libinfo_custom/libinfo_custom.c|simple_cmds_time_compat.c|$LIBC/sys/gettimeofday.c|ps_commpage_gettimeofday_stub.c|$LIBC/stdtime/FreeBSD/localtime.c|$LIBC/stdtime/timezone_unix03.c|$LIBC/stdtime/FreeBSD/asctime.c|$LIBC/stdtime/FreeBSD/strftime.c|$LIBC/stdtime/FreeBSD/timelocal.c"
    "cksum:$FILE_CMDS/cksum/cksum.c:$FILE_CMDS/cksum/crc.c|$FILE_CMDS/cksum/crc32.c|$FILE_CMDS/cksum/sum1.c|$FILE_CMDS/cksum/sum2.c|$FILE_CMDS/cksum/print.c"
    "pathchk:$FILE_CMDS/pathchk/pathchk.c:"
    "du:$FILE_CMDS/du/du.c:block3_last_resort_compat.c|$LIBC/gen/get_compat.c|du_fnmatch_libc_wrapper.c|$LIBC/gen/FreeBSD/getbsize.c|$LIBC/stdlib/FreeBSD/atoi.c"
    # ls: docs/roadmap.md's "Command priority reorganized 2026-08-20" top
    # priority -- real directory listing has never been built in this
    # project despite 65 other commands existing. Real, unmodified
    # ls.c/print.c/util.c/cmp.c, same fts (via libremovefile.a, already
    # proven by du/grep above) and real xattr getters (via libcopyfile.a,
    # already proven by cp/sed) this tier already has wired. New real
    # pieces: acl.c/acl_entry.c/acl_perm.c/acl_flag.c (real, unmodified
    # Apple posix1e ACL-entry-manipulation source, zero filesec_t
    # dependency -- needed at link time for print.c's printacl()).
    # DAR-148 (2026-09-15): acl_get_link_np()/acl_get_file() are now REAL,
    # not a stub -- acl_file.c/acl_translate.c/filesec.c/statx_np.c, all
    # real unmodified Apple source, plus ls_acl_syscalls.c (the three raw
    # stat64_extended/lstat64_extended/fstat64_extended syscalls,
    # NO_SYSCALL_STUB, same class of gap as cp_mv_syscalls.c's lchown/
    # setattrlist) and a small reconstructed <sys/filesec.h>
    # (ls_acl_compat_headers/sys/filesec.h) -- Apple dropped that ONE
    # small header from the public SDK, but the opaque filesec_t object
    # it declares is defined entirely privately inside filesec.c itself
    # (not reverse-engineered), and the real kernel-ABI struct
    # (kauth_filesec) it's built on is NOT missing -- Xcode 12's SDK ships
    # a full, real <sys/kauth.h> directly. This target's HFS+/msdosfs
    # mounts still have no kernel-side ACL *storage* wired, so
    # acl_get_link_np() correctly reports "no ACL" for every real file
    # today regardless -- same observable behavior as the old stub, but
    # via the real implementation stack rather than an honest placeholder.
    # ls_mbr_compat.c (honest always-fails mbr_identifier_translate --
    # this project has no opendirectoryd, same class of gap as nohup's
    # vprocmgr stub; ls.c's own real uuid_to_name() already has a correct
    # upstream fallback to plain uuid_unparse_upper() on failure);
    # bash_readline_termcap_compat.c (real, already-proven honest
    # "no termcap database" stub from the bash tier, reused verbatim --
    # ls -G's tgetent()==1 color-setup branch is simply skipped, same as
    # bash's own real readline fallback); strmode.c (real FreeBSD source,
    # already vendored); humanize_number.c (real, unmodified FreeBSD
    # libutil source, curl-fetched above -- genuinely missing from this
    # checkout, not a libc gap); getbsize.c (real, already proven by du);
    # uuid/uuidsrc/unparse.c (real, already-vendored uuid_unparse_upper).
    "ls:$FILE_CMDS/ls/ls.c:$FILE_CMDS/ls/print.c|$FILE_CMDS/ls/util.c|$FILE_CMDS/ls/cmp.c|$LIBC/posix1e/acl.c|$LIBC/posix1e/acl_entry.c|$LIBC/posix1e/acl_perm.c|$LIBC/posix1e/acl_flag.c|$LIBC/posix1e/acl_file.c|$LIBC/posix1e/acl_translate.c|$LIBC/gen/filesec.c|$LIBC/sys/statx_np.c|ls_acl_syscalls.c|ls_acl_pwgr_compat.c|ls_mbr_compat.c|bash_readline_termcap_compat.c|$LIBC/string/FreeBSD/strmode.c|$OUT/freebsd_extra/humanize_number.c|$LIBC/gen/FreeBSD/getbsize.c|$LIBC/uuid/uuidsrc/unparse.c|$LIBC/gen/get_compat.c|$LIBC/stdlib/FreeBSD/atoi.c|$LIBC/gen/strtofflags.c|mv_pwcache_compat.c|../libinfo_custom/libinfo_custom.c|$LIBC/gen/FreeBSD/time.c|ls_fenv_compat.c|$LIBC/stdtime/FreeBSD/localtime.c|$LIBC/stdtime/timezone_unix03.c|$LIBC/stdtime/FreeBSD/asctime.c|$LIBC/stdtime/FreeBSD/strftime.c|$LIBC/stdtime/FreeBSD/timelocal.c"
    # Block 4: chmod/chown, deliberately re-examined per the roadmap's own
    # note that "chmod needs real ACLs" was never actually verified for
    # the common mode-bit-only case -- turned out FALSE: real Apple
    # posix1e/acl.c (acl_get_file/acl_set_file/etc) was already compiled
    # into libcopyfile.a back in the cp/mv pass (copyfile's own real
    # ACL-copy path needed it) and confirmed present via nm before
    # attempting this, not assumed.
    "chmod:$FILE_CMDS/chmod/chmod.c:$FILE_CMDS/chmod/chmod_acl.c|$LIBC/gen/FreeBSD/setmode.c|$LIBC/gen/get_compat.c|$XNU/libsyscall/wrappers/unix03/chmod.c|$LIBC/emulated/lchmod.c|simple_cmds_sigprocmask_compat.c|chmod_mbr_compat.c"
    "chown:$FILE_CMDS/chown/chown.c:$LIBC/gen/get_compat.c|ps_passwd_compat.c|../libinfo_custom/libinfo_custom.c|simple_cmds_getgrouplist_compat.c|$LIBC/stdlib/FreeBSD/strtoul.c"
    # Block 5: nice/renice/nohup. df (needs real sysctl(2), a variable-
    # length-MIB syscall this project has never built) and install (needs
    # posix_spawn -- a whole unbuilt subsystem, its own real strip(1)
    # subprocess helper) were surveyed and deliberately deferred, real
    # complexity confirmed by reading xinstall.c/df.c directly, not
    # guessed -- both more involved than fits this pass.
    "nice:$SHELL_CMDS/nice/nice.c:$LIBC/gen/FreeBSD/exec.c|simple_cmds_execve_compat.c|$LIBC/stdlib/FreeBSD/atoi.c"
    "renice:$SHELL_CMDS/renice/renice.c:ps_passwd_compat.c|../libinfo_custom/libinfo_custom.c"
    "nohup:$SHELL_CMDS/nohup/nohup.c:$LIBC/gen/FreeBSD/exec.c|simple_cmds_execve_compat.c|$LIBC/gen/FreeBSD/signal.c|nohup_vproc_compat.c"
    # Block 6: chroot/apply (shell_cmds) and vis/unvis (text_cmds -- real
    # commands wrapping the real Libc/gen/FreeBSD/vis.c+unvis.c encode/
    # decode logic). sort hits TWO of this sweep's documented ceilings
    # at once (real source confirmed to call both pthread_create -- no
    # real pthread multithreading exists project-wide, bsd_pthread_stub.c
    # -- and regcomp) and md5 needs a whole unbuilt CommonCrypto-shaped
    # digest API (commoncrypto.h) rather than reusing this project's
    # from-scratch SHA-2 (a different, narrower, non-CommonCrypto-shaped
    # API) -- both surveyed and deliberately skipped, not overlooked.
    "chroot:$SHELL_CMDS/chroot/chroot.c:ps_passwd_compat.c|../libinfo_custom/libinfo_custom.c|$LIBC/gen/FreeBSD/exec.c|simple_cmds_execve_compat.c"
    "apply:$SHELL_CMDS/apply/apply.c:$LIBC/gen/FreeBSD/exec.c|simple_cmds_execve_compat.c|$LIBC/gen/FreeBSD/signal.c|$LIBC/compat-43/sigcompat.c|apply_sigsuspend_compat.c"
    "vis:$TEXT_CMDS/vis/vis.c:$TEXT_CMDS/vis/foldit.c|$LIBC/gen/FreeBSD/vis.c|$LIBC/stdlib/FreeBSD/atoi.c"
    "unvis:$TEXT_CMDS/unvis/unvis.c:$LIBC/gen/FreeBSD/unvis.c"
    # Block 7 (docs/roadmap.md Phase 4.6 "grep/sed/nl/xargs/ed" sweep,
    # now that item 3's real POSIX regex is linkable): grep, the first
    # and simplest of the five. Real, unmodified grep.c/file.c/queue.c/
    # util.c, built with four real upstream feature knobs disabled (see
    # this loop's own cmd_cflags case for the full reasoning) plus real
    # fnmatch() (du_fnmatch_libc_wrapper.c's real __LIBC__-scoped
    # Libc/gen/FreeBSD/fnmatch.c wrapper, reused verbatim -- not
    # du-specific despite the filename) and a real (not fabricated)
    # gzdopen()/gzread() for grep's -Z/.gz support (gz_gzip_impl.c +
    # gz_crc32_compat.c, genuine DEFLATE decompression via the real zlib
    # core vendored at third_party/u-boot/lib/zlib -- see zlib_glue
    # generation above and both files' own header comments).
    "grep:$TEXT_CMDS/grep/grep.c:$TEXT_CMDS/grep/file.c|$TEXT_CMDS/grep/queue.c|$TEXT_CMDS/grep/util.c|gz_gzip_impl.c|gz_crc32_compat.c|du_fnmatch_libc_wrapper.c|simple_cmds_mmap_compat.c|$LIBC/stdlib/FreeBSD/getopt_long.c|$LIBC/stdlib/FreeBSD/strtoll.c|$LIBC/stdlib/FreeBSD/strtoull.c"
    # sed: real, unmodified main.c/compile.c/process.c/misc.c, needing
    # only libc_regex.a (Phase 4.6 item 3) on top of what's already in
    # LINK_COMMON -- unlike grep, sed's own real source has no
    # fastmatch/lzma/bzip2/NLS optional subsystems at all (grepped
    # first, not guessed: plain <regex.h>, no WITHOUT_* knobs anywhere
    # in this program), and every other symbol it calls (getline(),
    # mbrtowc()/iswprint()/wcwidth(), ioctl(TIOCGWINSZ), getprogname())
    # was already confirmed present via nm in libc_stdio.a/libc_locale.a/
    # libc_gen.a before attempting this -- a normal new-command port,
    # nothing new to build.
    "sed:$TEXT_CMDS/sed/main.c:$TEXT_CMDS/sed/compile.c|$TEXT_CMDS/sed/process.c|$TEXT_CMDS/sed/misc.c|$LIBC/stdlib/FreeBSD/atoi.c"
    # nl: real, unmodified single-file nl.c -- like sed, only needs
    # libc_regex.a on top of LINK_COMMON (plain regcomp/regexec/regfree
    # for its per-section numbering-trigger patterns); no rename/unlink/
    # fchmod-class extras (grepped first, not guessed: no file-rewrite
    # path in this program at all, it only ever reads stdin/its input
    # file and writes stdout).
    "nl:$TEXT_CMDS/nl/nl.c:$LIBC/stdio/FreeBSD/freopen.c|$LIBC/stdio/FreeBSD/_flock_stub.c|$LIBC/stdlib/FreeBSD/strtoul.c"
    # xargs: real, unmodified shell_cmds/xargs.c + strnsubst.c. First
    # simple_cmds.sh binary to spawn real child processes (real vfork()/
    # waitpid(), already built for mv's cross-filesystem-rename fallback
    # in cp_mv_syscalls.c -- see that file's own header for the real
    # arm64 fork-ABI writeup -- and reused verbatim here, not
    # reimplemented) plus real execvp() (Libc/gen/FreeBSD/exec.c +
    # simple_cmds_execve_compat.c, the same pairing env/mv/nice/nohup/
    # apply/chroot already established). libc_regex.a (Phase 4.6 item 3)
    # is needed only for -p's real y/n confirmation prompt
    # (regcomp(nl_langinfo(YESEXPR))), not xargs' main argument-batching
    # logic, which has no regex in it at all.
    "xargs:$SHELL_CMDS/xargs/xargs.c:$SHELL_CMDS/xargs/strnsubst.c|$LIBC/gen/FreeBSD/exec.c|simple_cmds_execve_compat.c|$LIBC/stdlib/FreeBSD/strtoul.c|$LIBC/stdlib/FreeBSD/strtonum.c|$LIBC/stdlib/FreeBSD/strtoll.c|$LIBC/gen/get_compat.c|xargs_getrlimit_compat.c|$OUT/freebsd_extra/strstr.c"
    # ed: real, unmodified main.c/buf.c/cbc.c/glbl.c/io.c/re.c/sub.c/
    # undo.c. Built WITHOUT -DDES (real, upstream-supported build
    # variant -- cbc.c's own #ifdef DES guards every actual DES/
    # openssl-des.h reference, see this loop's own cmd_cflags case;
    # real crypto is an already-known permanent-ish ceiling this sweep
    # was told not to attempt). Three new real pieces this command
    # needed and nothing before it did: setjmp(3)/longjmp(3)/
    # sigsetjmp(3)/siglongjmp(3) (ed_setjmp_raw.s + ed_setjmp_compat.c,
    # see their own header comments for why Apple's real
    # libplatform arm64 setjmp.s can't be used complete/unmodified
    # here -- a genuinely missing __sigunaltstack real dependency, not
    # just a missing header); real popen()/pclose()/system()
    # (ed_popen_system_compat.c, same "Apple's real source needs
    # posix_spawn, an already-deferred whole subsystem" gap as
    # `install`/`df`, worked around with the classic pre-posix_spawn
    # fork+execve algorithm built on primitives this project already
    # has); and a real pipe(2) (this loop's own ed_pipe_trap.o
    # generation step above, real Apple asm). mkstemp (buf.c's real
    # temp-file-backed edit buffer) reuses mktemp's real
    # Libc/stdio/FreeBSD/mktemp.c.
    "ed:$TEXT_CMDS/ed/main.c:$TEXT_CMDS/ed/buf.c|$TEXT_CMDS/ed/cbc.c|$TEXT_CMDS/ed/glbl.c|$TEXT_CMDS/ed/io.c|$TEXT_CMDS/ed/re.c|$TEXT_CMDS/ed/sub.c|$TEXT_CMDS/ed/undo.c|ed_setjmp_compat.c|ed_popen_system_compat.c|$LIBC/stdio/FreeBSD/mktemp.c|$LIBC/gen/FreeBSD/arc4random.c|$LIBC/gen/get_compat.c|$LIBC/gen/FreeBSD/signal.c"
    # find: docs/roadmap.md Tier A #2. Real, unmodified find.c/function.c/
    # main.c/misc.c/operator.c/option.c PLUS find's own ls.c (not
    # file_cmds/ls/ls.c -- a separate, much smaller real source file in
    # the same directory implementing printlong() for the -ls predicate,
    # genuinely needed at link time, found while compiling, not
    # originally scoped but real/unmodified/required all the same).
    # getdate.y turned out unneeded: grepped, zero getdate() call sites
    # anywhere in find's real source (the -newer/-mtime/-atime/-ctime
    # family all do plain integer day/second arithmetic against
    # stat-returned timestamps, no date-string parser involved).
    # function.c's real __APPLE__ f_acl()/-acl predicate only calls
    # acl_get_link_np/acl_get_entry/acl_free -- resolved for find.macho
    # via libcopyfile.dylib (its own real, separately-built copy of
    # these symbols, unrelated to ls's own now-real DAR-148 chain; see
    # this file's own "find)" extra_libs case) --
    # its sibling ACL_TYPE_NFS4/acl_is_trivial_np code path is real
    # upstream's `#else /* !__APPLE__ */` branch, dead code under this
    # project's -target arm64-apple-ios build (confirmed by reading the
    # #ifdef, not assumed), so it needed no new ACL plumbing at all.
    # Real fork()/execvp()/waitpid() for -exec/-execdir (cp_mv_syscalls.c/
    # exec.c/simple_cmds_execve_compat.c, same real primitives xargs's
    # own -exec-like child-process path already proved).
    "find:$SHELL_CMDS/find/main.c:$SHELL_CMDS/find/find.c|$SHELL_CMDS/find/function.c|$SHELL_CMDS/find/misc.c|$SHELL_CMDS/find/operator.c|$SHELL_CMDS/find/option.c|$SHELL_CMDS/find/ls.c|$OUT/find_gen/getdate.c|$LIBC/gen/get_compat.c|du_fnmatch_libc_wrapper.c|$LIBC/gen/strtofflags.c|mv_pwcache_compat.c|../libinfo_custom/libinfo_custom.c|$LIBC/gen/FreeBSD/time.c|ls_fenv_compat.c|$LIBC/stdtime/FreeBSD/localtime.c|$LIBC/stdtime/timezone_unix03.c|$LIBC/stdtime/FreeBSD/asctime.c|$LIBC/stdtime/FreeBSD/strftime.c|$LIBC/stdtime/FreeBSD/timelocal.c|$LIBC/gen/FreeBSD/exec.c|simple_cmds_execve_compat.c|$LIBC/stdlib/FreeBSD/atoi.c|$LIBC/stdlib/FreeBSD/bsearch.c|$LIBC/stdlib/FreeBSD/strtoq.c|$LIBC/stdlib/FreeBSD/strtoll.c|$LIBC/gen/FreeBSD/setmode.c|$LIBC/emulated/lchflags.c|simple_cmds_sigprocmask_compat.c"
    # date: docs/roadmap.md Tier A #3. Real, unmodified date.c/vary.c.
    # netdate.c deliberately NOT compiled -- its one entry point
    # (netsettime(), the legacy `timed`-protocol UDP broadcast only
    # reached when actually setting the clock without -n) needs a real
    # resolver+socket stack (Phase 6, unstarted) for a protocol dead on
    # every real modern system anyway; date_compat.c's honest
    # always-fails stub replaces just that one symbol (see its own
    # header) -- DAR-149 (2026-09-17) re-triaged this specifically and
    # confirmed it's already the correct permanent answer, not a gap.
    #
    # pututxline() DAR-149 (2026-09-17): no longer stubbed. date now
    # compiles the same real Libc/gen/NetBSD/utmpx.c + gen/utmpx-darwin.c
    # pair build_login.sh already proved out for login.macho (DAR-164),
    # opting date_compat.c's own local stand-in out via
    # -DLOGIN_REAL_UTMPX_PUTUTXLINE=1 (this command's own cmd_cflags
    # case below). asl_client_compat.c/login_asl_extra_compat.c supply
    # the same already-established honest ASL no-op shims login uses
    # (no real bootstrap-namespace registrar to answer a real
    # asl_open() lookup yet -- see either file's own header); the
    # primary utmpx-file write itself (__pututxline()'s real
    # fopen("w+")/fwrite() of _PATH_UTMPX) needs no daemon at all and is
    # now genuinely real. date_utmpx_notify_compat.c supplies the same
    # honest notify_post() no-op login_syscalls.c already established,
    # duplicated (not shared) on purpose -- that file also carries
    # login-only symbols (fork/alarm/getpgid/etc) date has no business
    # linking. tar_pwgr_r_compat.c (already-built, reused verbatim from
    # tar's own DAR-5 case below) supplies getpwuid_r() for
    # utmpx-darwin.c's own (dead-from-date's-call-graph but still
    # link-required) getlastlogx() path, backed by libinfo.dylib's real
    # getpwuid() (this command's own extra_libs case below) -- same
    # "narrow real POSIX _r wrapper over an already-real non-_r
    # resolver" pattern as tar's own case, not new resolver plumbing.
    # Implements a REAL raw settimeofday(2) syscall (cheap,
    # NO_SYSCALL_STUB, same cp_mv_syscalls.c pattern) so -d/-t and an
    # actual privileged clock-set still really work. Real oldsyslog.c
    # (date -u's "date set by ..." audit log line) reuses
    # cat_socket_stub.c's existing honest socket()/connect() ENOSYS
    # stand-ins (same real AF_UNIX-to-syslogd path cat -u's stub already
    # established, syslogd itself not built yet either, Phase 5.5). Real
    # strptime.c (-f format parsing) and localtime.c's own real mktime()
    # (vary.c's real -v offset math) newly needed, both real Apple/
    # FreeBSD source already vendored.
    "date:$SHELL_CMDS/date/date.c:$SHELL_CMDS/date/vary.c|$LIBC/gen/get_compat.c|$LIBC/gen/oldsyslog.c|cat_socket_stub.c|date_compat.c|$LIBC/stdtime/FreeBSD/strptime.c|$LIBC/stdlib/FreeBSD/strtoq.c|$LIBC/stdlib/FreeBSD/strtoll.c|$LIBC/stdlib/FreeBSD/atoi.c|$LIBC/gen/FreeBSD/getlogin.c|$LIBC/gen/FreeBSD/time.c|ls_fenv_compat.c|$LIBC/sys/gettimeofday.c|ps_commpage_gettimeofday_stub.c|simple_cmds_time_compat.c|$LIBC/stdtime/FreeBSD/localtime.c|$LIBC/stdtime/timezone_unix03.c|$LIBC/stdtime/FreeBSD/asctime.c|$LIBC/stdtime/FreeBSD/strftime.c|$LIBC/stdtime/FreeBSD/timelocal.c|$LIBC/gen/NetBSD/utmpx.c|$LIBC/gen/utmpx-darwin.c|asl_client_compat.c|login_asl_extra_compat.c|date_utmpx_notify_compat.c|tar_pwgr_r_compat.c"
    # gzip: docs/roadmap.md Tier A #4. Real, unmodified gzip.c PLUS its
    # own real sibling unity-build includes (unbzip2.c/zuncompress.c/
    # unpack.c/unxz.c, each already individually #include-guarded by
    # gzip.c itself, see NO_BZIP2_SUPPORT/NO_XZ_SUPPORT below).
    # gz_deflate_impl.c is this pass's real new piece: the DEFLATE
    # (compression) half of zlib, genuinely missing before now -- see
    # that file's own header for why it's a separate, from-scratch unity
    # build from gz_gzip_impl.c (grep's own real but INFLATE-only,
    # narrow-gzdopen/gzread-wrapped file) rather than a reuse of it.
    # NO_BZIP2_SUPPORT/NO_XZ_SUPPORT: same real, upstream-supported
    # build-time knobs as grep's WITHOUT_BZIP2/WITHOUT_LZMA (neither
    # bzip2 nor lzma is vendored in this project) -- NO_COMPRESS_SUPPORT
    # and NO_PACK_SUPPORT deliberately left OFF (undefined) since
    # zuncompress.c/unpack.c are both real, self-contained, external-
    # dependency-free BSD source already sitting right next to gzip.c,
    # genuinely free real functionality, not scope creep.
    "gzip:$FILE_CMDS/gzip/gzip.c:$LIBC/gen/get_compat.c|gz_deflate_impl.c|gz_crc32_compat.c|gz_futimens_compat.c|$XNU/libsyscall/wrappers/unix03/chmod.c|$LIBC/stdlib/FreeBSD/getopt_long.c|$LIBC/gen/FreeBSD/signal.c|$LIBC/gen/FreeBSD/time.c|ls_fenv_compat.c|$LIBC/stdtime/FreeBSD/localtime.c|$LIBC/stdtime/timezone_unix03.c|$LIBC/stdtime/FreeBSD/asctime.c|$LIBC/stdtime/FreeBSD/strftime.c|$LIBC/stdtime/FreeBSD/timelocal.c"
    # compress: docs/roadmap.md Tier A #4's other half. Real, unmodified
    # compress.c/zopen.c -- a real, self-contained classic LZW codec,
    # zero zlib/deflate dependency at all (genuinely separate from
    # gzip's own DEFLATE work above). zopen.c's real FILE*-returning
    # zopen() wraps its own zread/zwrite/zclose via real funopen(3)
    # (Libc/stdio/FreeBSD/funopen.c, real BSD custom-stream-hooks API,
    # never used by any prior tier) -- confirmed compatible with this
    # project's own already-built libc_stdio.a before attempting: its
    # real internal __sfp() allocator funopen.c itself calls is already
    # a real, defined, linkable symbol there (checked via nm, not
    # assumed), i.e. this project's stdio was never actually narrowed to
    # a "5-function subset" at the ABI/internals level, only its
    # earliest bring-up milestone was.
    "compress:$FILE_CMDS/compress/compress.c:$FILE_CMDS/compress/zopen.c|$LIBC/stdio/FreeBSD/funopen.c|$XNU/libsyscall/wrappers/unix03/chmod.c"
    # Tier B (docs/roadmap.md "Command priority reorganized 2026-08-20"):
    # diff, the first Tier B command -- real, unmodified source newly
    # pinned via setup_third_party.sh (third_party/gnudiff, tag
    # gnudiff-25.40.1, confirmed against the macos-112 manifest; real
    # apple-oss-distributions project name for GNU diffutils is
    # "gnudiff", not "diffutils" -- see setup_third_party.sh's own
    # header comment). Source list matches gnudiff.xcodeproj's own real
    # "diff" + "diffutils" (its static-lib dependency, libdiffutils.a)
    # targets exactly (confirmed by reading project.pbxproj's own
    # PBXSourcesBuildPhase entries, not guessed): diff's own
    # analyze/context/dir/ed/ifdef/io/normal/side/util/version.c, plus
    # libdiffutils.a's basename/c-stack/cmpbuf/error/exclude/exitfail/
    # freesoft/hard-locale/imaxtostr/offtostr/posixver/prepargs/
    # quotesys/regex/setmode/strftime/umaxtostr/xmalloc/xstrtoumax.c.
    # Two real, deliberate deviations from that exact list:
    #   - lib/getopt.c + lib/getopt1.c (gnulib's own self-contained
    #     getopt_long, providing its own optind/optarg/opterr globals)
    #     are NOT compiled -- they'd be a real duplicate-strong-symbol
    #     conflict against this script's own common_getopt.o
    #     ($LIBC/stdlib/FreeBSD/getopt.c, linked into every command).
    #     diff.c's own `#include <getopt.h>` instead resolves to
    #     gnulib's lib/getopt.h (binary-identical `struct option`
    #     layout to $LIBC/include/getopt.h, confirmed field-by-field),
    #     satisfied at link time by the project's own real
    #     $LIBC/stdlib/FreeBSD/getopt_long.c (already proven by the
    #     grep tier) instead of gnulib's getopt1.c -- same real
    #     function, different real source, zero behavior gap for
    #     diff's own use (permuting getopt_long over argv).
    #   - lib/fnmatch.c is not compiled either (matching Apple's own
    #     libdiffutils.a source list, which omits it too) -- exclude.c's
    #     `#include "fnmatch.h"` (generated from fnmatch.hin, verbatim
    #     copy, see the gnudiff_glue generation above) declares the same
    #     real fnmatch() the project's own du/grep tiers already provide
    #     via du_fnmatch_libc_wrapper.c, reused verbatim here too.
    # c-stack.c's real sigaltstack(2)-based SIGSEGV/stack-overflow
    # handler needs both a real C wrapper ($LIBC/compat-43/sigaltstk.c,
    # calling `__sigaltstack`) and the raw syscall stub itself
    # ($OUT/simple_obj/sys____sigaltstack.o, generated above from
    # Apple's real hand-written libsyscall/custom/__sigaltstack.s --
    # NO_SYSCALL_STUB in syscalls.master, same category as pipe(2),
    # not auto-generated by create-syscalls.pl); the raw .o is added via
    # this loop's own extra_libs case below (like ed's
    # ed_pipe_trap.o/ed_setjmp_raw.o) since it isn't a plain .c source.
    # HAVE_CONFIG_H is deliberately NOT defined for regex.c specifically
    # (it's the one file in this list that only pulls in real
    # <config.h> -- and therefore any of its HAVE_MBRTOWC/HAVE_WCHAR_H/
    # HAVE_BTOWC-gated wide-char regex support -- when that macro is
    # set): every other file here reaches config.h unconditionally via
    # diff.h/system.h's own unguarded `#include <config.h>` regardless,
    # so this only turns off gnulib regex's optional wide-char-locale
    # matching path (an honest, narrow, documented gap -- real
    # mbrtowc()/wctype()/iswctype() linkability against this tier's
    # actual LINK_COMMON was not verified, and ASCII-range -I/--ignore-
    # matching-lines matching, the feature this pass actually verifies,
    # doesn't need it).
    "diff:$GNUDIFF/src/diff.c:$GNUDIFF/src/analyze.c|$GNUDIFF/src/context.c|$GNUDIFF/src/dir.c|$GNUDIFF/src/ed.c|$GNUDIFF/src/ifdef.c|$GNUDIFF/src/io.c|$GNUDIFF/src/normal.c|$GNUDIFF/src/side.c|$GNUDIFF/src/util.c|$GNUDIFF/src/version.c|$GNUDIFF/lib/basename.c|$GNUDIFF/lib/c-stack.c|$GNUDIFF/lib/cmpbuf.c|$GNUDIFF/lib/error.c|$GNUDIFF/lib/exclude.c|$GNUDIFF/lib/exitfail.c|$GNUDIFF/lib/freesoft.c|$GNUDIFF/lib/hard-locale.c|$GNUDIFF/lib/imaxtostr.c|$GNUDIFF/lib/offtostr.c|$GNUDIFF/lib/posixver.c|$GNUDIFF/lib/prepargs.c|$GNUDIFF/lib/quotesys.c|$GNUDIFF/lib/regex.c|$GNUDIFF/lib/setmode.c|$GNUDIFF/lib/strftime.c|$GNUDIFF/lib/umaxtostr.c|$GNUDIFF/lib/xmalloc.c|$GNUDIFF/lib/xstrtoumax.c|$LIBC/stdlib/FreeBSD/getopt_long.c|du_fnmatch_libc_wrapper.c|$LIBC/compat-43/sigaltstk.c|$LIBC/stdlib/FreeBSD/strtoll.c|$LIBC/stdlib/FreeBSD/strtoull.c|$LIBC/gen/FreeBSD/exec.c|simple_cmds_execve_compat.c|ed_setjmp_compat.c|$LIBC/stdlib/FreeBSD/qsort.c|$LIBC/gen/FreeBSD/signal.c|$LIBC/gen/FreeBSD/time.c|ls_fenv_compat.c|$LIBC/stdtime/FreeBSD/localtime.c|$LIBC/stdtime/timezone_unix03.c|$LIBC/stdtime/FreeBSD/asctime.c|$LIBC/stdtime/FreeBSD/timelocal.c|$OUT/freebsd_extra/flsl.c|$LIBC/stdlib/FreeBSD/heapsort.c|$LIBC/sys/gettimeofday.c|ps_commpage_gettimeofday_stub.c|$OUT/gnudiff_glue/libc_real_strftime.c"
    "tar:$LIBARCHIVE_TAR/bsdtar.c:$LIBARCHIVE/archive_acl.c|$LIBARCHIVE/archive_check_magic.c|$LIBARCHIVE/archive_cmdline.c|$LIBARCHIVE/archive_disk_acl_darwin.c|$LIBARCHIVE/archive_entry_copy_stat.c|$LIBARCHIVE/archive_entry_link_resolver.c|$LIBARCHIVE/archive_entry_sparse.c|$LIBARCHIVE/archive_entry_stat.c|$LIBARCHIVE/archive_entry_strmode.c|$LIBARCHIVE/archive_entry_xattr.c|$LIBARCHIVE/archive_entry.c|$LIBARCHIVE/archive_getdate.c|$LIBARCHIVE/archive_match.c|$LIBARCHIVE/archive_options.c|$LIBARCHIVE/archive_pack_dev.c|$LIBARCHIVE/archive_pathmatch.c|$LIBARCHIVE/archive_ppmd7.c|$LIBARCHIVE/archive_random.c|$LIBARCHIVE/archive_rb.c|$LIBARCHIVE/archive_read_add_passphrase.c|$LIBARCHIVE/archive_read_append_filter.c|$LIBARCHIVE/archive_read_data_into_fd.c|$LIBARCHIVE/archive_read_disk_entry_from_file.c|$LIBARCHIVE/archive_read_disk_posix.c|$LIBARCHIVE/archive_read_disk_set_standard_lookup.c|$LIBARCHIVE/archive_read_extract.c|$LIBARCHIVE/archive_read_extract2.c|$LIBARCHIVE/archive_read_open_fd.c|$LIBARCHIVE/archive_read_open_file.c|$LIBARCHIVE/archive_read_open_filename.c|$LIBARCHIVE/archive_read_open_memory.c|$LIBARCHIVE/archive_read_set_format.c|$LIBARCHIVE/archive_read_set_options.c|$LIBARCHIVE/archive_read_support_filter_all.c|$LIBARCHIVE/archive_read_support_filter_bzip2.c|$LIBARCHIVE/archive_read_support_filter_compress.c|$LIBARCHIVE/archive_read_support_filter_grzip.c|$LIBARCHIVE/archive_read_support_filter_gzip.c|$LIBARCHIVE/archive_read_support_filter_lrzip.c|$LIBARCHIVE/archive_read_support_filter_lz4.c|$LIBARCHIVE/archive_read_support_filter_lzop.c|$LIBARCHIVE/archive_read_support_filter_none.c|$LIBARCHIVE/archive_read_support_filter_program.c|$LIBARCHIVE/archive_read_support_filter_rpm.c|$LIBARCHIVE/archive_read_support_filter_uu.c|$LIBARCHIVE/archive_read_support_filter_xz.c|$LIBARCHIVE/archive_read_support_format_all.c|$LIBARCHIVE/archive_read_support_format_ar.c|$LIBARCHIVE/archive_read_support_format_by_code.c|$LIBARCHIVE/archive_read_support_format_cpio.c|$LIBARCHIVE/archive_read_support_format_empty.c|$LIBARCHIVE/archive_read_support_format_iso9660.c|$LIBARCHIVE/archive_read_support_format_lha.c|$LIBARCHIVE/archive_read_support_format_mtree.c|$LIBARCHIVE/archive_read_support_format_rar.c|$LIBARCHIVE/archive_read_support_format_raw.c|$LIBARCHIVE/archive_read_support_format_tar.c|$LIBARCHIVE/archive_read_support_format_warc.c|$LIBARCHIVE/archive_read.c|$LIBARCHIVE/archive_string_sprintf.c|$LIBARCHIVE/archive_string.c|$LIBARCHIVE/archive_util.c|$LIBARCHIVE/archive_version_details.c|$LIBARCHIVE/archive_virtual.c|$LIBARCHIVE/archive_write_add_filter_b64encode.c|$LIBARCHIVE/archive_write_add_filter_by_name.c|$LIBARCHIVE/archive_write_add_filter_bzip2.c|$LIBARCHIVE/archive_write_add_filter_compress.c|$LIBARCHIVE/archive_write_add_filter_grzip.c|$LIBARCHIVE/archive_write_add_filter_gzip.c|$LIBARCHIVE/archive_write_add_filter_lrzip.c|$LIBARCHIVE/archive_write_add_filter_lz4.c|$LIBARCHIVE/archive_write_add_filter_lzop.c|$LIBARCHIVE/archive_write_add_filter_none.c|$LIBARCHIVE/archive_write_add_filter_program.c|$LIBARCHIVE/archive_write_add_filter_uuencode.c|$LIBARCHIVE/archive_write_add_filter_xz.c|$LIBARCHIVE/archive_write_add_filter.c|$LIBARCHIVE/archive_write_disk_posix.c|$LIBARCHIVE/archive_write_disk_set_standard_lookup.c|$LIBARCHIVE/archive_write_open_fd.c|$LIBARCHIVE/archive_write_open_file.c|$LIBARCHIVE/archive_write_open_filename.c|$LIBARCHIVE/archive_write_open_memory.c|$LIBARCHIVE/archive_write_set_format_7zip.c|$LIBARCHIVE/archive_write_set_format_ar.c|$LIBARCHIVE/archive_write_set_format_by_name.c|$LIBARCHIVE/archive_write_set_format_cpio_newc.c|$LIBARCHIVE/archive_write_set_format_cpio.c|$LIBARCHIVE/archive_write_set_format_filter_by_ext.c|$LIBARCHIVE/archive_write_set_format_gnutar.c|$LIBARCHIVE/archive_write_set_format_iso9660.c|$LIBARCHIVE/archive_write_set_format_pax.c|$LIBARCHIVE/archive_write_set_format_raw.c|$LIBARCHIVE/archive_write_set_format_shar.c|$LIBARCHIVE/archive_write_set_format_ustar.c|$LIBARCHIVE/archive_write_set_format_v7tar.c|$LIBARCHIVE/archive_write_set_format_warc.c|$LIBARCHIVE/archive_write_set_format.c|$LIBARCHIVE/archive_write_set_options.c|$LIBARCHIVE/archive_write_set_passphrase.c|$LIBARCHIVE/archive_write.c|$LIBARCHIVE/filter_fork_posix.c|$LIBARCHIVE/xxhash.c|tar_zlib_convenience_compat.c|tar_cab_format_compat.c|tar_pwgr_r_compat.c|gz_deflate_impl.c|gz_crc32_compat.c|$OUT/freebsd_extra/strstr.c|$LIBC/gen/FreeBSD/signal.c|$LIBC/emulated/lchmod.c|$LIBC/emulated/lchflags.c|simple_cmds_lutimes_compat.c|$LIBC/gen/FreeBSD/time.c|ls_fenv_compat.c|$LIBC/stdtime/FreeBSD/localtime.c|$LIBC/stdtime/timezone_unix03.c|$LIBC/stdtime/FreeBSD/asctime.c|$LIBC/stdtime/FreeBSD/timelocal.c|$LIBC/gen/FreeBSD/exec.c|simple_cmds_execve_compat.c|$LIBC/stdlib/FreeBSD/strtoll.c|$LIBC/stdlib/FreeBSD/strtoull.c|$LIBARCHIVE_TAR/cmdline.c|$LIBARCHIVE_TAR/creation_set.c|$LIBARCHIVE_TAR/read.c|$LIBARCHIVE_TAR/subst.c|$LIBARCHIVE_TAR/util.c|$LIBARCHIVE_TAR/write.c|$LIBARCHIVE_FE/err.c|$LIBARCHIVE_FE/line_reader.c|$LIBARCHIVE_FE/passphrase.c|$XNU/libsyscall/wrappers/unix03/chmod.c|$LIBC/stdlib/FreeBSD/atoi.c|$LIBC/stdlib/FreeBSD/bsearch.c|$LIBC/gen/FreeBSD/arc4random.c|$LIBC/stdtime/FreeBSD/strftime.c|tar_pthread_testcancel_compat.c"
    "less:$LESS/main.c:$LESS/brac.c|$LESS/ch.c|$LESS/charset.c|$LESS/cmdbuf.c|$LESS/command.c|$LESS/cvt.c|$LESS/decode.c|$LESS/edit.c|$LESS/filename.c|$LESS/forwback.c|$LESS/help.c|$LESS/ifile.c|$LESS/input.c|$LESS/jump.c|$LESS/line.c|$LESS/linenum.c|$LESS/lsystem.c|$LESS/mark.c|$LESS/optfunc.c|$LESS/option.c|$LESS/opttbl.c|$LESS/os.c|$LESS/output.c|$LESS/pattern.c|$LESS/position.c|$LESS/prompt.c|$LESS/screen.c|$LESS/search.c|$LESS/signal.c|$LESS/tags.c|$LESS/ttyin.c|$LESS/version.c|$OUT/less_glue/libc_real_signal.c|ed_popen_system_compat.c|bash_readline_termcap_compat.c|$OUT/freebsd_extra/strstr.c|$LIBC/gen/FreeBSD/time.c|ls_fenv_compat.c|ed_setjmp_compat.c|$LIBC/stdlib/FreeBSD/atoi.c|$LIBC/stdlib/FreeBSD/abs.c|$LIBC/gen/get_compat.c|less_misc_compat.c|tar_pthread_testcancel_compat.c|$LIBC/stdtime/FreeBSD/localtime.c|$LIBC/stdtime/timezone_unix03.c|$LIBC/stdtime/FreeBSD/asctime.c|$LIBC/stdtime/FreeBSD/timelocal.c|$LIBC/stdtime/FreeBSD/strftime.c"
    # Phase 4.6 item 8 (docs/roadmap.md, re-scoped 2026-08-20): df/killall
    # against the real, already-general sysctl()/statfs() plumbing.
    # df: real, unmodified file_cmds/df/{df,vfslist}.c + real Libc/gen/
    # FreeBSD/getmntinfo.c (getmntinfo() -> getfsstat(), this project's
    # first real caller -- see simple_cmds_statfs64_compat.c, added
    # alongside this pass after finding the existing auto-generated
    # "_statfs" stub was wired to the WRONG, legacy 32-bit-struct kernel
    # syscall). getbsize.c/humanize_number.c already proven by du/ls.
    # fabs()/trunc()/ilogb() -- df.c's own unit_adjust() (-h/-H
    # human-readable sizes) and prtstat()'s Unix2003 round-up-percentage
    # rule call all three. UPDATED 2026-08-27: these now come from real
    # libc_build/libm.a (real Apple Libm, tag Libm-2026 -- see
    # setup_third_party.sh's own header comment and build_libm.sh)
    # instead of simple_cmds_libm_compat.c's narrow hand-rolled bit-level
    # implementation -- linked via this command's own extra_libs entry
    # below (df), not compiled into this file list.
    # simple_cmds_libm_compat.c itself is left in the tree (confirmed via
    # grep nothing else references it) but is no longer compiled here.
    "df:$FILE_CMDS/df/df.c:$FILE_CMDS/df/vfslist.c|$LIBC/gen/FreeBSD/getmntinfo.c|$LIBC/gen/FreeBSD/getbsize.c|$OUT/freebsd_extra/humanize_number.c|$LIBC/gen/get_compat.c"
    # killall: real, unmodified shell_cmds/killall/killall.c. Real
    # KERN_PROC/KERN_PROC_ALL process enumeration via the same sysctl()
    # already proven by hostname(1) (libc_gen.a, in LINK_COMMON); struct
    # kinfo_proc comes from the real kernel's own bsd/sys/sysctl.h
    # (transitively included by killall.c's own <sys/user.h>, already on
    # this tier's -I $XNU/bsd path -- no new -I needed, same as ps.c's
    # own real KERN_PROC use in build_ps.sh). Real getpwnam/getpwuid via
    # ps_passwd_compat.c + libinfo_custom.c (same Phase 5 flat-file
    # resolver rm/id/stat/chown/ps already use). Real kill(2) already
    # linked (libc_gen.a's sys_kill.o, ___kill, proven by kill(1)).
    # libc_regex.a: real regcomp/regexec/regfree (killall -m's real
    # regex-match mode), same archive tar/less/diff already proved.
    "killall:$SHELL_CMDS/killall/killall.c:$LIBC/gen/FreeBSD/siglist.c|ps_passwd_compat.c|../libinfo_custom/libinfo_custom.c"
    # md5 (Phase 4.6 item 6, closing the gap Block 6's own comment above
    # flagged and deferred): real, unmodified text_cmds/md5/md5.c, linked
    # against md5_commoncrypto_shim.c's real-CommonCrypto-header-shaped
    # digest API (see that file's own header, and this script's
    # commoncrypto_glue generation above) instead of real commoncrypto.c
    # (needs libdispatch, out of scope project-wide) or this project's
    # existing from-scratch SHA-2 (a different, non-CommonCrypto-shaped
    # API, see the deferred-Block-6 comment above). md5_rfc1321_core.c is
    # the actual MD5 math -- a userland port of xnu-7195's own already-
    # proven libkern/crypto/corecrypto_md5.c (see that file's own header
    # for why real Apple/FreeBSD sources aren't usable here).
    "md5:$TEXT_CMDS/md5/md5.c:md5_commoncrypto_shim.c|md5_rfc1321_core.c"
    # base64: Apple's real bintrans (text_cmds-165.0.4 -- see
    # setup_third_party.sh's text_cmds_bintrans note for why that is a
    # second, post-macos-112 pin). One multi-call binary; getprogname()
    # "base64" selects Apple's classic base64 (apple_base64.c, the macOS
    # `base64 [-Ddh] [-b num] [-i in] [-o out]` interface under
    # __APPLE__). main() also references the uuencode/uudecode/qp coders,
    # so those real files link too. They call b64_ntop()/b64_pton(), which
    # <resolv.h> maps to libresolv's res_9_b64_*: compiled here from
    # Apple's real libresolv base64.c, since libresolv.dylib is not on the
    # simple commands' link line. freopen.c/_flock_stub.c, setmode.c and
    # the libinfo getpwnam() (uudecode's ~user output paths) are the same
    # real sources fold/mkdir/id already link for the same symbols.
    "base64:$TEXT_CMDS_BINTRANS/bintrans/bintrans.c:$TEXT_CMDS_BINTRANS/bintrans/apple_base64.c|$TEXT_CMDS_BINTRANS/bintrans/uuencode.c|$TEXT_CMDS_BINTRANS/bintrans/uudecode.c|$TEXT_CMDS_BINTRANS/bintrans/qp.c|../../third_party/libresolv/base64.c|$LIBC/stdlib/FreeBSD/getopt_long.c|$LIBC/stdio/FreeBSD/freopen.c|$LIBC/stdio/FreeBSD/_flock_stub.c|$LIBC/gen/FreeBSD/setmode.c|ps_passwd_compat.c|../libinfo_custom/libinfo_custom.c"
)

built=0
failed=0
for spec in "${CMD_LIST[@]}"; do
    name="${spec%%:*}"
    rest="${spec#*:}"
    src="${rest%%:*}"
    extra_raw="${rest#*:}"
    extra="${extra_raw//|/ }"

    # Clear any previous command's $OUT/cmd_pre_include override before
    # this command's own cmd_cflags case (below) decides whether to
    # repopulate it -- see that directory's own header comment above
    # CFLAGS's definition. Without this, a header a prior command
    # dropped in here would silently keep shadowing the real one for
    # every later command too.
    rm -f "$OUT/cmd_pre_include"/*

    # ONLY_CMDS="a b": build just those (quick iteration; unset = all).
    if [ -n "${ONLY_CMDS:-}" ] && [[ " $ONLY_CMDS " != *" $name "* ]]; then
        continue
    fi

    real_main="${name}_cmd_real_main"
    obj="$OUT/simple_obj/${name}.o"
    wrapper_obj="$OUT/simple_obj/${name}_wrapper.o"

    if [ ! -f "$src" ]; then
        echo "error: $src not found" >&2
        failed=$((failed + 1))
        continue
    fi

    # Per-command extra CFLAGS (rare -- one command so far). grep's real,
    # unmodified grep.c/file.c/queue.c/util.c need four real upstream
    # build-time feature knobs disabled (WITHOUT_FASTMATCH -- this
    # checkout has no fastmatch.h at all, a real Apple-only literal/
    # substring fast-path optimization layered on top of the same real
    # regex engine, not a functional loss; WITHOUT_LZMA/WITHOUT_BZIP2 --
    # no real lzma/bzip2 codec vendored in this project, both cleanly
    # supported real upstream build variants, confirmed via grep.c's own
    # explicit "support was disabled at compile-time" runtime error path
    # for -M/-J, not silently dropped; WITHOUT_NLS -- no real
    # catgets(3)/message-catalog subsystem exists here, see
    # simple_cmds_catopen_compat.c's own header for the identical gap
    # uniq hit first) plus the three real u-boot-zlib-glue macros the
    # zlib_glue generation step above documents (STDC/NO_DUMMY_DECL/
    # __LITTLE_ENDIAN, needed by gz_gzip_impl.c/gz_crc32_compat.c, see
    # its own comment) and -I "$OUT/zlib_glue" so gz_gzip_impl.c's
    # `#include "zutil.h"` resolves. Deliberately NOT added to the
    # global $CFLAGS -- STDC in particular is a common enough macro name
    # in other vendored FreeBSD sources that toggling it project-wide
    # risked unrelated behavior changes in already-verified commands,
    # same reasoning as du_fnmatch_libc_wrapper.c's own __LIBC__ scoping
    # comment.
    cmd_cflags=""
    case "$name" in
        grep)
            cmd_cflags="-DWITHOUT_FASTMATCH -DWITHOUT_LZMA -DWITHOUT_BZIP2 -DWITHOUT_NLS -DSTDC -DNO_DUMMY_DECL -D__LITTLE_ENDIAN=1 -I $OUT/zlib_glue"
            ;;
        gzip)
            # Same real STDC/NO_DUMMY_DECL/__LITTLE_ENDIAN u-boot-zlib-glue
            # macros grep needed (gz_deflate_impl.c's own real deflate.c/
            # trees.c/inflate.c etc, same zutil.h dependency), plus
            # NO_BZIP2_SUPPORT/NO_XZ_SUPPORT (see this command's own
            # CMD_LIST comment).
            cmd_cflags="-DNO_BZIP2_SUPPORT -DNO_XZ_SUPPORT -DSTDC -DNO_DUMMY_DECL -D__LITTLE_ENDIAN=1 -DGZIP_APPLE_VERSION=\"1.0\" -DCONFIG_GZIP_COMPRESSED -I $OUT/zlib_glue"
            ;;
        ed)
            # ed.c's real `!cmd` shell-escape calls system(3), marked
            # __IOS_PROHIBITED (a real App-Store-review-time policy
            # annotation, not a functional restriction on this non-App-
            # Store target) by this project's vendored stdlib.h under
            # the uniform -target arm64-apple-ios14.4 this whole build
            # uses -- see ed_availability_override.h's own header
            # comment for the full story (same class of gap, same class
            # of fix, as Phase 4.6 item 3's regex_availability_override.h).
            cmd_cflags="-include ed_availability_override.h"
            ;;
        ls)
            # ls.c/print.c real `#include <termcap.h>`: reuse the real
            # GNU termcap.h declaring header bash's own tier already
            # vendors (third_party/bash/bash-3.2/lib/termcap/termcap.h --
            # matches bash_readline_termcap_compat.c's real function
            # definitions this command's own CMD_LIST extra list links
            # in). print.c's real `#include <membershipPriv.h>` (for
            # mbr_identifier_translate's declaration, ls_mbr_compat.c
            # supplies the honest stub body) needs real Apple
            # membership.h/membershipPriv.h/ntsid.h, vendored at
            # third_party/Libinfo/membership.subproj/, not previously on
            # any tier's include path. -DHAVE_STDINT_H=1: real
            # Libc/uuid/uuidsrc/uuidP.h (unparse.c's own private header)
            # falls back to a real, nonexistent-in-this-checkout
            # <uuid/uuid_types.h> unless this real, already-established
            # (build_copyfile.sh's POSIX1E_CFLAGS uses the same flag for
            # the same header) upstream build-time knob is set, which
            # makes it use plain <stdint.h> instead -- a real supported
            # upstream code path, not a workaround.
            cmd_cflags="-I ../../third_party/bash/bash-3.2/lib/termcap -I ../../third_party/Libinfo/membership.subproj -DHAVE_STDINT_H=1 -I ls_acl_compat_headers"
            ;;
        diff)
            # See the gnudiff_glue generation comment above (near
            # vproc_priv.h) for why `<regex.h>` must resolve to gnulib's
            # OWN regex engine here (struct re_pattern_buffer,
            # re_compile_pattern()), not this project's shared TRE/POSIX
            # one at $LIBC/include/regex.h -- and why `<getopt.h>` must
            # resolve to gnulib's own copy too (binary-identical struct
            # option layout, but declares the real getopt_long() this
            # command actually links against, from
            # $LIBC/stdlib/FreeBSD/getopt_long.c, not gnulib's own
            # getopt1.c -- see this command's own CMD_LIST comment for
            # why getopt.c/getopt1.c aren't compiled). Populating
            # $OUT/cmd_pre_include (see that directory's own header
            # comment above CFLAGS) makes both win over $CFLAGS's own
            # -I $LIBC/include, which comes later on the command line,
            # without disturbing any other command's own cmd_cflags
            # (only this directory, cleared before every command's own
            # turn, is ever allowed to shadow a $CFLAGS header). The
            # rest of gnulib's real headers (c-stack.h, xalloc.h,
            # cmpbuf.h, etc, plus lib/posix/regex.h our copied regex.h
            # itself `#include <posix/regex.h>`s, plus $GNUDIFF's own
            # real config.h for system.h's unconditional `#include
            # <config.h>`) have no same-named collision anywhere on
            # $CFLAGS's existing search path (confirmed by grep), so a
            # plain additional -I is enough for those, no shadowing
            # needed.
            cp "$OUT/gnudiff_glue/regex.h" "$OUT/gnudiff_glue/fnmatch.h" "$GNUDIFF/lib/getopt.h" "$OUT/cmd_pre_include/"
            # lib/dirname.h (basename.c's own header) declares
            # `size_t`-returning prototypes without including anything
            # that defines `size_t` itself -- real on real macOS too
            # (confirmed by reading the file: no #include at all beyond
            # its own include guard), relying on whatever earlier
            # translation-unit context already brought `size_t` in
            # (Xcode's real build environment apparently always has one;
            # this project's minimal -ffreestanding one doesn't). A
            # forced `-include sys/types.h` (same real, unmodified
            # project header every other command already gets `size_t`
            # from) supplies exactly that, matching this script's own
            # existing `-include libc_build_prototypes.h`/`-include
            # ed_availability_override.h` precedent for narrow
            # environment gaps -- not a source modification.
            # LONG_WIDTH=64: real flsl.c (freebsd_extra, see its own
            # generation comment above) is current FreeBSD source using
            # a real C23/<limits.h> macro this checkout's older vendored
            # limits.h doesn't define -- 64 is simply correct for this
            # target's LP64 `long` (arm64-apple-ios, confirmed via
            # sizeof(long)==8, not guessed), not a fabricated stand-in.
            cmd_cflags="-I $GNUDIFF/lib -I $GNUDIFF/lib/posix -I $GNUDIFF -include sys/types.h -include string.h -include $OUT/gnudiff_glue/diff_cstack_decls.h -DHAVE_CONFIG_H -DLONG_WIDTH=64"
            ;;
        tar)
            # libarchive_fe/err.h declares lafe_errc()/lafe_warnc()/
            # lafe_setprogname() -- real, unmodified tar/{bsdtar,cmdline,
            # creation_set,read,subst,util,write}.c all `#include
            # "err.h"` expecting THIS header, but $CFLAGS's own -I
            # $LIBC/include has its own real, unrelated BSD err.h
            # (err()/warn() family, zero lafe_* declarations) that wins
            # via plain quote-include search order otherwise -- same
            # real shadowing problem diff's own regex.h/getopt.h had,
            # fixed the same way (populate $OUT/cmd_pre_include, see its
            # own header comment above CFLAGS).
            cp "$LIBARCHIVE_FE/err.h" "$OUT/cmd_pre_include/"
            # -I $OUT/tar_glue: this command's own corrected config.h
            # (see its generation comment above, near vproc_priv.h) plus
            # the deflateInit/compressBound/uncompress and dd_fd-style
            # dirent-field-rename compat headers, both force-included
            # below. -DPRIVATE=1: xnu-7195/bsd/sys/dirent.h's own real
            # `getdirentries64_flags_t`/`GETDIRENTRIES64_EXTENDED_
            # BUFSIZE` (opendir.c's real 64-bit-inode buffer-tail-flags
            # support) are wrapped in a real `#if PRIVATE` this project's
            # userland builds don't normally set -- same real, already-
            # established knob build_copyfile.sh/build_libmalloc.sh use
            # for the identical reason, not new. -include .../_fbsd_
            # compat_.h: real, unmodified readdir.c calls
            # `_GENERIC_DIRSIZ()`, defined there and nowhere else on
            # this command's real include path (confirmed by grep) --
            # never previously force-included project-wide since no
            # earlier tier compiled any real FreeBSD dirent source (every
            # prior directory-walking command used libremovefile.a's
            # fts_* instead; archive_read_disk_posix.c's own real,
            # unmodified tree-walk genuinely calls opendir/readdir/
            # closedir/fdopendir directly). -I $OUT/zlib_glue plus the
            # real STDC/NO_DUMMY_DECL/__LITTLE_ENDIAN macros: gz_deflate_
            # impl.c's own real zutil.h dependency, identical to gzip's
            # own cmd_cflags case (real DEFLATE+INFLATE core, reused
            # verbatim here for tar's own real z/gzip-filter support and
            # archive_write_disk_posix.c's decmpfs deflate/inflate calls
            # -- both need the SAME real zlib core, only one compiled
            # copy per command as usual).
            # CONFIG_GZIP_COMPRESSED: real, same as gzip's own cmd_cflags
            # -- zlib_glue/zutil.h's real OS_CODE/PRESET_DICT (deflate.c's
            # own header-byte constants) are gated behind this exact
            # macro (confirmed by reading zutil.h directly after this
            # command's own first real build attempt failed here without
            # it -- not guessed).
            cmd_cflags="-DSTDC -DNO_DUMMY_DECL -D__LITTLE_ENDIAN=1 -DCONFIG_GZIP_COMPRESSED -DHAVE_CONFIG_H -DPRIVATE=1 -I $OUT/tar_glue -I $LIBARCHIVE -I $LIBARCHIVE_TAR -I $LIBARCHIVE_FE -I $OUT/zlib_glue -include $OUT/tar_glue/dirent_compat_macros.h"
            ;;
        less)
            # Real, unmodified less/main.c and friends need:
            #  - -include ed_availability_override.h: real, unmodified
            #    lsystem.c calls system(3) for its own `!cmd` shell-
            #    escape -- same real __IOS_PROHIBITED App-Store-policy
            #    annotation (not a functional restriction) ed(1)'s own
            #    identical `!cmd` already hit and fixed, reused verbatim.
            #  - -I .../bash-3.2/lib/termcap: real, unmodified screen.c's
            #    own `#include <termcap.h>` -- same real GNU termcap.h
            #    declaring header the ls(1)/bash tiers already vendor
            #    and share (bash_readline_termcap_compat.c's own real
            #    tgetent()/tgetnum()/tgetstr()/tgoto()/tputs()/PC/ospeed
            #    definitions, reused verbatim here too -- this project's
            #    already-proven honest "no termcap database" real
            #    fallback: real screen.c already has its own graceful
            #    real "hardcopy mode" (no cursor addressing) path for
            #    exactly this case, confirmed by reading
            #    get_term()'s own `if (tgetent(...) != TGETENT_OK)
            #    hardcopy = 1;`, not assumed).
            #  - -I $LESSROOT: real, pre-generated defines.h (this
            #    checkout's own real autotools output, same "real
            #    generated config, not re-run configure" pattern as
            #    gnudiff's config.h).
            cmd_cflags="-include ed_availability_override.h -I ../../third_party/bash/bash-3.2/lib/termcap -I $LESSROOT -I $LESS"
            ;;
        md5)
            # Real md5.c/commoncrypto.h's own `#include <CommonCrypto/
            # CommonDigest.h>` / `<CommonCrypto/CommonDigestSPI.h>` --
            # resolves to the real, unmodified headers this script's
            # commoncrypto_glue generation step (above) copied in.
            cmd_cflags="-I $OUT/commoncrypto_glue"
            ;;
        date)
            # DAR-149 (2026-09-17): real utmpx-darwin.c's own
            # `#include <asl.h>` needs this project's pinned real
            # syslog-385 asl.h (same real header build_login.sh's own
            # ASL_INC already points at -- these compat shim functions'
            # own asl_object_t definitions must match the SAME real
            # header's declarations, not whatever the SDK's own asl.h
            # would otherwise resolve to first). Its own
            # `#include <servers/bootstrap.h>` (unreached code, real
            # Mach bootstrap-port declarations date never calls, but
            # still a real preprocessor requirement) resolves the same
            # way build_login.sh's own XPC_INC does: this project's
            # pinned PureDarwin libxpc include dir really does carry a
            # real servers/bootstrap.h, confirmed present, not assumed.
            # LOGIN_REAL_UTMPX_PUTUTXLINE opts date_compat.c's own local
            # pututxline()/struct utmpx stand-in out (see that file's
            # own header) so the real NetBSD/utmpx.c + utmpx-darwin.c
            # pair's real definition wins instead, exactly like
            # build_login.sh's own CFLAGS.
            cmd_cflags="-DLOGIN_REAL_UTMPX_PUTUTXLINE=1 -I ../../third_party/syslog/libsystem_asl.tproj/include -I ../../third_party/PureDarwin/src/Libraries/XPC/libxpc/include"
            ;;
    esac

    if ! "$CLANG" $CFLAGS $cmd_cflags "-Dmain=$real_main" "$src" -o "$obj" 2>"$OUT/simple_obj/${name}.err"; then
        echo "  skip: $name (real source)"
        cat "$OUT/simple_obj/${name}.err" >&2
        failed=$((failed + 1))
        continue
    fi
    if ! "$CLANG" $CFLAGS "-DREAL_MAIN=$real_main" "-DCMD_NAME=\"$name\"" generic_main_wrapper.c -o "$wrapper_obj" 2>"$OUT/simple_obj/${name}_wrapper.err"; then
        echo "  skip: $name (wrapper)"
        cat "$OUT/simple_obj/${name}_wrapper.err" >&2
        failed=$((failed + 1))
        continue
    fi

    extra_objs=""
    for ef in $extra; do
        ef_base=$(basename "$ef" .c)
        ef_out="$OUT/simple_obj/${name}_${ef_base}.o"
        if ! "$CLANG" $CFLAGS $cmd_cflags "$ef" -o "$ef_out" 2>"$OUT/simple_obj/${name}_${ef_base}.err"; then
            echo "  skip: $name (extra: $ef)"
            cat "$OUT/simple_obj/${name}_${ef_base}.err" >&2
            failed=$((failed + 1))
            continue 2
        fi
        extra_objs="$extra_objs $ef_out"
    done

    # Per-command extra whole archives (e.g. rm needs all of
    # libremovefile.a's fts_*/removefile symbols, not one function at a
    # time) -- keyed by name here rather than a 5th CMD_LIST field, since
    # this is rare (one command so far).
    extra_libs=""
    common_read_compat="$OUT/simple_obj/common_simple_cmds_read_compat.o"
    case "$name" in
        rm|ln|rmdir|chflags|chown|grep)
            # libremovefile.a's own removefile_syscalls.o already defines
            # read()/read$UNIX2003 -- a real, genuine duplicate-symbol
            # conflict against the shared compat object every other
            # command uses, confirmed via the real link error, not
            # guessed preemptively. Use its copy instead. ln needs this
            # same archive member for its real _rmdir/_unlink (both
            # NO_SYSCALL_STUB in syscalls.master -- real Apple's public
            # wrappers for those two go through a private __unlink/
            # __rmdir + __inc_remove_counter indirection this project
            # hasn't built; libremovefile.a's removefile_syscalls.o
            # already has real, plain-named _unlink/_rmdir raw syscall
            # bodies serving the exact same real syscall numbers, same
            # precedent rm.c itself already established). grep also
            # needs this archive's real fts_open$INODE64/fts_read$INODE64/
            # fts_close$INODE64 (real util.c's -r/-R directory recursion,
            # confirmed present via nm before use) and hits the identical
            # read() duplicate-symbol conflict via gz_gzip_impl.c's own
            # real gzread()->read() call, resolved the same way. grep
            # additionally needs libc_regex.a (Phase 4.6 item 3's real
            # TRE engine, already built and proven via bash -- not
            # linked into any Tier-1/simple_cmds binary before now) for
            # its own real regcomp/regexec/regerror calls.
            # grep's own real regcomp/regexec/regerror no longer needs
            # libc_regex.a here -- libsystem_c.dylib already reexports
            # real TRE (build_libsystem_c.sh's own static-to-dylib
            # migration step 4, confirmed via nm -gU), available via
            # $LINK_DYLIBS for every command now, not just this one.
            extra_libs="$OUT/system/libremovefile.dylib"
            common_read_compat=""
            ;;
        sed)
            # sed needs libc_regex.a (Phase 4.6 item 3) for regcomp/
            # regexec/regerror, plus real -i (in-place edit) support:
            # main.c's mf_fgets() renames a real temp file over the
            # original and fchmod/fchown's it to match, needing
            # libremovefile.a's real rename()/unlink() (same duplicate
            # read() conflict as rm/ln/grep above, resolved the same
            # way) and libcopyfile.a's real fchmod()/fchown() (confirmed
            # via nm not to redefine read() itself, unlike
            # libremovefile.a -- no further conflict from adding it).
            # libc_regex.a no longer needed here -- see grep's own updated
            # comment above (real TRE now via $LINK_DYLIBS).
            extra_libs="$OUT/system/libremovefile.dylib $OUT/system/libcopyfile.dylib"
            common_read_compat=""
            ;;
        find)
            # Same real libremovefile.a/libcopyfile.a needs as sed
            # above (both used to be grouped in one `sed|find)` case;
            # split 2026-09-03, DAR-5, once find's own needs diverged --
            # see libinfo.dylib note just below).
            #
            # libinfo.dylib ADDED 2026-09-03 (DAR-5, libsystem_c.dylib
            # duplicate-symbol pass, real regression found as a side
            # effect of a full rebuild, not part of the dedup itself):
            # find_function.o's real -group/-user predicates call
            # getgrnam()/getpwnam() directly. Until today this resolved
            # via libSystem.B.dylib's reexport of libcopyfile.dylib,
            # which carried real Apple source but always-NULL ACL-text
            # stand-ins for exactly these 4 symbols. DAR-229 (commit
            # 31edb2a, same day) correctly `-Wl,-unexported_symbol`'d
            # them off libcopyfile.dylib in favor of libinfo.dylib's
            # real flat-file-backed implementation -- verified "0
            # relink-breakage" there by sweeping already-built .machos'
            # existing binds, not by a fresh from-source relink, so this
            # specific gap (find.macho/tar.macho's find case is here;
            # tar's is below) went undetected until this rebuild hit it
            # for real as a link error. Same real fix in both places:
            # add libinfo.dylib to the link so it resolves to the real
            # implementation instead of going undefined.
            extra_libs="$OUT/system/libremovefile.dylib $OUT/system/libcopyfile.dylib $OUT/system/libinfo.dylib"
            common_read_compat=""
            ;;
        df)
            # Real fabs()/trunc()/ilogb() (see this command's own
            # CMD_LIST comment above) -- libc_build/libm.a, scoped to
            # just this command since it's the only current simple_cmds
            # consumer of any of the three (confirmed via grep before
            # adding this project-wide to LINK_COMMON, which would widen
            # blast radius for every other command in this tier with no
            # current benefit).
            extra_libs="$OUT/system/libsystem_m.dylib"
            ;;
        date)
            # DAR-203 (2026-09-04): date_compat.c no longer carries its
            # own local send()/writev() ENOSYS stubs (see that file's
            # own updated header for the real investigation) -- date
            # now needs libsystem_kernel.dylib's real, direct
            # `_send$UNIX2003`/`_writev$UNIX2003` exports for real.
            # Listed here directly (ahead of $LINK_DYLIBS's
            # libsystem_c.dylib, which only plain-LOADs -- does not
            # reexport -- libsystem_kernel.dylib) rather than relying
            # on the transitive reexport through libSystem.B.dylib at
            # the tail of $LINK_DYLIBS: same real "ld64 records the
            # bind against whichever dylib on the link line reaches the
            # symbol first, and real dyld only searches that image's
            # own exports/reexports at load time" hazard CLAUDE.md's
            # DAR-217 note documents, and the same fix this file's own
            # libsystem_platform.dylib/libcopyfile.dylib/
            # libremovefile.dylib LINK_DYLIBS comments already
            # established -- list the real owning dylib directly.
            #
            # libinfo.dylib ADDED 2026-09-17 (DAR-149): real
            # utmpx-darwin.c's own getlastlogx()/getlastlogxbyname()
            # (dead from date's actual call graph -- setthetime() only
            # ever reaches pututxline()'s WRITE side -- but still a real
            # link-time undefined-symbol requirement since the whole
            # object file is linked, not lazily pulled from an archive)
            # call a local static _pwuid_r() wrapping getpwuid_r().
            # tar_pwgr_r_compat.c (this command's own new CMD_LIST
            # extra, reused verbatim from tar's own DAR-5 case above)
            # supplies the real POSIX _r entry point over this project's
            # already-real, already-built plain getpwuid()/getpwnam()/
            # getgrnam()/getgrgid() -- libinfo.dylib is the real owner of
            # those four (same real export tar/find/gzip's own extra_libs
            # cases above already rely on), so it's listed here too.
            extra_libs="$OUT/system/libsystem_kernel.dylib $OUT/system/libinfo.dylib"
            ;;
        ed)
            # libc_regex.a: re.c's real regcomp/regexec/regerror
            # (search/substitute addressing). $OUT/ed_setjmp_raw.o: this
            # command's own AAPCS64 setjmp/longjmp core (generated above,
            # see this loop's own comment there for why it's its own
            # dedicated .o rather than a CMD_LIST extra source).
            # $OUT/ed_pipe_trap.o (real Apple pipe(2) asm) and its
            # tar_pipe_compat.c public-name forwarder (this command's own
            # CMD_LIST entry above) REMOVED 2026-09-02 (DAR-196):
            # libsystem_kernel.dylib now genuinely exports both `_pipe`
            # and `___pipe` (confirmed via `xcrun dyld_info -exports`),
            # reachable via $LINK_DYLIBS's libSystem.B.dylib reexport --
            # this project's own copy (plain Apple __pipe.s + a
            # forwarder, no special-casing) was a straight duplicate, not
            # a divergent implementation.
            # libremovefile.a: real unlink() (buf.c's real temp-file
            # cleanup) -- same real duplicate read()/read$UNIX2003
            # conflict as rm/ln/grep/sed above, common_read_compat
            # cleared the same way. libcopyfile.a: real
            # open_dprotected_np() (mktemp.c's real content-protection-
            # class-aware mkstemp() path, same real dependency the
            # `mktemp` command itself already established).
            extra_libs="$OUT/ed_setjmp_raw.o $OUT/system/libremovefile.dylib $OUT/system/libcopyfile.dylib"
            common_read_compat=""
            ;;
        compress)
            # libc_regex.a: real rpmatch() (compress -f's real y/n
            # overwrite-confirmation prompt) pulls in regexec/regfree
            # from libc_locale_for_stdio.a's own rpmatch.o, same real
            # regcomp(nl_langinfo(YESEXPR)) dependency xargs -p's own
            # CMD_LIST comment already documents. libremovefile.a: real
            # unlink() (compress/decompress's real "remove the original
            # after successful compression" step) -- same real duplicate
            # read()/read$UNIX2003 conflict as rm/ln/grep/gzip above,
            # common_read_compat cleared the same way.
            extra_libs="$OUT/system/libremovefile.dylib"
            common_read_compat=""
            ;;
        gzip)
            # Same real duplicate-symbol reasoning as rm|ln|grep above
            # (libremovefile.a's real fts_open$INODE64/fts_read$INODE64/
            # fts_close$INODE64 for gzip -r's real directory recursion,
            # libcopyfile.a's real fcopyfile() for gzip's real
            # ACL/xattr-preserving output-file creation). Plus this
            # command's own new real fgetattrlist/fsetattrlist raw
            # syscalls (plain generatable stubs, not NO_SYSCALL_STUB --
            # generated into the standard SYS_STUBS_NEEDED batch above,
            # referenced directly here since they're niche enough not to
            # warrant a global LINK_COMMON addition).
            extra_libs="$OUT/system/libremovefile.dylib $OUT/system/libcopyfile.dylib $OUT/simple_obj/sys__fgetattrlist.o $OUT/simple_obj/sys__fsetattrlist.o $OUT/simple_obj/sys__dup.o"
            common_read_compat=""
            ;;
        cp|mv|mktemp|du|chmod|ls)
            # Same real duplicate-symbol reasoning as rm|ln above
            # (libremovefile.a's removefile_syscalls.o already defines
            # plain read()) plus libcopyfile.a for real copyfile()/
            # fcopyfile() (cp's data+xattr+ACL copy, mv's fcopyfile
            # ACL/xattr preservation across a rename fallback) and its
            # already-built realpath/fchmod/fchown/fchflags members
            # (mv.c needs all three, reused rather than regenerated).
            # Normal (non-force_load) archive linking for both -- only
            # symbols cp.c/mv.c/utils.c actually reference get pulled,
            # so no duplicate-symbol risk against this script's own
            # freshly generated sys__readlink.o/sys__chown.o/etc (first
            # definition on the link line wins, archive members for an
            # already-resolved symbol are simply never pulled).
            extra_libs="$OUT/system/libremovefile.dylib $OUT/system/libcopyfile.dylib"
            common_read_compat=""
            ;;
        diff)
            # Real sigaltstack(2) raw syscall (see this command's own
            # CMD_LIST comment) plus AAPCS64 setjmp/longjmp
            # register-save/restore core (dir.c's real recursive-
            # directory-diff error unwinding) -- both generated above as
            # their own .o files already, not CMD_LIST extra .c sources,
            # same precedent as the ed tier that first needed them.
            # diff.c's own begin_output()/finish_output() real
            # vfork()+pipe()+execl() "diff | pager" path (PR_PROGRAM/
            # DEFAULT_PAGER in diff.h/system.h) used to need ed's own
            # real pipe(2) raw syscall too ($OUT/ed_pipe_trap.o); REMOVED
            # 2026-09-02 (DAR-196), see the `ed)` case above for why --
            # same real dylib export, same non-special-cased code.
            extra_libs="$OUT/simple_obj/sys____sigaltstack.o $OUT/ed_setjmp_raw.o"
            ;;
        tar)
            # libremovefile.a: real fts_*/unlink/rmdir (archive_read_
            # disk_posix.c's own -exclude-vcs handling and general real
            # filesystem cleanup) -- same real duplicate read()/
            # read$UNIX2003 conflict rm/ln/grep/sed/ed/gzip/compress all
            # already hit against the shared common_read_compat object,
            # resolved the same way (common_read_compat cleared below).
            # libcopyfile.a: real fcopyfile()/ACL+xattr copy helpers
            # archive_read_disk_posix.c/archive_disk_acl_darwin.c
            # actually call. libc_regex.a: real regcomp/regexec/regerror
            # (archive_write.c's/bsdtar's own real -s substitution-
            # pattern support, subst.c). filter_fork_posix.c's real
            # forked-external-decompressor fallback path used to need
            # ed's own real pipe(2) raw syscall too ($OUT/ed_pipe_trap.o,
            # reused verbatim from the ed tier); REMOVED 2026-09-02
            # (DAR-196), see the `ed)` case above for why.
            #
            # libinfo.dylib ADDED 2026-09-03 (DAR-5): tar_pwgr_r_compat.o's
            # real getpwnam_r/getpwuid_r/getgrnam_r/getgrgid_r wrappers
            # call the plain getpwnam()/getpwuid()/getgrnam()/getgrgid()
            # forms, same real gap as the `find)` case above -- see its
            # comment for the full DAR-229/DAR-5 trail, identical fix.
            extra_libs="$OUT/system/libremovefile.dylib $OUT/system/libcopyfile.dylib $OUT/system/libinfo.dylib $OUT/simple_obj/sys__dup.o $OUT/simple_obj/sys__readlinkat.o $OUT/simple_obj/sys__poll.o"
            common_read_compat=""
            ;;
        less)
            # libc_regex.a: real pattern.c's regcomp/regexec/regerror
            # (POSIX search, HAVE_POSIX_REGCOMP). $OUT/ed_setjmp_raw.o:
            # real AAPCS64 setjmp/longjmp register-save/restore core
            # (ed_setjmp_compat.c's own real setjmp()/longjmp() bodies,
            # which command.c's real multi-key-sequence input handling
            # calls, reused verbatim from the ed tier). $OUT/simple_obj/
            # sys__fsync.o: real fsync(2) raw syscall (ch.c's log-file
            # durability), generated above via the standard
            # SYS_STUBS_NEEDED batch.
            # libremovefile.a: real rename() (mark.c's real lock-file
            # rename), plus the same real duplicate read()/read$UNIX2003
            # conflict as rm/ln/grep/sed/ed/gzip/tar above (common_read_
            # compat cleared below). libcopyfile.a: real fchmod().
            # lsystem.c's real popen()-backed pipe-to-external-command
            # support used to need ed's own real pipe(2) raw syscall too
            # ($OUT/ed_pipe_trap.o via tar_pipe_compat.c's forwarder,
            # reused verbatim); REMOVED 2026-09-02 (DAR-196), see the
            # `ed)` case above for why. $OUT/simple_obj/sys__dup.o: real
            # dup(2) (also lsystem.c's real stdin/stdout-fd-juggling
            # around a forked child).
            # sys__fsync.o dropped -- libSystem.B.dylib already exports
            # real fsync(2) directly (confirmed via nm -gU).
            extra_libs="$OUT/ed_setjmp_raw.o $OUT/system/libremovefile.dylib $OUT/system/libcopyfile.dylib $OUT/simple_obj/sys__dup.o"
            common_read_compat=""
            ;;
        sleep|tail)
            # Real Mach IPC (Phase 4.6 item 5, 2026-08-21): both real
            # nanosleep() entry points these two commands need
            # (simple_cmds_nanosleep_pub_compat.c's public `nanosleep()`
            # for sleep(1); simple_cmds_nanosleep_compat.c's private
            # `_nanosleep` for tail -f's poll interval, via usleep.c) are
            # now genuinely blocking, via real host_get_clock_service +
            # semaphore_create + clock_get_time + __semwait_signal Mach
            # IPC -- see build_mach_ipc.sh for the full real chain this
            # links (this project's first-ever mach_msg()-trap exercise).
            #
            # DAR-204 (2026-09-04): $OUT/libmach_ipc.a swapped for the
            # narrowed $OUT/libmach_ipc_no_voucher_for_simple_cmds.a built
            # above -- mach_port_insert_right/extract_right/unguard stay
            # exactly as before (out of scope), voucher_mach_msg_set no
            # longer resolves to mach_ipc_dead_stubs.o's local honest-FALSE
            # stand-in.
            #
            # ORDER MATTERS, found the hard way: the narrowed archive is
            # listed FIRST, libsystem_kernel.dylib SECOND -- NOT the
            # reverse. clock_get_time/host_get_clock_service/
            # semaphore_create are real, live, USING_VOUCHERS-gated MIG
            # routines this project's own clockUser.o/mach_hostUser.o/
            # taskUser.o provide (Phase 4.6 item 5's own QEMU-verified
            # real Mach IPC exercise) -- libsystem_kernel.dylib ALSO now
            # exports all three of those names for real (DAR-14's own MIG
            # regen work). Listing the dylib first (tried initially) let
            # ld64 claim ALL THREE from the dylib instead of this
            # project's own local MIG stubs the instant it was encountered
            # first on the link line -- a real, much bigger behavior
            # change than DAR-204 asked for, caught via a before/after
            # `nm`/`dyld_info -fixups` diff showing _clock_get_time flip
            # from a local T symbol to an external bind. Listing the
            # archive first preserves local resolution for all 3 exactly
            # as before; only voucher_mach_msg_set (hidden, non-external,
            # in the narrowed object) falls through past the archive to
            # libsystem_kernel.dylib. Still listed directly here (ahead of
            # $LINK_DYLIBS's libsystem_c.dylib, which only plain-loads --
            # does not reexport -- libsystem_kernel.dylib), same real
            # "ld64 records the bind against whichever dylib on the link
            # line reaches the symbol first" DAR-217 hazard as date's own
            # entry above; libsystem_kernel.dylib is confirmed the only
            # dylib in these two binaries' full transitive closure that
            # exports voucher_mach_msg_set at all (see this loop's own
            # pre-build comment for why libdispatch.dylib -- the OTHER
            # real owner, and the one launchd.macho/launchctl.macho must
            # keep their own local stub to avoid -- is unreachable here).
            extra_libs="$OUT/libmach_ipc_no_voucher_for_simple_cmds.a $OUT/system/libsystem_kernel.dylib"
            ;;
        md5)
            # Real md5.c's MDTimeTrial() (-t flag) calls getrusage(2)
            # unconditionally at file scope (not dead-code-eliminated
            # even though -t is rarely exercised) -- not previously
            # needed by any simple_cmds.sh command, so not in the shared
            # SYS_STUBS_NEEDED batch's LINK_COMMON; generated there
            # (SYS_STUBS_NEEDED now includes _getrusage) and linked here
            # instead, same scoping precedent as tar/less/killall's own
            # per-command extra_libs above.
            extra_libs="$OUT/simple_obj/sys__getrusage.o"
            ;;
    esac

    # Small, scoped alias set for the handful of real UNIX2003/INODE64
    # gaps that remain -- every base below is real and confirmed present
    # (via nm) on the exact link line it's appended to, unlike the old
    # 60-entry universal LINK_ALIASES saga this replaced, so this does
    # not reproduce the real ld64-609 combined-complexity crash that
    # forced that mechanism to exist in the first place.
    #
    # _pread$UNIX2003/_pwrite$UNIX2003/_statfs$INODE64 aliases REMOVED
    # 2026-09-04 (DAR-196, second pass): their bases (`_pread`/`_pwrite`/
    # `_statfs`) are no longer locally defined here (see LINK_COMMON's
    # own comment above) -- libcopyfile.dylib already exports all three
    # `$UNIX2003`/`$INODE64` names directly, no local alias needed.
    # `_getfsstat$INODE64` stays: `_getfsstat` is still genuinely local
    # (statfs64_compat.o, narrowed to keep only `_statfs` hidden), and no
    # shipped dylib exports either `_getfsstat` name.
    link_aliases="-Wl,-alias,__err,_err -Wl,-alias,__warn,_warn \
        -Wl,-alias,_lchown,_lchown\$UNIX2003 \
        -Wl,-alias,_setattrlist,_setattrlist\$UNIX2003 \
        -Wl,-alias,_waitpid,_waitpid\$UNIX2003 \
        -Wl,-alias,_confstr,_confstr\$UNIX2003 \
        -Wl,-alias,_getfsstat,_getfsstat\$INODE64"
    case "$extra_libs" in
        *libcopyfile.dylib*)
            link_aliases="$link_aliases -Wl,-alias,_fchmod,_fchmod\$UNIX2003 -Wl,-alias,_getattrlist,_getattrlist\$UNIX2003" ;;
    esac
    case "$extra_objs" in
        *apply_sigsuspend_compat.o*) link_aliases="$link_aliases -Wl,-alias,_sigsuspend,_sigsuspend\$UNIX2003" ;;
    esac
    # _send/_writev aliases dropped from this case 2026-08-28:
    # cat_socket_stub.c only ever defined socket()/connect()/shutdown()
    # (see that file's own header -- real cat.c's udom_open()/raw_cat()
    # socket path never calls send()/writev() directly), so aliasing
    # them here aliased a base symbol that was never defined anywhere
    # in cat's own link. Xcode 12's own ld64-609 silently tolerated
    # that (real, now-confirmed-benign divergence from the daily-driver
    # Xcode's newer ld, which this script now links with -- see
    # NEWLD_BINDIR's header comment); the newer ld hard-fails on it
    # instead ("symbol(s) not found ... in command-line-aliases-file"),
    # which is what surfaced this as a real dead/incorrect alias rather
    # than a load-bearing one.
    case "$extra_objs" in
        *cat_socket_stub.o*) link_aliases="$link_aliases -Wl,-alias,_connect,_connect\$UNIX2003" ;;
    esac
    # date's own local ENOSYS send()/writev() stubs (and this alias
    # pair) REMOVED 2026-09-04 (DAR-203): real investigation (see
    # date_compat.c's own updated header) found writev(2) is dead code
    # from date's actual call graph (both real oldsyslog.c call sites
    # are gated on _sl_LogStat bits date.c never sets) and send(2),
    # while genuinely reached from date's real -- non -j -- clock-set
    # path, is always called with fd == -1 (cat_socket_stub.c's own
    # socket() unconditionally ENOSYS's first), so libsystem_kernel
    # .dylib's real send$UNIX2003 syscall wrapper just gets an honest
    # real EBADF there instead -- same "always fails" outcome, no
    # actual networking. date's own extra_libs case below now lists
    # libsystem_kernel.dylib directly so both symbols resolve for real
    # rather than relying on a transitive reexport through
    # libsystem_c.dylib's plain (non-reexport) load of it -- same real
    # bind-resolution hazard as this file's libsystem_platform.dylib/
    # libcopyfile.dylib/libremovefile.dylib LINK_DYLIBS comments and
    # CLAUDE.md's own DAR-217 note.
    case "$extra_objs" in
        *xargs_getrlimit_compat.o*) link_aliases="$link_aliases -Wl,-alias,_getrlimit,_getrlimit\$UNIX2003" ;;
    esac
    case "$name" in
        tar) link_aliases="$link_aliases -Wl,-alias,_poll,_poll\$UNIX2003" ;;
        date)
            # DAR-149 (2026-09-17): real utmpx-darwin.c's own
            # _utmpx_working_copy() calls ttyname_r(0/1/2, ...) under
            # this build's -D__DARWIN_UNIX03=1, which makes the real SDK
            # header declare (and the compiler reference) the versioned
            # `_ttyname_r$UNIX2003` name. libsystem_c.dylib only exports
            # the bare `_ttyname_r` (confirmed via `xcrun dyld_info
            # -exports`) -- same real gap build_login.sh's own identical
            # alias already fixes for login.macho (confirmed via a real
            # `xcrun dyld_info -exports login.macho`, which shows a
            # genuine `[re-export] _ttyname_r$UNIX2003 (_ttyname_r from
            # libsystem_c)`, not the malformed-ordinal-0 result the
            # ld64 alias/nonlocal-base hazard would produce).
            link_aliases="$link_aliases -Wl,-alias,_ttyname_r,_ttyname_r\$UNIX2003" ;;
    esac

    # locale_extra_objs: RESOLVED 2026-08-28 (see NEWLD_BINDIR's own
    # header comment above and docs/backlog.md item d9). Always linked
    # now, same "harmless if unreferenced" reasoning as
    # common_table.o/common_none.o joining COMMON_EXTRA above -- the
    # real ld64-609 crash this used to trigger only reproduces under
    # Xcode 12's own bundled linker; NEWLD_BINDIR's -B override on the
    # link line below avoids it.
    #
    # getcwd.o dropped 2026-08-30 (static-linking cleanup, Phase 2a) --
    # see COMMON_EXTRA's own comment. ldpart.o/collate.o/collcmp.o/
    # findfp.o (now narrowed) stay: real per-command extras need symbols
    # only they provide.
    locale_extra_objs="$OUT/simple_obj/common_libc_locale_globals_compat.o $OUT/simple_obj/common_table.o $OUT/simple_obj/common_none.o $OUT/simple_obj/common_ldpart.o $OUT/simple_obj/common_collate.o $OUT/simple_obj/common_collcmp.o $OUT/simple_obj/common_findfp.o"

    # Real dynamic link (2026-08-28) -- no -static/-e start (see
    # LINK_DYLIBS's own header comment near LINK_COMMON for what this
    # replaces). $common_read_compat is deliberately NOT linked here any
    # more -- its whole purpose was forwarding to the private __read raw
    # syscall that used to live in libc_locale_for_stdio.a; libSystem.B.
    # dylib now exports real public _read directly (confirmed via
    # nm -gU), making that forwarder redundant. -e start is gone too:
    # confirmed via a real prototype trial link that ld64 ignores it for
    # this target's non-static link mode regardless ("Ignoring '-e
    # start' because entry point 'start' is not used for the targeted OS
    # version") and always emits LC_MAIN instead of LC_UNIXTHREAD --
    # matching build_dyld_test_exec.sh's own already-QEMU-verified
    # dyld_test_exec.macho exactly, not a new or untested code shape.
    #
    # -DCRT_DYNAMIC_LINKING ADDED 2026-09-04 (DAR-196): every one of
    # these ~76 commands is a real, genuinely dynamically-linked
    # executable (LC_LOAD_DYLINKER + real LC_LOAD_DYLIB below), exactly
    # the case DAR-161 already built this macro for (see
    # build_launchctl.sh/build_launchd.sh/build_pd_console_login.sh/
    # build_dyld_test_exec.sh for the same, already-QEMU-verified
    # pattern) -- without it, Csu/crt.c's own NXArgc/NXArgv/environ/
    # __progname local storage silently shadowed the real, canonical
    # copies libdyld.dylib's libdyldGlue.o exports (confirmed real
    # exports via `xcrun dyld_info -exports`, all 4 directly on
    # $LINK_DYLIBS already). This was simply never applied to this
    # script's own link line before now.
    if ! "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
        -DCRT_DYNAMIC_LINKING \
        -B"$NEWLD_BINDIR" \
        -Wl,-fixup_chains \
        -o "${name}.macho" \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$wrapper_obj" "$obj" $extra_objs \
        "$OUT/simple_obj/common_err.o" "$OUT/simple_obj/common_getopt.o" \
        "$OUT/simple_obj/common_simple_cmds_libsystem_c_gap_compat.o" \
        "$OUT/simple_obj/common_cp_mv_syscalls.o" \
        $extra_libs $locale_extra_objs \
        $LINK_COMMON $LINK_DYLIBS \
        $link_aliases 2>"$OUT/simple_obj/${name}_link.err"; then
        echo "  skip: $name (link)"
        cat "$OUT/simple_obj/${name}_link.err" >&2
        failed=$((failed + 1))
        continue
    fi

    bad=0
    if ! otool -l "${name}.macho" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld"; then
        echo "  FAIL: $name -- no LC_LOAD_DYLINKER /usr/lib/dyld, not a real dynamic executable" >&2
        bad=1
    fi
    if ! otool -l "${name}.macho" | grep -q "cmd LC_LOAD_DYLIB"; then
        echo "  FAIL: $name -- no LC_LOAD_DYLIB, not a real dynamic executable" >&2
        bad=1
    fi
    if ! otool -l "${name}.macho" | grep -q "cmd LC_MAIN"; then
        echo "  FAIL: $name -- no LC_MAIN (real unixproc entry, see LINK_DYLIBS' own comment on the load_dylinker() kernel bug this guards against)" >&2
        bad=1
    fi
    if [ "$bad" -ne 0 ]; then
        failed=$((failed + 1))
        continue
    fi

    echo "  ok: $name.macho ($(wc -c < "${name}.macho") bytes)"
    built=$((built + 1))

    # DAR-263 report-only gate, sleep/tail only: these two are the only
    # commands in this loop that link libmach_ipc_no_voucher_for_simple_cmds.a
    # (see this loop's own DAR-204 case-statement comment above), and so
    # the only ones that can carry mach_ipc_dead_stubs.o's KERN_FAILURE
    # stand-ins at all. Confirmed (2026-09-07) reachable only from
    # mach_msg_destroy_port()/mach_msg_destroy() -- the provably-dead
    # path they were written for -- so this is informational, not a
    # build failure.
    #
    # Deliberately NOT the reorder fix that closed DAR-261 for notifyd/
    # configd/launchctl: DAR-204's own comment above documents that
    # listing libsystem_kernel.dylib ahead of this narrowed archive for
    # sleep/tail causes a DIFFERENT, confirmed, worse regression --
    # clock_get_time/host_get_clock_service/semaphore_create silently
    # resolve to the dylib instead of this project's own local MIG stubs
    # the instant the dylib is listed first. The current order is
    # correct and must not change for this reason alone.
    if { [ "$name" = "sleep" ] || [ "$name" = "tail" ]; } && [ -x ./dead_stub_audit.sh ]; then
        ./dead_stub_audit.sh "${name}.macho"
    fi
done

echo "build_simple_cmds: $built built, $failed failed"
[ "$failed" -eq 0 ]
