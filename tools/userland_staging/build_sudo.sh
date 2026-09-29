#!/bin/bash
# Builds sudo.macho: real, unmodified sudo-project/sudo v1.9.5p2
# (third_party/sudo, pinned by setup_third_party.sh; DAR-340, DAR-329's
# "user management commands" parent). Apple/FreeBSD have no source for
# sudo at all (see setup_third_party.sh's SUDO block) so this is the
# tier-3 upstream in CLAUDE.md's source-priority rule.
#
# STATUS (2026-09-28): compiles all 173 objects with zero errors (also with
# -Werror=implicit-function-declaration live) and LINKS against the current
# libc_build dylibs; bind_target_audit.sh, dead_stub_audit.sh and
# weak_def_audit.sh all pass on that image (checked against a scratch
# output path, libc_build untouched). NEVER BOOTED -- no QEMU run has
# happened, and sudo has never executed. Every answer that could be wrong is
# listed in sudo_compat/sudo_config_darwin.def with a tag. Gaps closed by the
# first real compile/link (each is commented at its fix site): PACKAGE_*,
# PUTENV_CONST, GETGROUPS_T=gid_t, _PATH_LDAP_*, SUDOERS_LOG_CLIENT (+
# lib/logsrv), getspwuid.c, def_data.c is #included not compiled, -include
# sys/stat.h/signal.h, sudo_utmp_compat.c, extra syscall stubs/aliases,
# libinfo+libresolv on the link line.
#
# Build shape (all decisions here are deliberate, not accidents):
#   * STATIC sudoers plugin (STATIC_SUDOERS_PLUGIN): the sudoers policy
#     plugin is linked into the one sudo executable and registered by
#     src/preload.c ("sudoers.so" -> sudoers_policy/sudoers_io/
#     sudoers_audit), so no plugin .so or dlopen of it is needed. The
#     BINARY is still a normal dynamic Mach-O (CLAUDE.md standing rule
#     DAR-314: only pwtool may be fully static).
#   * NO PAM: plugins/sudoers/auth/passwd.c (getpwnam + crypt(3)) is the
#     only auth backend (see DAR-340's scoping notes; passwd.c is the
#     default backend when nothing else is configured, configure.ac:253
#     `AUTH_DEF=passwd`). This makes the port independent of
#     DAR-339's static-PAM machinery.
#   * NO bison/flex: gram.c/toke.c are pre-generated and shipped in the
#     tag's git tree.
#   * NO LDAP/SSSD/BSM-audit/openssl/zlib/selinux/noexec/
#     visudo/cvtsudoers/sudoreplay -- none needed for a minimal build.
#   * config.h / pathnames.h are GENERATED from upstream's own
#     config.h.in / pathnames.h.in by gen_sudo_config_h.sh plus the two
#     checked-in .def files under sudo_compat/. There is no configure run.
#
# Prerequisites: the same dylib set su uses (see the check loop below).
# Read build_su.sh's header before touching this one -- the link recipe,
# the syscall stub objects, the locale support objects, the err/warn
# aliases and the bind-audit gate are copied from it for the reasons
# documented there (DAR-217/DAR-169/DAR-314).
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
SUDO="../../third_party/sudo"
OUT="libc_build"
OBJ="$OUT/sudo_obj"
GEN="$OUT/sudo_gen"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$SUDO/src/sudo.c" ]; then
    echo "error: $SUDO/src/sudo.c not found -- run setup_third_party.sh (third_party/sudo)" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a \
           libc_locale_for_stdio.a liblaunchd_syscalls.a libmach_ipc.a libc_regex.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

rm -rf "$OUT/sudo_obj" "$OUT/sudo_gen"
mkdir -p "$OBJ" "$GEN"

# ---- generated headers/sources (no configure run) ------------------------
./gen_sudo_config_h.sh "$SUDO/config.h.in"    sudo_compat/sudo_config_darwin.def    "$GEN/config.h"
./gen_sudo_config_h.sh "$SUDO/pathnames.h.in" sudo_compat/sudo_pathnames_darwin.def "$GEN/pathnames.h"

