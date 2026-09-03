/* DAR-24 pass, 2026-09-03: os_log_create()/os_log_type_enabled()/
 * os_variant_has_internal_diagnostics()/_os_log_impl() removed. All four
 * are now real exports of a dylib syslogd.macho already loads --
 * libsystem_c.dylib's own os_log_compat.c (os_log_create/
 * os_log_type_enabled/_os_log_impl) and libsystem_notify.dylib's own
 * libsystem_notify_misc_compat.c (os_variant_has_internal_diagnostics) --
 * same author, same "honest no-op, no log daemon to deliver to" reasoning
 * this file's own removed copies used, confirmed byte-for-byte equivalent
 * before deleting, not just same-named. See
 * tools/userland_staging/dup_symbol_audit.sh. */
