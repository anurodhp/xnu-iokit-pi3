#!/bin/bash
# Builds a real, dynamically-linked sshd.macho from real, unmodified
# OpenSSH source -- DAR-183 (Phase 7b).
#
# SOURCE CHOICE (per DAR-183's own instruction, 2026-09-05: "use a current
# freebsd sshd instead of an old Mac one. It is ok that it doesn't use
# launchd"): third_party/freebsd-src/crypto/openssh @ release/13.2.0 ==
# real OpenSSH 9.3p1 + FreeBSD's own patches (version.h's own
# SSH_VERSION_FREEBSD "FreeBSD-20230316"). NOT third_party/OpenSSH
# (apple-oss-distributions OpenSSH-240.40.1 == OpenSSH 8.1p1, two years
# and ~5 releases older) -- that checkout is still pinned and IS used
# here, but only as a reference for the Darwin-side HAVE_* answers in the
# generated config.h below (Apple's own checked-in, real Darwin-generated
# openssh/config.h), never for source.
#
# CRYPTO: built WITHOUT OpenSSL (config.h override table below undefines
# WITH_OPENSSL). This is a real, upstream-supported OpenSSH configuration
# (configure's own --without-openssl), not a stub or a narrowing invented
# here: the resulting sshd speaks curve25519-sha256 /
# sntrup761x25519-sha512 KEX, ssh-ed25519 host+user keys,
# chacha20-poly1305@openssh.com and aes*-ctr ciphers (OpenSSH's OWN
# bundled rijndael.c/cipher-aesctr.c, not libcrypto's), and
# hmac-sha2-256/512 MACs -- all from OpenSSH's own in-tree
# implementations. This project has no OpenSSL port and porting one is a
# far larger effort than this ticket; DAR-183's own open question ("whether
# OpenSSH needs further crypto from [the kernel g_crypto_funcs] table")
# resolves to "no" for the same reason -- OpenSSH's userland crypto is
# entirely self-contained in this configuration and never touches the
# kernel's crypto table.
#
# Likewise WITHOUT zlib (no zlib port here; WITH_ZLIB is its own real
# upstream ifdef), WITHOUT PAM (this project's OpenPAM has no service
# modules -- see openpam_dynamic_compat.c), WITHOUT GSSAPI/Kerberos, BSM
# audit, libedit, PKCS#11 and FIDO/U2F security keys.
#
# Toolchain: Xcode-12's iPhoneOS14.4 SDK, same as every other script in
# this directory -- see CLAUDE.md's "Build commands" section.

set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

# Same real ld64-609-crash-under-fixup_chains workaround every dynamic
# executable in this directory uses -- see build_syslogd.sh's own header
# comment for the full trail (docs/backlog.md item d9).
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
SSHDIR_SRC="../../third_party/freebsd-src/crypto/openssh"
APPLE_SSH="../../third_party/OpenSSH/openssh"
OUT="libc_build"

if [ ! -f "$SSHDIR_SRC/sshd.c" ]; then
    echo "error: $SSHDIR_SRC not found -- run setup_third_party.sh (it now" >&2
    echo "       sparse-checks out crypto/openssh from the freebsd-src pin)" >&2
    exit 1
fi

SOUT="$OUT/sshd_obj"
SRCTREE="$OUT/sshd_src"
mkdir -p "$SOUT"

