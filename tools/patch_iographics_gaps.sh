#!/bin/bash
#
# Patches the real, structural compile gaps in the pinned Apple IOGraphics
# drop (third_party/IOGraphics, IOGraphics-585) that tools/iographics_gap_
# audit.sh finds: declarations the .cpp sources use that the published
# headers never declare (see that script's own header for why -- the
# .cpp files are from a newer internal revision than the headers shipped
# beside them, verified across three tags).
#
# WHY A SEPARATE SCRIPT, NOT HAND EDITS: third_party/IOGraphics is
# gitignored and re-cloned by setup_third_party.sh, so any fix has to be
# reproducible against a fresh clone, same reasoning as vendor_usb_into_
# xnu.sh's FIXES section. This project has not vendored IOGraphics into
# third_party/xnu-7195 yet (IOFramebuffer.cpp does not compile without
# these fixes, so there is nothing to vendor and link yet) -- once it is,
# fold this script's patches into that vendoring script's own FIXES
# section instead of running both; until then this is the one reproducing
# script for these fixes and tools/iographics_gap_audit.sh depends on it.
#
# Each fix cites the specific real source (this pinned checkout, or
# third_party/xnu-7195 for kernel semantics) it is inferred from, per
# this project's DAR-313 convention. Idempotent: safe to re-run after a
# fresh ./setup_third_party.sh.
#
# Run directly, or via tools/iographics_gap_audit.sh (which runs it first
# unless IOG_AUDIT_NO_PATCH=1).

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

IOG="third_party/IOGraphics/IOGraphicsFamily"
XNU="third_party/xnu-7195"

if [ ! -f "$IOG/IOFramebuffer.cpp" ]; then
	echo "error: $IOG not found - run ./setup_third_party.sh" >&2
	exit 1
fi

FBPRIV="$IOG/IOKit/graphics/IOFramebufferPrivate.h"
FBCPP="$IOG/IOFramebuffer.cpp"
DWH="$IOG/IOKit/graphics/IODisplay.h"
DWCPP="$IOG/IODisplayWrangler.cpp"

# ---------------------------------------------------------------------
# 1. sysAssertGated() -- IODisplay.cpp:484 calls framebuffer->
#    sysAssertGated() (IODisplay is a friend of IOFramebuffer, see
#    IOFramebuffer.h:277), IOFramebuffer.cpp:2428 defines it
#    (`void IOFramebuffer::sysAssertGated(void) { SYSASSERTGATED(); }`,
#    a thin wrapper round the same gate-assert macro used at 10+ other
#    call sites in the same file), but no declaration exists anywhere in
#    the published headers. Mechanical: the definition pins the exact
#    signature.
# ---------------------------------------------------------------------
F="$FBPRIV"
if ! grep -q "void sysAssertGated(void);" "$F"; then
	MARKER='    IOReturn waitQuietController(void);'
	if ! grep -qF "$MARKER" "$F"; then
		echo "error: sysAssertGated anchor not found in $F" >&2
		exit 1
	fi
	LC_ALL=C sed -i '' "s|$MARKER|$MARKER\n\n    // Declared here, defined IOFramebuffer.cpp:2428 -- called from\n    // IODisplay.cpp:484 (a friend class, see IOFramebuffer.h:277), and\n    // published nowhere. See tools/patch_iographics_gaps.sh.\n    void sysAssertGated(void);|" "$F"
fi
if ! grep -q "void sysAssertGated(void);" "$F"; then
	echo "error: sysAssertGated declaration did not apply" >&2
	exit 1
fi

