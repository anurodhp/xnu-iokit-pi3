/* IOPowerSources entry points for htop.macho's Platform_getBattery() (DARW htop port).
 *
 * Real IOPSCopyPowerSourcesInfo() is third_party/IOKitUser/ps.subproj/IOPowerSources.c:324; it reads the
 * power-source blob that powerd publishes (POWERD_XPC_ID, :51), and no powerd runs on this image.
 * A Pi 3 has no battery or UPS either. htop's own handling of "no power sources" is
 * third_party/htop/darwin/Platform.c:680-688: *percent = NAN and *isOnAC = AC_ERROR are set before the call, and
 * `if (!power_sources) goto cleanup;` leaves them, which the BatteryMeter draws as "N/A". So returning NULL here is
 * the documented no-battery answer, not a fake reading. The two accessors are only reached with a non-NULL blob
 * (Platform.c:690 returns early otherwise), so they return NULL as well. */
#include <CoreFoundation/CoreFoundation.h>
CFTypeRef IOPSCopyPowerSourcesInfo(void) { return NULL; }
CFArrayRef IOPSCopyPowerSourcesList(CFTypeRef blob) { (void)blob; return NULL; }
CFDictionaryRef IOPSGetPowerSourceDescription(CFTypeRef blob, CFTypeRef ps) { (void)blob; (void)ps; return NULL; }
