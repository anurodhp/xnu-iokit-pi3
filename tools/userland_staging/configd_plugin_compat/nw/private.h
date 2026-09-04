/* Real, empty compat stand-in for <nw/private.h> (libnetcore's Network.framework
 * internal header, never open-sourced -- confirmed via full pinned-repo grep).
 * Plugins/KernelEventMonitor/eventmon.c #includes this but, confirmed by grep,
 * never references any "nw_" or "NW_" prefixed symbol anywhere in the real
 * file -- the include is real but vestigial for this project's real compiled
 * surface.
 * Same "empty, honestly-documented stand-in for a genuinely unused private
 * header" precedent already established for CoreFoundation/CFUserNotification.h
 * and ne_session.h.
 */