# ---------------------------------------------------------------------
# 2. IOFramebufferPrivate: three data members the .cpp uses that the
#    published struct never declares. Types pinned by their real use
#    sites, all cited:
#      fCloseWorkES     IOInterruptEventSource* -- assigned from
#          IOInterruptEventSource::interruptEventSource(...)
#          (IOFramebuffer.cpp:4788-4792) and ->interruptOccurred()'d
#          (IOFramebuffer.cpp:10234), both real IOInterruptEventSource
#          APIs.
#      fClosePending     bool -- looped on with a plain
#          `do { ... } while (__private->fClosePending);`
#          (IOFramebuffer.cpp:10232-10236) and set true/false as a plain
#          flag (IOFramebuffer.cpp:10247).
#      fServerMsgIDSentDim  integer_t -- used identically to its two
#          siblings fServerMsgIDSentPower/fServerMsgIDAckedPower
#          (both already `integer_t` in the published struct, just above
#          this insertion point) in the same GPACKUINT32T() packing call
#          (IOFramebuffer.cpp:8649-8652).
# ---------------------------------------------------------------------
F="$FBCPP"
ANCHOR='    integer_t                   fServerMsgIDSentPower;
    integer_t                   fServerMsgIDAckedPower;
};'
if ! grep -qF "IOInterruptEventSource *    fCloseWorkES;" "$F"; then
	NEW='    integer_t                   fServerMsgIDSentPower;
    integer_t                   fServerMsgIDAckedPower;
    integer_t                   fServerMsgIDSentDim;      // patched, see tools/patch_iographics_gaps.sh

    IOInterruptEventSource *    fCloseWorkES;             // patched, see tools/patch_iographics_gaps.sh
    bool                        fClosePending;            // patched, see tools/patch_iographics_gaps.sh
};'
	python3 - "$F" "$ANCHOR" "$NEW" <<'PYEOF'
import sys
f, anchor, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(f).read()
if anchor not in s:
    print("error: IOFramebufferPrivate struct-tail anchor not found in %s" % f, file=sys.stderr)
    sys.exit(1)
s = s.replace(anchor, new, 1)
open(f, "w").write(s)
PYEOF
fi
if ! grep -qF "IOInterruptEventSource *    fCloseWorkES;" "$F"; then
	echo "error: IOFramebufferPrivate struct patch did not apply" >&2
	exit 1
fi

# ---------------------------------------------------------------------
# 3. gTraceData / agdcTraceData -- IOFramebuffer.cpp:1966,1971 pass
#    &IOFramebuffer::gTraceData and &IOFramebuffer::agdcTraceData as the
#    `breadcrumb_func bcf` argument of GTraceBuffer::make() (signature:
#    `IOReturn (*)(void* context, void* buffer, uint16_t* sizeP)`,
#    GTrace.hpp:120-122), so both must be static member functions of
#    that exact signature, not data members. GTrace.hpp's own doc
#    (:143-151) says bcf "may be null" and exists only so a client can
#    "copy up to 16KiB of additional data used by the client's decode
#    module" (an iogdiagnose decode module under
#    /AppleInternal/AppleGraphicsControl/IOGDiagnose_modules). This
#    project ships no such decode module and no iogdiagnose -- same
#    class of gap as OpenPAM's dlopen module loader (CLAUDE.md,
#    "Source priority" section, DAR-226): a real, documented
#    architectural absence, not a guess at what Apple's internal
#    breadcrumb bodies actually contain. A safe no-op (report zero
#    bytes) is the honest completion.
# ---------------------------------------------------------------------
F="$FBPRIV"
if ! grep -q "static IOReturn gTraceData" "$F"; then
	MARKER='    void sysAssertGated(void);'
	LC_ALL=C sed -i '' "s|$MARKER|$MARKER\n\n    // breadcrumb_func for GTraceBuffer::make() (IOFramebuffer.cpp:1966,\n    // 1971). No-op: this project has no gtrace decode module for either\n    // buffer to serve data to. See tools/patch_iographics_gaps.sh.\n    static IOReturn gTraceData(void * context, void * buffer, uint16_t * sizeP);\n    static IOReturn agdcTraceData(void * context, void * buffer, uint16_t * sizeP);|" "$F"
fi
if ! grep -q "static IOReturn gTraceData" "$F"; then
	echo "error: gTraceData/agdcTraceData declaration did not apply" >&2
	exit 1
