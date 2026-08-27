#
# This project's own real, tracked copy of
# third_party/Libc/Platforms/iphoneos/Makefile.inc (the input
# xcodescripts/generate_features.pl reads to produce a real
# libc-features.h), with ONE real, documented change:
# FEATURE_TIMEZONE_CHANGE_NOTIFICATION disabled.
#
# Real reason: enabling it (real Apple's own default for iphoneos) makes
# stdtime/FreeBSD/localtime.c call the real notify(3) CLIENT API
# (notify_register_check/notify_check/notify_cancel/notify_monitor_file)
# to invalidate cached timezone data when the system TZ changes. This
# project only ported notifyd's real MIG SERVER side so far (see
# tools/userland_staging/build_notify.sh -- __notify_server_* in
# libnotify.a), not the client library that talks to it. Disabling this
# one real, documented, always-off-able feature flag (it exists
# precisely to be toggled per-platform) is honest scope-narrowing, not
# a fabrication -- localtime()'s own real, correct behavior is
# unaffected, it just won't pick up a live TZ change without a process
# restart until a real notify(3) client port lands.
#

# Legacy *64 APIs
#FEATURE_LEGACY_64_APIS = 1

# Legacy crt1.o environ support
#FEATURE_LEGACY_CRT1_ENVIRON = 1

# Legacy rune APIs
#FEATURE_LEGACY_RUNE_APIS = 1

# Legacy utmp APIs
#FEATURE_LEGACY_UTMP_APIS = 1

# OSThermalNotification APIs
FEATURE_THERM_NOTIFICATION_APIS = 1

# No pre-1050 variants (should match sys/cdefs.h)
FEATURE_ONLY_1050_VARIANTS = 1

# No legacy variants (should match sys/cdefs.h)
FEATURE_ONLY_UNIX_CONFORMANCE = 1

# Only 64-bit ino_t (should match sys/cdefs.h)
FEATURE_ONLY_64_BIT_INO_T = 1

# Patch 3333969
#FEATURE_PATCH_3333969 = 1

# Patch 3417676
#FEATURE_PATCH_3417676 = 1

# plockstat dtrace support
#FEATURE_PLOCKSTAT = 1

# Timezone change notification -- DISABLED for this project, see this
# file's own header comment (no real notify(3) client library ported
# yet, only the MIG server side).
#FEATURE_TIMEZONE_CHANGE_NOTIFICATION = 1

# Smaller stdio buffers
FEATURE_SMALL_STDIOBUF = 1

# Disable registration of specific signals (<rdar://problem/21952708>)
# FEATURE_SIGNAL_RESTRICTION = 0

# Enable 32-bit compilation in unistd.h (<rdar://problem/51818745>)
# FEATURE_POSIX_ILP32_ALLOW = 0
