/*
 * compat/iohid/IOHIDFamilyPrivateMissing.h -- DAR-245.
 *
 * Force-included (-include) into every IOHIDFamily-1633.81.2 kernel
 * object by tools/iohid_gap_audit.sh and the vendoring step. It supplies
 * declarations that the drop's .cpp files use but that none of its own
 * published headers declare. This is the same class of gap as IOGraphics
 * (compat/IOKit/graphics/IOGraphicsTypesPrivateMissing.h): Apple ships
 * its private headers stripped. AppleHIDUsageTables.h is a 22-line
 * license-only stub in EVERY published tag from 1090 to 2238, and
 * IOHIDEventServiceKeys_Private.h in 1633 carries 8 of the dozens of
 * keys the sources use.
 *
 * AppleHIDUsageTables.h itself is NOT handled here. The anurodhp/IOHIDFamily
 * fork (iokit-port, DAR-356) replaces the stub with Apple's last real
 * published version
 * (IOHIDFamily-1035.70.7, 268 lines), so everything that tag defines
 * comes from Apple's own file.
 *
 * Every definition below is one of three kinds, and says which:
 *
 *   [2238] Apple's real value, published later in IOHIDFamily-2238.120.5
 *          IOHIDFamily/IOHIDEventServiceKeys_Private.h (line cited). The
 *          code paths that use these are identical in 1633 and 2238.
 *
 *   [ioreg] Not published in any of the 116 tags (every tag was searched
 *          with `git grep` for a #define or enum). The string value is the
 *          property name observed in the live IORegistry of a real macOS
 *          host (Darwin 27) running Apple's shipped IOHIDFamily, on the
 *          same classes that set it here. The key-to-name mapping follows
 *          Apple's kIOHID<Name>Key -> "<Name>" convention, which every
 *          [2238] key below obeys; kIOHIDDKStartKey -> "HIDDKStart" is the
 *          one exception, taken as observed.
 *
 *   [recon] Not published and not observable. Every one is a property the
 *          family sets and reads ITSELF, or reads from a personality
 *          property this project never sets (cited per key). Only
 *          self-consistency matters, so the value is Apple's naming
 *          convention and is marked as reconstructed.
 *
 * NUMERIC USAGES THAT NO TAG PUBLISHES are given values ABOVE 0xFFFF.
 * HID usage pages and usages are 16-bit on the wire (HID 1.11 section
 * 6.2.2.7), while every comparison site here is UInt32 (e.g.
 * IOHIDDevice.cpp:1356-1357, IOHIDEventDriver.cpp's switch(usage)). So
 * these values can never equal a real device's page or usage, and the
 * Apple-hardware-only paths they guard (motion sensors, trackpad phases,
 * BT sniff) are provably unreachable. Guessing a 16-bit value would risk
 * colliding with a third-party vendor-defined page in 0xFF00-0xFFFF and
 * routing a real device into Apple-only code.
 */
#ifndef _COMPAT_IOHID_FAMILY_PRIVATE_MISSING_H
#define _COMPAT_IOHID_FAMILY_PRIVATE_MISSING_H

/* ---- [2238] IOHIDEventServiceKeys_Private.h -------------------------- */
#define kIOHIDEventTypeKey                      "EventType"              /* :66  */
#define kIOHIDUsagePageKey                      "UsagePage"              /* :76  */
#define kIOHIDUsageKey                          "Usage"                  /* :85  */
#define kIOHIDKeyboardEnabledKey                "KeyboardEnabled"        /* :113 */
#define kIOHIDKeyboardEnabledEventKey           "KeyboardEnabledEvent"   /* :125 */
#define kIOHIDKeyboardEnabledEventUsagePageKey  "UsagePage"              /* :131 */
#define kIOHIDKeyboardEnabledEventUsageKey      "Usage"                  /* :137 */
#define kIOHIDKeyboardEnabledEventEventTypeKey  "EventType"              /* :143 */
#define kIOHIDKeyboardEnabledByEventKey         "KeyboardEnabledByEvent" /* :150 */
#define kIOHIDEventServiceSensorPropertySupportedKey "SensorPropertySupported" /* :197 */
#define kIOHIDSensorPropertySniffControlKey     "SniffControl"           /* :247 */
enum {                                                          /* :205-233 */
	kSensorPropertyReportInterval   = (1 << 0),
	kSensorPropertyReportLatency    = (1 << 1),
	kSensorPropertySniffControl     = (1 << 2),
	kSensorPropertySampleInterval   = (1 << 3),
	kSensorPropertyMaxFIFOEvents    = (1 << 4),
};
/*
 * 2238 :95 documents this exact semantic ("Number or array of numbers that
 * represents vendor usage pairs") under the name kPrimaryVendorUsages =
 * "PrimaryVendorUsages"; 1633's IOHIDEventDriver.cpp:2320 reads it as
 * kIOHIDPrimaryVendorUsagesKey, and 2238's own IOHIDEventDriver.cpp:2524
 * uses the same identifier on the same line of code.
 */
#define kIOHIDPrimaryVendorUsagesKey            "PrimaryVendorUsages"

/* ---- [ioreg] ---------------------------------------------------------- */
/* Read before registerService (IOHIDDevice.cpp:477, IOHIDEventService.cpp:346),
 * set false by IOHIDResourceUserClient.cpp:520; seen as "RegisterService" on
 * IOHIDDevice and IOHIDEventService nodes. */
#define kIOHIDRegisterServiceKey                "RegisterService"
/* Seen as "HIDDKStart" on IOHIDDevice/IOHIDEventService nodes. Set by
 * IOHIDDevice.cpp:2303 / IOHIDEventService.cpp:2652 when a DriverKit
 * service starts, read by AppleUserHIDDevice.cpp:165. */