fi

F="$FBCPP"
if ! grep -q "IOFramebuffer::gTraceData(" "$F"; then
	ANCHOR='void IOFramebuffer::sysAssertGated(void)
{
    SYSASSERTGATED();
}'
	NEW='void IOFramebuffer::sysAssertGated(void)
{
    SYSASSERTGATED();
}

// No decode module consumes either buffer on this project (no
// iogdiagnose) -- see the declaration in IOFramebufferPrivate.h.
IOReturn IOFramebuffer::gTraceData(void * /* context */, void * /* buffer */, uint16_t * sizeP)
{
    if (sizeP) *sizeP = 0;
    return (kIOReturnUnsupported);
}

IOReturn IOFramebuffer::agdcTraceData(void * /* context */, void * /* buffer */, uint16_t * sizeP)
{
    if (sizeP) *sizeP = 0;
    return (kIOReturnUnsupported);
}'
	python3 - "$F" "$ANCHOR" "$NEW" <<'PYEOF'
import sys
f, anchor, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(f).read()
if anchor not in s:
    print("error: sysAssertGated definition anchor not found in %s" % f, file=sys.stderr)
    sys.exit(1)
s = s.replace(anchor, new, 1)
open(f, "w").write(s)
PYEOF
fi
if ! grep -q "IOFramebuffer::gTraceData(" "$F"; then
	echo "error: gTraceData/agdcTraceData definitions did not apply" >&2
	exit 1
fi

# ---------------------------------------------------------------------
# 4. IODisplayConnect::getFBRegistryID() and the two recordGTraceToken()
#    overloads -- IODisplayWrangler.cpp defines all three
#    (:94-113, :1016 calls the accessor from a different TU,
#    IODisplay.cpp:1016) but the class (IODisplay.h:108-136) declares
#    none of them. fFBRegID is a private member of IODisplayConnect
#    (IODisplay.h:115) that only IODisplayWrangler can reach directly
#    (the sole friend, :111) -- IODisplay.cpp is not a friend, so this
#    has to be a real public accessor, matching how getFramebuffer()/
#    getConnection() are already exposed right beside it.
# ---------------------------------------------------------------------
F="$DWH"
if ! grep -q "getFBRegistryID" "$F"; then
	ANCHOR='    virtual void joinPMtree ( IOService * driver ) APPLE_KEXT_OVERRIDE;

};'
	NEW='    virtual void joinPMtree ( IOService * driver ) APPLE_KEXT_OVERRIDE;

    // Declared here, defined IODisplayWrangler.cpp -- see
    // tools/patch_iographics_gaps.sh.
    uint64_t getFBRegistryID(void);
    void recordGTraceToken(const uint16_t line,
                           const uint16_t tag0, const uint64_t fbRegID,
                           const uint16_t tag1, const uint64_t arg1,
                           const uint16_t tag2, const uint64_t arg2,
                           const uint16_t tag3, const uint64_t arg3);
    void recordGTraceToken(const uint16_t line,
                           const uint16_t fnID, const uint8_t  fnType,
                           const uint16_t tag1, const uint64_t arg1,
                           const uint16_t tag2, const uint64_t arg2,
                           const uint16_t tag3, const uint64_t arg3);

};'
	python3 - "$F" "$ANCHOR" "$NEW" <<'PYEOF'
import sys
f, anchor, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(f).read()
if anchor not in s:
    print("error: IODisplayConnect class-tail anchor not found in %s" % f, file=sys.stderr)
    sys.exit(1)
s = s.replace(anchor, new, 1)
open(f, "w").write(s)
PYEOF
fi
if ! grep -q "getFBRegistryID" "$F"; then
	echo "error: getFBRegistryID/recordGTraceToken declarations did not apply" >&2
	exit 1
fi

