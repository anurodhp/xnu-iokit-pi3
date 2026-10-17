/*
 * Hand-built IOKitPersonalities for this board's built-in drivers, standing
 * in for the normal kext-Info.plist -> prelinked-kernel -> gIOCatalogue
 * pipeline, which does not exist in this bare Mach-O boot path (no
 * prelink/kext-cache infrastructure - see project memory). All of these
 * drivers are compiled directly into the kernel (see iokit/conf/files)
 * rather than as separate loadable kext bundles, so there is no Info.plist
 * for OSKext to parse; this is the only source of their personalities.
 *
 * Two real, separate gaps had to be closed here, discovered in order:
 *
 * 1. "intc"/"sdhci" nubs need a personality to match against, same as any
 *    IOKit driver. IODTPlatformExpert::createNubs()/createNub()
 *    (iokit/Kernel/IOPlatformExpert.cpp) already turns every device-tree
 *    node into a real IOPlatformDevice nub and registerService()'s it
 *    automatically - no board-specific code needed for that part.
 *
 * 2. The deeper, initially-missed gap: createNubs() for the top-level tree
 *    is only ever reached from IODTPlatformExpert::configure() ->
 *    processTopLevel(), which only runs if some concrete IODTPlatformExpert
 *    subclass successfully matches AND starts against the *root* nub in
 *    the first place. IODTPlatformExpert itself is
 *    OSDeclareAbstractStructors (deleteList()/excludeList() are pure
 *    virtual) - not instantiable via matching - and this kernel had zero
 *    personalities (confirmed: "0 kext personality sent to the
 *    IOCatalogue") before this file existed, except IOPanicPlatform's own
 *    built-in catch-all (iokit/KernelConfigTables.cpp's
 *    gIOKernelConfigTables, IOProviderClass=IOPlatformExpertDevice,
 *    IOProbeScore=0, no IONameMatch - deliberately generic, meant to be
 *    outscored by a real match). So root itself was only ever "claimed" by
 *    the neutered panic-platform catch-all (which correctly declines,
 *    returning false), and processTopLevel() never ran *for any node* -
 *    confirmed empirically by instrumenting createNubs()/startMatching()
 *    and seeing it fire exactly once, for "/", never for any child.
 *    BCM2837PlatformExpert (a trivial concrete IODTPlatformExpert
 *    subclass, see that file) plus the "root" personality below closes
 *    this - IOProbeScore is set explicitly higher than IOPanicPlatform's 0
 *    so it wins outright rather than relying on catalogue ordering.
 *
 * IOCatalogue::addDrivers()'s doNubMatching=true retroactively re-matches
 * already-registered nubs, so this can safely run either before or after
 * createNubs() - confirmed via IOStartIOKit.cpp/pe_init.c's actual call
 * order: InitIOKit() (where this runs) completes strictly before
 * ConfigureIOKit() -> configure() -> processTopLevel() creates the intc/
 * sdhci nubs, and actual matching doesn't start until StartIOKitMatching()
 * (PE_lockdown_iokit(), much later), so there's no ordering/race risk
 * either way. The root nub itself is a special case already created in
 * InitIOKit() (gRootNub) before our call, so root matching against our
 * new personality also benefits from doNubMatching's retroactive re-match.
 */

#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSNumber.h>
#include <IOKit/IOCatalogue.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSUnserialize.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>

// score < 0 means "don't set IOProbeScore, use kIODefaultProbeScore".
// nameMatch == NULL means "don't set IONameMatch, match by class alone" -
// needed for drivers like IOBlockStorageDriver that attach generically to
// any matching provider class rather than a specific named DT node.
static OSDictionary *
makePersonality(const char *providerClass, const char *ioClass, const char *nameMatch, int score)
{
	OSDictionary *dict = OSDictionary::withCapacity(4);
	if (dict == NULL) {
		return NULL;
	}

	OSString *providerClassStr = OSString::withCString(providerClass);
	OSString *classStr = OSString::withCString(ioClass);
	OSString *nameMatchStr = (nameMatch != NULL) ? OSString::withCString(nameMatch) : NULL;
	OSNumber *scoreNum = (score >= 0) ? OSNumber::withNumber(score, 32) : NULL;

	if (providerClassStr == NULL || classStr == NULL ||
	    (nameMatch != NULL && nameMatchStr == NULL) ||
	    (score >= 0 && scoreNum == NULL)) {
		OSSafeReleaseNULL(providerClassStr);
		OSSafeReleaseNULL(classStr);
		OSSafeReleaseNULL(nameMatchStr);
		OSSafeReleaseNULL(scoreNum);
		dict->release();
		return NULL;
	}

	dict->setObject(kIOProviderClassKey, providerClassStr);
	dict->setObject(kIOClassKey, classStr);
	if (nameMatchStr != NULL) {
		dict->setObject(kIONameMatchKey, nameMatchStr);
		nameMatchStr->release();
	}
	if (scoreNum != NULL) {
		dict->setObject(kIOProbeScoreKey, scoreNum);
		scoreNum->release();
	}

	providerClassStr->release();
	classStr->release();

	return dict;
}

/*
 * Add one numeric match property (bInterfaceClass, bDeviceSubClass, ...).
 *
 * IOUSBDevice::matchPropertyTable()/IOUSBInterface::matchPropertyTable()
 * compare these against the device's real descriptor fields, so the values
 * have to be genuine descriptor values, not conveniences. Returns false so a
 * caller can refuse to publish a half-built personality rather than one that
 * silently matches more (or less) than intended.
 */
static bool
addNumberProperty(OSDictionary *dict, const char *key, uint32_t value)
{
	if (dict == NULL) {
		return false;
	}
	OSNumber *num = OSNumber::withNumber(value, 32);
	if (num == NULL) {
		return false;
	}
	bool ok = dict->setObject(key, num);
	num->release();
	return ok;
}