#define kIOHIDDKStartKey                        "HIDDKStart"
/* Seen as "RequiresTCCAuthorization" on keyboard IOHIDDevice nodes;
 * set at IOHIDDevice.cpp:466. */
#define kIOHIDRequiresTCCAuthorizationKey       "RequiresTCCAuthorization"

/* Seen as "DeviceTypeHint" = "Headset" on a headset's HID nodes. Set by
 * IOHIDEventService.cpp:338-340 when supportsHeadset() matches. */
#define kIOHIDDeviceTypeHintKey                 "DeviceTypeHint"
#define kIOHIDDeviceTypeHeadsetKey              "Headset"

/* ---- [recon] ---------------------------------------------------------- */
/* Set by IOHIDDevice.cpp:745 (newIsAccessProtected), read back at :1384,
 * copied by IOHIDEventService.cpp:278-281, read by IOHIDLibUserClient.cpp:333. */
#define kIOHIDProtectedAccessKey                "ProtectedAccess"
/* Set by IOHIDDevice.cpp:386, read at :389 and IOHIDInterface.cpp:345.
 * Only set for an Apple-vendor multi-interface collection. */
#define kIOHIDMultipleInterfaceEnabledKey       "MultipleInterfaceEnabled"
/* Personality-supplied arrays (IOHIDEventDriver.cpp:530, gIOServicePlane
 * lookup). No personality in this project sets them, so getBlessedUsagePairs()
 * takes its "no blessed pairs" early exit. */
#define kIOHIDEventDriverBlessedUsagePairsKey   "BlessedUsagePairs"
#define kIOHIDEventDriverBlessedUsagePageKey    "UsagePage"
#define kIOHIDEventDriverBlessedUsageKey        "Usage"
/* Personality-supplied bool (IOHIDEventDriver.cpp:1026); not set here. */
#define kIOHIDDigitizerCollectionDispatchKey    "DigitizerCollectionDispatch"
/* Set true on the user client itself as a temporary marker
 * (IOHIDLibUserClient.cpp:280), then read from it (:1776, :1981) and from
 * the provider chain (:1983). */
#define kIOHIDExtendedDataKey                   "ExtendedData"
/* Key in an IOHIDUserDevice (userspace virtual HID) property dictionary,
 * IOHIDResourceUserClient.cpp:1073. No userspace creates one here. */
#define kIOHIDUserDeviceSuspendKey              "Suspend"

/*
 * The two list macros are the bodies of static `const OSSymbol *[]`
 * initialisers (IOHIDDevice.cpp:1053, IOHIDInterface.cpp:422,
 * IOHIDEventService.cpp:561) iterated with
 * `if (key->isEqualTo(prop))`. They name the properties that are forwarded
 * to the owner, or trigger a kIOMessageServicePropertyChange, when set
 * from userspace. Apple's list is unpublished. A single nullptr makes
 * both loops no-ops: `prop` is `const OSSymbol *`, so the overload taken
 * is OSSymbol::isEqualTo(const OSSymbol *), which is `return aSymbol ==
 * this;` (xnu-7195 libkern/c++/OSSymbol.cpp:702-705), false for NULL. With no userspace HID client
 * on this board, nothing sets those properties anyway. An empty `{}`
 * is not an option: a zero-length array is ill-formed in C++.
 */
#define kIOHIDDeviceMessagePropertyUpdateKeys   nullptr
#define kIOHIDPropagatePropertyKeys             nullptr

/* ---- [recon] numeric usages: values > 0xFFFF, see header comment ------ */
enum {
	kHIDPage_AppleVendorHIDEvent                               = 0x10001,
	kHIDPage_AppleVendorSensor                                 = 0x10002,

	kHIDUsage_AppleVendor_MultipleInterfaces                   = 0x10101,

	kHIDUsage_AppleVendorHIDEvent_PhaseBegan                   = 0x10201,
	kHIDUsage_AppleVendorHIDEvent_PhaseChanged                 = 0x10202,
	kHIDUsage_AppleVendorHIDEvent_PhaseEnded                   = 0x10203,
	kHIDUsage_AppleVendorHIDEvent_PhaseCancelled               = 0x10204,
	kHIDUsage_AppleVendorHIDEvent_PhaseMayBegin                = 0x10205,

	kHIDUsage_AppleVendorKeyboard_LongPress                    = 0x10301,

	kHIDUsage_AppleVendorMotion_Type                           = 0x10401,
	kHIDUsage_AppleVendorMotion_Path                           = 0x10402,
	kHIDUsage_AppleVendorMotion_Generation                     = 0x10403,
	kHIDUsage_AppleVendorMotion_DeviceOrientationTypeAmbiguous          = 0x10411,
	kHIDUsage_AppleVendorMotion_DeviceOrientationTypePortrait           = 0x10412,
	kHIDUsage_AppleVendorMotion_DeviceOrientationTypePortraitUpsideDown = 0x10413,
	kHIDUsage_AppleVendorMotion_DeviceOrientationTypeLandscapeLeft      = 0x10414,
	kHIDUsage_AppleVendorMotion_DeviceOrientationTypeLandscapeRight     = 0x10415,
	kHIDUsage_AppleVendorMotion_DeviceOrientationTypeFaceUp             = 0x10416,
	kHIDUsage_AppleVendorMotion_DeviceOrientationTypeFaceDown           = 0x10417,

	kHIDUsage_AppleVendorSensor_BTSniffOff                     = 0x10501,
};

#endif /* _COMPAT_IOHID_FAMILY_PRIVATE_MISSING_H */