# ---------------------------------------------------------------------
# Out-of-tree source staging (symlink farm), and why it is needed rather
# than just a -I ahead of the source dir:
#
# includes.h line 19 is an UNCONDITIONAL `#include "config.h"`, and a
# quoted include is resolved against the directory of the INCLUDING FILE
# before any -I path. FreeBSD's checkout ships its own real, checked-in
# crypto/openssh/config.h (autoconf output for FreeBSD), so compiling the
# sources in place always picks THAT up and silently ignores the one
# generated below -- confirmed for real: the first build attempt here
# failed all 201 files on `security/pam_appl.h file not found` even
# though the generated config.h had HAVE_SECURITY_PAM_APPL_H undefined.
#
# So every source and header is symlinked into $SRCTREE (clang resolves
# the quoted include against the symlink's own directory, not the link
# target's), with this port's generated config.h as the only real file
# there. Nothing under third_party/ is modified. openbsd-compat/ gets the
# same treatment -- its sys-tree.h has the same unconditional quoted
# include, which falls through to $SRCTREE/config.h via -I.
rm -rf "$SRCTREE"
mkdir -p "$SRCTREE/openbsd-compat"
SSHDIR_ABS="$(cd "$SSHDIR_SRC" && pwd)"
for f in "$SSHDIR_ABS"/*.c "$SSHDIR_ABS"/*.h; do
    b="$(basename "$f")"
    [ "$b" = "config.h" ] && continue
    ln -s "$f" "$SRCTREE/$b"
done
for f in "$SSHDIR_ABS"/openbsd-compat/*.c "$SSHDIR_ABS"/openbsd-compat/*.h; do
    ln -s "$f" "$SRCTREE/openbsd-compat/$(basename "$f")"
done

# ---------------------------------------------------------------------
# config.h
#
# Generated, not hand-written, from FreeBSD's own real checked-in
# crypto/openssh/config.h (which is autoconf output for exactly this
# 9.3p1 source tree, so its KNOB SET is correct by construction -- no
# drift between a hand-maintained header and the source's #ifdefs) plus
# the explicit override table below. Every override falls into one of
# four buckets and says which:
#   [nolib]  a library this project does not have (OpenSSL, zlib, PAM,
#            Kerberos, BSM, libedit, PKCS#11, FIDO)
#   [darwin] a genuinely OS-dependent answer, taken from Apple's own real
#            Darwin-generated config.h (third_party/OpenSSH/openssh/
#            config.h, OpenSSH 8.1p1) rather than guessed
#   [gap]    a function this project's curated libc really does not
#            export -- each one verified with a real `nm -gU` sweep over
#            libc_build/system/*.dylib, not assumed; OpenSSH's own
#            openbsd-compat/ replacement gets compiled in instead
#   [freebsd] a FreeBSD-only kernel feature with no Darwin counterpart
# ---------------------------------------------------------------------
python3 - "$SSHDIR_SRC/config.h" "$SRCTREE/config.h" <<'PY_EOF'
import re, sys
base_path, out_path = sys.argv[1], sys.argv[2]

# name -> (value or None for #undef, bucket, why)
OVERRIDES = {
    # --- [nolib] no OpenSSL anywhere in this system -------------------
    "WITH_OPENSSL":            (None, "nolib", "no OpenSSL port; real upstream --without-openssl build"),
    "USE_OPENSSL_ENGINE":      (None, "nolib", "libcrypto ENGINE API"),
    "ENABLE_PKCS11":           (None, "nolib", "ssh-pkcs11 needs libcrypto + a real PKCS#11 module"),
    "HAVE_DLOPEN":             (None, "nolib", "only consulted by ssh-pkcs11.c, which is off"),
    # --- [nolib] no zlib ---------------------------------------------
    "WITH_ZLIB":               (None, "nolib", "no zlib port; real upstream ifdef, compression simply off"),
    # --- [nolib] no PAM/Kerberos/BSM/libedit/FIDO --------------------
    "USE_PAM":                 (None, "nolib", "this project's OpenPAM has no service modules"),
    "HAVE_SECURITY_PAM_APPL_H":(None, "nolib", "includes.h pulls it in on this knob alone, not on USE_PAM"),
    "HAVE_PAM_PAM_APPL_H":     (None, "nolib", "same, Linux-PAM spelling"),
    "USE_LIBEDIT":             (None, "nolib", "sftp(1) line editing only; sshd does not use it"),
    "ENABLE_SK":               (None, "nolib", "FIDO/U2F security keys need libfido2"),
    "ENABLE_SK_INTERNAL":      (None, "nolib", "same"),
    # --- [darwin] uid/gid switching ----------------------------------
    "HAVE_SETRESUID":          (None, "darwin", "Darwin has no setresuid(2)"),
    "HAVE_SETRESGID":          (None, "darwin", "Darwin has no setresgid(2)"),
    "SETEUID_BREAKS_SETUID":   ("1",  "darwin", "Apple's own config.h"),
    "BROKEN_SETREUID":         ("1",  "darwin", "Apple's own config.h"),
    "BROKEN_SETREGID":         ("1",  "darwin", "Apple's own config.h"),
    # --- [darwin] ptys ------------------------------------------------
    "HAVE_DEV_PTMX":           ("1",  "darwin", "XNU's real /dev/ptmx clone device (bsd/kern/tty_ptmx.c, compiled into this kernel -- verified with nm on the built kernel)"),
    "HAVE_LIBUTIL_H":          (None, "darwin", "FreeBSD-only header"),
    # --- [darwin] misc headers/APIs FreeBSD has and Darwin does not ---
    "HAVE_LOGIN_CAP_H":        (None, "darwin", "no login.conf/login_cap on Darwin"),
    "HAVE_LOGIN_GETCAPBOOL":   (None, "darwin", "same"),
    "HAVE_LOGIN_GETPWCLASS":   (None, "darwin", "same"),
    "HAVE_FLOATINGPOINT_H":    (None, "darwin", "FreeBSD-only header"),
    "HAVE_ELF_H":              (None, "darwin", "Mach-O, not ELF"),
    "HAVE_SYS_TIMERS_H":       (None, "darwin", "FreeBSD-only header"),
    "HAVE_NET_IF_TUN_H":       (None, "darwin", "no if_tun.h on Darwin"),
    "SSH_TUN_COMPAT_AF":       ("1",  "darwin", "Apple's own config.h (Darwin utun address-family framing)"),
    "SSH_TUN_PREPEND_AF":      ("1",  "darwin", "Apple's own config.h"),
    "HAVE_STRUCT_STAT_ST_MTIM":(None, "darwin", "Darwin's struct stat has st_mtimespec, not st_mtim"),
    "HAVE_STRUCT_STATFS_F_FLAGS": (None, "darwin", "Apple's own config.h"),
    "SNPRINTF_CONST":          ("/* not const */", "darwin", "Apple's own config.h"),
    "SPT_TYPE":                ("SPT_REUSEARGV", "darwin", "no setproctitle(3) on Darwin; Apple's own config.h"),
    "HAVE_SETPROCTITLE":       (None, "darwin", "same"),
    "XAUTH_PATH":              (None, "darwin", "no X11 in this system at all"),
    "LOCKED_PASSWD_PREFIX":    (None, "darwin", "FreeBSD's *LOCKED* convention; this project's master.passwd is written by tools/pwtool, which does not use it"),
    # --- [freebsd] kernel features with no Darwin counterpart ---------
    "HAVE_SYS_CAPSICUM_H":     (None, "freebsd", "Capsicum is FreeBSD-only"),
    "HAVE_CAP_RIGHTS_LIMIT":   (None, "freebsd", "same"),
    "SANDBOX_CAPSICUM":        (None, "freebsd", "same"),
    "SANDBOX_SKIP_RLIMIT_NOFILE": (None, "freebsd", "Capsicum-sandbox-specific"),
    "SANDBOX_NULL":            ("1",  "freebsd", "the real upstream fallback sandbox (sandbox-null.c). NOT SANDBOX_DARWIN: that one calls sandbox_init(3)/Seatbelt, which this system has no implementation of"),
    "HAVE_GETRANDOM":          (None, "freebsd", "Linux/FreeBSD getrandom(2); this kernel has getentropy(2) instead, kept below"),
    # --- [darwin] arc4random --------------------------------------------
    "HAVE_ARC4RANDOM_STIR":    ("1",  "darwin", "real libsystem_c.dylib export here (verified nm -gU), and the SDK's own stdlib.h declares it -- leaving it undefined makes openbsd-compat.h #define arc4random_stir() an empty macro, which then mangles that very declaration ('too many arguments provided to function-like macro invocation'). Apple's own config.h has it set too"),
    "HAVE_SYS_PROCCTL_H":      (None, "freebsd", "procctl(2) is FreeBSD-only; platform-tracing.c falls back to its real no-op path"),
    "HAVE_FNMATCH_H":          (None, "gap", "the SDK does ship fnmatch.h, but openbsd-compat/fnmatch.h is EARLIER on the include path and, with this knob set, defines nothing at all -- so fnmatch.c's own <fnmatch.h> resolves to the empty shim and FNM_CASEFOLD never arrives. Undefining it makes that same compat header define the flags itself, which is what it is for"),
    "HAVE__RES_EXTERN":        (None, "gap", "no libc here exports the BIND8 `_res` resolver-state global. It IS the real Darwin answer -- /usr/lib/system/libsystem_info.dylib on a real macOS exports __res (checked with dyld_info) -- but this project's libinfo.dylib does not, and neither does its libresolv.dylib (build_libresolv.sh documents why: Apple's own shipping libresolv.9.dylib exports no _res either, it builds with -DUSE__RES_9). Undefining this knob makes openbsd-compat/getrrsetbyname.c define the global itself, which is exactly the fallback that `#ifndef HAVE__RES_EXTERN` is there for"),
    "HAVE_STRTOULL":           (None, "gap", "only libc++.dylib exports _strtoull, which is not its owner; openbsd-compat/strtoull.c supplies it. (HAVE_STRTOLL stays set -- libsystem_c.dylib really does export _strtoll.)"),
    "HAVE_READPASSPHRASE":     (None, "gap", "not exported by any dylib here; openbsd-compat/readpassphrase.c supplies it"),
    "HAVE_READPASSPHRASE_H":   (None, "gap", "same shadowing trap as HAVE_FNMATCH_H below: openbsd-compat/readpassphrase.h is earlier on the include path and defines nothing while this knob is set, so readpass.c's <readpassphrase.h> resolves to an empty shim and RPP_ECHO_ON never arrives"),
    "BIND_8_COMPAT":           ("1",  "darwin", "Apple's own config.h -- getrrsetbyname.c needs the BIND8 HEADER/res_* names out of arpa/nameser.h"),
    "HAVE_OPENPTY":            ("1",  "darwin", "real Apple Libc util/pty.c (openpty/forkpty) is compiled into this binary below -- WITHOUT it openbsd-compat/bsd-openpty.c falls into its HAVE_DEV_PTMX branch, whose tail issues UNGUARDED Solaris STREAMS ioctl(I_PUSH, \"ptem\"/\"ldterm\") calls that do not compile (and would not work) on Darwin"),
    "HAVE_UTIL_H":             ("1",  "darwin", "third_party/Libc/include/util.h (on the -idirafter path) is the real declaration of the openpty() above; Apple's own config.h sets this too"),
    "HAVE_SYS_STATVFS_H":      (None, "gap", "the SDK ships sys/statvfs.h but nothing in this userland implements statvfs(3); including it collides with openbsd-compat/bsd-statvfs.h's own struct statvfs, which is what actually gets used here (built on statfs(2))"),
    # --- [gap] real, verified gaps in this project's curated libc -----
    #     (nm -gU over libc_build/system/*.dylib -- openbsd-compat/
    #     supplies each replacement, which is what those files are for)
    "HAVE_CLOSEFROM":          (None, "gap", "not exported by any dylib here"),
    "HAVE_EXPLICIT_BZERO":     (None, "gap", "not exported by any dylib here"),
    "HAVE_REALLOCARRAY":       (None, "gap", "only libremovefile.dylib exports it -- a real bind-target hazard (DAR-217 class), so use the compat copy"),
    "HAVE_STRTONUM":           (None, "gap", "not exported by any dylib here"),
    "HAVE_POLL":               (None, "gap", "no poll(2) wrapper in this libc; openbsd-compat/bsd-poll.c builds it on select(2)"),
    "HAVE_PPOLL":              (None, "gap", "same"),
    "HAVE_PSELECT":            (None, "gap", "only libresolv.dylib exports it; bsd-pselect.c is safer than that bind"),
    "HAVE_FNMATCH":            (None, "gap", "not exported by any dylib here"),
    "HAVE_GLOB":               (None, "gap", "not exported by any dylib here"),
    "HAVE_STRPTIME":           (None, "gap", "not exported by any dylib here"),
    "HAVE_DAEMON":             (None, "gap", "not exported by any dylib here"),
    "HAVE_STATVFS":            (None, "gap", "not exported by any dylib here"),
    "HAVE_FSTATVFS":           (None, "gap", "same"),
    "HAVE_GETPEEREID":         (None, "gap", "not exported by any dylib here"),
    "HAVE_SETUTXDB":           (None, "gap", "no utmpx implementation here"),
    "HAVE_GETUTXUSER":         (None, "gap", "same"),
    "HAVE___B64_NTOP":         (None, "gap", "libresolv.dylib here exports neither __b64_ntop nor b64_ntop"),
    "HAVE___B64_PTON":         (None, "gap", "same"),
    "HAVE_UTMPX_H":            (None, "gap", "header exists in the SDK but nothing implements it here"),
    "HAVE_UTMP_H":             (None, "gap", "same"),
    "DISABLE_UTMP":            ("1",  "gap", "no utmp/utmpx implementation in this userland"),
    "DISABLE_WTMP":            ("1",  "gap", "same"),
    "DISABLE_UTMPX":           ("1",  "gap", "same"),
    "DISABLE_WTMPX":           ("1",  "gap", "same"),
    "DISABLE_LASTLOG":         ("1",  "gap", "same (FreeBSD's own config.h already has this)"),
}

