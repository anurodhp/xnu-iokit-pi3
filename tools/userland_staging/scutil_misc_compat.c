/*
 * DAR-283: the two link-level odds and ends scutil.macho needs that are
 * neither real Apple source nor a subsystem stand-in.
 *
 * ------------------------------------------------------------------
 * 1. ne_session_always_on_vpn_configs_present()
 * ------------------------------------------------------------------
 * NetworkExtension.framework, never open-sourced by Apple (confirmed by
 * a real `git ls-remote` against
 * apple-oss-distributions/NetworkExtension: "Repository not found"), and
 * with no FreeBSD/upstream equivalent to fall back to -- it is an
 * Apple-private always-on-VPN policy query, not a portable subsystem.
 *
 * The single caller is real, unmodified
 * SystemConfiguration.fproj/SCProxies.c:
 *
 *     if (CFDictionaryContainsKey(globalConfiguration, kSCPropNetProxiesBypassAllowed) &&
 *         ne_session_always_on_vpn_configs_present()) {
 *             // forcing managed proxy configurations is only done with AOVPN present
 *             goto useDefault;
 *     }
 *
 * Providing this one function is what lets scutil link the REAL
 * SCProxies.c (`scutil --proxy`, SCDynamicStoreCopyProxies,
 * SCNetworkProxiesCopyMatching) instead of four fabricated
 * SystemConfiguration API stand-ins -- strictly more real Apple code,
 * not less.
 *
 * Returning false is not a guess about Apple's semantics; it is the
 * factually correct answer for this target. An always-on VPN
 * configuration is installed by a NetworkExtension VPN provider through
 * a configuration profile, and this port has no NetworkExtension, no
 * SCNetworkConnection (build_libsystemconfiguration_client.sh
 * deliberately excludes the whole PPP/VPN dial subsystem), no VPN
 * provider of any kind, and no configuration-profile mechanism. There is
 * no state in which "an always-on VPN config is present" could be true
 * here. The guarded branch is genuinely dead code on this target -- the
 * same "real feature genuinely does not apply to this embedded target"
 * category as configd_plugin_compat/vpn_status_compat.h's own documented
 * stand-in.
 *
 * ------------------------------------------------------------------
 * 2. _write$UNIX2003
 * ------------------------------------------------------------------
 * Not a stand-in at all -- a symbol-name bridge, the same one
 * bash_syscalls.c already carries for a dozen libc entry points.
 *
 * This checkout's own vendored third_party/Libc/include/sys/cdefs.h
 * (found ahead of the SDK's copy via this project's -I $LIBC/include
 * precedence) never defines __DARWIN_ONLY_UNIX_CONFORMANCE, so
 * __DARWIN_UNIX03's "$UNIX2003" symbol-suffixing branch is live: real,
 * unmodified Apple source that calls write(2) asm-references
 * `_write$UNIX2003`, while this project's libsystem_kernel.dylib exports
 * the plain `_write`. In scutil.macho the callers are net.o and tests.o
 * (confirmed per-object with `nm -u`, not assumed). Forwarding
 * trampoline, real write(2) underneath, no behaviour change.
 *
 * CAUGHT BY THE PRE-COMMIT ADVERSARIAL REVIEW, 2026-09-10 -- do not
 * "simplify" this back. The obvious spelling of this trampoline,
 *
 *	ssize_t write_u2003(int fd, const void *buf, size_t n)
 *	{ return write(fd, buf, n); }
 *
 * compiles clean, links clean, passes every audit in this project, and
 * is an INFINITE RECURSION: <unistd.h> declares write(2) with
 * __DARWIN_ALIAS_C under the same live $UNIX2003 branch, so the call in
 * the body resolves right back to this same symbol. Verified from the
 * real binary, not reasoned about -- `otool -tV scutil.macho` showed
 * literally `_write$UNIX2003: ... bl _write$UNIX2003`. It survived a
 * real QEMU boot only because neither caller's code path was exercised
 * there. Referencing the plain `_write` symbol explicitly, by asm name,
 * is what makes the forward actually go somewhere -- the same technique
 * bash_syscalls.c already uses for its own dozen $UNIX2003 bridges.
 */
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>

bool
ne_session_always_on_vpn_configs_present(void)
{
	return false;
}

/* The real, un-suffixed write(2) libsystem_kernel.dylib exports. Named in
 * asm so the __DARWIN_ALIAS_C declaration in <unistd.h> cannot redirect
 * it back to the $UNIX2003 symbol this file is defining. */
extern ssize_t __scutil_real_write(int fd, const void *buf, size_t nbyte) asm("_write");

extern ssize_t write_u2003(int fd, const void *buf, size_t nbyte) asm("_write$UNIX2003");

ssize_t
write_u2003(int fd, const void *buf, size_t nbyte)
{
	return __scutil_real_write(fd, buf, nbyte);
}