/*
 * DAR-151: real "Content Table" for IOFDiskPartitionScheme
 * (kIOFDiskPartitionSchemeContentTable, third_party/IOStorageFamily/
 * IOFDiskPartitionScheme.cpp:49 - literally "Content Table"), translating
 * a raw MBR partition-type byte into a content-hint string
 * (instantiateMediaObject(), same file, ~line 486). With no table wired,
 * every partition's real Content property is just the raw "0xNN" hex
 * string, and iokit/bsddev/IOKitBSDInit.cpp's generic
 * IOProviderClass=IOMedia/Content=Apple_HFS root-matching fallback (used
 * when XNU boots with no rd= boot-arg, real hardware's actual default)
 * can never succeed without it - QEMU never hits this path since
 * runvm.sh always passes rd=disk0s2 explicitly.
 *
 * Real, unmodified table, copied verbatim from
 * third_party/IOStorageFamily/Info.plist's own "IOFDiskPartitionScheme"
 * -> "Content Table" dict (real Apple source, not invented) - this
 * project has no Info.plist/prelink pipeline (see this file's own
 * project-wide header comment), so personalities are hand-built here
 * instead, but the DATA is the real table, not a reduced guess.
 */
static bool
addContentTableEntry(OSDictionary *table, const char *hexKey, const char *contentHint)
{
	OSString *value = OSString::withCString(contentHint);
	if (value == NULL) {
		return false;
	}
	bool ok = table->setObject(hexKey, value);
	value->release();
	return ok;
}

static OSDictionary *
makeFDiskContentTable(void)
{
	OSDictionary *table = OSDictionary::withCapacity(24);
	if (table == NULL) {
		return NULL;
	}

	struct { const char *key; const char *hint; } entries[] = {
		{ "0x01", "DOS_FAT_12" },
		{ "0x04", "DOS_FAT_16_S" },
		{ "0x06", "DOS_FAT_16" },
		{ "0x07", "Windows_NTFS" },
		{ "0x0B", "DOS_FAT_32" },
		{ "0x0C", "Windows_FAT_32" },
		{ "0x0E", "Windows_FAT_16" },
		{ "0x14", "DOS_FAT_16_S_Hidden" },
		{ "0x16", "DOS_FAT_16_Hidden" },
		{ "0x42", "Windows_LDM" },
		{ "0x82", "Linux_Swap" },
		{ "0x83", "Linux" },
		{ "0x8E", "Linux_LVM" },
		{ "0xA5", "FreeBSD" },
		{ "0xA6", "OpenBSD" },
		{ "0xA7", "Apple_Rhapsody_UFS" },
		{ "0xA8", "Apple_UFS" },
		{ "0xA9", "NetBSD" },
		{ "0xAB", "Apple_Boot" },
		{ "0xAE", "Apple_Encrypted" },
		{ "0xAF", "Apple_HFS" },
		{ "0xFD", "Linux_RAID" },
	};

	for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
		if (!addContentTableEntry(table, entries[i].key, entries[i].hint)) {
			table->release();
			return NULL;
		}
	}

	return table;
}

/*
 * DAR-331: IOGraphics (vendored by tools/vendor_iographics_into_xnu.sh).
 *
 * 1. Module start. IOGraphicsFamilyModuleStart() (IOGraphicsFamily/
 *    IOFramebuffer.cpp:1978) is the family kext's MODULE_START
 *    (IOGraphics.xcodeproj/project.pbxproj:2188). It creates
 *    gIOFBSystemWorkLoop (it panics itself if it can't), the global
 *    framebuffer arrays, and the gtrace buffers that IOFramebuffer
 *    uses. Built into the kernel, nothing calls it. OSKext::load() runs
 *    a kext's start() (libkern/c++/OSKext.cpp:5814) before it sends the
 *    kext's personalities to the catalogue (:5831), and sends them only
 *    if start succeeded; and no class of a demand-loaded kext can be
 *    instantiated before that load has run start(). Same order here: it
 *    runs right before the IOGraphics personalities are added, from
 *    InitIOKit() (IOStartIOKit.cpp), before StartIOKitMatching(). If it
 *    fails, none of the IOGraphics personalities are published.
 *
 * 2. Apple's own personalities, verbatim from Info-IOGraphicsFamily.plist
 *    (IODisplayWrangler, AppleDisplay, AppleBacklightDisplay), minus
 *    CFBundleIdentifier, which no built-in personality in this file
 *    carries. IODisplayWrangler's two parameter dicts are real inputs:
 *    IOFramebuffer::setPreferences() reads both from it
 *    (IOFramebuffer.cpp:14358-14368).
 *
 * 3. IOBootFramebuffer has no published personality -- on Apple's
 *    platforms IONDRVSupport instantiates it, and that path is PCI/NDRV
 *    only. Ours matches the top-level "display" device-tree node
 *    (devicetrees/rpi3.dts), which carries the AAPL,boot-display
 *    property IOBootFramebuffer::probe() requires (IOBootFramebuffer.cpp:50).
 *    IOMatchCategory "IOFramebuffer" is the category Apple's own
 *    framebuffer personalities use (Info-IONDRVSupport.plist).
 */
extern "C" kern_return_t IOGraphicsFamilyModuleStart(kmod_info_t *ki, void *data);

static const char kIOGraphicsPersonalitiesXML[] =
    "<array>"
    "<dict>"
    "<key>IOClass</key><string>AppleBacklightDisplay</string>"
    "<key>IOProbeScore</key><integer>3000</integer>"
    "<key>IOProviderClass</key><string>IODisplayConnect</string>"
    "</dict>"
    "<dict>"
    "<key>IOClass</key><string>AppleDisplay</string>"
    "<key>IOProbeScore</key><integer>2000</integer>"
    "<key>IOProviderClass</key><string>IODisplayConnect</string>"
    "</dict>"
    "<dict>"
    "<key>IOClass</key><string>IODisplayWrangler</string>"
    "<key>IOGraphicsPrefsParameters</key><dict>"
    "<key>oscn</key><true/>"
    "<key>tbri</key><true/>"
    "<key>thue</key><true/>"
    "<key>tsat</key><true/>"
    "<key>vbst</key><true/>"
    "<key>cyuv</key><integer>268435456</integer>"
    "<key>pscn</key><true/>"
    "</dict>"
    "<key>IOGraphicsIgnoreParameters</key><dict>"
    "<key>aupc</key><true/>"
    "<key>auph</key><true/>"
    "<key>aupp</key><true/>"
    "<key>aums</key><true/>"
    "<key> bpc</key><true/>"
    "</dict>"
    "<key>IOMatchCategory</key><string>IOGraphics</string>"
    "<key>IOProviderClass</key><string>IOResources</string>"
    "<key>IOResourceMatch</key><string>IOKit</string>"
    "</dict>"
    "<dict>"
    "<key>IOClass</key><string>IOBootFramebuffer</string>"
    "<key>IOMatchCategory</key><string>IOFramebuffer</string>"
    "<key>IONameMatch</key><string>display</string>"
    "<key>IOProviderClass</key><string>IOPlatformDevice</string>"
    "</dict>"
    "</array>";