src = open(base_path, errors="replace").read().splitlines()
seen = set()
out = []
out.append("/* GENERATED by tools/userland_staging/build_sshd.sh -- DO NOT EDIT. */")
out.append("/* Base: third_party/freebsd-src/crypto/openssh/config.h (real FreeBSD */")
out.append("/* release/13.2.0 autoconf output for this exact OpenSSH 9.3p1 tree),  */")
out.append("/* plus this port's override table. Each override is annotated with    */")
out.append("/* its bucket: nolib / darwin / gap / freebsd -- see the script.       */")
out.append("")

def emit(name, val, why):
    if val is None:
        out.append("/* #undef %s */\t/* [%s] */" % (name, why))
    else:
        out.append("#define %s %s\t/* [%s] */" % (name, val, why))

# Every knob that only exists to describe a libcrypto feature is false in a
# --without-openssl build. Listing ~40 of them by hand would be both noisy
# and easy to leave incomplete as the pin moves, so they are matched by
# name instead -- the rule is simply "no libcrypto, so no libcrypto
# feature". This is not cosmetic: ssh-ecdsa-sk.c picks its real body vs its
# stub on `#ifndef OPENSSL_HAS_ECC` (not on WITH_OPENSSL), and
# cipher-chachapoly-libcrypto.c on `#if defined(HAVE_EVP_CHACHA20)`, so
# leaving either set makes those two files #include <openssl/*.h> and fail
# to compile even with WITH_OPENSSL off.
OPENSSL_KNOB = re.compile(
    r'^(OPENSSL_|HAVE_OPENSSL|HAVE_EVP_|HAVE_BN_|HAVE_DSA_|HAVE_RSA_|HAVE_EC_|'
    r'HAVE_ECDSA_|HAVE_DH_|HAVE_HMAC_|HAVE_DES_|HAVE_CRYPTO_|HAVE_SSL_|'
    r'HAVE_ERR_|HAVE_X509)')

def openssl_knob(name):
    return bool(OPENSSL_KNOB.match(name)) and name not in OVERRIDES

for line in src:
    s = line.strip()
    m = re.match(r'^#define\s+(\w+)(?:\s+(.*))?$', s)
    if not m:
        m2 = re.match(r'^/\*\s*#undef\s+(\w+)\s*\*/$', s)
        if m2 and m2.group(1) in OVERRIDES:
            n = m2.group(1)
            seen.add(n)
            v, b, why = OVERRIDES[n]
            emit(n, v, "%s: %s" % (b, why))
            continue
        out.append(line)
        continue
    n = m.group(1)
    if n in OVERRIDES:
        seen.add(n)
        v, b, why = OVERRIDES[n]
        emit(n, v, "%s: %s" % (b, why))
    elif openssl_knob(n):
        emit(n, None, "nolib: libcrypto feature knob, no OpenSSL in this build")
    else:
        out.append(line)

