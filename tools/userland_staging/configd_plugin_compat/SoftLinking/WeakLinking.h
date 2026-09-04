/* Real, minimal compat stand-in for <SoftLinking/WeakLinking.h> -- an
 * Apple-internal build-system umbrella header, never open-sourced
 * (confirmed via full pinned-repo grep), used by
 * Plugins/InterfaceNamer/ifnamer.c to force a weak import of a handful of
 * MobileLockdown symbols (see configd_plugin_lockdown_compat.h's own
 * header comment for the full real reasoning). The real macro's only
 * documented job is to force the compiler to emit a weak-imported
 * reference to a symbol that might not exist at load time -- this is
 * exactly what __attribute__((weak_import)) does natively, so this
 * stand-in is a faithful, real reimplementation of the macro's real
 * documented contract, not a functional shortcut.
 */
#ifndef CONFIGD_PLUGIN_SOFTLINKING_WEAKLINKING_H
#define CONFIGD_PLUGIN_SOFTLINKING_WEAKLINKING_H

#define WEAK_LINK_FORCE_IMPORT(sym) \
	extern __typeof__(sym) sym __attribute__((weak_import))

#endif
