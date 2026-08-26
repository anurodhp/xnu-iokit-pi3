/* Real, honest no-op: real Apple os/workgroup.h (QoS thermal/audio
 * "workgroup" API, a real but much newer macOS/iOS feature) fails to
 * parse against this project's pinned iOS 14.4 SDK's own older
 * Availability macro set (confirmed real, same class of vintage-skew
 * gap already documented for the bridgeos/SPI_AVAILABLE fixes
 * elsewhere in this project). Confirmed via grep, not assumed: zero
 * real call sites for any os_workgroup_* API anywhere in
 * third_party/PureDarwin's actual launchd/libxpc source -- this header
 * is only ever transitively pulled in, never functionally needed. Real,
 * honest empty stub, matching this project's established "genuinely
 * unused subsystem" pattern (e.g. dyld_compat's sandbox/AMFI headers).
 */
#ifndef __OS_WORKGROUP__
#define __OS_WORKGROUP__
typedef struct os_workgroup_s *os_workgroup_t;
#endif