missing = [n for n in OVERRIDES if n not in seen]
if missing:
    out.append("")
    out.append("/* Overrides for knobs the base config.h never mentions at all: */")
    for n in sorted(missing):
        v, b, why = OVERRIDES[n]
        emit(n, v, "%s: %s" % (b, why))
out.append("")
open(out_path, "w").write("\n".join(out) + "\n")
print("config.h: %d overrides applied (%d added that the base never listed)"
      % (len(OVERRIDES), len(missing)))
PY_EOF

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-implicit-function-declaration -Wno-deprecated-declarations \
    -DHAVE_CONFIG_H -D_LIBC_NO_FEATURE_VERIFICATION \
    `# -ffreestanding sets __STDC_HOSTED__=0, so clang's own <limits.h> skips` \
    `# its #include_next of the SDK's -- and SSIZE_MAX lives three headers` \
    `# down that chain, in the SDK's arm/limits.h:97 as exactly this` \
    `# "#define SSIZE_MAX LONG_MAX". openbsd-compat/glob.c needs it.` \
    -DSSIZE_MAX=LONG_MAX \
    -I "$SRCTREE" -I "$SRCTREE/openbsd-compat" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall" -idirafter "$XNU/libkern" \
    -idirafter "$LIBC/include" -idirafter "$LIBC/darwin" -idirafter "$LIBC" \
    -idirafter "$PTHREAD/include" -idirafter "$PTHREAD/private" \
    -idirafter "$PLATFORM/private" -idirafter "$PLATFORM/include")

ok=0
fail=0
mkdir -p "$SOUT/compat"
compile() {
    local src="$1"; shift
    local subdir="${SSHD_COMPILE_SUBDIR:-}"
    local base
    base=$(basename "$src" .c)
    local out="$SOUT/${subdir}${base}.o"
    if "$CLANG" "${CFLAGS[@]}" "$@" "$src" -o "$out" 2>"$SOUT/${subdir}${base}.err"; then
        ok=$((ok + 1))
        rm -f "$SOUT/${subdir}${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: ${subdir}${base} -- $(grep -m1 'error:' "$SOUT/${subdir}${base}.err")"
    fi
}

# Real upstream Makefile.in object lists, verbatim (LIBOPENSSH_OBJS +
# LIBSSH_OBJS + SSHDOBJS). Files that belong to a disabled feature are
# NOT dropped from the list -- they are #ifdef'd to empty translation
# units by their own source, which is how upstream builds them too.
LIBSSH_SRCS="ssh_api ssherr sshbuf sshkey sshbuf-getput-basic sshbuf-misc \
    sshbuf-getput-crypto krl bitmap \
    authfd authfile canohost channels cipher cipher-aes cipher-aesctr \
    cleanup compat fatal hostfile log match moduli nchan packet \
    readpass ttymodes xmalloc addr addrmatch atomicio dispatch mac misc utf8 \
    monitor_fdpass rijndael ssh-dss ssh-ecdsa ssh-ecdsa-sk ssh-ed25519-sk \
    ssh-rsa dh msg progressmeter dns entropy gss-genr umac umac128 \
    ssh-pkcs11 smult_curve25519_ref poly1305 chacha cipher-chachapoly \
    cipher-chachapoly-libcrypto ssh-ed25519 digest-openssl digest-libc \
    hmac ed25519 hash kex kexdh kexgex kexecdh kexc25519 kexgexc kexgexs \
    kexsntrup761x25519 sntrup761 kexgen sftp-realpath platform-pledge \
    platform-tracing platform-misc sshbuf-io \
    `# FreeBSD's secure/lib/libssh/Makefile carries this as its own` \
    `# 'SRCS+= ssh-sk-client.c' line, and upstream's Makefile.in as` \
    `# $(SKOBJS) -- either way sshd needs it even with security keys off:` \
    `# sshkey.c:2100 calls sshsk_sign() from an UNGUARDED` \
    `# 'if (sshkey_is_sk(key))' branch, and this file's own` \
    `# '#ifndef ENABLE_SK -> return SSH_ERR_FEATURE_UNSUPPORTED' body is` \
    `# what makes that branch resolve at all.` \
    ssh-sk-client"

SSHD_SRCS="sshd auth-rhosts auth-passwd audit audit-bsm audit-linux platform \
    sshpty sshlogin servconf serverloop auth auth2 auth-options session \
    auth2-chall groupaccess auth-bsdauth auth2-hostbased auth2-kbdint \
    auth2-none auth2-passwd auth2-pubkey auth2-pubkeyfile monitor \
    monitor_wrap auth-krb5 auth2-gss gss-serv gss-serv-krb5 loginrec \
    auth-pam auth-shadow auth-sia srclimit sftp-server sftp-common \
    sandbox-null sandbox-rlimit sandbox-systrace sandbox-darwin \
    sandbox-seccomp-filter sandbox-capsicum sandbox-pledge sandbox-solaris \
    uidswap"

# openbsd-compat/Makefile.in's own OPENBSD + COMPAT + PORTS lists, verbatim.
COMPAT_SRCS="arc4random arc4random_uniform base64 basename bcrypt_pbkdf \
    bindresvport blowfish daemon dirname explicit_bzero fmt_scaled freezero \
    fnmatch getcwd getgrouplist getopt_long getrrsetbyname glob inet_aton \
    inet_ntoa inet_ntop md5 memmem mktemp pwcache readpassphrase \
    reallocarray recallocarray rresvport setenv setproctitle sha1 sha2 \
    sigact strcasestr strlcat strlcpy strmode strndup strnlen strptime \
    strsep strtoll strtonum strtoull strtoul timingsafe_bcmp vis \
    bsd-asprintf bsd-closefrom bsd-cygwin_util bsd-err bsd-flock \
    bsd-getentropy bsd-getline bsd-getpagesize bsd-getpeereid bsd-malloc \
    bsd-misc bsd-nextstep bsd-openpty bsd-poll bsd-pselect bsd-setres_id \
    bsd-signal bsd-snprintf bsd-statvfs bsd-timegm bsd-waitpid \
    fake-rfc2553 getrrsetbyname-ldns kludge-fd_set openssl-compat \
    libressl-api-compat xcrypt \
    port-aix port-irix port-linux port-prngd port-solaris port-net port-uw"

SSHD_COMPILE_SUBDIR="core/"
mkdir -p "$SOUT/core"
for f in $LIBSSH_SRCS; do
    compile "$SRCTREE/$f.c"
done

# ssh-keygen(1)'s own two extra sources (FreeBSD's
# secure/usr.bin/ssh-keygen/Makefile: "SRCS= ssh-keygen.c sshsig.c
# ssh-sk-client.c", the third already in the libssh list above). Built
# because this system needs to be able to MAKE its ed25519 host key on
# the device -- baking a private host key into the repo or the image
# build would be both a real secret in version control and the same key
# on every board.
SSHD_COMPILE_SUBDIR="keygen/"
mkdir -p "$SOUT/keygen"
for f in ssh-keygen sshsig; do
    compile "$SRCTREE/$f.c"