# src/sudo_usage.h: sed of upstream's own sudo_usage.h.in. The @X@ tokens
# it uses (checked: sudo_usage.h.in lines 28-36) are BSD-auth/SELinux/
# login-class usage fragments -- all empty in a build with none of them.
sed -e 's/@BSDAUTH_USAGE@//g' -e 's/@SELINUX_USAGE@//g' -e 's/@LOGINCAP_USAGE@//g' \
    -e 's|@CONFIGURE_ARGS@|(none; see tools/userland_staging/build_sudo.sh)|' \
    "$SUDO/src/sudo_usage.h.in" > "$GEN/sudo_usage.h"
if grep -q '@[A-Z_]*@' "$GEN/sudo_usage.h"; then
    echo "error: sudo_usage.h still has unsubstituted @TOKENS@ -- update the sed in build_sudo.sh:" >&2
    grep -n '@[A-Z_]*@' "$GEN/sudo_usage.h" >&2
    exit 1
fi

# siglist.c/signame.c: upstream's Makefile builds mksiglist/mksigname and
# RUNS them at build time (lib/util/Makefile.in:209-219). They must run
# on the build host, so compile them with the host macOS SDK, not the
# iPhoneOS one. Their inputs mksiglist.h/mksigname.h ship in the tag.
MACSDK="$(xcode-select -p)/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
HOSTCC="$(xcrun -f clang 2>/dev/null || command -v clang)"
for t in mksiglist mksigname; do
    "$HOSTCC" -isysroot "$MACSDK" -I "$GEN" -I "$SUDO" -I "$SUDO/include" -I "$SUDO/lib/util" \
        "$SUDO/lib/util/$t.c" -o "$GEN/$t"
done
"$GEN/mksiglist" > "$GEN/siglist.c"
"$GEN/mksigname" > "$GEN/signame.c"

# ---- compile ---------------------------------------------------------------
# Recipe = build_su.sh's CFLAGS minus the OpenPAM-only pieces (no PAM here,
# so no -D__FreeBSD__ / pic_override.h / -I OpenPAM), plus sudo's own -I/-D
# (mirrors plugins/sudoers/Makefile.in:72-79, src/Makefile.in:50-54 and
# lib/util/Makefile.in:63-66 -- the real Makefiles' CPPFLAGS).
# -D_DARWIN_UNLIMITED_GETGROUPS: configure.ac's own Darwin branch
# (AC_DEFINE([_DARWIN_UNLIMITED_GETGROUPS])).
# -Wno-everything: upstream is warning-noisy under -ffreestanding/-O0. NOT
# plain -w: -w also silences -Werror=implicit-function-declaration, and an
# implicit declaration is exactly how a missing/mis-guarded function (e.g.
# audit.c's audit_to_eventlog, see the .def) would hide until link time.
# Drop -Wno-everything when triaging.
# -include sys/stat.h: include/sudo_compat.h:461-478 #defines fstatat/fchmodat/
# utimensat/futimens to sudo_* fallbacks (HAVE_* left undefined on purpose),
# and the SDK's own declarations in <sys/stat.h> (xnu-7195
# bsd/sys/stat.h:571-579) then collide with those macros in every file that
# reaches stat.h AFTER sudo_compat.h (parse.h:23). Upstream's fallback .c
# files include <sys/stat.h> first, so pre-including it reproduces that.
# -include signal.h: same reason for killpg (sudo_compat.h:271-272 defines it
# as a macro because HAVE_KILLPG is undefined; Libc/include/signal.h:92
# declares it).
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-everything -Werror=implicit-function-declaration \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -include sys/stat.h -include signal.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -D_DARWIN_UNLIMITED_GETGROUPS \
    -DSUDOERS_UID=0 -DSUDOERS_GID=0 -DSUDOERS_MODE=0440 \
    -I $GEN -I $SUDO/include -I $SUDO -I $SUDO/src -I $SUDO/plugins/sudoers \
    -I $SUDO/lib/util -I $SUDO/lib/eventlog -I $SUDO/lib/iolog \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall -I $XNU/libsyscall/wrappers \
    -I ../libinfo_custom \
    -I ../../generated"

