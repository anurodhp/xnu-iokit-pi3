/* DAR-170: real, minimal glue mapping real Apple ipconfigd.c's own
 * generic bundle entry points (load()/start()/prime(), the plain names
 * real dlopen-based CFBundleGetFunctionPointerForName() lookup expects --
 * see ipconfigd.c's own load()/start()/prime() definitions, unmodified,
 * near the end of that 9000+-line file) onto the fixed load_<Name>/
 * start_<Name>/prime_<Name> names this project's builtin_plugins[] table
 * (third_party-patches/configd.patch) declares extern and references.
 *
 * This is NOT a reimplementation of any real logic -- every one of these
 * three functions is a pure one-line forwarding call to the real,
 * unmodified ipconfigd.c function of the same generic name. The
 * necessity for this glue at all is itself the honestly-documented real
 * architectural deviation: real Apple only ever needs the generic names,
 * because real Apple always reaches IPConfiguration via a dlopen'd
 * bundle (getBundleSymbol() tries the generic name first). This project
 * treats IPConfiguration as a builtin instead (see configd.patch's own
 * comment for why), and plugin_support.c's real, unmodified
 * builtin_plugins[] mechanism only knows how to reference a fixed,
 * per-plugin symbol name -- so a name-forwarding shim is the one real
 * gap that specific choice introduces, and this file is the entire
 * extent of it. */
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SCDPlugin.h>

extern void load(CFBundleRef bundle, Boolean bundleVerbose);
extern void start(const char *bundleName, const char *bundleDir);
extern void prime(void);

void
load_IPConfiguration(CFBundleRef bundle, Boolean bundleVerbose)
{
	load(bundle, bundleVerbose);
}

void
start_IPConfiguration(const char *bundleName, const char *bundleDir)
{
	start(bundleName, bundleDir);
}

void
prime_IPConfiguration(void)
{
	prime();
}