done

# ssh(1) itself -- FreeBSD's secure/usr.bin/ssh/Makefile's own SRCS list
# (gss-genr.c is already in the libssh list above). Built for the same
# reason ssh-keygen is: without a client on the device there is no way to
# test this sshd end-to-end over lo0 inside QEMU, and "it linked" is not
# the bar this project holds a port to.
SSHD_COMPILE_SUBDIR="ssh/"
mkdir -p "$SOUT/ssh"
for f in ssh readconf clientloop sshtty sshconnect sshconnect2 mux; do
    case "$f" in
    clientloop)
        # clientloop.c:384 calls system(3) for `ssh -o PermitLocalCommand`.
        # The iPhoneOS SDK marks system() __API_UNAVAILABLE(ios) -- an
        # SDK POLICY annotation (App Store rules), not a statement that
        # the function is missing from the C library: Apple still ships
        # its real implementation, and this project has the real source
        # (Libc/stdlib/FreeBSD/system.c, compiled in below). Neutralising
        # the annotation for this one translation unit is therefore
        # honest, and narrower than the alternatives -- the same
        # "SDK-annotation, not a real gap" call build_bootp_dhcp_engine.sh
        # already documents for its own __API_UNAVAILABLE_PLATFORM_*
        # overrides.
        compile "$SRCTREE/$f.c" -include sshd_ios_prohibited_override.h
        ;;
    *)
        compile "$SRCTREE/$f.c"
        ;;
    esac
done

SSHD_COMPILE_SUBDIR="sshd/"
mkdir -p "$SOUT/sshd"
for f in $SSHD_SRCS; do
    case "$f" in
    sshd)
        # sshd.c:2124 `madvise(NULL, 0, MADV_PROTECT)` is a FreeBSD-LOCAL
        # patch, not upstream OpenSSH: it is absent from Apple's own
        # OpenSSH-240.40.1 sshd.c (third_party/OpenSSH/openssh/sshd.c,
        # checked -- zero hits), present unguarded here, and MADV_PROTECT
        # ("do not kill this process to reclaim swap") is a FreeBSD-only
        # madvise(2) advice with no Darwin counterpart at all
        # (third_party/xnu-7195/bsd/sys/mman.h defines MADV_NORMAL,
        # _WILLNEED, _DONTNEED, _FREE, _ZERO_WIRED_PAGES, _FREE_REUSABLE,
        # _FREE_REUSE, _CAN_REUSE, _PAGEOUT -- no _PROTECT).
        #
        # Compiled as MADV_NORMAL (0) rather than patched out: that turns
        # the call into a real zero-length madvise(NULL, 0, MADV_NORMAL)
        # no-op instead of removing a line from unmodified third_party
        # source, and the advice was only ever advisory here anyway --
        # sshd's own code on the very next line treats any failure as a
        # debug() message and carries on. The real behaviour lost is
        # FreeBSD's swap-pressure kill protection for the listener, which
        # this kernel has no mechanism for in the first place.
        compile "$SRCTREE/$f.c" -DMADV_PROTECT=MADV_NORMAL
        ;;
    *)
        compile "$SRCTREE/$f.c"
        ;;
    esac
done
SSHD_COMPILE_SUBDIR="compat/"
for f in $COMPAT_SRCS; do
    compile "$SRCTREE/openbsd-compat/$f.c"
done
SSHD_COMPILE_SUBDIR=

# Real openpty(3)/forkpty(3): Apple's own Libc/util/pty.c, declared by
# Apple's own Libc/include/util.h (both already pinned here). This project
# has no libutil port -- and Apple's separate libutil repo
# (third_party/libutil) does NOT contain pty.c at all, checked, so Libc is
# the real home of these two on Darwin. Same real-Apple-source-first
# direction build_vim.sh already took for grantpt/posix_openpt/ptsname/
# unlockpt out of Libc/stdlib/grantpt.c (which is what this pty.c itself
# calls), and the reason config.h above can set HAVE_OPENPTY.
SSHD_COMPILE_SUBDIR="compat/"
compile "$LIBC/util/pty.c"
SSHD_COMPILE_SUBDIR=

# ---------------------------------------------------------------------
# Real libc pieces this project's dylibs do not provide.
#
# Every symbol below was checked against a real `nm -gU` sweep of
# libc_build/system/*.dylib first. Most are absent outright; the rest are
# present ONLY as an incidental export of a dylib that has no business
# owning them (libCoreFoundation.dylib exports _accept/_listen/
# _socketpair/_getrlimit, libncurses.dylib exports _chdir/_link,
# libSystemConfiguration.dylib exports _setuid/_setgid/_dup2 ...).
# Binding sshd to any of those is the exact DAR-217 hazard CLAUDE.md
# warns about -- a clean link that aborts at dyld load, or, worse here, a
# working link that drags CoreFoundation+ICU into a security daemon. So
# sshd carries its own real objects for all of them, the same way
# build_vim.sh and build_ping.sh do.
#
# Real Apple source in every case (source-priority rule), no hand-written
# reimplementations.
SSHD_COMPILE_SUBDIR="extra/"
mkdir -p "$SOUT/extra"

# Apple's FreeBSD-derived Libc files (stdlib/FreeBSD/, gen/FreeBSD/) open
# with `#include "namespace.h"`, Apple's own real symbol-namespacing
# header, which lives in Libc/fbsdcompat/ rather than beside them -- the
# same real include this project's other Libc-FreeBSD compiles already
# add (build_launchd_syscalls.sh's CFLAGS_LAUNCHD_LIBC set,
# build_vim.sh's FBSD_TIME_INC).
FBSD_INC=(-I "$LIBC/fbsdcompat")

# getopt(3) + optarg/optind/optreset -- sshd.c's own argument parsing.
compile "$LIBC/stdlib/FreeBSD/getopt.c" "${FBSD_INC[@]}"
# login_tty(3) -- sshpty.c's pty_make_controlling_tty(), and Apple's own
# pty.c (compiled below) calls it too.
compile "$LIBC/util/login_tty.c"
# killpg(3) -- session.c's session_close()/serverloop.c signal delivery.
compile "$LIBC/compat-43/FreeBSD/killpg.c"
# raise(3) -- openbsd-compat/bsd-signal.c and sshd.c's SIGHUP re-exec path.
compile "$LIBC/gen/raise.c"
# usleep(3) -- sshd.c's rate-limiting sleep in server_accept_loop().
compile "$LIBC/gen/FreeBSD/usleep.c" "${FBSD_INC[@]}" \
    `# namespace.h renames nanosleep -> _nanosleep (Apple's internal,` \
    `# non-cancellable spelling, which real Libc defines alongside the` \
    `# public one). Only the public _nanosleep asm symbol exists here` \
    `# (libsystem_kernel.dylib), so undo that one rename for this TU` \
    `# rather than inventing a second nanosleep.` \
    -D_nanosleep=nanosleep
