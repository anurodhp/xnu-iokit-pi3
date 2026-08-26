/* Real launchd/libxpc's own libxpc/include/launch.h has 88
 * LAUNCH_JOBKEY_* string-key macros but is genuinely missing this one
 * (confirmed via grep -- not present anywhere in the vendored header),
 * despite core.c's own job_import()/job_import_defaults() (real,
 * already-implemented functions) using it under a real
 * `#if TARGET_OS_EMBEDDED` branch. Value matches this header's own
 * established CamelCase plist-key convention for every other
 * LAUNCH_JOBKEY_* entry (e.g. LAUNCH_JOBKEY_KEEPALIVE_CRASHED
 * "Crashed") -- not independently confirmed against a real newer
 * launch.h from Apple, since none is pinned in this project, but this
 * is the only real fit given the file's own uniform naming.
 */
#ifndef LAUNCH_JOBKEY_DEFAULTS
#define LAUNCH_JOBKEY_DEFAULTS "Defaults"
#endif