static void
addIOGraphicsPersonalities(OSArray *personalities)
{
	kern_return_t kr = IOGraphicsFamilyModuleStart(NULL, NULL);
	if (kr != KERN_SUCCESS) {
		IOLog("BCM2837RegisterBuiltinPersonalities: IOGraphicsFamilyModuleStart "
		    "failed (0x%x) - IOGraphics personalities NOT published\n", kr);
		return;
	}

	OSString *errorString = NULL;
	OSObject *parsed = OSUnserializeXML(kIOGraphicsPersonalitiesXML, &errorString);
	OSArray *iog = OSDynamicCast(OSArray, parsed);
	if (iog == NULL) {
		IOLog("BCM2837RegisterBuiltinPersonalities: IOGraphics personality XML "
		    "did not parse: %s\n",
		    errorString ? errorString->getCStringNoCopy() : "(no error string)");
	} else {
		personalities->merge(iog);
	}
	OSSafeReleaseNULL(parsed);
	OSSafeReleaseNULL(errorString);
}

/*
 * DAR-245: HID (vendored by tools/vendor_iohid_into_xnu.sh).
 *
 * Apple's own personalities, verbatim except for CFBundleIdentifier (no
 * built-in personality in this file carries one) and IOCFPlugInTypes
 * (userspace IOHIDLib plugin paths, meaningless without that userland):
 *
 *   IOHIDSystem          Info-IOHIDFamily.plist "IOHIDSystem"
 *                        (IOResources / IOResourceMatch IOBSD)
 *   IOHIDEventDriver     Info-IOHIDEventDriver.plist "HID Keyboard Driver"
 *                        (IOHIDInterface, DeviceUsagePairs GD/Keyboard,
 *                        GD/Keypad). Its IOMatchDefer is inert here:
 *                        NO_KEXTD compiles the defer path out
 *                        (IOService.cpp:3969).
 *   IOHIDEventDriver     Info-IOHIDEventDriverSafeBoot.plist "HID Pointing/
 *                        Consumer/System Control Driver" (IOHIDInterface,
 *                        DeviceUsagePairs GD/Mouse, GD/MultiAxis,
 *                        GD/SystemControl and the Button, Consumer,
 *                        Telephony, Digitizer and Camera pages). DAR-391:
 *                        mice (QEMU usb-mouse, real USB mice) get an
 *                        IOHIDEventService for pointer events. It is the one
 *                        pointer personality this IOHIDFamily tag
 *                        (1633.81.2) ships; Info-IOHIDEventDriver.plist has
 *                        only keyboard, buttons and sensor.
 *   IOUSBCompositeDriver IOUSBFamily-560.4.2 IOUSBCompositeDriver.plist
 *                        "IOUSBComposite" (IOUSBDevice, bDeviceClass 0 /
 *                        bDeviceSubClass 0: class defined per interface, which is
 *                        what keyboards report). It SetConfiguration()s the
 *                        device, which is what creates the IOUSBInterface nubs
 *                        below. Nothing else does that for a generic device.
 *   IOUSBHIDDriver x2    IOUSBFamily-560.4.2 IOUSBHIDDriverSafeBoot.plist
 *                        "IOUSBHIDDriver0"/"IOUSBHIDDriver1": any
 *                        IOUSBInterface with bInterfaceClass 3 and subclass 0
 *                        or 1, i.e. every USB HID interface. Chosen over
 *                        IOUSBHIDDriver.plist's "Generic Keyboard"
 *                        (subclass 1 / protocol 1 only), which would miss
 *                        keyboards that do not advertise boot protocol.
 *
 * IOHIDFamily has no MODULE_START (the IOHIDFamily target sets none in
 * IOHIDFamily.xcodeproj), so unlike IOGraphics nothing has to run first.
 *
 * Keys reach the console through IOHIDEventService's legacy keyboard shim
 * (IOHIDKeyboard -> IOHIDSystem -> cons_cinput, IOHIDSystem.cpp:2309-2317).
 * The shim is off unless boot-arg hid-legacy-shim=1 or single-user
 * (IOHIDEventService.cpp:311-330); runvm_hdmi.sh passes the boot-arg.
 */