# Source lists mirror the real Makefiles (cited per group). Files whose
# fallback body is gated `#ifndef HAVE_X` compile to (nearly) nothing when
# HAVE_X is defined -- that IS upstream's intended mechanism, so the lists
# are deliberately not filtered by hand.
# lib/util: every .c in lib/util/Makefile.in's object list except the
# AIX-only aix.c, the openssl/gcrypt digest backends, the generator
# programs (built above), event_poll.c (HAVE_POLL is left undefined, see
# the .def), and regress/.
UTIL_SRCS="arc4random arc4random_buf arc4random_uniform cfmakeraw closefrom digest dup3 event event_select \
 explicit_bzero fatal fchmodat fnmatch freezero fstatat getaddrinfo getcwd getdelim getentropy getgrouplist \
 gethostname getopt_long gettime getusershell gidlist glob inet_ntop inet_pton isblank json key_val lbuf \
 locking logfac logpri memrchr mkdir_parents mktemp nanosleep openat parseln pipe2 pread progname pw_dup \
 pwrite reallocarray roundup secure_path setgroups sha2 sig2str snprintf str2sig strlcat strlcpy strndup \
 strnlen strsignal strsplit strtobool strtoid strtomode strtonum sudo_conf sudo_debug sudo_dso term \
 ttyname_dev ttysize unlinkat utimens uuid vsyslog"
# lib/eventlog/Makefile.in:85, lib/iolog/Makefile.in:89-90.
EVENTLOG_SRCS="eventlog logwrap"
IOLOG_SRCS="iolog_fileio iolog_json iolog_path iolog_util host_port hostcheck"
# lib/logsrv/Makefile.in:76 (LIBLOGSRV_OBJS = protobuf-c.lo log_server.pb-c.lo):
# the bundled protobuf-c runtime + generated message code that
# plugins/sudoers/log_client.c (SUDOERS_LOG_CLIENT) links against.
LOGSRV_SRCS="protobuf-c log_server.pb-c"
# plugins/sudoers/Makefile.in:162-176 (LIBPARSESUDOERS_OBJS + SUDOERS_OBJS
# without the @SUDOERS_OBJS@ optional backends). def_data.c is NOT in the
# list: it is a generated fragment #included by defaults.c:76, not a
# standalone object. log_client.c/audit.c
# are in the object lists; both need SUDOERS_LOG_CLIENT (see the .def).
# getspwuid.c (plugins/sudoers/, not auth/): configure.ac:4641 adds
# getspwuid.lo alongside passwd.lo for the passwd backend; it defines
# sudo_getepw/sudo_setspent/sudo_endspent (auth/passwd.c:53 calls sudo_getepw).
SUDOERS_SRCS="alias audit base64 defaults digestname getspwuid exptilde filedigest gentime gmtoff gram \
 hexchar match match_addr match_command match_digest pwutil pwutil_impl rcstr redblack strlist \
 sudoers_debug timeout timestr toke toke_util boottime check editor env env_pattern file find_path \
 fmtsudoers gc goodpath group_plugin interfaces iolog iolog_path_escapes locale log_client logging \
 parse policy prompt set_perms starttime sudo_nss sudoers timestamp"
AUTH_SRCS="sudo_auth passwd"
# src/Makefile.in:126-130 minus utmp.c (replaced by sudo_utmp_compat.c, see its
# header) + preload.c (added by configure.ac:4239 when
# --enable-static-sudoers).
SRC_SRCS="conversation copy_file edit_open env_hooks exec exec_common exec_monitor exec_nopty exec_pty \
 get_pty hooks limits load_plugins net_ifs parse_args preserve_fds signal sudo sudo_edit \
 tcsetpgrp_nobg tgetpass ttyname preload"

