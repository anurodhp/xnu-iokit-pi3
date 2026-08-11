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
	}
	// "cpu0" - closes the real ARM64-IRQ-delivery gap found investigating
	// why BCM2837SDHCI never sees a real interrupt reach handleInterrupt()
	// despite the hardware genuinely raising it: nothing ever called
	// ml_install_interrupt_handler() (osfmk/arm64/machine_routines.c) for
	// this CPU, so sleh_irq() had nothing real to dispatch to. See
	// BCM2837CPU.h for the full chain.
	OSDictionary *cpu0 = makePersonality("IOPlatformDevice", "BCM2837CPU", "cpu0", -1);

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

	IOLog("BCM2837RegisterBuiltinPersonalities: built %u personalities, gIOCatalogue=%p\n",
	    personalities->getCount(), (void *)(IOCatalogue *)gIOCatalogue);

	if (personalities->getCount() > 0) {
		bool ok = gIOCatalogue->addDrivers(personalities, true);
		IOLog("BCM2837RegisterBuiltinPersonalities: addDrivers returned %d\n", ok);
	}
	personalities->release();
}