static const char kIOHIDPersonalitiesXML[] =
    "<array><dict><key>HIDPowerOnDelayNS</key><integer>500000000</integer>"
    "<key>HIDScrollCountBootDefault</key><dict><key>HIDScrollCountAccelerationFactor</key>"
    "<integer>163840</integer><key>HIDScrollCountIgnoreMomentumScrolls</key><true/>"
    "<key>HIDScrollCountMax</key><integer>2000</integer>"
    "<key>HIDScrollCountMaxTimeDeltaBetween</key><integer>600</integer>"
    "<key>HIDScrollCountMaxTimeDeltaToSustain</key><integer>250</integer>"
    "<key>HIDScrollCountMinDeltaToStart</key><integer>30</integer>"
    "<key>HIDScrollCountMinDeltaToSustain</key><integer>20</integer>"
    "<key>HIDScrollCountMouseCanReset</key><true/></dict><key>HIDServiceGlobalModifiersUsage"
    "</key><integer>1</integer><key>HIDServiceSupport</key><true/><key>IOClass</key>"
    "<string>IOHIDSystem</string><key>IOMatchCategory</key><string>IOHID</string>"
    "<key>IOProviderClass</key><string>IOResources</string><key>IOResourceMatch</key>"
    "<string>IOBSD</string><key>PrimaryUsage</key><integer>23</integer><key>PrimaryUsagePage"
    "</key><integer>65280</integer><key>VendorID</key><integer>1452</integer></dict><dict>"
    "<key>DeviceUsagePairs</key><array><dict><key>DeviceUsage</key><integer>6</integer>"
    "<key>DeviceUsagePage</key><integer>1</integer></dict><dict><key>DeviceUsage</key>"
    "<integer>7</integer><key>DeviceUsagePage</key><integer>1</integer></dict></array>"
    "<key>HIDAccelCurves</key><array><dict><key>HIDAccelGainLinear</key><integer>65536"
    "</integer><key>HIDAccelIndex</key><integer>0</integer><key>HIDAccelTangentSpeedLinear"
    "</key><integer>524288</integer></dict><dict><key>HIDAccelGainCubic</key><integer>5243"
    "</integer><key>HIDAccelGainLinear</key><integer>60293</integer>"
    "<key>HIDAccelGainParabolic</key><integer>26214</integer><key>HIDAccelIndex</key>"
    "<integer>8192</integer><key>HIDAccelTangentSpeedLinear</key><integer>537395</integer>"
    "<key>HIDAccelTangentSpeedParabolicRoot</key><integer>1245184</integer></dict><dict>"
    "<key>HIDAccelGainCubic</key><integer>6554</integer><key>HIDAccelGainLinear</key>"
    "<integer>60948</integer><key>HIDAccelGainParabolic</key><integer>36045</integer>"
    "<key>HIDAccelIndex</key><integer>32768</integer><key>HIDAccelTangentSpeedLinear</key>"
    "<integer>543949</integer><key>HIDAccelTangentSpeedParabolicRoot</key><integer>1179648"
    "</integer></dict><dict><key>HIDAccelGainCubic</key><integer>7864</integer>"
    "<key>HIDAccelGainLinear</key><integer>61604</integer><key>HIDAccelGainParabolic</key>"
    "<integer>46531</integer><key>HIDAccelIndex</key><integer>45056</integer>"
    "<key>HIDAccelTangentSpeedLinear</key><integer>550502</integer>"
    "<key>HIDAccelTangentSpeedParabolicRoot</key><integer>1114112</integer></dict><dict>"
    "<key>HIDAccelGainCubic</key><integer>9830</integer><key>HIDAccelGainLinear</key>"
    "<integer>62259</integer><key>HIDAccelGainParabolic</key><integer>57672</integer>"
    "<key>HIDAccelIndex</key><integer>57344</integer><key>HIDAccelTangentSpeedLinear</key>"
    "<integer>557056</integer><key>HIDAccelTangentSpeedParabolicRoot</key><integer>1048576"
    "</integer></dict><dict><key>HIDAccelGainCubic</key><integer>11796</integer>"
    "<key>HIDAccelGainLinear</key><integer>62915</integer><key>HIDAccelGainParabolic</key>"
    "<integer>69468</integer><key>HIDAccelIndex</key><integer>65536</integer>"
    "<key>HIDAccelTangentSpeedLinear</key><integer>563610</integer>"
    "<key>HIDAccelTangentSpeedParabolicRoot</key><integer>983040</integer></dict><dict>"
    "<key>HIDAccelGainCubic</key><integer>14418</integer><key>HIDAccelGainLinear</key>"
    "<integer>63570</integer><key>HIDAccelGainParabolic</key><integer>81920</integer>"
    "<key>HIDAccelIndex</key><integer>98304</integer><key>HIDAccelTangentSpeedLinear</key>"
    "<integer>570163</integer><key>HIDAccelTangentSpeedParabolicRoot</key><integer>917504"
    "</integer></dict><dict><key>HIDAccelGainCubic</key><integer>17695</integer>"
    "<key>HIDAccelGainLinear</key><integer>64225</integer><key>HIDAccelGainParabolic</key>"
    "<integer>95027</integer><key>HIDAccelIndex</key><integer>131072</integer>"
    "<key>HIDAccelTangentSpeedLinear</key><integer>576717</integer>"
    "<key>HIDAccelTangentSpeedParabolicRoot</key><integer>851968</integer></dict><dict>"
    "<key>HIDAccelGainCubic</key><integer>21627</integer><key>HIDAccelGainLinear</key>"
    "<integer>64881</integer><key>HIDAccelGainParabolic</key><integer>108790</integer>"
    "<key>HIDAccelIndex</key><integer>163840</integer><key>HIDAccelTangentSpeedLinear</key>"
    "<integer>583270</integer><key>HIDAccelTangentSpeedParabolicRoot</key><integer>786432"
    "</integer></dict><dict><key>HIDAccelGainCubic</key><integer>26214</integer>"
    "<key>HIDAccelGainLinear</key><integer>65536</integer><key>HIDAccelGainParabolic</key>"
    "<integer>123208</integer><key>HIDAccelIndex</key><integer>196608</integer>"
    "<key>HIDAccelTangentSpeedLinear</key><integer>589824</integer>"
    "<key>HIDAccelTangentSpeedParabolicRoot</key><integer>786432</integer></dict></array>"
    "<key>HIDDefaultBehavior</key><string></string><key>IOClass</key><string>IOHIDEventDriver"
    "</string><key>IOMatchDefer</key><true/><key>IOProviderClass</key><string>IOHIDInterface"
    "</string></dict><dict><key>HIDDefaultBehavior</key><string></string><key>IOClass</key>"
    "<string>IOUSBHIDDriver</string><key>IOProviderClass</key><string>IOUSBInterface</string>"
    "<key>bInterfaceClass</key><integer>3</integer><key>bInterfaceSubClass</key><integer>0"
    "</integer></dict><dict><key>HIDDefaultBehavior</key><string></string><key>IOClass</key>"
    "<string>IOUSBHIDDriver</string><key>IOProviderClass</key><string>IOUSBInterface</string>"
    "<key>bInterfaceClass</key><integer>3</integer><key>bInterfaceSubClass</key><integer>1"
    "</integer></dict>"
    "<dict><key>DeviceUsagePairs</key><array><dict><key>DeviceUsage</key><integer>2</integer>"
    "<key>DeviceUsagePage</key><integer>1</integer></dict><dict><key>DeviceUsage</key><intege"
    "r>8</integer><key>DeviceUsagePage</key><integer>1</integer></dict><dict><key>DeviceUsage"
    "</key><integer>128</integer><key>DeviceUsagePage</key><integer>1</integer></dict><dict><"
    "key>DeviceUsagePage</key><integer>11</integer></dict><dict><key>DeviceUsagePage</key><in"
    "teger>12</integer></dict><dict><key>DeviceUsagePage</key><integer>13</integer></dict><di"
    "ct><key>DeviceUsagePage</key><integer>16</integer></dict><dict><key>DeviceUsagePage</key"
    "><integer>144</integer></dict></array><key>HIDAccelCurves</key><array><dict><key>HIDAcce"
    "lGainLinear</key><integer>65536</integer><key>HIDAccelIndex</key><integer>0</integer><ke"
    "y>HIDAccelTangentSpeedLinear</key><integer>524288</integer></dict><dict><key>HIDAccelGai"
    "nCubic</key><integer>5243</integer><key>HIDAccelGainLinear</key><integer>60293</integer>"
    "<key>HIDAccelGainParabolic</key><integer>26214</integer><key>HIDAccelIndex</key><integer"
    ">8192</integer><key>HIDAccelTangentSpeedLinear</key><integer>537395</integer><key>HIDAcc"
    "elTangentSpeedParabolicRoot</key><integer>1245184</integer></dict><dict><key>HIDAccelGai"
    "nCubic</key><integer>6554</integer><key>HIDAccelGainLinear</key><integer>60948</integer>"
    "<key>HIDAccelGainParabolic</key><integer>36045</integer><key>HIDAccelIndex</key><integer"
    ">32768</integer><key>HIDAccelTangentSpeedLinear</key><integer>543949</integer><key>HIDAc"
    "celTangentSpeedParabolicRoot</key><integer>1179648</integer></dict><dict><key>HIDAccelGa"
    "inCubic</key><integer>7864</integer><key>HIDAccelGainLinear</key><integer>61604</integer"
    "><key>HIDAccelGainParabolic</key><integer>46531</integer><key>HIDAccelIndex</key><intege"
    "r>45056</integer><key>HIDAccelTangentSpeedLinear</key><integer>550502</integer><key>HIDA"
    "ccelTangentSpeedParabolicRoot</key><integer>1114112</integer></dict><dict><key>HIDAccelG"
    "ainCubic</key><integer>9830</integer><key>HIDAccelGainLinear</key><integer>62259</intege"
    "r><key>HIDAccelGainParabolic</key><integer>57672</integer><key>HIDAccelIndex</key><integ"
    "er>57344</integer><key>HIDAccelTangentSpeedLinear</key><integer>557056</integer><key>HID"
    "AccelTangentSpeedParabolicRoot</key><integer>1048576</integer></dict><dict><key>HIDAccel"
    "GainCubic</key><integer>11796</integer><key>HIDAccelGainLinear</key><integer>62915</inte"
    "ger><key>HIDAccelGainParabolic</key><integer>69468</integer><key>HIDAccelIndex</key><int"
    "eger>65536</integer><key>HIDAccelTangentSpeedLinear</key><integer>563610</integer><key>H"
    "IDAccelTangentSpeedParabolicRoot</key><integer>983040</integer></dict><dict><key>HIDAcce"
    "lGainCubic</key><integer>14418</integer><key>HIDAccelGainLinear</key><integer>63570</int"
    "eger><key>HIDAccelGainParabolic</key><integer>81920</integer><key>HIDAccelIndex</key><in"
    "teger>98304</integer><key>HIDAccelTangentSpeedLinear</key><integer>570163</integer><key>"
    "HIDAccelTangentSpeedParabolicRoot</key><integer>917504</integer></dict><dict><key>HIDAcc"
    "elGainCubic</key><integer>17695</integer><key>HIDAccelGainLinear</key><integer>64225</in"
    "teger><key>HIDAccelGainParabolic</key><integer>95027</integer><key>HIDAccelIndex</key><i"
    "nteger>131072</integer><key>HIDAccelTangentSpeedLinear</key><integer>576717</integer><ke"
    "y>HIDAccelTangentSpeedParabolicRoot</key><integer>851968</integer></dict><dict><key>HIDA"
    "ccelGainCubic</key><integer>21627</integer><key>HIDAccelGainLinear</key><integer>64881</"
    "integer><key>HIDAccelGainParabolic</key><integer>108790</integer><key>HIDAccelIndex</key"
    "><integer>163840</integer><key>HIDAccelTangentSpeedLinear</key><integer>583270</integer>"
    "<key>HIDAccelTangentSpeedParabolicRoot</key><integer>786432</integer></dict><dict><key>H"
    "IDAccelGainCubic</key><integer>26214</integer><key>HIDAccelGainLinear</key><integer>6553"
    "6</integer><key>HIDAccelGainParabolic</key><integer>123208</integer><key>HIDAccelIndex</"
    "key><integer>196608</integer><key>HIDAccelTangentSpeedLinear</key><integer>589824</integ"
    "er><key>HIDAccelTangentSpeedParabolicRoot</key><integer>786432</integer></dict></array><"
    "key>HIDDefaultBehavior</key><string></string><key>IOClass</key><string>IOHIDEventDriver<"
    "/string><key>IOMatchDefer</key><true/><key>IOProviderClass</key><string>IOHIDInterface</"
    "string></dict>"
    "<dict><key>IOClass</key><string>IOUSBCompositeDriver</string>"
    "<key>IOProviderClass</key><string>IOUSBDevice</string>"
    "<key>bDeviceClass</key><integer>0</integer>"
    "<key>bDeviceSubClass</key><integer>0</integer></dict></array>";