# memset_s(3) -- explicit_bzero.c's real preferred backend.
compile "$LIBC/string/NetBSD/memset_s.c"
# realpath(3) -- misc.c/sftp-server.c. No -Wl,-alias is needed for the
# _realpath$DARWIN_EXTSN name every call site actually references: this
# TU compiles straight to that suffixed symbol (verified with nm on the
# built object -- `T _realpath$DARWIN_EXTSN` is its ONLY definition),
# because Apple's own <stdlib.h> __DARWIN_ALIAS-es realpath at the
# declaration this same file includes. An alias pointing at a plain
# _realpath that nothing defines just fails the link, which is how this
# was found.
compile "$LIBC/stdlib/FreeBSD/realpath.c" "${FBSD_INC[@]}"
# popen(3)/pclose(3) -- sshd.c's AuthorizedKeysCommand/sshrc handling.
# Needs this project's own shared libc_build_prototypes.h force-include
# (the only file here that does): its THREAD_LOCK()/THREAD_UNLOCK() reach
# __isthreaded, which Apple declares in libc_private.h -- not part of the
# public header set these compiles see. That header is exactly where this
# project already keeps the real __isthreaded declaration and the
# matching FLOCKFILE/FUNLOCKFILE definitions (DAR-227); libsystem_c.dylib
# exports the real ___isthreaded global it resolves to.
compile "$LIBC/gen/FreeBSD/popen.c" "${FBSD_INC[@]}" -include libc_build_prototypes.h \
    `# same namespace.h un-rename as usleep.c below: _wait4 -> wait4,` \
    `# the real public symbol libsystem_c.dylib exports.` \
    -D_wait4=wait4
# clock_gettime(3) -- misc.c's monotime()/monotime_tv(), i.e. every
# rekey/keepalive/login-grace timer sshd has. The ONLY dylib exporting it
# today is libunwind.dylib, which is plainly not its owner.
compile "$LIBC/gen/clock_gettime.c"
# __strlcat_chk -- the real _FORTIFY_SOURCE wrapper behind every
# strlcat() in kex.c/match.c/misc.c/servconf.c/loginrec.o. Pairs with
# $OUT/chk_fail.o (built by build_libmalloc.sh), listed at link time.
compile "$LIBC/secure/strlcat_chk.c"
# utimensat(2) -- sftp-server.c's process_setstat(). NOT in
# syscalls.master at all (checked: zero hits for "utimens") -- on Darwin
# it is a libsyscall-level wrapper, and this is that real wrapper,
# third_party/xnu-7195/libsyscall/wrappers/utimensat.c.
compile "$XNU/libsyscall/wrappers/utimensat.c"
# posix_openpt/grantpt/unlockpt/ptsname_r -- Apple's real Libc
# implementation, the same file build_vim.sh already compiles for the
# same four functions (see its own header for the arc4random_compat_include
# chain this needs).
compile "$LIBC/stdlib/grantpt.c" -I arc4random_compat_include
# NO_SYSCALL_STUB syscalls sshd needs: accept/listen/socketpair/setlogin/
# fchmod/rename. See this file's own header for the per-syscall citations.
compile sshd_syscalls.c
# posix_spawn(3) + its file-actions API -- Apple's real libsyscall
# implementation (libsyscall/wrappers/spawn/posix_spawn.c). Pulled in by
# Apple's own popen.c above, which spawns rather than forks; no dylib
# here exports any of the five symbols it needs.
compile "$XNU/libsyscall/wrappers/spawn/posix_spawn.c" \
    -I "$XNU/libsyscall/wrappers/spawn" -I "$XNU/libsyscall/mach"
# machine/cpu_capabilities.h: the two libsyscall time wrappers below read
# the commpage through `#include <machine/cpu_capabilities.h>`, an
# installed-SDK-style path whose real content in this checkout is
# osfmk/arm/cpu_capabilities.h. Same forwarding-shim pattern
# build_libc_locale.sh/build_dyld_core.sh already use for the
# System/machine/ spelling of the very same header.
#
# -DPRIVATE on those two compiles is load-bearing, not hygiene: the
# ENTIRE osfmk/arm/cpu_capabilities.h body sits behind a single
# `#ifdef PRIVATE` at its line 28, so without it the header includes
# cleanly and defines nothing, and every _COMM_PAGE_* constant reads as
# an undeclared identifier. Real Apple builds libsyscall with PRIVATE
# defined for exactly this reason; several scripts here already do the
# same (build_bash.sh, build_chrony.sh, build_configd.sh).
mkdir -p "$OUT/sshd_compat_include/machine"
cat > "$OUT/sshd_compat_include/machine/cpu_capabilities.h" <<'CAPS_EOF'
/* Forwarding shim -- real content is $XNU/osfmk/arm/cpu_capabilities.h,
 * reachable as <arm/cpu_capabilities.h> via this build's own -idirafter
 * osfmk. Generated by build_sshd.sh; do not edit. */
#include <arm/cpu_capabilities.h>
CAPS_EOF

# mach_continuous_time()/mach_continuous_approximate_time()/
# mach_boottime_usec()/mach_approximate_time() -- real Apple libsyscall
# wrappers, needed by clock_gettime.c above for CLOCK_MONOTONIC_RAW and
# CLOCK_BOOTTIME. libdispatch.dylib does export _mach_continuous_time,
# but it is no more that symbol's owner than libunwind is
# clock_gettime's, and binding a security daemon to libdispatch for one
# timer call would drag the whole dispatch runtime in.
compile "$XNU/libsyscall/wrappers/mach_continuous_time.c" -I "$XNU/libsyscall/mach" \
    -I "$OUT/sshd_compat_include" -DPRIVATE
# mach_boottime_usec() -- clock_gettime.c's CLOCK_BOOTTIME branch; its
# own real libsyscall wrapper, same treatment as the two above.
compile "$XNU/libsyscall/wrappers/mach_boottime.c" -I "$XNU/libsyscall/mach" \
    -I "$OUT/sshd_compat_include" -DPRIVATE
compile "$XNU/libsyscall/wrappers/mach_approximate_time.c" -I "$XNU/libsyscall/mach" \
    -I "$OUT/sshd_compat_include" -DPRIVATE