F="$DWCPP"
if ! grep -q "IODisplayConnect::getFBRegistryID" "$F"; then
	ANCHOR='IOFramebuffer * IODisplayConnect::getFramebuffer( void )'
	NEW='uint64_t IODisplayConnect::getFBRegistryID( void )
{
    return (fFBRegID);
}

IOFramebuffer * IODisplayConnect::getFramebuffer( void )'
	python3 - "$F" "$ANCHOR" "$NEW" <<'PYEOF'
import sys
f, anchor, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(f).read()
if anchor not in s:
    print("error: getFramebuffer anchor not found in %s" % f, file=sys.stderr)
    sys.exit(1)
s = s.replace(anchor, new, 1)
open(f, "w").write(s)
PYEOF
fi
if ! grep -q "IODisplayConnect::getFBRegistryID" "$F"; then
	echo "error: getFBRegistryID definition did not apply" >&2
	exit 1
fi

# ---------------------------------------------------------------------
# 5. IODWBuiltinPanelPower_t / IODW_BUILTIN_PANEL_POWER_VERSION --
#    IODisplayWrangler.cpp:888-893 (builtinPanelPowerNotify) constructs
#    and messageClients()'s this struct, but it is declared nowhere in
#    the published sources. Its three real field uses (.version, .mct,
#    .state) pin the shape; `state` is the function's own `bool state`
#    parameter and `.mct` is a real mach_continuous_time() (uint64_t).
#    This is Apple's OWN wire format for a message it sends via
#    messageClients(kIOMessageServicePropertyChange, ...) to any
#    interested IOService client -- since no real client on this project
#    parses it (no WindowServer, no AppleGraphicsControl consumer), the
#    exact byte layout is inert here; this reconstructs the minimum
#    shape the sender itself needs to compile, not Apple's published ABI.
# ---------------------------------------------------------------------
F="$DWCPP"
if ! grep -q "struct IODWBuiltinPanelPower_t" "$F"; then
	ANCHOR='namespace {
static const OSSymbol *
sIODWBuiltinPanelPowerKey = OSSymbol::withCString("AAPL,builtin-panel-power");
}'
	NEW='namespace {
static const OSSymbol *
sIODWBuiltinPanelPowerKey = OSSymbol::withCString("AAPL,builtin-panel-power");

// Reconstructed from its only real use (builtinPanelPowerNotify, below) --
// declared nowhere in the published sources. See
// tools/patch_iographics_gaps.sh. Inert on this project: nothing here
// parses a kIOMessageServicePropertyChange for
// AAPL,builtin-panel-power (no WindowServer/AppleGraphicsControl
// client exists), so this is only the shape the sender needs to build.
#define IODW_BUILTIN_PANEL_POWER_VERSION 1
struct IODWBuiltinPanelPower_t
{
    uint32_t version;
    uint64_t mct;
    bool     state;
};
}'
	python3 - "$F" "$ANCHOR" "$NEW" <<'PYEOF'
import sys
f, anchor, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(f).read()
if anchor not in s:
    print("error: sIODWBuiltinPanelPowerKey anchor not found in %s" % f, file=sys.stderr)
    sys.exit(1)
s = s.replace(anchor, new, 1)
open(f, "w").write(s)
PYEOF
fi
if ! grep -q "struct IODWBuiltinPanelPower_t" "$F"; then
	echo "error: IODWBuiltinPanelPower_t patch did not apply" >&2
	exit 1
fi