static void
addIOHIDPersonalities(OSArray *personalities)
{
	OSString *errorString = NULL;
	OSObject *parsed = OSUnserializeXML(kIOHIDPersonalitiesXML, &errorString);
	OSArray *hid = OSDynamicCast(OSArray, parsed);
	if (hid == NULL) {
		IOLog("BCM2837RegisterBuiltinPersonalities: HID personality XML "
		    "did not parse: %s\n",
		    errorString ? errorString->getCStringNoCopy() : "(no error string)");
	} else {
		personalities->merge(hid);
	}
	OSSafeReleaseNULL(parsed);
	OSSafeReleaseNULL(errorString);
}

extern "C" void
BCM2837RegisterBuiltinPersonalities(void)
{
	OSArray *personalities = OSArray::withCapacity(8);
	if (personalities == NULL) {
		IOLog("BCM2837RegisterBuiltinPersonalities: OSArray::withCapacity failed\n");
		return;
	}

	// Root's own synthesized DT "name" property is "/" (empirically
	// confirmed via IOService::startMatching's own diagnostic log during
	// development - the root DT node in devicetrees/rpi3.dts has no
	// explicit compatible/model/name property, so the ADT compiler's
	// automatic name-from-node-label synthesis, DT__AddChild() in
	// third_party/dtc-AppleDeviceTree/AppleDeviceTree.c, gives it "/").
	// Score 1000 to beat IOPanicPlatform's IOProbeScore=0 outright.
	OSDictionary *root = makePersonality("IOPlatformExpertDevice", "BCM2837PlatformExpert", "/", 1000);
	// "local-intc" - top of the interrupt tree, root-level sibling of
	// "arm-io" (see devicetrees/rpi3.dts header comment on that node for
	// why it isn't nested under arm-io). "intc"'s own upstream interrupt
	// cascades into this controller - see BCM2837LocalIntc.h.
	OSDictionary *localIntc = makePersonality("IOPlatformDevice", "BCM2837LocalIntc", "local-intc", -1);
	// "arm-io" is the SoC peripheral bus container "intc"/"sdhci" actually
	// live under (devicetrees/rpi3.dts) - see BCM2837ArmIO.h for why a
	// driver for it is needed at all before its children can be published.
	OSDictionary *armio = makePersonality("IOPlatformDevice", "BCM2837ArmIO", "arm-io", -1);
	OSDictionary *intc = makePersonality("IOPlatformDevice", "BCM2837InterruptController", "intc", -1);
	OSDictionary *sdhci = makePersonality("IOPlatformDevice", "BCM2837SDHCI", "sdhci", -1);
	// "sdhost" - the second SD controller. BCM2837SDHost::probe() declines
	// unless boot-arg wifi=1, which moves the SD card there so "sdhci" can
	// drive the WiFi chip (docs/wifi-plan.md).
	OSDictionary *sdhost = makePersonality("IOPlatformDevice", "BCM2837SDHost", "sdhost", -1);
	// The WiFi driver matches the SDIO nub BCM2837SDHCI publishes in SDIO
	// mode (wifi=1 only).
	OSDictionary *wifi = makePersonality("BCM2837SDIODevice", "BCM43430Wifi", NULL, -1);
	// "bt-uart" - the PL011 wired to the Bluetooth controller.
	// BCM43430Bluetooth::probe() declines unless boot-arg bt=1
	// (docs/bluetooth-plan.md).
	OSDictionary *bt = makePersonality("IOPlatformDevice", "BCM43430Bluetooth", "bt-uart", -1);
	// "dma" - the DMA controller (DARW-313), used by the audio engine.
	OSDictionary *dma = makePersonality("IOPlatformDevice", "BCM2837DMA", "dma", -1);
	// "pwm" - the PWM block, the 3.5mm audio jack (DARW-314). The DT node is
	// only present in builds that define RPI3_HAVE_PWM.
	OSDictionary *pwm = makePersonality("IOPlatformDevice", "BCM2837PWM", "pwm", -1);
	// The audio device matches on the PWM service itself (DARW-315).
	OSDictionary *pwmAudio = makePersonality("BCM2837PWM", "BCM2837PWMAudioDevice", NULL, -1);
	// "usb" - the DWC2 USB 2.0 OTG host controller, the board's only USB
	// silicon and therefore the gate on all Phase 6 networking (the SMSC
	// LAN9514 Ethernet chip is a USB device behind it). See BCM2837DWC2.h
	// and docs/usb-support-plan.md.
	OSDictionary *usb = makePersonality("IOPlatformDevice", "BCM2837DWC2", "usb", -1);
	// "v3d" - the VideoCore IV 3D engine (DARW-295). The DT node exists
	// only in builds that define RPI3_HAVE_V3D (rpi3-realhw.dts), so this
	// personality is inert under QEMU.
	OSDictionary *v3d = makePersonality("IOPlatformDevice", "BCM2837V3D", "v3d", -1);
	// "vchiq" - the VCHIQ message queue to the VPU firmware (its video
	// decoder, for one). The DT node exists only in builds that define
	// RPI3_HAVE_VCHIQ (rpi3-realhw.dts); QEMU raspi3 has no VPU.
	OSDictionary *vchiq = makePersonality("IOPlatformDevice", "BCM2837VCHIQ", "vchiq", -1);
	// IOBlockStorageDriver (real apple-oss-distributions/IOStorageFamily,
	// vendored into iokit/Storage/ and iokit/IOKit/storage/, matching
	// vintage IOStorageFamily-218.1.4 - the initially-vendored 337.100.1
	// used IOMallocType(), a newer API this xnu-7195 tree doesn't have)
	// matches generically against *any* IOBlockStorageDevice, no
	// IONameMatch - same as real macOS's own IOStorageFamily kext
	// personality. Publishes the whole-disk IOMedia a filesystem (msdosfs)
	// can mount directly, superfloppy-style - no partition scheme wired up
	// yet (see docs/sd-and-ethernet-plan.md's sequencing; deliberately out
	// of scope for the first proof-of-concept boot).
	OSDictionary *blockStorageDriver = makePersonality("IOBlockStorageDevice", "IOBlockStorageDriver", NULL, -1);
	// IOMediaBSDClient matches generically against any IOMedia and creates
	// its /dev/diskN block+character device nodes - needed for BSD-level
	// device lookup (IOFindBSDRoot()/mountroot, "rd=disk0" boot-args) to
	// find our whole-disk IOMedia at all.
	OSDictionary *mediaBSDClient = makePersonality("IOMedia", "IOMediaBSDClient", NULL, -1);
	// IOFDiskPartitionScheme (real apple-oss-distributions/IOStorageFamily,
	// vendored the same way as the above) matches generically against any
	// IOMedia too, but its own probe() reads the provider's first sector
	// and only claims it (nonzero score) if a real MBR signature (0x55AA)
	// is present - so it naturally declines on the msdosfs-superfloppy
	// whole-disk media used so far, and on its own partition children
	// (which don't carry a nested MBR), with no isWhole() check needed
	// here. Publishes one child IOMedia per MBR entry (disk0s1, disk0s2,
	// ...), each of which matches IOMediaBSDClient/IOBlockStorageDriver
	// again automatically via doNubMatching - this is what makes a real
	// two-partition (FAT32 boot + HFS root) SD card resolve to a distinct
	// root-mountable BSD device instead of the whole card being treated
	// as one filesystem blob. See docs/roadmap.md Phase 3.5b.
	//
	// Real bug fix: without an explicit IOMatchCategory, both this and
	// mediaBSDClient above fall into IOKit's shared default match
	// category for the same IOMedia provider (IOService.cpp's
	// gIODefaultMatchCategoryKey fallback) - only one client per category
	// per provider is ever actually start()'d (copyClientWithCategory()'s
	// exclusivity check), so IOFDiskPartitionScheme's probe() was
	// confirmed (via temp IOLog) to succeed and find both partitions, yet
	// never reached start() because IOMediaBSDClient had already claimed
	// the default category first. Real apple-oss-distributions storage
	// drivers avoid this by placing themselves in the dedicated
	// "IOStorage" category (kIOStorageCategory, iokit/IOKit/storage/
	// IOStorage.h) reserved exactly for this - IOMedia.cpp's own
	// attachToChild()/handleOpen() already special-case clients tagged
	// with it. mediaBSDClient deliberately stays in the default category
	// (it is not a storage-interpreting driver).
	OSDictionary *fdiskScheme = makePersonality("IOMedia", "IOFDiskPartitionScheme", NULL, -1);
	if (fdiskScheme != NULL) {
		OSString *storageCategory = OSString::withCString("IOStorage");
		if (storageCategory != NULL) {
			fdiskScheme->setObject(kIOMatchCategoryKey, storageCategory);
			storageCategory->release();
		}
		// DAR-151: see makeFDiskContentTable()'s own header comment.
		OSDictionary *contentTable = makeFDiskContentTable();
		if (contentTable != NULL) {
			fdiskScheme->setObject("Content Table", contentTable);
			contentTable->release();
		} else {
			IOLog("BCM2837RegisterBuiltinPersonalities: makeFDiskContentTable failed\n");
		}
	}
	// "cpu0" - closes the real ARM64-IRQ-delivery gap found investigating
	// why BCM2837SDHCI never sees a real interrupt reach handleInterrupt()
	// despite the hardware genuinely raising it: nothing ever called
	// ml_install_interrupt_handler() (osfmk/arm64/machine_routines.c) for
	// this CPU, so sleh_irq() had nothing real to dispatch to. See
	// BCM2837CPU.h for the full chain.
	OSDictionary *cpu0 = makePersonality("IOPlatformDevice", "BCM2837CPU", "cpu0", -1);
	// DAR-455: the secondary cores. BCM2837CPU::probe() only accepts a
	// cpuN nub that IODTPlatformExpert::createNubs() gave a
	// "logical-cpu-id", i.e. one ml_parse_cpu_topology() kept, which on
	// BCM2837 means the kernel was booted with cpus=N (default 1). So
	// these are inert on every default boot.
	OSDictionary *cpuN[3] = {
		makePersonality("IOPlatformDevice", "BCM2837CPU", "cpu1", -1),
		makePersonality("IOPlatformDevice", "BCM2837CPU", "cpu2", -1),
		makePersonality("IOPlatformDevice", "BCM2837CPU", "cpu3", -1),
	};

	/*
	 * --- Real Apple AppleUSBCDCDriver (CDC-ECM) personalities ---
	 *
	 * These match against the IOUSBDevice/IOUSBInterface nubs
	 * BCM2837USBHostController now publishes (DAR-243). Before that
	 * controller existed there was nothing of either class in the registry
	 * and these three entries would have been permanently dead - which is
	 * exactly why they were deliberately NOT added in the earlier vendoring
	 * pass.
	 *
	 * Values are transcribed from Apple's own shipped plists in
	 * third_party/AppleUSBCDCDriver/Plists/ (AppleUSBCDC-Info.plist's
	 * "AppleUSBCDCDevice200", AppleUSBCDCECMControl-Info.plist, and
	 * AppleUSBCDCECMData-Info.plist's "AppleUSBCDCECMData" - NOT its
	 * "AppleUSBCDCECMData1" sibling, which wants bInterfaceSubClass 6).
	 * They were independently cross-checked against QEMU's own
	 * desc_iface_cdc[] in third_party/qemu-zhuowei/hw/usb/dev-network.c,
	 * which builds the CDC control interface as class 2 / subclass 6 /
	 * protocol 0 and the data interface as class 10 / subclass 0.
	 *
	 * This is additive, not a replacement for SMSC9514Ethernet: a real Pi 3
	 * has a LAN9514, which is a vendor-specific (class 0xFF) device that
	 * matches none of these, and is still hand-wired in
	 * BCM2837DWC2::start(). The CDC path is what makes QEMU's usb-net
	 * usable, which is what DAR-170 needs to verify DHCP end-to-end without
	 * real hardware.
	 */

	// AppleUSBCDC is the master/common driver: it matches the DEVICE, picks
	// the CDC configuration and calls SetConfiguration(), which is what
	// causes the IOUSBInterface nubs the two drivers below match against to
	// be published at all. Without it the interface personalities never get
	// anything to match. IOProbeScore 10000 is Apple's own value.
	OSDictionary *cdcDevice = makePersonality("IOUSBDevice", "AppleUSBCDC", NULL, 10000);
	if (cdcDevice != NULL &&
	    !(addNumberProperty(cdcDevice, "bDeviceClass", 2) &&
	      addNumberProperty(cdcDevice, "bDeviceSubClass", 0) &&
	      addNumberProperty(cdcDevice, "bDeviceProtocol", 0))) {
		cdcDevice->release();
		cdcDevice = NULL;
	}

	// CDC-ECM control interface: carries the interrupt endpoint that reports
	// link state and the class requests that set the packet filter.
	OSDictionary *cdcEcmControl = makePersonality("IOUSBInterface",
	    "AppleUSBCDCECMControl", NULL, -1);
	if (cdcEcmControl != NULL &&
	    !(addNumberProperty(cdcEcmControl, "bInterfaceClass", 2) &&
	      addNumberProperty(cdcEcmControl, "bInterfaceSubClass", 6))) {
		cdcEcmControl->release();
		cdcEcmControl = NULL;
	}

	// CDC-ECM data interface: the bulk IN/OUT pair, and the driver that
	// actually publishes an IOEthernetInterface (i.e. en0).
	OSDictionary *cdcEcmData = makePersonality("IOUSBInterface",
	    "AppleUSBCDCECMData", NULL, -1);
	if (cdcEcmData != NULL &&
	    !(addNumberProperty(cdcEcmData, "bInterfaceClass", 10) &&
	      addNumberProperty(cdcEcmData, "bInterfaceSubClass", 0))) {
		cdcEcmData->release();
		cdcEcmData = NULL;
	}

	if (root != NULL) {
		personalities->setObject(root);
		root->release();
	}
	if (localIntc != NULL) {
		personalities->setObject(localIntc);
		localIntc->release();
	}
	if (armio != NULL) {
		personalities->setObject(armio);
		armio->release();
	}
	if (intc != NULL) {
		personalities->setObject(intc);
		intc->release();
	}
	if (sdhci != NULL) {
		personalities->setObject(sdhci);
		sdhci->release();
	}
	if (sdhost != NULL) {
		personalities->setObject(sdhost);
		sdhost->release();
	}
	if (wifi != NULL) {
		personalities->setObject(wifi);
		wifi->release();
	}
	if (bt != NULL) {
		personalities->setObject(bt);
		bt->release();
	}
	if (dma != NULL) {
		personalities->setObject(dma);
		dma->release();
	}
	if (pwm != NULL) {
		personalities->setObject(pwm);
		pwm->release();
	}
	if (pwmAudio != NULL) {
		personalities->setObject(pwmAudio);
		pwmAudio->release();
	}
	if (usb != NULL) {
		personalities->setObject(usb);
		usb->release();
	}
	if (v3d != NULL) {
		personalities->setObject(v3d);
		v3d->release();
	}
	if (vchiq != NULL) {
		personalities->setObject(vchiq);
		vchiq->release();
	}
	if (blockStorageDriver != NULL) {
		personalities->setObject(blockStorageDriver);
		blockStorageDriver->release();
	}
	if (mediaBSDClient != NULL) {
		// TEMP DIAGNOSTIC / real bug fix: this personality was built but
		// never added to the array before this fix - IOMediaBSDClient
		// never actually matched/started, so /dev/diskN was never
		// created, which would have silently defeated FAT32/HFS+ root
		// mount regardless of anything else in the boot path.
		personalities->setObject(mediaBSDClient);
		mediaBSDClient->release();
	}
	if (fdiskScheme != NULL) {
		personalities->setObject(fdiskScheme);
		fdiskScheme->release();
	}
	if (cpu0 != NULL) {
		personalities->setObject(cpu0);
		cpu0->release();
	}
	for (unsigned i = 0; i < sizeof(cpuN) / sizeof(cpuN[0]); i++) {
		if (cpuN[i] != NULL) {
			personalities->setObject(cpuN[i]);
			cpuN[i]->release();
		}
	}
	if (cdcDevice != NULL) {
		personalities->setObject(cdcDevice);
		cdcDevice->release();
	}
	if (cdcEcmControl != NULL) {
		personalities->setObject(cdcEcmControl);
		cdcEcmControl->release();
	}
	if (cdcEcmData != NULL) {
		personalities->setObject(cdcEcmData);
		cdcEcmData->release();
	}

	/*
	 * AppleUSBHub (DAR-353): Apple's hub driver, for hot-plug. Its two
	 * personalities are AppleUSBHub.plist's AppleUSBHub0/AppleUSBHub1
	 * (third_party/IOUSBFamily/AppleUSBHub/AppleUSBHub.plist): IOUSBDevice
	 * with bDeviceClass 9, bDeviceSubClass 0 or 1.
	 *
	 * Opt-in via the usb-apple-hub boot-arg while it is brought up. It
	 * matches the family's root hub device too, and from there resets the
	 * root port and enumerates the whole bus itself - so it must not run
	 * alongside BCM2837DWC2's own boot-time enumeration, which that
	 * boot-arg also turns off (BCM2837DWC2::start()).
	 */
	/*
	 * SMSC9514USBDriver (DAR-353 stage 3): the LAN9514's Ethernet function
	 * as an ordinary IOUSBDevice, matched by vendor/product the way a vendor
	 * USB Ethernet driver matches on a Mac. Only AppleUSBHub publishes that
	 * device (with usb-apple-hub); the legacy path hand-wires
	 * SMSC9514Ethernet and never publishes it (BCM2837DWC2::publishable()),
	 * so this personality is inert there. 0x0424/0xec00: SMSC's vendor ID
	 * and the LAN9512/LAN9514 Ethernet PID (Linux smsc95xx.c's device
	 * table, USB_DEVICE(0x0424, 0xec00)).
	 */
	OSDictionary *smscUSB = makePersonality("IOUSBDevice", "SMSC9514USBDriver", NULL, -1);
	if (smscUSB != NULL) {
		if (addNumberProperty(smscUSB, "idVendor", 0x0424) &&
		    addNumberProperty(smscUSB, "idProduct", 0xec00)) {
			personalities->setObject(smscUSB);
		}
		smscUSB->release();
	}

	// Default on, as BCM2837DWC2::start(); usb-apple-hub=0 turns it off.
	uint32_t appleHub = 1;
	PE_parse_boot_argn("usb-apple-hub", &appleHub, sizeof(appleHub));
	if (appleHub != 0) {
		for (uint32_t sub = 0; sub <= 1; sub++) {
			OSDictionary *hub = makePersonality("IOUSBDevice", "AppleUSBHub", NULL, -1);
			if (hub == NULL) {
				continue;
			}
			if (addNumberProperty(hub, "bDeviceClass", 9) &&
			    addNumberProperty(hub, "bDeviceSubClass", sub)) {
				personalities->setObject(hub);
			}
			hub->release();
		}
	}

	addIOGraphicsPersonalities(personalities);
	addIOHIDPersonalities(personalities);

	IOLog("BCM2837RegisterBuiltinPersonalities: built %u personalities, gIOCatalogue=%p\n",
	    personalities->getCount(), (void *)(IOCatalogue *)gIOCatalogue);

	if (personalities->getCount() > 0) {
		bool ok = gIOCatalogue->addDrivers(personalities, true);
		IOLog("BCM2837RegisterBuiltinPersonalities: addDrivers returned %d\n", ok);
	}
	personalities->release();
}