# __private_getcwd() -- Apple's real Libc getcwd.c, realpath.c's own
# internal dependency (it is a Libc-private entry point, not the public
# getcwd(3) libsystem_c.dylib already exports).
compile "$LIBC/gen/FreeBSD/getcwd.c" "${FBSD_INC[@]}"
# system(3) -- ssh(1)'s clientloop.c PermitLocalCommand path. Apple's
# own real Libc implementation; no dylib here exports it (the iPhoneOS
# SDK does not ship it as a usable symbol at all), and it is built on the
# real posix_spawn compiled just above.
compile "$LIBC/stdlib/FreeBSD/system.c" "${FBSD_INC[@]}" -include libc_build_prototypes.h \
    `# same namespace.h un-renames as popen.c/usleep.c above: _sigaction` \
    `# and _wait4 are Apple's internal spellings, and only the public` \
    `# symbols exist here (libsystem_c.dylib exports _sigaction/_wait4).` \
    -D_sigaction=sigaction -D_wait4=wait4
# getrlimit(2) -- NO_SYSCALL_STUB #194, sshd.c's own rlimit_nofile
# sizing and session.c. This project's already-proven single-function
# wrapper, reused rather than re-hand-writing a second copy of it.
compile xargs_getrlimit_compat.c
# crypt(3) -> this project's own real li_crypt() (same hash format
# tools/pwtool writes and login(1) verifies).
compile sshd_crypt_compat.c
compile ../libinfo_custom/crypt_des.c -I ../libinfo_custom
SSHD_COMPILE_SUBDIR=

echo "sshd: $ok compiled, $fail gaps (see $SOUT/*.err, $SOUT/compat/*.err and $SOUT/extra/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

# --- real raw syscall stubs -------------------------------------------
# Plain (NOT NO_SYSCALL_STUB) entries in
# third_party/xnu-7195/bsd/kern/syscalls.master, generated with Apple's
# own libsyscall/xcodescripts/create-syscalls.pl -- real Apple asm, not
# hand-written. Same invocation build_simple_cmds.sh/build_vim.sh use.
# Kept in sshd's own object dir because build_simple_cmds.sh guards its
# generation block behind a sentinel file and would not regenerate for a
# newly-added name.
#
#   fchown #123    sshpty.c pty_setowner(), sftp-server.c process_fsetstat()
#   fchownat #468  openbsd-compat/bsd-misc.c
#   getpgid #151   session.c / serverloop.c process-group handling
#   getppid #39    sshd.c re-exec and monitor plumbing
#   getsid #310    platform.c platform_pre_listen()
#   getsockopt #118 misc.c get_sock_af()/set_nodelay(), channels.c
#   readv #120     atomicio.c atomiciov() -- every packet read sshd does
#   setsid #147    sshd.c daemonise + session.c do_child()
#   shutdown #134  packet.c ssh_packet_close(), channels.c
#   truncate #200  sftp-server.c process_setstat()
#   fsetattrlist #229  Apple's realpath.c/getcwd.c attribute path
#   setattrlistat #524  utimensat.c's real backend
#   getrusage #117      Apple's popen.c wait4 bookkeeping
SSHD_SYS_STUBS="_fchown _fchownat _getpgid _getppid _getsid _getsockopt \
    _readv _setsid _shutdown _truncate _fsetattrlist _setattrlistat _getrusage"
need_stubs=0
for sy in $SSHD_SYS_STUBS; do
    [ -f "$SOUT/syscalls/sys_${sy}.o" ] || need_stubs=1
done
if [ "$need_stubs" -eq 1 ]; then
    mkdir -p "$SOUT/syscalls" "$OUT/sys_asm_compat/mach/arm64"
    if [ ! -f "$OUT/sys_asm_compat/mach/arm64/asm.h" ]; then
        cat > "$OUT/sys_asm_compat/mach/arm64/asm.h" <<'ASM_EOF'
/* SYS.h expects the installed-SDK-style path mach/arm64/asm.h; real
 * content is osfmk/arm64/asm.h in this checkout -- same forwarding-shim
 * pattern build_simple_cmds.sh/build_bash.sh/build_vim.sh already use. */
#include <arm64/asm.h>
ASM_EOF
    fi
    SYS_TMPGEN="$(mktemp -d)"
    trap 'rm -rf "$SYS_TMPGEN"' EXIT
    ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
        "$XNU/bsd/kern/syscalls.master" \
        "$XNU/libsyscall/custom" \
        "$XNU/libsyscall/Platforms" \
        iPhoneOS \
        "$SYS_TMPGEN" >/dev/null
    for sy in $SSHD_SYS_STUBS; do
        "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
            -I"$SYS_TMPGEN" -I"$OUT/sys_asm_compat" -I"$XNU/libsyscall/custom" \
            -I"$XNU/libsyscall/mach" -I"$XNU/osfmk" \
            "$SYS_TMPGEN/${sy}.s" -o "$SOUT/syscalls/sys_${sy}.o" \
            || { echo "error: $sy raw syscall stub failed" >&2; exit 1; }
    done
    rm -rf "$SYS_TMPGEN"
    trap - EXIT
fi

# ptrace(2) is NOT create-syscalls.pl output: syscalls.master has it as a
# plain entry, but libsyscall ships a HAND-WRITTEN
# libsyscall/custom/__ptrace.s for it (it zeroes errno before the trap,
# which the generated shape cannot express), so the generator emits
# nothing and the real Apple asm is assembled directly here.
# platform-tracing.c's platform_disable_tracing() ->
# ptrace(PT_DENY_ATTACH, 0, 0, 0) is sshd's only call site.
#
# That file defines ___ptrace (double underscore). The plain _ptrace the
# caller wants is the SAME function on real Darwin, not a wrapper around
# it -- verified against the running system, not assumed:
#   $ nm -gU /usr/lib/system/libsystem_kernel.dylib | grep -i ptrace
#   0000000000008cc8 T ___ptrace
#   0000000000008cc8 T _ptrace        <- identical address
# so the -Wl,-alias on the link line below reproduces exactly what Apple
# ships, with the alias base local to this link (what ld64 needs).
if [ ! -f "$SOUT/syscalls/custom___ptrace.o" ]; then
    mkdir -p "$SOUT/syscalls"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
        -I"$OUT/sys_asm_compat" -I"$XNU/libsyscall/custom" \
        -I"$XNU/libsyscall/mach" -I"$XNU/osfmk" \
        "$XNU/libsyscall/custom/__ptrace.s" -o "$SOUT/syscalls/custom___ptrace.o" \
        || { echo "error: __ptrace.s failed to assemble" >&2; exit 1; }
fi

# __thread_selfusage: same story as __ptrace above -- hand-written Apple
# asm in libsyscall/custom/, not generator output. clock_gettime.c's
# CLOCK_THREAD_CPUTIME_ID branch calls it.
if [ ! -f "$SOUT/syscalls/custom___thread_selfusage.o" ]; then
    mkdir -p "$SOUT/syscalls"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
        -I"$OUT/sys_asm_compat" -I"$XNU/libsyscall/custom" \
        -I"$XNU/libsyscall/mach" -I"$XNU/osfmk" \
        "$XNU/libsyscall/custom/__thread_selfusage.s" \
        -o "$SOUT/syscalls/custom___thread_selfusage.o" \
        || { echo "error: __thread_selfusage.s failed to assemble" >&2; exit 1; }