# ---------------------------------------------------------------------
# 6. vm_map_wire() -- IOFramebuffer.cpp:6292 calls the plain kext-facing
#    `vm_map_wire(map, start, end, access_type, user_wire)` (5 args).
#    third_party/xnu-7195/osfmk/vm/vm_map.h:1273-1322 makes that name
#    resolve only `#else /* XNU_KERNEL_PRIVATE */` -- i.e. only for an
#    out-of-tree kext. This kernel build always defines
#    XNU_KERNEL_PRIVATE (confirmed in tools/iographics_gap_audit.sh's
#    own extracted compile line), same as every other IOKit family
#    already compiled into this kernel (IOUSBFamily, IONetworkingFamily,
#    ...), so only vm_map_wire_kernel(map, start, end, access_type, tag,
#    user_wire) -- 6 args, with a vm_tag_t -- is visible
#    (vm_map.h:1276-1282). Real, already-in-tree IOKit code built into
#    this same kernel does exactly this swap for exactly this reason:
#    third_party/xnu-7195/iokit/Kernel/IOMemoryDescriptor.cpp:5447 calls
#    vm_map_wire_kernel(..., (vm_tag_t) fMemory->getVMTag(kernel_map), ...).
#    This call site has no IOMemoryDescriptor to ask for a tag (it wires
#    a plain IOFreePageable'd buffer via IOPageableMapForAddress, not a
#    descriptor's mapping) -- third_party/xnu-7195/iokit/Kernel/IOLib.cpp
#    tags every other IOKit pageable-memory allocation/wire with the
#    same constant (VM_KERN_MEMORY_IOKIT, e.g. IOLib.cpp:196,864,957),
#    so that is the real, cited tag to use here too.
# ---------------------------------------------------------------------
F="$FBCPP"
OLD='				vm_map_wire( IOPageableMapForAddress( (vm_address_t) __private->saveFramebuffer ),
							 (vm_address_t) __private->saveFramebuffer,
							 ((vm_address_t) __private->saveFramebuffer) + __private->saveLength,
							 VM_PROT_READ | VM_PROT_WRITE, FALSE );'
NEW='				/* was vm_map_wire(); XNU_KERNEL_PRIVATE hides that name, see
				 * tools/patch_iographics_gaps.sh */
				vm_map_wire_kernel( IOPageableMapForAddress( (vm_address_t) __private->saveFramebuffer ),
							 (vm_address_t) __private->saveFramebuffer,
							 ((vm_address_t) __private->saveFramebuffer) + __private->saveLength,
							 VM_PROT_READ | VM_PROT_WRITE, VM_KERN_MEMORY_IOKIT, FALSE );'
if ! grep -q "vm_map_wire_kernel(" "$F"; then
	python3 - "$F" "$OLD" "$NEW" <<'PYEOF'
import sys
f, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(f).read()
if old not in s:
    print("error: vm_map_wire call-site anchor not found in %s" % f, file=sys.stderr)
    sys.exit(1)
s = s.replace(old, new, 1)
open(f, "w").write(s)
PYEOF
fi
if grep -qE '^\s+vm_map_wire\(' "$F"; then
	echo "error: vm_map_wire -> vm_map_wire_kernel fix did not apply" >&2
	exit 1
fi