ok=0; skip=0
compile() { # <src file> <object name> [extra flags]
    local src="$1" name="$2"; shift 2
    if "$CLANG" $CFLAGS "$@" "$src" -o "$OBJ/$name.o" 2>"$OBJ/$name.err"; then
        ok=$((ok + 1))
    else
        skip=$((skip + 1)); echo "  skip: $name"; cat "$OBJ/$name.err" >&2
    fi
}
for b in $UTIL_SRCS;     do compile "$SUDO/lib/util/$b.c"        "util_$b"; done
compile "$GEN/siglist.c" util_siglist
compile "$GEN/signame.c" util_signame
for b in $EVENTLOG_SRCS; do compile "$SUDO/lib/eventlog/$b.c"    "eventlog_$b"; done
for b in $IOLOG_SRCS;    do compile "$SUDO/lib/iolog/$b.c"       "iolog_$b"; done
for b in $LOGSRV_SRCS;   do compile "$SUDO/lib/logsrv/$b.c"      "logsrv_$b"; done
for b in $SUDOERS_SRCS;  do compile "$SUDO/plugins/sudoers/$b.c" "sudoers_$b"; done
for b in $AUTH_SRCS;     do compile "$SUDO/plugins/sudoers/auth/$b.c" "auth_$b"; done
for b in $SRC_SRCS; do
    case $b in
        sudo) compile "$SUDO/src/sudo.c" src_sudo -Dmain=sudo_cmd_real_main ;;
        *)    compile "$SUDO/src/$b.c"   "src_$b" ;;
    esac
done
# Project glue -- same set su links for the same reasons (build_su.sh):
# real passwd/group lookup shim, fork/atfork compat, getusershell, crypt,
# sigsetops, and the shared locale-globals object (NEVER a narrower
# hand-written one, DAR-169).
for f in sudo_main_wrapper.c sudo_utmp_compat.c ps_passwd_compat.c ../libinfo_custom/libinfo_custom.c chpass_fork_compat.c \
         "$LIBC/gen/getusershell.c" "$LIBC/gen/crypt.c" "$LIBC/gen/sigsetops.c" \
         libc_locale_globals_compat.c "$LIBC/locale/FreeBSD/table.c" "$LIBC/locale/FreeBSD/none.c"; do
    compile "$f" "glue_$(basename "$f" .c)"
done

echo "sudo (real sudo-project/sudo v1.9.5p2): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: sudo has real compile failures, not building sudo.macho" >&2
    exit 1
fi

# Diagnostic-landed check (CLAUDE.md, DAR-200): the pre-generated parser
# and the static-plugin table must actually be in the objects.
nm "$OBJ/sudoers_gram.o" | grep -q "_sudoersparse" || { echo "error: gram.o has no sudoersparse" >&2; exit 1; }
nm "$OBJ/src_preload.o"  | grep -q "_preload_static_symbols" || {
    echo "error: preload.o has no preload_static_symbols -- STATIC_SUDOERS_PLUGIN did not take" >&2; exit 1; }

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OBJ/sudo_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OBJ/sudo_sched_yield.o"

# Raw syscall wrapper objects that no shipped dylib exports (same
# mechanism and citation as build_su.sh / build_libsystem_c.sh). su needed
# _chdir/_getpgrp/_getpgid/_getpriority; sudo additionally calls the
# setuid family etc., so START from that set and extend it from the first
# real link's undefined-symbol list (gap_audit.sh classifies each one), via
# SUDO_EXTRA_SYSCALLS="_setreuid _seteuid ..." until the list is settled,
# then bake the settled list in here.
TMPGEN="$(mktemp -d)"
trap 'rm -rf "$TMPGEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" "$XNU/libsyscall/custom" "$XNU/libsyscall/Platforms" \
    iPhoneOS "$TMPGEN" >/dev/null
SYSCFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -I $XNU/libsyscall/custom -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -idirafter $MACSDK/usr/include"
# Added for sudo (each undefined in a real object-file nm -u pass against the
# shipped dylib exports; generated stub names come from create-syscalls.pl
# run over xnu-7195 bsd/kern/syscalls.master):
#   _getsid #310 (src/sudo.c), _sigpending #52 (auth/sudo_auth.c),
#   _futimes #139 (lib/util/utimens.c),
#   ___setreuid #126 / ___setregid #127 -- syscalls.master marks both
#   NO_SYSCALL_STUB but the generator still emits ___setreuid/___setregid,
#   and libsyscall/Platforms/iPhoneOS/arm64/syscall.map:64-65 aliases
#   _setreuid/_setregid to them; the aliases are on the link line below.
SUDO_SYSCALLS="_chdir _getpgrp _getpgid _getpriority _getsid _sigpending _futimes ___setreuid ___setregid $SUDO_EXTRA_SYSCALLS"
for s in $SUDO_SYSCALLS; do
    "$CLANG" $SYSCFLAGS "$TMPGEN/${s}.s" -o "$OBJ/sys${s}.o"
done

DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi
for d in libsystem_c libsystem_kernel libsystem_pthread libsystem_malloc libsystem_platform libdyld \
         libcopyfile libremovefile libbsm libSystem.B libinfo libresolv; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run its build_*.sh first (see build_su.sh's prerequisite block)" >&2
        exit 1
    fi
done
LINK_DYLIBS="$OUT/system/libsystem_c.dylib $OUT/system/libsystem_kernel.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libcopyfile.dylib $OUT/system/libremovefile.dylib $OUT/system/libbsm.dylib $OUT/system/libSystem.B.dylib"
# libinfo/libresolv: sudo needs getaddrinfo/getgrouplist/innetgr (libinfo) and
# getifaddrs/inet_pton/inet_ntop/gethostname/pselect (libresolv) -- found by a
# real link, which listed them undefined. Same list position as build_ping.sh:360
# and build_ifconfig.sh:275.
LINK_DYLIBS="$LINK_DYLIBS $OUT/system/libinfo.dylib $OUT/system/libresolv.dylib"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o sudo.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o \
    "$OUT/liblaunchd_syscalls.a" \
    "$OUT/libmach_ipc.a" \
    "$OUT/libc_regex.a" \
    $LINK_DYLIBS \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_waitpid,'_waitpid$UNIX2003' \
    -Wl,-alias,_setrlimit,'_setrlimit$UNIX2003' \
    -Wl,-alias,_fchmod,'_fchmod$UNIX2003' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,_sigaction,'_sigaction$UNIX2003' \
    -Wl,-alias,_sigprocmask,'_sigprocmask$UNIX2003' \
    -Wl,-alias,___setreuid,'_setreuid$UNIX2003' \
    -Wl,-alias,___setregid,'_setregid$UNIX2003' \
    -Wl,-alias,_getgroups,'_getgroups$DARWIN_EXTSN' \
    -Wl,-alias,_glob,'_glob$INODE64' \
    -Wl,-alias,_socketpair,'_socketpair$UNIX2003'
echo "wrote sudo.macho"

bad=0
otool -l sudo.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: sudo: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l sudo.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: sudo: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l sudo.macho | grep -q "cmd LC_MAIN" || {
    echo "error: sudo: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
strings sudo.macho | grep -q "sudoers_policy" || { echo "error: sudo.macho lacks the static sudoers policy symbol table" >&2; exit 1; }

# Hard gates (CLAUDE.md: not optional for a new/converted link).
./bind_target_audit.sh sudo.macho
if [ -x ./dead_stub_audit.sh ]; then
    ./dead_stub_audit.sh sudo.macho
fi