fi

# Stubs from the build_simple_cmds.sh tier, reused rather than
# regenerated (all real create-syscalls.pl output, same generator):
#   chdir chown chroot dup dup2 fsync futimes link readlink setgid
#   setgroups setuid symlink umask utimes fchmodat
REUSED_STUBS=""
for sy in chdir chown chroot dup dup2 fsync futimes link readlink setgid \
          setgroups setuid symlink umask utimes fchmodat; do
    o="$OUT/simple_obj/sys__${sy}.o"
    if [ ! -f "$o" ]; then
        echo "error: $o not found -- run build_simple_cmds.sh first" >&2
        exit 1
    fi
    REUSED_STUBS="$REUSED_STUBS $o"
done
for o in "$OUT/chk_fail.o" "$OUT/cerror_stub.o"; do
    if [ ! -f "$o" ]; then
        echo "error: $o not found -- run build_libmalloc.sh (chk_fail.o) /" >&2
        echo "       build_simple_cmds.sh (cerror_stub.o) first" >&2
        exit 1
    fi
done

# libssh + openbsd-compat + the real-libc extras: the shared half, what
# FreeBSD builds as its own libssh.a. sshd's and ssh-keygen's OWN objects
# are deliberately NOT in here -- both define main(), and an archive
# holding two of those would resolve crt.c's _main reference to whichever
# member the linker happened to scan first.
ar rcs "$OUT/libsshcore.a" "$SOUT"/core/*.o "$SOUT"/compat/*.o "$SOUT"/extra/*.o
echo "wrote $OUT/libsshcore.a"

# --- link -------------------------------------------------------------
# DYNAMIC LINK (DAR-314 standing rule): every shipped binary is dynamic
# except tools/pwtool. Same real ld64-609-crash-under-fixup_chains
# workaround, Csu start.s/crt.c entry point and -B<newer ld> as every
# other dynamic executable here (see build_bash.sh's header for the trail).
#
# Dylib order is deliberate, per CLAUDE.md's bind_target_audit rule --
# ld64 records each bind against whichever LISTED dylib it reached the
# symbol through, and dyld-940 then searches only that image:
#   libsystem_c/pthread/malloc/platform/kernel/libdyld  the real libc core
#   libinfo     real getpwnam/getgrnam/getgrouplist/getaddrinfo (flat-file
#               auth, the same path login(1)/su already use)
#   libresolv   real res_* for openbsd-compat/getrrsetbyname.c
#   libopenpam  _initgroups + _syslog/_openlog/_closelog. sshd is built
#               WITHOUT PAM, so this is not a PAM dependency: it is that
#               dylib's real, Apple-sourced Libc/gen/oldsyslog.c
#               (build_openpam.sh compiles it) and the copy of
#               initgroups() DAR-229 deliberately designated as THE
#               exported one for this project's auth path -- it
#               unexported libinfo.dylib's own competing copy at the same
#               time, specifically keeping "the established, real-auth-
#               path-relied-upon side" (login/su) exported. sshd is that
#               same auth path, so it binds to the same one instead of
#               growing a third, local copy.
LINK_DYLIBS=("$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libdyld.dylib" \
    "$OUT/system/libinfo.dylib" "$OUT/system/libresolv.dylib" \
    "$OUT/system/libopenpam.dylib" "$OUT/system/libSystem.B.dylib")
for d in "${LINK_DYLIBS[@]}"; do
    if [ ! -f "$d" ]; then
        echo "error: $d not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

# Both binaries link identically apart from their own objects.
link_ssh_binary() {
    local outmacho="$1"; shift
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$outmacho" \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$@" \
    "$OUT/libsshcore.a" \
    "$OUT/chk_fail.o" "$OUT/cerror_stub.o" \
    $REUSED_STUBS \
    "$SOUT/syscalls"/*.o \
    "${LINK_DYLIBS[@]}" \
    `# libsystem_c.dylib exports _strftime$UNIX2003 but never the bare` \
    `# _strftime (confirmed nm -gU) -- same real alias, for the same real` \
    `# reason, as build_vim.sh's and build_cron.sh's.` \
    -Wl,-alias,'_strftime$UNIX2003',_strftime \
    "${EXTRA_LDFLAGS[@]}" \
    `# ...and real Apple's own ___ptrace/_ptrace same-address pair (see` \
    `# the __ptrace.s block above for the nm evidence).` \
    -Wl,-alias,___ptrace,_ptrace \
    2>"$OUT/${outmacho%.macho}_link.err" && echo "wrote $outmacho" \
    || { echo "LINK FAILED, see $OUT/${outmacho%.macho}_link.err:"; \
         tail -60 "$OUT/${outmacho%.macho}_link.err"; exit 1; }

    # Real dynamic-executable assertions (DAR-314).
    local bad=0
    otool -l "$outmacho" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
        echo "error: $outmacho: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
    otool -l "$outmacho" | grep -q "cmd LC_LOAD_DYLIB" || {
        echo "error: $outmacho: no LC_LOAD_DYLIB" >&2; bad=1; }
    otool -l "$outmacho" | grep -q "cmd LC_MAIN" || {
        echo "error: $outmacho: no LC_MAIN" >&2; bad=1; }
    [ "$bad" -eq 0 ] && echo "$outmacho: real dynamic-executable assertions OK"

    # Hard gate, same as build_syslogd.sh: a clean link does NOT prove the
    # two-level binds resolve (DAR-217).
    ./bind_target_audit.sh "$outmacho" || {
        echo "error: $outmacho failed bind_target_audit.sh" >&2; exit 1; }
}

EXTRA_LDFLAGS=()
link_ssh_binary sshd.macho "$SOUT"/sshd/*.o
link_ssh_binary ssh-keygen.macho "$SOUT"/keygen/*.o

# ssh(1) only: Apple's real Libc system.c compiles to ___system (its
# namespace.h-renamed internal name, confirmed with nm), and real Libc
# publishes the public spelling as an alias onto it -- __DARWIN_ALIAS_C
# in stdlib.h. Reproduced here rather than hand-writing a wrapper. Not on
# the other two links: neither sshd nor ssh-keygen references system(3),
# so system.o is never pulled out of libsshcore.a for them and an alias
# whose base is absent is a hard link error (which is how the
# _realpath alias above was found to be unnecessary).
EXTRA_LDFLAGS=(-Wl,-alias,___system,_system)
link_ssh_binary ssh.macho "$SOUT"/ssh/*.o
EXTRA_LDFLAGS=()