# ---------------------------------------------------------------------
# 7. kServerAckProtocolGraphicsTypesRev -- IOFramebuffer.cpp:675 (comment)
#    and :8780 compare an incoming `UInt32 messageType` (from
#    extRegisterNotificationPort, a real user-client entry point a
#    userspace display server calls) against this constant to decide
#    whether that caller uses "modern acks". No real caller exists on
#    this project: there is no WindowServer / CoreGraphics-equivalent
#    server here (see this ticket's own scoping note -- a working
#    IOFramebuffer gives no pixels without one, separate unscoped work),
#    so this comparison is presently dead code with no way to derive
#    Apple's real internal revision number for it. Defined honestly as a
#    value one past this drop's own published protocol revision
#    (IOGRAPHICSTYPES_REV, IOGraphicsTypes.h:34, currently 72) so it can
#    never legitimately be true rather than guessing at a number staged
#    as if it were Apple's real cutover point -- update this to the real
#    value if this project ever wires up a real notification-port client.
# ---------------------------------------------------------------------
F="$FBCPP"
if ! grep -qE '^\s*#define\s+kServerAckProtocolGraphicsTypesRev' "$F"; then
	ANCHOR='enum
{
	kIOFBEventCaptureSetting     = 0x00000001,'
	NEW='// No real notification-port client exists on this project (no
// WindowServer-equivalent) to pin this to Apple'"'"'s real internal
// revision cutover -- see tools/patch_iographics_gaps.sh. One past this
// drop'"'"'s own published IOGRAPHICSTYPES_REV (IOGraphicsTypes.h:34) so the
// comparison at IOFramebuffer.cpp:8780 can never spuriously be true.
#define kServerAckProtocolGraphicsTypesRev (IOGRAPHICSTYPES_REV + 1)

enum
{
	kIOFBEventCaptureSetting     = 0x00000001,'
	python3 - "$F" "$ANCHOR" "$NEW" <<'PYEOF'
import sys
f, anchor, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(f).read()
if anchor not in s:
    print("error: kIOFBEvent enum anchor not found in %s" % f, file=sys.stderr)
    sys.exit(1)
s = s.replace(anchor, new, 1)
open(f, "w").write(s)
PYEOF
fi
if ! grep -qE '^\s*#define\s+kServerAckProtocolGraphicsTypesRev' "$F"; then
	echo "error: kServerAckProtocolGraphicsTypesRev definition did not apply" >&2
	exit 1
fi

# ---------------------------------------------------------------------
# 8. clearEvent() / triggerEvent() -- 20 real call sites
#    (11x clearEvent, 9x triggerEvent, tools/iographics_gap_audit.sh's
#    own output), no declaration or definition anywhere in the drop.
#    Semantics reconstructed from the sibling call sites Apple did NOT
#    refactor to use these names (same file, same gIOFBGlobalEvents):
#      - IOFramebuffer.cpp:6935 `atomic_load(&gIOFBGlobalEvents)`,
#        :7066/:8258 `atomic_fetch_or(&gIOFBGlobalEvents, ...)`,
#        :11720 the same immediately followed by
#        `controller->didWork(kWorkStateChange)` -- i.e. real,
#        already-published code sets bits with a plain atomic OR and
#        explicitly kicks the controller's work afterward.
#      - Every real clearEvent() call site (grep -n clearEvent
#        IOFramebuffer.cpp) is inside systemWork() or a function it
#        calls while already on the system workloop (:6832, :6942,
#        :6976, :6997, :7046, :7110, :7146, :7166, :7192, :7596, :8486)
#        -- it does not need to re-kick a loop it is already running on,
#        so clearEvent is the direct atomic_fetch_and() equivalent with
#        no kick. Its one call site with an assignment
#        (`events = clearEvent(kIOFBEventSystemPowerOn);`, :6832) uses
#        the return value as the PRE-clear bit state, matching
#        atomic_fetch_and()'s real C11 semantics exactly.
#      - Every real triggerEvent() call site (:4484, :6795, :6965,
#        :8091, :8099, :9632, :11887, :13125, :14714) runs from contexts
#        OUTSIDE systemWork() (start(), pmSettingsChange(),
#        delayedEvent(), clamshellHandler(), setAttributeForConnection(),
#        an HPD interrupt handler) with no nearby didWork()/startThread()
#        call of their own -- so, mirroring the direct-OR-plus-didWork
#        pattern above, triggerEvent must also kick the system work
#        thread itself or the event it just set could sit unprocessed
#        indefinitely. This is the one real judgement call in this
#        patch (flagged as such in the ticket's own prior scoping note):
#        kicking unconditionally is the safe choice either way --
#        systemWork() no-ops if no event bit is actually set, so a
#        spurious kick costs one extra interruptOccurred() call, while
#        NOT kicking risks a real, silent stuck event.
# ---------------------------------------------------------------------
F="$FBPRIV"
if ! grep -q "static void triggerEvent" "$F"; then
	MARKER='    static void systemWork(OSObject * owner,
                           IOInterruptEventSource * evtSrc, int intCount);'
	if ! grep -qF "$MARKER" "$F"; then
		echo "error: systemWork anchor not found in $F" >&2
		exit 1
	fi
	LC_ALL=C python3 - "$F" "$MARKER" <<'PYEOF'
import sys
f, marker = sys.argv[1], sys.argv[2]
s = open(f).read()
new = marker + '''

    // Reconstructed -- no declaration exists in the published drop. See
    // tools/patch_iographics_gaps.sh for the real call-site evidence
    // both semantics (return value, kick-or-not) are inferred from.
    static void triggerEvent(uint32_t event);
    static uint_fast32_t clearEvent(uint32_t event);'''
if marker not in s:
    print("error: systemWork anchor not found", file=sys.stderr)
    sys.exit(1)
s = s.replace(marker, new, 1)
open(f, "w").write(s)
PYEOF
fi
if ! grep -q "static void triggerEvent" "$F"; then
	echo "error: triggerEvent/clearEvent declarations did not apply" >&2
	exit 1
fi

F="$FBCPP"
if ! grep -q "IOFramebuffer::triggerEvent(" "$F"; then
	ANCHOR='void IOFramebuffer::systemWork(OSObject * owner,'
	NEW='// See the declaration in IOFramebufferPrivate.h for the real call-site
// reasoning behind kicking on trigger but not on clear.
void IOFramebuffer::triggerEvent(uint32_t event)
{
    atomic_fetch_or(&gIOFBGlobalEvents, static_cast<uint_fast32_t>(event));
    startThread(false);
}

uint_fast32_t IOFramebuffer::clearEvent(uint32_t event)
{
    return atomic_fetch_and(&gIOFBGlobalEvents,
            ~static_cast<uint_fast32_t>(event));
}

void IOFramebuffer::systemWork(OSObject * owner,'
	python3 - "$F" "$ANCHOR" "$NEW" <<'PYEOF'
import sys
f, anchor, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(f).read()
if anchor not in s:
    print("error: systemWork definition anchor not found in %s" % f, file=sys.stderr)
    sys.exit(1)
s = s.replace(anchor, new, 1)
open(f, "w").write(s)
PYEOF
fi
if ! grep -q "IOFramebuffer::triggerEvent(" "$F"; then
	echo "error: triggerEvent/clearEvent definitions did not apply" >&2
	exit 1
fi

# ---------------------------------------------------------------------
# 9. XXH64() -- IODisplay.cpp:662 hashes the EDID with XXH64() and
#    IODisplay.cpp:441-442 formats the result into the display-prefs
#    key, but the drop never includes xxhash.h (Apple vendors xxHash
#    internally; every published tag lacks it). Include the real
#    upstream, pinned in setup_third_party.sh (third_party/xxHash,
#    v0.8.2), through compat/IOKit/graphics/IOGraphicsXXHash.h, which
#    documents upstream's own kernel configuration (XXH_INLINE_ALL,
#    XXH_NO_STDLIB, XXH_NO_XXH3) and supplies the seed-0 overload for
#    Apple's 2-argument call. The caller must put third_party/xxHash and
#    compat/xxhash_kernel (the <limits.h> redirect) on the include path
#    (tools/iographics_gap_audit.sh does).
# ---------------------------------------------------------------------
F="$IOG/IODisplay.cpp"
if ! grep -q "IOGraphicsXXHash.h" "$F"; then
	MARKER='#include "GMetric.hpp"'
	if [ "$(grep -cxF "$MARKER" "$F")" != 1 ]; then
		echo "error: XXH64 include anchor not found exactly once in $F" >&2
		exit 1
	fi
	LC_ALL=C sed -i '' "s|^$MARKER\$|$MARKER\n// Real upstream xxHash for XXH64() below -- see tools/patch_iographics_gaps.sh fix 9.\n#include <IOKit/graphics/IOGraphicsXXHash.h>|" "$F"
fi
if [ "$(grep -c "IOGraphicsXXHash.h" "$F")" != 1 ]; then
	echo "error: XXH64 include did not apply exactly once" >&2
	exit 1
fi

echo "=== IOGraphics gaps patched in $IOG ==="
echo "Re-run tools/iographics_gap_audit.sh to confirm."
