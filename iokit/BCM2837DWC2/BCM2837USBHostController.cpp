/*
 * BCM2837USBHostController - IOUSBControllerV2 over BCM2837DWC2.
 * See BCM2837USBHostController.h for the design rationale; this file is the
 * implementation only.
 */

#include "BCM2837USBHostController.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/usb/IOUSBDevice.h>
#include <IOKit/usb/IOUSBRootHubDevice.h>
#include <IOKit/usb/USB.h>
#include <IOKit/usb/USBHub.h>
#include <IOKit/usb/USBSpec.h>
#include <libkern/OSByteOrder.h>
#include <pexpert/pexpert.h>

OSDefineMetaClassAndStructors(BCM2837USBPortPolicy, IOUSBHubPolicyMaker);

#define super IOUSBControllerV2
OSDefineMetaClassAndStructors(BCM2837USBHostController, IOUSBControllerV2);

/*
 * IOUSBController keeps most of its state behind _expansionData and exposes
 * it to its own .cpp through these macros. IOUSBControllerV2.h re-declares
 * most of them for subclasses, but not _addressPending, which this UIM needs
 * so that GetNewAddress() cannot hand the root hub simulation an address
 * BCM2837DWC2's own enumeration already assigned to a real device on the wire.
 */
#define _addressPending		_expansionData->_addressPending

// kUSBSetup is not in any public header - IOUSBController.cpp:157 defines it
// locally as kUSBNone, and that is the value ControlTransaction() passes down
// as the SETUP-stage "direction". Same definition here, same reason.
#define kUSBSetup		kUSBNone

/*
 * ===================== THE QUEUE ENGINE'S SCHEDULE (DAR-370) =================
 *
 * Until DAR-370 the engine ran on a fixed 1 ms tick for the whole uptime,
 * because there is always at least one transfer queued: a HID interrupt IN,
 * the CDC-ECM notification IN, the CDC-ECM bulk IN. It woke 1000 times a
 * second whether or not anything was due. Now every endpoint carries its own
 * nextDue time, and fPollTimer is armed for the earliest nextDue of the
 * endpoints that have a transfer queued. With nothing queued it is not
 * armed at all, and a new UIMCreate*Transfer arms it.
 *
 * How long an endpoint waits after an attempt that left its head transfer
 * pending:
 *
 *   interrupt  bInterval, the rate the device asked to be polled at. The
 *              family passes bInterval through untranslated (IOUSBPipeV2.cpp
 *              :160 "_endpoint.interval = ed->bInterval"), and it means what
 *              AppleUSBEHCI_UIM.cpp:2629-2643 decodes it as: 2^(bInterval-1)
 *              microframes at high speed, bInterval frames (ms) at full or
 *              low speed. HID keyboards ask for 8-10 ms, hubs up to 255 ms.
 *   bulk IN    a NAK back-off capped at kBulkInBackoffMaxUS. This is a
 *              policy, not a USB rule: a bulk endpoint has no interval.
 *              A real EHCI walks its async schedule continuously in
 *              hardware. The DWC2 can also retry a NAKed non-periodic IN
 *              by itself (BCM2837DWC2.cpp, waitForChannelSettled()'s
 *              comment), but only while a channel stays armed on that
 *              endpoint, and this driver has one channel for every
 *              endpoint (DAR-351). So an idle read is retried in software.
 *              The wait doubles on each consecutive empty attempt, from
 *              one frame up to the cap, and drops back to one frame as soon
 *              as the endpoint moves data. Under load the cadence is the old
 *              one; the cost is up to one cap of extra latency for the
 *              first packet after an idle stretch.
 *   others     one frame. Control stages and OUT data are short-lived and
 *              finish in a few frames. Their timeouts below count attempts,
 *              so these must keep the old per-frame cadence to keep the
 *              old timeout durations.
 *
 * The base frame is still scaled by the QEMU cpu-factor (pollIntervalUS()
 * below), and every wait is at least one base frame.
 */

// One scheduling frame. 1ms is the USB full-speed frame time.
#define kPollIntervalUS		1000

// Longest wait between attempts on an idle bulk IN. See the schedule above.
#define kBulkInBackoffMaxUS	8000

// While a bulk IN is parked on a DWC2 channel, the engine still checks it at
// least this often, in case its completion interrupt is lost. See
// "PARKED BULK IN".
#define kAsyncPollFallbackMS	1000

// QEMU-only poll-interval scaling (2026-09-15): kPollIntervalUS above is
// correct and cheap on real hardware -- 1ms costs almost nothing on real
// silicon. Under QEMU/TCG it does not: every tick's frameNumber() read is a
// real MMIO trap-and-emulate, and processQueues() takes the workloop gate
// lock even when there is nothing to service, 1000 times a second, for the
// entire uptime (fEngineRunning never goes false in normal operation). Found
// via a real QEMU monitor register-probe session at an otherwise-idle login
// prompt -- host CPU pinned near 100%, PC parked in
// _lck_mtx_lock/_hw_lock_lock/_lck_mtx_unlock, called from
// IOTimerEventSource's own thread_call_start_iotes_invocation, not a hang.
// debug_cpu_performance_degradation_factor (pexpert/gen/pe_gen.c) is the
// exact same "this platform is known slow, don't hold it to real-hardware
// timing" signal runvm.sh's own cpu-factor=10 boot-arg already sets for the
// INTERRUPT_MASKED_DEBUG watchdog -- it is 1 on real hardware (unchanged
// behavior) and 10 under this project's own QEMU boot args, so reusing it
// here cuts QEMU's polling rate 10x (10ms/100Hz) without a new boot-arg and
// without touching real-hardware cadence at all.
//
// DAR-370 removed the fixed tick this paragraph describes, and the engine
// no longer reads HFNUM when it runs. The scaling stays until a QEMU
// measurement shows it is no longer needed.
static inline UInt32 pollIntervalUS(void)
{
	return kPollIntervalUS * (UInt32)debug_cpu_performance_degradation_factor;
}

static inline uint64_t
usToAbs(UInt32 us)
{
	uint64_t abs;
	clock_interval_to_absolutetime_interval(us, kMicrosecondScale, &abs);
	return abs;
}

static inline uint64_t
nowAbs(void)
{
	uint64_t now;
	clock_get_uptime(&now);
	return now;
}

/*
 * bInterval -> microseconds, decoded as AppleUSBEHCI_UIM.cpp:2629-2643 does.
 * speed is IOUSBFamily's kUSBDeviceSpeed*. The caller has already rejected
 * pollingRate 0, as AppleUSBEHCI_UIM.cpp:2583-2584 does.
 */
static UInt32
interruptPeriodUS(short speed, short pollingRate)
{
	if (speed == kUSBDeviceSpeedHigh) {
		// EHCI clamps an out-of-range exponent to 16; same here.
		int exp = (pollingRate > 16) ? 16 : pollingRate;
		return (UInt32)(125U << (exp - 1));
	}
	return (UInt32)pollingRate * 1000U;
}

// How long a transfer may make no progress before it is failed. Control and
// OUT get real ceilings because a stuck one must not hang a caller forever.
// IN transfers get NONE on purpose: an outstanding bulk/interrupt read with
// no data yet is not a fault, it is the entire point of the asynchronous
// API, and timing one out would break every network driver.
//
// These used to be attempt counts (1000 and 5000 one-frame ticks). DAR-370
// spaces attempts per endpoint, and an interrupt OUT at bInterval 255 would
// have turned 5000 attempts into 21 minutes, so they are times now. Scaled by
// the same QEMU cpu-factor as the frame, which keeps the old durations
// exactly: 1 s and 5 s on real hardware, 10 s and 50 s under QEMU.
#define kControlStageTimeoutMS		1000
#define kDataOutTimeoutMS		5000

// One attempt's NAK budget inside BCM2837DWC2, in milliseconds. Zero means
// "one try, then tell me" - the queue engine owns retrying, so the DWC2 layer
// must never block the workloop spinning on a NAK.
#define kAttemptNakBudgetMS	0

// Bounce buffer between IOMemoryDescriptor and BCM2837DWC2's plain-pointer
// transfer API. Large transfers are moved a chunk at a time and resumed, so
// this bounds memory use rather than transfer size.
#define kStagingBufferSize	(16 * 1024)

// How many transfers one endpoint may complete in a single tick. Bounded so a
// busy endpoint cannot starve the rest of the workloop, but high enough that a
// control transaction's three queued stages all finish in one tick.
#define kMaxTransfersPerTick	8

#pragma mark - Lifecycle

bool
BCM2837USBHostController::init(OSDictionary *propTable)
{
	if (!super::init(propTable)) {
		return false;
	}

	fDWC2 = NULL;
	fPollTimer = NULL;
	fEndpoints = NULL;
	fStaging = NULL;
	fStagingSize = 0;
	/*
	 * Not 0. Every "is this the root hub?" test in this file compares against
	 * fRootHubAddress, and 0 is a real USB address - the default address a
	 * device answers on before SET_ADDRESS. Leaving the sentinel at 0 would
	 * make any transfer to device zero silently take the root-hub simulation
	 * path. SetHubAddress() replaces this with the real address during
	 * IOUSBController::start(), before any traffic can occur.
	 */
	fRootHubAddress = 0xFFFF;
	fEngineRunning = false;
	fInEngine = false;
	fArmedDeadline = 0;
	fFrameNumber64 = 0;
	fFrameSampleTime = 0;
	fFrameLock = IOSimpleLockAlloc();
	if (fFrameLock == NULL) {
		return false;
	}
	fEngineStats = false;
	fEngineWakeups = 0;
	fEngineAttempts = 0;
	fEngineStatsSince = 0;
	fAsyncEvents = 0;
	fAsyncPolled = 0;
	fAsyncEvent = NULL;
	fAsyncEventDeferred = false;
	fReenumTestCall = NULL;
	fReenumTestMask = 0;
	fRootHubIntPending = false;
	bzero(&fRootHubIntCompletion, sizeof(fRootHubIntCompletion));

	/*
	 * The DWC2 core is a real USB 2.0 high-speed controller. This is what
	 * CreateRootHubDevice() records as the root hub device's speed and what
	 * IOUSBControllerV2::CreateDevice() compares a child's speed against when
	 * deciding whether that child needs split transactions.
	 */
	_controllerSpeed = kUSBDeviceSpeedHigh;

	return true;
}

bool
BCM2837USBHostController::start(IOService *provider)
{
	// Nothing to add - this override exists only to widen the access
	// IOUSBControllerV2 narrowed. See the header.
	return super::start(provider);
}

void
BCM2837USBHostController::free(void)
{
	if (fReenumTestCall != NULL) {
		thread_call_cancel_wait(fReenumTestCall);
		thread_call_free(fReenumTestCall);
		fReenumTestCall = NULL;
	}

	// UIMFinalize() does the real teardown; this is the belt-and-braces path
	// for a controller that failed part-way through start(). Stop the
	// engine first, so returnTransfers() below cannot park anything new,
	// and take back whatever is parked before the doorbell goes away.
	fEngineRunning = false;
	for (DWC2UIMEndpoint *ep = fEndpoints; ep != NULL; ep = ep->next) {
		if (ep->asyncChan >= 0) {
			collectAsync(ep, true);
		}
	}
	if (fDWC2 != NULL) {
		fDWC2->setAsyncNotify(NULL);
	}

	while (fEndpoints != NULL) {
		DWC2UIMEndpoint *ep = fEndpoints;
		fEndpoints = ep->next;
		returnTransfers(ep, kIOReturnAborted);
		IOFree(ep, sizeof(*ep));
	}

	if (fPollTimer != NULL) {
		fPollTimer->cancelTimeout();
		if (_workLoop != NULL) {
			_workLoop->removeEventSource(fPollTimer);
		}
		fPollTimer->release();
		fPollTimer = NULL;
	}

	if (fAsyncEvent != NULL) {
		if (fDWC2 != NULL) {
			fDWC2->setAsyncNotify(NULL);
		}
		fAsyncEvent->disable();
		if (_workLoop != NULL) {
			_workLoop->removeEventSource(fAsyncEvent);
		}
		fAsyncEvent->release();
		fAsyncEvent = NULL;
	}

	if (fStaging != NULL) {
		IOFree(fStaging, fStagingSize);
		fStaging = NULL;
		fStagingSize = 0;
	}

	if (fFrameLock != NULL) {
		IOSimpleLockFree(fFrameLock);
		fFrameLock = NULL;
	}

	super::free();
}

#pragma mark - UIM initialisation

IOReturn
BCM2837USBHostController::UIMInitialize(IOService *provider)
{
	fDWC2 = OSDynamicCast(BCM2837DWC2, provider);
	if (fDWC2 == NULL) {
		IOLog("BCM2837USBHostController: provider is not a BCM2837DWC2\n");
		return kIOReturnBadArgument;
	}

	fStagingSize = kStagingBufferSize;
	fStaging = (UInt8 *)IOMalloc(fStagingSize);
	if (fStaging == NULL) {
		fStagingSize = 0;
		return kIOReturnNoMemory;
	}

	/*
	 * Reserve every USB address BCM2837DWC2's own enumeration already put on
	 * the wire. IOUSBController::start() is about to call GetNewAddress() for
	 * the root hub simulation, and GetNewAddress() hands out the first
	 * address that is neither claimed by an existing IOUSBDevice client nor
	 * marked pending (IOUSBController.cpp:804-823). Without this it would
	 * return 1 - which under QEMU is the real, already-addressed usb-hub -
	 * and every root hub request would then alias a real device.
	 */
	uint32_t reserved = 0;
	for (uint32_t i = 0; i < fDWC2->deviceCount(); i++) {
		const DWC2USBDevice *dev = fDWC2->deviceAt(i);
		if (dev != NULL && dev->address > 0 && dev->address < kUSBMaxDevices) {
			_addressPending[dev->address] = true;
			reserved++;
		}
	}

	fPollTimer = IOTimerEventSource::timerEventSource(this, pollTimerFired);
	if (fPollTimer == NULL) {
		return kIOReturnNoResources;
	}
	if (_workLoop->addEventSource(fPollTimer) != kIOReturnSuccess) {
		fPollTimer->release();
		fPollTimer = NULL;
		return kIOReturnNoResources;
	}

	/*
	 * usb-engine-stats=1: log engine wake-ups and transfer attempts per
	 * second, about every 10 s. The log is written from an engine wake-up
	 * that is already happening, so it adds no wake-ups of its own. This is
	 * the counter DAR-370's acceptance asks for, next to ps state and TIME.
	 */
	uint32_t stats = 0;
	fEngineStats = PE_parse_boot_argn("usb-engine-stats", &stats, sizeof(stats)) && stats != 0;

	/*
	 * Completion doorbell for parked async bulk IN transfers. No provider:
	 * nothing registers it against a hardware interrupt. BCM2837DWC2's own
	 * handler calls its interruptOccurred() directly, which is the
	 * primary-interrupt entry point of IOInterruptEventSource. Not having
	 * it is not fatal: bulk IN then stays on the timer engine.
	 */
	fAsyncEvent = IOInterruptEventSource::interruptEventSource(this, asyncEventFired);
	if (fAsyncEvent != NULL && _workLoop->addEventSource(fAsyncEvent) != kIOReturnSuccess) {
		fAsyncEvent->release();
		fAsyncEvent = NULL;
	}
	if (fAsyncEvent != NULL) {
		fAsyncEvent->enable();
		fDWC2->setAsyncNotify(fAsyncEvent);
	}

	/*
	 * usb-reenum-test=<mask> (DAR-373 verification only): usb-reenum-delay
	 * seconds after start (default 90), call IOUSBDevice::ReEnumerateDevice()
	 * on every published device whose address has its bit set in <mask>.
	 * That is the same public call a driver's recovery path makes. Nothing
	 * in this kernel calls it on demand otherwise, and there is no USB user
	 * client to reach it from userspace.
	 */
	uint32_t reenumMask = 0;
	if (PE_parse_boot_argn("usb-reenum-test", &reenumMask, sizeof(reenumMask)) &&
	    reenumMask != 0) {
		uint32_t delay = 90;
		PE_parse_boot_argn("usb-reenum-delay", &delay, sizeof(delay));
		fReenumTestMask = reenumMask;
		fReenumTestCall = thread_call_allocate(reenumTestFired, this);
		if (fReenumTestCall != NULL) {
			uint64_t deadline;
			clock_interval_to_deadline(delay, kSecondScale, &deadline);
			thread_call_enter_delayed(fReenumTestCall, deadline);
			IOLog("BCM2837USBHostController: usb-reenum-test mask %#x in %u s\n",
			    reenumMask, delay);
		}
	}

	// Nothing is queued yet, so the engine starts parked. The first
	// UIMCreate*Transfer arms it (enqueueTransfer() -> armEngine()).
	fEngineRunning = true;
	fArmedDeadline = 0;

	IOLog("BCM2837USBHostController: UIM up on %s, %u enumerated device "
	      "address(es) reserved\n", fDWC2->getName(), reserved);

	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::UIMFinalize(void)
{
	if (fReenumTestCall != NULL) {
		thread_call_cancel_wait(fReenumTestCall);
	}

	fEngineRunning = false;

	if (fPollTimer != NULL) {
		fPollTimer->cancelTimeout();
	}
	fArmedDeadline = 0;

	// Take every parked transfer back from the hardware before the
	// doorbell goes away, so no channel is left armed with a DMA target
	// that is about to be freed.
	for (DWC2UIMEndpoint *ep = fEndpoints; ep != NULL; ep = ep->next) {
		if (ep->asyncChan >= 0) {
			collectAsync(ep, true);
		}
	}
	if (fAsyncEvent != NULL) {
		fDWC2->setAsyncNotify(NULL);
		fAsyncEvent->disable();
	}

	while (fEndpoints != NULL) {
		DWC2UIMEndpoint *ep = fEndpoints;
		fEndpoints = ep->next;
		returnTransfers(ep, kIOReturnAborted);
		IOFree(ep, sizeof(*ep));
	}

	if (fRootHubIntPending) {
		fRootHubIntPending = false;
		Complete(fRootHubIntCompletion, kIOReturnAborted, 0);
	}

	return kIOReturnSuccess;
}

#pragma mark - Endpoint bookkeeping

DWC2UIMEndpoint *
BCM2837USBHostController::findEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction)
{
	for (DWC2UIMEndpoint *ep = fEndpoints; ep != NULL; ep = ep->next) {
		if (ep->retired) {
			continue;
		}
		if (ep->functionNumber != functionNumber || ep->endpointNumber != endpointNumber) {
			continue;
		}
		// A control endpoint is bidirectional; callers pass kUSBAnyDirn,
		// kUSBNone or a real direction for it more or less interchangeably,
		// so direction is not part of its identity.
		if (ep->transferType == kUSBControl || endpointNumber == 0) {
			return ep;
		}
		if (direction == kUSBAnyDirn || ep->direction == direction) {
			return ep;
		}
	}
	return NULL;
}

/*
 * Endpoint state is created on demand as well as through the UIMCreate*Endpoint
 * calls. That is deliberate: BCM2837DWC2 already knows every endpoint of every
 * device it enumerated, so an endpoint the family asks us to transfer on but
 * never explicitly opened is recoverable rather than a hard kIOUSBEndpointNotFound.
 * The DWC2-level descriptor is built by BCM2837DWC2::makeEndpoint(), which is
 * the one place that knows this board's split-transaction rules.
 */
DWC2UIMEndpoint *
BCM2837USBHostController::createEndpoint(UInt8 functionNumber, UInt8 endpointNumber,
    UInt8 direction, UInt8 transferType, UInt16 maxPacketSize)
{
	DWC2UIMEndpoint *ep = findEndpoint(functionNumber, endpointNumber, direction);
	if (ep != NULL) {
		// Re-opening an existing endpoint (e.g. a SET_CONFIGURATION that
		// re-creates pipes) refreshes its geometry but must not disturb
		// queued transfers.
		if (maxPacketSize != 0) {
			ep->dep.maxPacket = maxPacketSize;
		}
		return ep;
	}

	const DWC2USBDevice *dev = fDWC2->findDevice(functionNumber);
	if (dev == NULL) {
		IOLog("BCM2837USBHostController: no enumerated device at address %u "
		      "(endpoint %u)\n", functionNumber, endpointNumber);
		return NULL;
	}

	ep = (DWC2UIMEndpoint *)IOMalloc(sizeof(DWC2UIMEndpoint));
	if (ep == NULL) {
		return NULL;
	}
	bzero(ep, sizeof(*ep));

	UInt8 epAddress = endpointNumber;
	if (endpointNumber != 0 && direction == kUSBIn) {
		epAddress |= 0x80;
	}

	if (!fDWC2->makeEndpoint(dev, epAddress, &ep->dep)) {
		IOLog("BCM2837USBHostController: device %u has no endpoint 0x%02x\n",
		    functionNumber, epAddress);
		IOFree(ep, sizeof(*ep));
		return NULL;
	}

	// The family's view of maxPacketSize wins when it supplied one: it comes
	// from the endpoint descriptor the device itself reported for the
	// configuration that is actually selected.
	if (maxPacketSize != 0) {
		ep->dep.maxPacket = maxPacketSize;
	}

	ep->functionNumber = functionNumber;
	ep->endpointNumber = endpointNumber;
	ep->direction = direction;
	ep->transferType = transferType;
	ep->asyncChan = -1;
	ep->next = fEndpoints;
	fEndpoints = ep;

	return ep;
}

/*
 * Completions run client code, which can call straight back into this
 * controller: re-queue on the same endpoint, delete it, or call
 * PollInterrupts(). Outside the engine, the completions run with fInEngine
 * set, the same guard the engine itself uses. That keeps processQueues()
 * from reaping (freeing) an endpoint that a caller further up the stack is
 * still about to touch: UIMClearEndpointStall() writes the toggle after this
 * returns. Anything the completions queued is started afterwards.
 */
void
BCM2837USBHostController::returnTransfers(DWC2UIMEndpoint *ep, IOReturn status)
{
	DWC2UIMTransfer *xfer = ep->head;
	ep->head = NULL;
	ep->tail = NULL;

	bool outermost = !fInEngine;
	fInEngine = true;

	while (xfer != NULL) {
		DWC2UIMTransfer *next = xfer->next;
		IOUSBCompletion completion = xfer->completion;
		UInt32 remaining = xfer->length - xfer->done;
		IOFree(xfer, sizeof(*xfer));
		Complete(completion, status, remaining);
		xfer = next;
	}

	if (outermost) {
		leaveEngine();
	}
}

/*
 * The one way out of an fInEngine section. An async doorbell that rang
 * while the section held fInEngine (asyncEventFired() had to skip it) is
 * serviced here rather than left to the 1 s fallback. Then anything the
 * section's completions queued is parked or scheduled.
 */
void
BCM2837USBHostController::leaveEngine(void)
{
	fInEngine = false;
	while (fAsyncEventDeferred && fEngineRunning) {
		fAsyncEventDeferred = false;
		collectAllAsync();
	}
	kickAsync();
	rearmEngine();
}

/*
 * Collect every parked endpoint whose channel has halted. Sets fInEngine
 * around the completions it delivers, and leaves through leaveEngine()'s
 * caller.
 */
void
BCM2837USBHostController::collectAllAsync(void)
{
	fInEngine = true;
	for (DWC2UIMEndpoint *ep = fEndpoints; ep != NULL; ep = ep->next) {
		if (!ep->retired && ep->asyncChan >= 0 && collectAsync(ep, false)) {
			fAsyncEvents++;
		}
	}
	fInEngine = false;
}

/*
 * Pop the endpoint's head transfer and deliver its completion.
 */
void
BCM2837USBHostController::completeHead(DWC2UIMEndpoint *ep, IOReturn status)
{
	DWC2UIMTransfer *xfer = ep->head;
	if (xfer == NULL) {
		return;
	}
	ep->head = xfer->next;
	if (ep->head == NULL) {
		ep->tail = NULL;
	}
	IOUSBCompletion completion = xfer->completion;
	UInt32 leftover = xfer->length - xfer->done;
	IOFree(xfer, sizeof(*xfer));
	Complete(completion, status, leftover);
}

/*
 * Endpoints deleted from inside a completion routine cannot be freed
 * immediately - processQueues() is walking the very list they are on. They are
 * marked retired instead (which makes findEndpoint() ignore them at once) and
 * unlinked at the top of the engine's next pass.
 */
void
BCM2837USBHostController::reapRetiredEndpoints(void)
{
	DWC2UIMEndpoint **link = &fEndpoints;
	while (*link != NULL) {
		DWC2UIMEndpoint *ep = *link;
		if (ep->retired && ep->head == NULL) {
			*link = ep->next;
			IOFree(ep, sizeof(*ep));
		} else {
			link = &ep->next;
		}
	}
}

#pragma mark - Queue engine

IOReturn
BCM2837USBHostController::enqueueTransfer(DWC2UIMEndpoint *ep, IOUSBCompletion completion,
    IOMemoryDescriptor *memDesc, const void *inlineData, UInt32 length,
    bool isIn, bool usePid, UInt8 pid)
{
	DWC2UIMTransfer *xfer = (DWC2UIMTransfer *)IOMalloc(sizeof(DWC2UIMTransfer));
	if (xfer == NULL) {
		return kIOReturnNoMemory;
	}
	bzero(xfer, sizeof(*xfer));

	xfer->completion = completion;
	xfer->memDesc = memDesc;
	xfer->length = length;
	xfer->isIn = isIn;
	xfer->usePid = usePid;
	xfer->pid = pid;

	if (inlineData != NULL) {
		if (length > sizeof(xfer->inlineData)) {
			IOFree(xfer, sizeof(*xfer));
			return kIOReturnBadArgument;
		}
		bcopy(inlineData, xfer->inlineData, length);
		xfer->useInlineData = true;
	}

	if (ep->tail != NULL) {
		ep->tail->next = xfer;
	} else {
		ep->head = xfer;
	}
	ep->tail = xfer;

	// Park it on a DWC2 channel if it can be, or else wake the engine when
	// this endpoint is next due. Inside the engine (or inside
	// returnTransfers()), the kickAsync()/rearmEngine() at the end of the
	// pass picks it up instead.
	if (!fInEngine) {
		uint64_t now = nowAbs();
		if (startAsync(ep)) {
			// Parked. Make sure the lost-interrupt fallback pass is
			// scheduled; armEngine() keeps an earlier deadline if one is set.
			armEngine(now + usToAbs(kAsyncPollFallbackMS * 1000U));
		} else {
			armEngine((ep->nextDue > now) ? ep->nextDue : now);
		}
	}

	return kIOReturnSuccess;
}

/*
 * Give the endpoint's head transfer one non-blocking attempt.
 *
 * Returns true when that transfer is finished and its completion has been
 * delivered (so the caller may look at the next one), false when it is still
 * outstanding.
 */
bool
BCM2837USBHostController::serviceTransfer(DWC2UIMEndpoint *ep, bool *progressed)
{
	*progressed = false;

	DWC2UIMTransfer *xfer = ep->head;
	if (xfer == NULL) {
		return false;
	}

	UInt32 remaining = xfer->length - xfer->done;
	UInt32 chunk = (remaining > fStagingSize) ? fStagingSize : remaining;
	UInt32 moved = 0;
	IOReturn status;

	if (xfer->useInlineData) {
		// SETUP packets and other tiny, already-in-hand payloads.
		bcopy(xfer->inlineData + xfer->done, fStaging, chunk);
	} else if (chunk > 0 && !xfer->isIn) {
		if (xfer->memDesc == NULL) {
			status = kIOReturnBadArgument;
			goto finished;
		}
		if (xfer->memDesc->readBytes(xfer->done, fStaging, chunk) != chunk) {
			status = kIOReturnVMError;
			goto finished;
		}
	}

	{
		// Control stages carry their own PID; data endpoints must keep using
		// the endpoint's persistent toggle across transfers, which is what USB
		// requires and what BCM2837DWC2::transfer() does for its own callers.
		UInt8 *pid = xfer->usePid ? &xfer->pid : &ep->dep.toggle;

		status = fDWC2->transferStage(&ep->dep, pid, xfer->isIn, fStaging,
		    chunk, &moved, kAttemptNakBudgetMS);
	}

	if (moved > 0) {
		*progressed = true;
		if (xfer->isIn && !xfer->useInlineData) {
			if (xfer->memDesc == NULL ||
			    xfer->memDesc->writeBytes(xfer->done, fStaging, moved) != moved) {
				status = kIOReturnVMError;
				xfer->done += moved;
				goto finished;
			}
		}
		xfer->done += moved;
	}

	if (status == kIOReturnTimeout) {
		/*
		 * Nothing more could be moved this tick. For a bulk/interrupt IN that
		 * is the normal idle case - the device NAKed because it has no data -
		 * and the transfer stays queued indefinitely, which is exactly what an
		 * asynchronous read means. Control stages and OUT transfers do get a
		 * ceiling, because a stuck one there is a real fault.
		 */
		uint64_t now = nowAbs();
		if (moved > 0 || xfer->stallStart == 0) {
			xfer->stallStart = now;
		}
		UInt32 limitMS = xfer->usePid ? kControlStageTimeoutMS :
		    (xfer->isIn ? 0 : kDataOutTimeoutMS);
		if (limitMS != 0) {
			uint64_t limitAbs;
			clock_interval_to_absolutetime_interval(
			    limitMS * (UInt32)debug_cpu_performance_degradation_factor,
			    kMillisecondScale, &limitAbs);
			if (now - xfer->stallStart >= limitAbs) {
				status = kIOUSBTransactionTimeout;
				goto finished;
			}
		}
		return false;
	}

	if (status != kIOReturnSuccess) {
		if (status == kIOReturnNotResponding) {
			// BCM2837DWC2 reports a real endpoint STALL this way.
			status = kIOUSBPipeStalled;
		}
		goto finished;
	}

	xfer->stallStart = 0;

	// A short IN packet ends the transfer early; that is a normal, successful
	// result, not a truncation error.
	if (xfer->done < xfer->length && !(xfer->isIn && moved < chunk)) {
		return false;
	}

finished:
	// The device answered (data, a zero-length packet, or an error) rather
	// than NAKing, which is what the bulk IN back-off cares about.
	*progressed = true;
	completeHead(ep, status);
	return true;
}

void
BCM2837USBHostController::processQueues(void)
{
	/*
	 * Completions run client code, and client code may call back into this
	 * controller. PollInterrupts() in particular is public and routes straight
	 * here, so a completion could re-enter the engine while it is part-way
	 * through walking the endpoint list - which would service the same
	 * transfer twice and walk a list another frame is mutating. Nothing in
	 * the vendored family does this today; the guard is what keeps that true
	 * cheaply rather than by assumption.
	 */
	if (fInEngine) {
		return;
	}
	fInEngine = true;

	reapRetiredEndpoints();

	uint64_t now = nowAbs();

	for (DWC2UIMEndpoint *ep = fEndpoints; ep != NULL; ep = ep->next) {
		if (ep->retired) {
			continue;
		}
		if (ep->asyncChan >= 0) {
			// Parked on the hardware. Check it anyway: a completion is
			// normally delivered by asyncEventFired(), and this is the
			// fallback if that interrupt never arrives.
			if (collectAsync(ep, false)) {
				fAsyncPolled++;
			}
			continue;
		}
		if (ep->head == NULL || ep->nextDue > now) {
			continue;
		}
		bool progressed = false;
		for (int i = 0; i < kMaxTransfersPerTick; i++) {
			if (ep->head == NULL) {
				break;
			}
			bool p;
			fEngineAttempts++;
			bool finished = serviceTransfer(ep, &p);
			progressed |= p;
			if (!finished) {
				break;
			}
		}
		scheduleNext(ep, now, progressed);
	}

	leaveEngine();
}

/*
 * ============================ PARKED BULK IN (DAR-370) =======================
 *
 * The timer engine retries an idle bulk IN in software, because
 * transferStage() gives up on the first NAK. When BCM2837DWC2 has a spare
 * host channel, a bulk IN is instead parked on it (BCM2837DWC2::asyncStart();
 * see its header for why the hardware then retries the NAKs by itself and
 * interrupts only on completion). A parked endpoint costs no timer wake-ups at
 * all while its device is idle. When the channel halts, BCM2837DWC2's
 * interrupt handler pokes fAsyncEvent, and asyncEventFired() collects the
 * result on this workloop and re-parks the endpoint for its next transfer.
 *
 * Only bulk IN, and only what BCM2837DWC2::asyncEligible() accepts
 * (non-split). Everything else, and any bulk IN that finds no free channel,
 * stays on the timer engine exactly as before.
 *
 * While parked, the engine still reads the channel's HCINT on each of its
 * passes (processQueues()), and rearmEngine() makes sure there is a pass at
 * least every kAsyncPollFallbackMS. That fallback matches
 * BCM2837DWC2::waitForChannelInt()'s: a lost interrupt costs latency, not the
 * transfer. The stats line reports how many completions arrived each way.
 */

bool
BCM2837USBHostController::startAsync(DWC2UIMEndpoint *ep)
{
	if (ep->asyncChan >= 0) {
		return true;
	}
	if (fAsyncEvent == NULL || ep->retired || ep->head == NULL ||
	    ep->transferType != kUSBBulk || ep->direction != kUSBIn) {
		return false;
	}

	DWC2UIMTransfer *xfer = ep->head;
	UInt32 remaining = xfer->length - xfer->done;
	if (remaining == 0 || xfer->memDesc == NULL) {
		// A zero-length read has nothing to park; the engine finishes it.
		return false;
	}
	if (remaining > fStagingSize) {
		remaining = fStagingSize;
	}

	uint32_t armed = 0;
	int chan = fDWC2->asyncStart(&ep->dep, remaining, &armed);
	if (chan < 0) {
		return false;
	}
	ep->asyncChan = chan;
	ep->asyncReq = armed;
	return true;
}

void
BCM2837USBHostController::kickAsync(void)
{
	if (fInEngine || !fEngineRunning) {
		return;
	}
	for (DWC2UIMEndpoint *ep = fEndpoints; ep != NULL; ep = ep->next) {
		if (ep->asyncChan < 0 && ep->head != NULL) {
			startAsync(ep);
		}
	}
}

/*
 * Collect a parked transfer if its channel has halted, or take it back
 * (`abort`). Returns true if the endpoint is no longer parked. On a normal
 * finish the head transfer is completed here, or left queued for the next
 * chunk. On an abort the bytes that did arrive are written to the transfer
 * and counted, and the caller returns it (returnTransfers()).
 *
 * Must run with fInEngine set, or from a caller that sets it, because it can
 * deliver a completion.
 */
bool
BCM2837USBHostController::collectAsync(DWC2UIMEndpoint *ep, bool abort)
{
	if (ep->asyncChan < 0) {
		return true;
	}

	uint32_t actual = 0;
	uint32_t hcint = 0;
	IOReturn status;
	if (abort) {
		status = fDWC2->asyncAbort(ep->asyncChan, &ep->dep, fStaging, fStagingSize, &actual);
	} else if (!fDWC2->asyncCollect(ep->asyncChan, &ep->dep, fStaging, fStagingSize,
	    &actual, &status, &hcint)) {
		return false;
	}
	ep->asyncChan = -1;

	DWC2UIMTransfer *xfer = ep->head;
	if (xfer == NULL) {
		// The transfer was returned while parked. That can't happen - every
		// path that returns transfers aborts the channel first - but if it
		// ever does, the bytes have nowhere to go.
		if (actual > 0) {
			IOLog("BCM2837USBHostController: %u async bytes on %u.%u with no "
			    "transfer queued - dropped\n", actual, ep->functionNumber,
			    ep->endpointNumber);
		}
		return true;
	}

	if (actual > 0) {
		if (xfer->memDesc->writeBytes(xfer->done, fStaging, actual) != actual) {
			status = kIOReturnVMError;
		}
		xfer->done += actual;
	}

	if (abort) {
		return true;
	}

	if (status == kIOReturnSuccess) {
		xfer->xactErrors = 0;
		// Short packet, or everything asked for: done. Otherwise the next
		// chunk is parked by kickAsync() after this pass.
		if (xfer->done >= xfer->length || actual < ep->asyncReq) {
			completeHead(ep, kIOReturnSuccess);
		}
		return true;
	}

	// A transaction error gets the same three tries doTransfer() gives it
	// (its XACTERR comment: transient, and confirmed on the real LAN9514).
	// Re-parking resumes at xfer->done with the toggle the channel left.
	if ((hcint & kHCINT_XACTERR) && xfer->done < xfer->length &&
	    ++xfer->xactErrors < 3) {
		IOLog("BCM2837USBHostController: XACTERR on async %u.%u, attempt %u/3, "
		    "re-parking\n", ep->functionNumber, ep->endpointNumber, xfer->xactErrors);
		return true;
	}

	if (status == kIOReturnNotResponding) {
		// BCM2837DWC2 reports a real endpoint STALL this way.
		status = kIOUSBPipeStalled;
	}
	completeHead(ep, status);
	return true;
}

void
BCM2837USBHostController::asyncEventFired(OSObject *owner, IOInterruptEventSource *sender,
    int count)
{
#pragma unused (sender, count)
	BCM2837USBHostController *me = OSDynamicCast(BCM2837USBHostController, owner);
	if (me == NULL) {
		return;
	}
	if (me->fInEngine) {
		// A completion further up this thread gave up the gate. The
		// producer count is already consumed, so remember the doorbell;
		// that section's leaveEngine() services it.
		me->fAsyncEventDeferred = true;
		return;
	}

	me->collectAllAsync();
	me->leaveEngine();
}

/*
 * Set ep->nextDue after an attempt. The rules are in "THE QUEUE ENGINE'S
 * SCHEDULE" at the top of this file.
 */
void
BCM2837USBHostController::scheduleNext(DWC2UIMEndpoint *ep, uint64_t now, bool progressed)
{
	UInt32 base = pollIntervalUS();
	UInt32 waitUS = base;

	if (ep->periodUS != 0) {
		// Interrupt endpoint: keep to bInterval even when its queue is empty
		// right now, because the client usually re-queues from its
		// completion and that next read is due one interval later.
		waitUS = (ep->periodUS > base) ? ep->periodUS : base;
		ep->nextDue = now + usToAbs(waitUS);
		return;
	}

	if (ep->head == NULL) {
		// No interval to keep, so the next transfer queued on this endpoint
		// can go straight away.
		ep->nextDue = 0;
		ep->backoffUS = 0;
		return;
	}

	if (ep->transferType == kUSBBulk && ep->direction == kUSBIn) {
		UInt32 cap = (kBulkInBackoffMaxUS > base) ? kBulkInBackoffMaxUS : base;
		if (progressed || ep->backoffUS == 0) {
			ep->backoffUS = base;
		} else if (ep->backoffUS < cap) {
			ep->backoffUS = (ep->backoffUS * 2 > cap) ? cap : ep->backoffUS * 2;
		}
		waitUS = ep->backoffUS;
	}

	ep->nextDue = now + usToAbs(waitUS);
}

/*
 * Arm the engine for `deadline` unless it is already armed for something
 * earlier. The deadline must not be 0: IOTimerEventSource::wakeAtTime()
 * treats an abstime of 0 as "do not arm" (IOTimerEventSource.cpp:552).
 */
void
BCM2837USBHostController::armEngine(uint64_t deadline)
{
	if (!fEngineRunning || fPollTimer == NULL || fInEngine) {
		return;
	}
	if (fArmedDeadline != 0 && fArmedDeadline <= deadline) {
		return;
	}
	fArmedDeadline = deadline;
	// Re-arming a pending IOTimerEventSource replaces it: wakeAtTime() bumps
	// calloutGeneration, and timeoutAndRelease() drops a callout whose
	// generation is stale (IOTimerEventSource.cpp:182).
	fPollTimer->wakeAtTime(deadline);
}

/*
 * Arm the engine for the earliest endpoint that has work queued. If none
 * has, park it: no timer until the next enqueueTransfer().
 */
void
BCM2837USBHostController::rearmEngine(void)
{
	if (!fEngineRunning || fPollTimer == NULL) {
		return;
	}

	bool any = false;
	uint64_t earliest = 0;
	bool parked = false;
	bool reap = false;
	for (DWC2UIMEndpoint *ep = fEndpoints; ep != NULL; ep = ep->next) {
		if (ep->retired) {
			// Deleted endpoints are freed at the top of the next engine
			// pass, from the timer, where no caller can still hold one.
			// So a pass is needed even if nothing else is queued. Only
			// for one reapRetiredEndpoints() can actually free (head
			// empty), or this would re-arm for "now" forever.
			if (ep->head == NULL) {
				reap = true;
			}
			continue;
		}
		if (ep->asyncChan >= 0) {
			parked = true;
			continue;
		}
		if (ep->head == NULL) {
			continue;
		}
		if (!any || ep->nextDue < earliest) {
			earliest = ep->nextDue;
			any = true;
		}
	}
	if (reap) {
		uint64_t now = nowAbs();
		if (!any || now < earliest) {
			earliest = now;
			any = true;
		}
	}
	if (parked) {
		// The lost-interrupt fallback in "PARKED BULK IN".
		uint64_t fallback = nowAbs() + usToAbs(kAsyncPollFallbackMS * 1000U);
		if (!any || fallback < earliest) {
			earliest = fallback;
			any = true;
		}
	}

	if (!any) {
		if (fArmedDeadline != 0) {
			fPollTimer->cancelTimeout();
			fArmedDeadline = 0;
		}
		return;
	}

	uint64_t now = nowAbs();
	if (earliest < now) {
		earliest = now;
	}
	// armEngine() keeps an armed deadline that is already earlier. At worst
	// that costs one wake-up with nothing due, which is cheaper than
	// cancelling and re-entering the thread_call on every pass.
	armEngine(earliest);
}

void
BCM2837USBHostController::pollTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
#pragma unused (sender)
	BCM2837USBHostController *me = OSDynamicCast(BCM2837USBHostController, owner);
	if (me == NULL) {
		return;
	}

	// The armed deadline has been consumed; processQueues() re-arms.
	me->fArmedDeadline = 0;
	me->fEngineWakeups++;

	me->processQueues();

	if (me->fEngineStats) {
		uint64_t now = nowAbs();
		if (me->fEngineStatsSince == 0) {
			me->fEngineStatsSince = now;
		} else {
			uint64_t ns;
			absolutetime_to_nanoseconds(now - me->fEngineStatsSince, &ns);
			if (ns >= 10ULL * NSEC_PER_SEC) {
				uint64_t ms = ns / NSEC_PER_MSEC;
				IOLog("BCM2837USBHostController: DAR-370 engine %u wakeups, %u attempts "
				    "in %llu ms (%llu wakeups/s); async completions %u by interrupt, "
				    "%u by poll\n", me->fEngineWakeups, me->fEngineAttempts, ms,
				    (uint64_t)me->fEngineWakeups * 1000ULL / ms, me->fAsyncEvents,
				    me->fAsyncPolled);
				me->fEngineWakeups = 0;
				me->fEngineAttempts = 0;
				me->fAsyncEvents = 0;
				me->fAsyncPolled = 0;
				me->fEngineStatsSince = now;
			}
		}
	}
}

#pragma mark - UIM: endpoint creation

IOReturn
BCM2837USBHostController::UIMCreateControlEndpoint(UInt8 functionNumber, UInt8 endpointNumber,
    UInt16 maxPacketSize, UInt8 speed)
{
	return UIMCreateControlEndpoint(functionNumber, endpointNumber, maxPacketSize, speed, 0, 0);
}

IOReturn
BCM2837USBHostController::UIMCreateControlEndpoint(UInt8 functionNumber, UInt8 endpointNumber,
    UInt16 maxPacketSize, UInt8 speed, USBDeviceAddress highSpeedHub, int highSpeedPort)
{
#pragma unused (speed, highSpeedHub, highSpeedPort)
	// Split-transaction routing comes from BCM2837DWC2's own device table via
	// makeEndpoint(), which recorded the real hub address and port during
	// enumeration; the family's highSpeedHub/highSpeedPort say the same thing
	// and are deliberately not used as a second, divergeable source.
	if (functionNumber == fRootHubAddress) {
		return kIOReturnSuccess;
	}
	return (createEndpoint(functionNumber, endpointNumber, kUSBAnyDirn, kUSBControl,
	    maxPacketSize) != NULL) ? kIOReturnSuccess : kIOUSBEndpointNotFound;
}

IOReturn
BCM2837USBHostController::UIMCreateBulkEndpoint(UInt8 functionNumber, UInt8 endpointNumber,
    UInt8 direction, UInt8 speed, UInt8 maxPacketSize)
{
	return UIMCreateBulkEndpoint(functionNumber, endpointNumber, direction, speed,
	    (UInt16)maxPacketSize, 0, 0);
}

IOReturn
BCM2837USBHostController::UIMCreateBulkEndpoint(UInt8 functionNumber, UInt8 endpointNumber,
    UInt8 direction, UInt8 speed, UInt16 maxPacketSize,
    USBDeviceAddress highSpeedHub, int highSpeedPort)
{
#pragma unused (speed, highSpeedHub, highSpeedPort)
	if (functionNumber == fRootHubAddress) {
		return kIOReturnBadArgument;
	}
	return (createEndpoint(functionNumber, endpointNumber, direction, kUSBBulk,
	    maxPacketSize) != NULL) ? kIOReturnSuccess : kIOUSBEndpointNotFound;
}

IOReturn
BCM2837USBHostController::UIMCreateInterruptEndpoint(short functionAddress, short endpointNumber,
    UInt8 direction, short speed, UInt16 maxPacketSize, short pollingRate)
{
	return UIMCreateInterruptEndpoint(functionAddress, endpointNumber, direction, speed,
	    maxPacketSize, pollingRate, 0, 0);
}

IOReturn
BCM2837USBHostController::UIMCreateInterruptEndpoint(short functionAddress, short endpointNumber,
    UInt8 direction, short speed, UInt16 maxPacketSize, short pollingRate,
    USBDeviceAddress highSpeedHub, int highSpeedPort)
{
#pragma unused (highSpeedHub, highSpeedPort)
	// The root hub's status-change endpoint is simulated, not real silicon:
	// there is no channel to program for it.
	if (functionAddress == fRootHubAddress) {
		return kIOReturnSuccess;
	}
	// AppleUSBEHCI_UIM.cpp:2583-2584 rejects a zero polling rate outright.
	if (pollingRate == 0) {
		return kIOReturnBadArgument;
	}
	DWC2UIMEndpoint *ep = createEndpoint((UInt8)functionAddress, (UInt8)endpointNumber,
	    direction, kUSBInterrupt, maxPacketSize);
	if (ep == NULL) {
		return kIOUSBEndpointNotFound;
	}
	// Re-creating an open endpoint is how SetPipePolicy() changes the
	// interval (AppleUSBEHCI_UIM.cpp:2586-2588), so always take the new one,
	// and don't make the next attempt wait out the old interval.
	ep->periodUS = interruptPeriodUS(speed, pollingRate);
	ep->nextDue = 0;
	if (fEngineStats) {
		IOLog("BCM2837USBHostController: interrupt endpoint %d.%d %s bInterval %d -> "
		    "polled every %u us\n", functionAddress, endpointNumber,
		    (direction == kUSBIn) ? "IN" : "OUT", pollingRate, ep->periodUS);
	}
	return kIOReturnSuccess;
}

/*
 * Isochronous is genuinely unimplemented, and saying so is the honest answer.
 * BCM2837DWC2 is single-channel and polled (its own header: "Isochronous
 * transfers are unimplemented and will not be: no consumer"), and this kernel
 * has no isochronous client of any kind. A stub that pretended to succeed
 * would let a future audio/video driver queue transfers that silently never
 * move.
 */
IOReturn
BCM2837USBHostController::UIMCreateIsochEndpoint(short functionAddress, short endpointNumber,
    UInt32 maxPacketSize, UInt8 direction)
{
#pragma unused (functionAddress, endpointNumber, maxPacketSize, direction)
	return kIOReturnUnsupported;
}

IOReturn
BCM2837USBHostController::UIMCreateIsochEndpoint(short functionAddress, short endpointNumber,
    UInt32 maxPacketSize, UInt8 direction, USBDeviceAddress highSpeedHub, int highSpeedPort)
{
#pragma unused (functionAddress, endpointNumber, maxPacketSize, direction, highSpeedHub, highSpeedPort)
	return kIOReturnUnsupported;
}

IOReturn
BCM2837USBHostController::UIMCreateIsochTransfer(short functionAddress, short endpointNumber,
    IOUSBIsocCompletion completion, UInt8 direction, UInt64 frameStart,
    IOMemoryDescriptor *pBuffer, UInt32 frameCount, IOUSBIsocFrame *pFrames)
{
#pragma unused (functionAddress, endpointNumber, completion, direction, frameStart, pBuffer, frameCount, pFrames)
	return kIOReturnUnsupported;
}

#pragma mark - UIM: endpoint control

IOReturn
BCM2837USBHostController::UIMAbortEndpoint(short functionNumber, short endpointNumber,
    short direction)
{
	if (functionNumber == fRootHubAddress) {
		if (fRootHubIntPending) {
			fRootHubIntPending = false;
			Complete(fRootHubIntCompletion, kIOReturnAborted, 0);
		}
		return kIOReturnSuccess;
	}

	DWC2UIMEndpoint *ep = findEndpoint((UInt8)functionNumber, (UInt8)endpointNumber,
	    (UInt8)direction);
	if (ep == NULL) {
		return kIOUSBEndpointNotFound;
	}

	collectAsync(ep, true);
	ep->nextDue = 0;
	ep->backoffUS = 0;
	returnTransfers(ep, kIOUSBTransactionReturned);
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::UIMDeleteEndpoint(short functionNumber, short endpointNumber,
    short direction)
{
	if (functionNumber == fRootHubAddress) {
		return kIOReturnSuccess;
	}

	DWC2UIMEndpoint *ep = findEndpoint((UInt8)functionNumber, (UInt8)endpointNumber,
	    (UInt8)direction);
	if (ep == NULL) {
		return kIOUSBEndpointNotFound;
	}

	// Retired before the completions run, so a client that re-queues from
	// its kIOUSBTransactionReturned completion cannot land a transfer on an
	// endpoint that is being deleted (findEndpoint() skips it). Unlinked and
	// freed by reapRetiredEndpoints() at the engine's next pass, never here:
	// a caller further up the stack may still hold `ep`.
	ep->retired = true;
	collectAsync(ep, true);
	returnTransfers(ep, kIOUSBTransactionReturned);
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::UIMClearEndpointStall(short functionNumber, short endpointNumber,
    short direction)
{
	if (functionNumber == fRootHubAddress) {
		return kIOReturnSuccess;
	}

	DWC2UIMEndpoint *ep = findEndpoint((UInt8)functionNumber, (UInt8)endpointNumber,
	    (UInt8)direction);
	if (ep == NULL) {
		return kIOUSBEndpointNotFound;
	}

	collectAsync(ep, true);
	ep->nextDue = 0;
	ep->backoffUS = 0;
	// USB 2.0 9.4.5: clearing a halt resets the endpoint's data toggle to
	// DATA0 on both sides. The device half is done by the CLEAR_FEATURE the
	// pipe layer sends; this is the host half. Before returnTransfers(),
	// not after: a read the client re-queues from its Returned completion
	// is parked (kickAsync()) as returnTransfers() leaves, and must start
	// from DATA0. After the parked channel was taken back above, nothing
	// else writes the toggle.
	ep->dep.toggle = kHCPID_DATA0;
	returnTransfers(ep, kIOUSBTransactionReturned);
	return kIOReturnSuccess;
}

#pragma mark - UIM: transfers

IOReturn
BCM2837USBHostController::queueControlStage(short functionNumber, short endpointNumber,
    IOUSBCompletion completion, IOMemoryDescriptor *memDesc, const void *rawSetup,
    UInt32 bufferSize, short direction)
{
	if (functionNumber == fRootHubAddress) {
		// Root hub control requests never reach the UIM: IOUSBRootHubDevice
		// overrides DeviceRequest() and dispatches to the Get/Set/ClearRootHub*
		// methods below. Reaching here means something bypassed that path.
		IOLog("BCM2837USBHostController: unexpected root hub control transfer\n");
		Complete(completion, kIOReturnUnsupported, bufferSize);
		return kIOReturnSuccess;
	}

	DWC2UIMEndpoint *ep = createEndpoint((UInt8)functionNumber, (UInt8)endpointNumber,
	    kUSBAnyDirn, kUSBControl, 0);
	if (ep == NULL) {
		return kIOUSBEndpointNotFound;
	}

	if (direction == kUSBSetup) {
		unsigned char setup[8];
		if (rawSetup != NULL) {
			bcopy(rawSetup, setup, sizeof(setup));
		} else if (memDesc != NULL &&
		    memDesc->readBytes(0, setup, sizeof(setup)) == sizeof(setup)) {
			// nothing more to do
		} else {
			return kIOReturnBadArgument;
		}
		// A SETUP packet is always 8 bytes, always OUT, always the SETUP PID.
		return enqueueTransfer(ep, completion, NULL, setup, sizeof(setup), false, true, kHCPID_SETUP);
	}

	// Data and status stages. Control data always begins at DATA1 (USB 2.0
	// 8.5.3); a zero-length status stage is a DATA1 packet too.
	bool isIn = (direction == kUSBIn);
	return enqueueTransfer(ep, completion, memDesc, NULL, bufferSize, isIn, true, kHCPID_DATA1);
}

IOReturn
BCM2837USBHostController::UIMCreateControlTransfer(short functionNumber, short endpointNumber,
    IOUSBCompletion completion, void *CBP, bool bufferRounding, UInt32 bufferSize, short direction)
{
#pragma unused (bufferRounding)
	return queueControlStage(functionNumber, endpointNumber, completion, NULL,
	    (direction == kUSBSetup) ? CBP : NULL, bufferSize, direction);
}

IOReturn
BCM2837USBHostController::UIMCreateControlTransfer(short functionNumber, short endpointNumber,
    IOUSBCompletion completion, IOMemoryDescriptor *CBP, bool bufferRounding,
    UInt32 bufferSize, short direction)
{
#pragma unused (bufferRounding)
	return queueControlStage(functionNumber, endpointNumber, completion, CBP, NULL,
	    bufferSize, direction);
}

IOReturn
BCM2837USBHostController::UIMCreateControlTransfer(short functionNumber, short endpointNumber,
    IOUSBCommand *command, void *CBP, bool bufferRounding, UInt32 bufferSize, short direction)
{
#pragma unused (bufferRounding)
	if (command == NULL) {
		return kIOReturnBadArgument;
	}
	return queueControlStage(functionNumber, endpointNumber, command->GetUSLCompletion(),
	    NULL, (direction == kUSBSetup) ? CBP : NULL, bufferSize, direction);
}

IOReturn
BCM2837USBHostController::UIMCreateControlTransfer(short functionNumber, short endpointNumber,
    IOUSBCommand *command, IOMemoryDescriptor *CBP, bool bufferRounding,
    UInt32 bufferSize, short direction)
{
#pragma unused (bufferRounding)
	if (command == NULL) {
		return kIOReturnBadArgument;
	}
	return queueControlStage(functionNumber, endpointNumber, command->GetUSLCompletion(),
	    CBP, NULL, bufferSize, direction);
}

IOReturn
BCM2837USBHostController::queueDataTransfer(short functionNumber, short endpointNumber,
    UInt8 transferType, IOUSBCompletion completion, IOMemoryDescriptor *memDesc,
    UInt32 bufferSize, short direction)
{
	bool isIn = (direction == kUSBIn);

	if (functionNumber == fRootHubAddress) {
		/*
		 * The root hub's endpoint 1 status-change pipe. Nothing in this kernel
		 * opens it (AppleUSBHub is not vendored), and BCM2837DWC2 enumerates
		 * the bus once at boot rather than reacting to hot-plug, so there is
		 * no status change to report and the read stays outstanding until it
		 * is aborted. Tracking it is what makes that abort clean instead of a
		 * leaked completion.
		 */
		if (endpointNumber != 1 || !isIn) {
			// DAR-296: every other error path in this function (ep ==
			// NULL below, bufferSize/memDesc mismatch, fRootHubIntPending
			// already set) signals failure by return value alone - the
			// real IOUSBFamily controller-command dispatch layer above
			// this UIM is what calls Complete() when a UIMCreate*Transfer
			// call returns non-success. Calling Complete() here too was a
			// real double-notification: the real caller may see both a
			// completed-transfer callback and a distinct error return for
			// what should be one terminal outcome.
			return kIOUSBEndpointNotFound;
		}
		if (fRootHubIntPending) {
			return kIOReturnNotPermitted;
		}
		fRootHubIntCompletion = completion;
		fRootHubIntPending = true;
		return kIOReturnSuccess;
	}

	DWC2UIMEndpoint *ep = createEndpoint((UInt8)functionNumber, (UInt8)endpointNumber,
	    isIn ? kUSBIn : kUSBOut, transferType, 0);
	if (ep == NULL) {
		return kIOUSBEndpointNotFound;
	}

	if (bufferSize > 0 && memDesc == NULL) {
		return kIOReturnBadArgument;
	}

	return enqueueTransfer(ep, completion, memDesc, NULL, bufferSize, isIn, false, 0);
}

IOReturn
BCM2837USBHostController::UIMCreateBulkTransfer(short functionNumber, short endpointNumber,
    IOUSBCompletion completion, IOMemoryDescriptor *CBP, bool bufferRounding,
    UInt32 bufferSize, short direction)
{
#pragma unused (bufferRounding)
	return queueDataTransfer(functionNumber, endpointNumber, kUSBBulk, completion, CBP,
	    bufferSize, direction);
}

IOReturn
BCM2837USBHostController::UIMCreateBulkTransfer(IOUSBCommand *command)
{
	if (command == NULL) {
		return kIOReturnBadArgument;
	}
	return queueDataTransfer(command->GetAddress(), command->GetEndpoint(), kUSBBulk,
	    command->GetUSLCompletion(), command->GetBuffer(), command->GetReqCount(),
	    command->GetDirection());
}

IOReturn
BCM2837USBHostController::UIMCreateInterruptTransfer(short functionNumber, short endpointNumber,
    IOUSBCompletion completion, IOMemoryDescriptor *CBP, bool bufferRounding,
    UInt32 bufferSize, short direction)
{
#pragma unused (bufferRounding)
	return queueDataTransfer(functionNumber, endpointNumber, kUSBInterrupt, completion, CBP,
	    bufferSize, direction);
}

IOReturn
BCM2837USBHostController::UIMCreateInterruptTransfer(IOUSBCommand *command)
{
	if (command == NULL) {
		return kIOReturnBadArgument;
	}
	return queueDataTransfer(command->GetAddress(), command->GetEndpoint(), kUSBInterrupt,
	    command->GetUSLCompletion(), command->GetBuffer(), command->GetReqCount(),
	    command->GetDirection());
}

#pragma mark - UIM: bus state

/*
 * Bandwidth accounting exists for periodic (interrupt/isochronous) scheduling.
 * This UIM does no periodic scheduling at all - every transfer goes through
 * one queue on one hardware channel - so there is no real number to report.
 * Reporting the full high-speed microframe budget would be a fabrication; 0 is
 * the honest answer and is only ever consulted by isochronous clients, of
 * which this kernel has none.
 */
UInt32
BCM2837USBHostController::GetBandwidthAvailable(void)
{
	return 0;
}

/*
 * HFNUM.FRNUM is 14 bits and counts SOFs, and SOFs are microframes on a
 * high-speed root port (QEMU's model: hcd-dwc2.c sets usb_frame_time to
 * 125 us for high speed, 1 ms otherwise). It wraps every 2 s at high speed.
 * The old code extended it by counting wraps seen by the 1 ms engine tick.
 * With the engine parked there is no tick, so each sample instead predicts
 * the count from elapsed uptime and takes the value matching HFNUM that is
 * closest to that prediction. Nothing in this kernel calls GetFrameNumber()
 * today (a grep of the vendored IOUSBFamily/IOHIDFamily finds no caller).
 *
 * The prediction assumes SOFs are running. With the root port disabled
 * they are not and HFNUM stands still, so the sample only resynchronises
 * the time base: no uptime-based wrap is added. fFrameLock serialises
 * callers, because the family does not promise to call this on the
 * workloop.
 */
UInt64
BCM2837USBHostController::sampleFrameNumber(void)
{
	IOSimpleLockLock(fFrameLock);

	uint64_t frame = fDWC2->frameNumber();
	uint64_t now = nowAbs();

	if (fFrameSampleTime == 0 || !(fDWC2->rootPortRegister() & kHPRT0_PRTENA)) {
		if (fFrameSampleTime == 0) {
			fFrameNumber64 = frame;
		}
		fFrameSampleTime = now;
		UInt64 value = fFrameNumber64;
		IOSimpleLockUnlock(fFrameLock);
		return value;
	}

	uint64_t elapsedNS;
	absolutetime_to_nanoseconds(now - fFrameSampleTime, &elapsedNS);
	uint64_t unitNS = (fDWC2->rootPortSpeed() == kUSBSpeedHigh) ? 125000ULL : 1000000ULL;
	uint64_t predicted = fFrameNumber64 + elapsedNS / unitNS;

	const uint64_t span = (uint64_t)kHFNUM_MAX_FRNUM + 1;
	uint64_t candidate = (predicted & ~(span - 1)) | frame;
	if (candidate + span / 2 < predicted) {
		candidate += span;
	} else if (candidate > predicted + span / 2 && candidate >= span) {
		candidate -= span;
	}
	if (candidate < fFrameNumber64) {
		candidate = fFrameNumber64;
	}

	fFrameNumber64 = candidate;
	fFrameSampleTime = now;
	IOSimpleLockUnlock(fFrameLock);
	return candidate;
}

UInt64
BCM2837USBHostController::GetFrameNumber(void)
{
	return sampleFrameNumber();
}

UInt32
BCM2837USBHostController::GetFrameNumber32(void)
{
	return (UInt32)sampleFrameNumber();
}

/*
 * PollInterrupts() is how the family asks a controller to drain its hardware
 * interrupt state outside the normal interrupt path. BCM2837DWC2 takes real
 * channel interrupts (DAR-136: interruptOccurred()/waitForChannelInt(),
 * GAHBCFG.GLBLINTRMSK set in its start()), but only to complete a transfer
 * that is already running. Starting transfers and moving completions up to
 * the family is this class's queue engine, driven by its timer (see "THE
 * QUEUE ENGINE'S SCHEDULE"). So running the engine here is the correct
 * equivalent, not a no-op. It services only endpoints that are due, as a
 * timer wake-up would.
 */
void
BCM2837USBHostController::PollInterrupts(IOUSBCompletionAction safeAction)
{
#pragma unused (safeAction)
	processQueues();
}

#pragma mark - Root hub simulation

/*
 * Modelled on AppleUSBEHCI_RootHub.cpp, which is the real Apple implementation
 * of this same interface for a non-PCI-agnostic controller. The values that
 * differ are the ones that genuinely differ on this silicon: one root port,
 * and no per-port power switching (DWC2 has a single HPRT0.PRTPWR bit).
 */
IOReturn
BCM2837USBHostController::GetRootHubDeviceDescriptor(IOUSBDeviceDescriptor *desc)
{
	IOUSBDeviceDescriptor newDesc =
	{
		sizeof(IOUSBDeviceDescriptor),		// bLength
		kUSBDeviceDesc,				// bDescriptorType
		HostToUSBWord(kUSBRel20),		// bcdUSB
		kUSBHubClass,				// bDeviceClass
		kUSBHubSubClass,			// bDeviceSubClass
		1,					// bDeviceProtocol (single TT)
		64,					// bMaxPacketSize0
		HostToUSBWord(kAppleVendorID),		// idVendor
		HostToUSBWord(0x8005),			// idProduct
		HostToUSBWord(0x0200),			// bcdDevice
		2,					// iManufacturer
		1,					// iProduct
		0,					// iSerialNumber
		1					// bNumConfigurations
	};

	if (desc == NULL) {
		return kIOReturnNoMemory;
	}
	bcopy(&newDesc, desc, newDesc.bLength);
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::GetRootHubDescriptor(IOUSBHubDescriptor *desc)
{
	IOUSBHubDescriptor hubDesc;

	if (desc == NULL) {
		return kIOReturnNoMemory;
	}

	bzero(&hubDesc, sizeof(hubDesc));
	hubDesc.length = sizeof(IOUSBHubDescriptor);
	hubDesc.hubType = kUSBHubDescriptorType;
	hubDesc.numPorts = 1;
	// DWC2 has one port-power bit for the whole controller, so power
	// switching is ganged, not per-port: characteristics bit 0 stays clear.
	hubDesc.characteristics = HostToUSBWord(0);
	hubDesc.powerOnToGood = 50;	// in 2ms units, matching AppleUSBEHCI
	hubDesc.hubCurrent = 0;

	// One port -> one byte of removable flags and one of power-control flags.
	// 0 = not removable (the LAN9514 really is soldered down on a Pi 3).
	hubDesc.removablePortFlags[0] = 0;
	hubDesc.pwrCtlPortFlags[0] = 0xFF;

	hubDesc.length -= ((sizeof(hubDesc.removablePortFlags) - 1) +
	    (sizeof(hubDesc.pwrCtlPortFlags) - 1));

	bcopy(&hubDesc, desc, hubDesc.length);
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::SetRootHubDescriptor(OSData *buffer)
{
#pragma unused (buffer)
	// AppleUSBEHCI does the same: the simulated descriptor is generated from
	// live hardware state, so there is nothing to store.
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::GetRootHubConfDescriptor(OSData *desc)
{
	IOUSBConfigurationDescriptor confDesc =
	{
		sizeof(IOUSBConfigurationDescriptor),
		kUSBConfDesc,
		HostToUSBWord(sizeof(IOUSBConfigurationDescriptor) +
		    sizeof(IOUSBInterfaceDescriptor) +
		    sizeof(IOUSBEndpointDescriptor)),
		1,			// bNumInterfaces
		1,			// bConfigurationValue
		0,			// iConfiguration
		0x60,			// self powered, remote wakeup
		0			// bMaxPower
	};
	IOUSBInterfaceDescriptor intfDesc =
	{
		sizeof(IOUSBInterfaceDescriptor),
		kUSBInterfaceDesc,
		0,			// bInterfaceNumber
		0,			// bAlternateSetting
		1,			// bNumEndpoints
		kUSBHubClass,
		kUSBHubSubClass,
		1,			// bInterfaceProtocol
		0			// iInterface
	};
	IOUSBEndpointDescriptor endptDesc =
	{
		sizeof(IOUSBEndpointDescriptor),
		kUSBEndpointDesc,
		0x81,			// IN, endpoint 1
		kUSBInterrupt,
		HostToUSBWord(8),
		9			// bInterval
	};

	if (desc == NULL) {
		return kIOReturnNoMemory;
	}
	if (!desc->appendBytes(&confDesc, confDesc.bLength)) {
		return kIOReturnNoMemory;
	}
	if (!desc->appendBytes(&intfDesc, intfDesc.bLength)) {
		return kIOReturnNoMemory;
	}
	if (!desc->appendBytes(&endptDesc, endptDesc.bLength)) {
		return kIOReturnNoMemory;
	}
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::GetRootHubStringDescriptor(UInt8 index, OSData *desc)
{
	// UTF-16LE, as USB string descriptors are. Index 1 is iProduct and index 2
	// iManufacturer, matching what GetRootHubDeviceDescriptor() reports.
	static const UInt8 productName[] = {
		20, kUSBStringDesc,
		'D', 0, 'W', 0, 'C', 0, '2', 0, ' ', 0,
		'R', 0, 'o', 0, 'o', 0, 't', 0
	};
	static const UInt8 vendorName[] = {
		16, kUSBStringDesc,
		'B', 0, 'C', 0, 'M', 0, '2', 0, '8', 0, '3', 0, '7', 0
	};

	if (desc == NULL) {
		return kIOReturnNoMemory;
	}
	if (index == 1) {
		return desc->appendBytes(productName, sizeof(productName)) ?
		    kIOReturnSuccess : kIOReturnNoMemory;
	}
	if (index == 2) {
		return desc->appendBytes(vendorName, sizeof(vendorName)) ?
		    kIOReturnSuccess : kIOReturnNoMemory;
	}
	return kIOReturnBadArgument;
}

IOReturn
BCM2837USBHostController::GetRootHubStatus(IOUSBHubStatus *status)
{
	if (status == NULL) {
		return kIOReturnBadArgument;
	}
	// No local-power or over-current reporting at the hub (as opposed to
	// port) level on this core - same as AppleUSBEHCI.
	status->statusFlags = 0;
	status->changeFlags = 0;
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::SetRootHubFeature(UInt16 wValue)
{
#pragma unused (wValue)
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::ClearRootHubFeature(UInt16 wValue)
{
#pragma unused (wValue)
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::GetRootHubPortStatus(IOUSBHubPortStatus *status, UInt16 port)
{
	if (status == NULL) {
		return kIOReturnBadArgument;
	}
	if (port != 1) {
		return kIOReturnBadArgument;
	}

	uint32_t hprt0 = fDWC2->rootPortRegister();
	UInt16 statusFlags = 0;
	UInt16 changeFlags = 0;

	if (hprt0 & kHPRT0_PRTCONNSTS) {
		statusFlags |= kHubPortConnection;
	}
	if (hprt0 & kHPRT0_PRTENA) {
		statusFlags |= kHubPortEnabled;
	}
	if (hprt0 & kHPRT0_PRTOVRCURRACT) {
		statusFlags |= kHubPortOverCurrent;
	}
	if (hprt0 & kHPRT0_PRTRST) {
		statusFlags |= kHubPortBeingReset;
	}
	if (hprt0 & kHPRT0_PRTPWR) {
		statusFlags |= kHubPortPower;
	}

	uint32_t speed = (hprt0 & kHPRT0_PRTSPD_MASK) >> kHPRT0_PRTSPD_SHIFT;
	if (speed == kHPRT0_PRTSPD_LOW) {
		statusFlags |= kHubPortLowSpeed;
	} else if (speed == kHPRT0_PRTSPD_HIGH) {
		statusFlags |= kHubPortHighSpeed;
	}

	if (hprt0 & kHPRT0_PRTCONNDET) {
		changeFlags |= kHubPortConnection;
	}
	if (hprt0 & kHPRT0_PRTENCHNG) {
		changeFlags |= kHubPortEnabled;
	}
	if (hprt0 & kHPRT0_PRTOVRCURRCHNG) {
		changeFlags |= kHubPortOverCurrent;
	}

	status->statusFlags = HostToUSBWord(statusFlags);
	status->changeFlags = HostToUSBWord(changeFlags);
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::SetRootHubPortFeature(UInt16 wValue, UInt16 port)
{
	if (port != 1) {
		return kIOReturnBadArgument;
	}
	return fDWC2->rootPortFeature(wValue, true);
}

IOReturn
BCM2837USBHostController::ClearRootHubPortFeature(UInt16 wValue, UInt16 port)
{
	if (port != 1) {
		return kIOReturnBadArgument;
	}
	return fDWC2->rootPortFeature(wValue, false);
}

IOReturn
BCM2837USBHostController::GetRootHubPortState(UInt8 *state, UInt16 port)
{
#pragma unused (state, port)
	// GET_BUS_STATE, obsolete since USB 1.0. AppleUSBEHCI does the same.
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::SetHubAddress(UInt16 wValue)
{
	fRootHubAddress = wValue;
	// The root hub simulation answers at this address; keep enumeration
	// from handing it to a real device.
	fDWC2->reserveAddress((uint8_t)wValue);
	return kIOReturnSuccess;
}

/*
 * A real controller calls these off its port-change interrupt to tell the hub
 * driver that a port changed. BCM2837DWC2 enumerates once at boot and does not
 * enable or handle port-change interrupts (HPRT0 PRTCONNDET/PRTENCHNG; its
 * DAR-136 interrupt handler covers host-channel completion only), and there is
 * no hub driver here to notify, so there is nothing to signal. This is a real
 * limitation, not a placeholder: USB hot-plug does not work in this kernel
 * (DAR-353).
 */
void
BCM2837USBHostController::UIMRootHubStatusChange(void)
{
}

void
BCM2837USBHostController::UIMRootHubStatusChange(bool abort)
{
	if (abort && fRootHubIntPending) {
		fRootHubIntPending = false;
		Complete(fRootHubIntCompletion, kIOReturnAborted, 0);
	}
}

#pragma mark - Publishing device nubs

IOReturn
BCM2837USBHostController::PublishEnumeratedDevice(const DWC2USBDevice *dev)
{
	if (dev == NULL || !dev->inUse) {
		return kIOReturnBadArgument;
	}

	UInt8 speed;
	switch (dev->speed) {
	case kUSBSpeedHigh:	speed = kUSBDeviceSpeedHigh;	break;
	case kUSBSpeedLow:	speed = kUSBDeviceSpeedLow;	break;
	default:		speed = kUSBDeviceSpeedFull;	break;
	}

	IOUSBDevice *nub = IOUSBDevice::NewDevice();
	if (nub == NULL) {
		return kIOReturnNoMemory;
	}

	/*
	 * The V2 form, so IOUSBControllerV2::CreateDevice() records this device's
	 * high-speed ancestor hub and port. BCM2837DWC2's own transfer engine gets
	 * its split-transaction routing from its enumeration table rather than
	 * from here, but the family publishes these as registry properties and
	 * uses them for its own bookkeeping, so they must be right either way.
	 */
	// IOUSBControllerV2::CreateDevice() (IOUSBControllerV2.cpp:659-678)
	// takes the parent hub and derives the transaction translator from
	// _highSpeedHub[hub] - set only for hubs it created itself. Hubs here
	// are never published, so that lookup is always 0 and a device behind a
	// full-speed hub would get that hub as its TT. Pass the TT this driver
	// already resolved (splitHubAddr/Port, the high-speed hub whose
	// translator it uses) when there is one; CreateDevice() then records it
	// as-is through the same "first non high speed device" branch.
	USBDeviceAddress ttHub = dev->splitHubAddr ? dev->splitHubAddr : dev->parentHubAddr;
	int ttPort = dev->splitHubAddr ? dev->splitHubPort : dev->parentPort;
	IOReturn err = CreateDevice(nub, dev->address, (UInt8)dev->maxPacket0, speed,
	    kUSB500mAAvailable, ttHub, ttPort);
	if (err != kIOReturnSuccess) {
		IOLog("BCM2837USBHostController: CreateDevice(addr %u) failed (0x%x)\n",
		    dev->address, err);
		nub->release();
		return err;
	}

	IOLog("BCM2837USBHostController: published IOUSBDevice %04x:%04x at address %u "
	      "(class %u, %s-speed)\n", dev->idVendor, dev->idProduct, dev->address,
	    dev->deviceClass, (speed == kUSBDeviceSpeedHigh) ? "high" :
	    ((speed == kUSBDeviceSpeedLow) ? "low" : "full"));

	// The hub side of this device's port (DAR-372; see BCM2837USBPortPolicy).
	// Before registerService(), so no driver can match and suspend the
	// device before its hub parent exists.
	BCM2837USBPortPolicy *policy = BCM2837USBPortPolicy::withDevice(this, nub);
	if (policy != NULL) {
		nub->SetHubParent(policy);      // retains it
		policy->release();
	} else {
		IOLog("BCM2837USBHostController: no hub policy for address %u - "
		    "SuspendDevice() will not send kIOUSBMessagePortHasBeenResumed "
		    "(DAR-372)\n", dev->address);
	}

	// CreateDevice() attaches and starts the nub but deliberately does not
	// register it for matching - CreateRootHubDevice() does that separately
	// for the root hub. This is the equivalent step for a real device, and it
	// is what lets AppleUSBCDC's IOUSBDevice personality match.
	nub->registerService();

	// DAR-298: NewDevice() (above) hands back a reference owned by this
	// function. CreateDevice()'s successful attach() already gave the
	// registry its own, independent reference (real
	// IORegistryEntry::attachToParent() -> makeLink() adds this nub to
	// the parent's child OSArray, and OSArray::setObject() retains -
	// confirmed by reading xnu-7195's own IORegistryEntry.cpp, not
	// assumed), so this function's own reference must be released once
	// it's done using the local pointer, same as the error path above
	// already does. Left unreleased, every enumerated device leaked one
	// refcount - real, compounding IORegistry bloat on repeated
	// enumeration/hot-plug cycles.
	nub->release();

	return kIOReturnSuccess;
}

/*
 * Retire every UIM endpoint still open for `address`, on the workloop. The
 * old IOUSBDevice's pipes were closed when it terminated, but a driver can
 * re-queue from its "returned" completion and createEndpoint() would have
 * recreated the endpoint on demand. Without this, the new device at the same
 * address would inherit that endpoint: its toggle, its back-off and a
 * transfer whose completion belongs to a driver that is gone.
 */
IOReturn
BCM2837USBHostController::purgeAddressGated(OSObject *owner, void *arg0, void *, void *, void *)
{
	BCM2837USBHostController *me = (BCM2837USBHostController *)owner;
	UInt8 address = (UInt8)(uintptr_t)arg0;
	for (DWC2UIMEndpoint *ep = me->fEndpoints; ep != NULL; ep = ep->next) {
		if (ep->retired || ep->functionNumber != address) {
			continue;
		}
		ep->retired = true;
		me->collectAsync(ep, true);
		// The status UIMDeleteEndpoint() returns transfers with.
		me->returnTransfers(ep, kIOUSBTransactionReturned);
	}
	// Free them now if nothing up the stack can hold one (the engine
	// may be parked, so its next pass could be a long way off).
	if (!me->fInEngine) {
		me->reapRetiredEndpoints();
	}
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::purgeAllGated(OSObject *owner, void *, void *, void *, void *)
{
	BCM2837USBHostController *me = (BCM2837USBHostController *)owner;
	for (DWC2UIMEndpoint *ep = me->fEndpoints; ep != NULL; ep = ep->next) {
		if (ep->retired) {
			continue;
		}
		ep->retired = true;
		me->collectAsync(ep, true);
		me->returnTransfers(ep, kIOUSBTransactionReturned);
	}
	if (!me->fInEngine) {
		me->reapRetiredEndpoints();
	}
	return kIOReturnSuccess;
}

void
BCM2837USBHostController::RemoveAllDevices(void)
{
	// Collect first: terminating changes the client list being walked.
	OSArray *devices = OSArray::withCapacity(8);
	if (devices == NULL) {
		return;
	}
	OSIterator *it = getClientIterator();
	if (it != NULL) {
		OSObject *obj;
		while ((obj = it->getNextObject()) != NULL) {
			if (OSDynamicCast(IOUSBDevice, obj) != NULL &&
			    OSDynamicCast(IOUSBRootHubDevice, obj) == NULL) {
				devices->setObject(obj);
			}
		}
		it->release();
	}

	uint32_t gen = fDWC2->recoveryGeneration();
	for (unsigned int i = 0; i < devices->getCount(); i++) {
		IOUSBDevice *dev = OSDynamicCast(IOUSBDevice, devices->getObject(i));
		fDWC2->detachDevice(dev->GetAddress(), gen);
		RemoveNub(dev);
	}
	IOLog("BCM2837USBHostController: removed %u device(s) for controller recovery\n",
	    devices->getCount());
	devices->release();

	if (_commandGate != NULL) {
		_commandGate->runAction(purgeAllGated);
	}
}

/*
 * Remove one published device, as Apple's RemoveDevice() does
 * (AppleUSBHubPort.cpp:1074-1150): leave the USB plane, wait while busy,
 * terminate synchronously. The caller has already detached its address.
 * Shared by BCM2837USBPortPolicy::ReEnumeratePort() and RemoveAllDevices().
 */
void
BCM2837USBHostController::RemoveNub(IOUSBDevice *dev)
{
	const IORegistryPlane *usbPlane = dev->getPlane(kIOUSBPlane);
	if (usbPlane != NULL) {
		dev->detachAll(usbPlane);
	}
	for (UInt32 retries = 100; dev->getBusyState() && retries > 0; retries--) {
		IOSleep(100);
	}
	if (dev->getBusyState()) {
		IOLog("BCM2837USBHostController: device %u still busy after 10 s, "
		    "terminating anyway\n", dev->GetAddress());
	}
	dev->terminate(kIOServiceRequired | kIOServiceSynchronous);
}

IOReturn
BCM2837USBHostController::DetachAddress(UInt8 address, uint32_t recoveryGen)
{
	return fDWC2->detachDevice(address, recoveryGen);
}

IOReturn
BCM2837USBHostController::ReEnumerateAddress(UInt8 address, bool extraResetDelay,
    uint32_t recoveryGen)
{
	// The old device was detached (DetachAddress()) before it was torn down,
	// so createEndpoint() could not recreate an endpoint for it meanwhile.
	// Retire whatever the UIM still has open for it.
	if (_commandGate != NULL) {
		_commandGate->runAction(purgeAddressGated, (void *)(uintptr_t)address);
	}

	const DWC2USBDevice *dev = NULL;
	IOReturn ret = fDWC2->reenumerateDevice(address, extraResetDelay, recoveryGen, &dev);
	if (ret != kIOReturnSuccess || dev == NULL) {
		return (ret != kIOReturnSuccess) ? ret : kIOReturnNoDevice;
	}
	if (!fDWC2->publishable(dev)) {
		// Came back as something boot would not have published either (a
		// hub, or the SMSC9514Ethernet function). The old nub is gone and
		// nothing replaces it: report that, not success.
		IOLog("BCM2837USBHostController: device %u came back unpublishable, "
		    "not republished\n", address);
		return kIOReturnUnsupported;
	}
	IOLog("BCM2837USBHostController: re-enumerated device %u, publishing it again\n",
	    address);
	return PublishEnumeratedDevice(dev);
}

void
BCM2837USBHostController::reenumTestFired(thread_call_param_t owner, thread_call_param_t)
{
	BCM2837USBHostController *me = (BCM2837USBHostController *)owner;

	for (UInt8 address = 1; address < 32; address++) {
		if (!(me->fReenumTestMask & (1U << address))) {
			continue;
		}
		IOUSBDevice *target = NULL;
		OSIterator *it = me->getClientIterator();
		if (it != NULL) {
			OSObject *obj;
			while ((obj = it->getNextObject()) != NULL) {
				IOUSBDevice *d = OSDynamicCast(IOUSBDevice, obj);
				if (d != NULL && OSDynamicCast(IOUSBRootHubDevice, obj) == NULL &&
				    d->GetAddress() == address) {
					target = d;
					target->retain();
					break;
				}
			}
			it->release();
		}
		if (target == NULL) {
			IOLog("BCM2837USBHostController: usb-reenum-test: no device at %u\n", address);
			continue;
		}
		IOReturn ret = target->ReEnumerateDevice(0);
		IOLog("BCM2837USBHostController: usb-reenum-test: ReEnumerateDevice(%u) "
		    "returned 0x%x\n", address, ret);
		target->release();
		// ReEnumerateDevice() queues the work; give it time before the next.
		IOSleep(5000);
	}
}

#pragma mark - BCM2837USBPortPolicy (DAR-372)

BCM2837USBPortPolicy *
BCM2837USBPortPolicy::withDevice(BCM2837USBHostController *controller, IOUSBDevice *device)
{
	BCM2837USBPortPolicy *me = new BCM2837USBPortPolicy;
	if (me == NULL) {
		return NULL;
	}
	if (!me->init()) {
		me->release();
		return NULL;
	}
	me->fDevice = device;
	me->fController = controller;
	return me;
}

// Never started, so neither of these is ever called; they are pure in the
// base class.
bool
BCM2837USBPortPolicy::ConfigureHubDriver(void)
{
	return false;
}

IOReturn
BCM2837USBPortPolicy::HubPowerChange(unsigned long powerStateOrdinal)
{
#pragma unused (powerStateOrdinal)
	return kIOReturnUnsupported;
}

IOReturn
BCM2837USBPortPolicy::SuspendPort(UInt32 portNum, bool suspend)
{
#pragma unused (portNum)
	if (!suspend) {
		// Never suspended (see below): AppleUSBHubPort.cpp:1344-1351.
		return kIOUSBDevicePortWasNotSuspended;
	}

	/*
	 * The port stays up. Tell the clients, inline on this thread, the way
	 * AppleUSBHub does when a suspend did not take: SuspendPort() calls
	 * DoPortAction() directly (AppleUSBHub.cpp:5277), which calls
	 * MessageDeviceClients(kIOUSBMessagePortHasBeenResumed) directly
	 * (:3515-3522). A caller holding the USB workloop gate is fine: the
	 * gate is recursive, and a client handler that needs it (e.g.
	 * AppleUSBCDCECMData's SetAlternateInterface()) runs in the same context
	 * the caller's own next request would.
	 */
	IOReturn status = kIOReturnSuccess;
	IOUSBDevice *device = fDevice;
	if (device == NULL) {
		return kIOReturnNoDevice;    // being re-enumerated
	}
	device->retain();
	device->messageClients(kIOUSBMessagePortHasBeenResumed, &status, sizeof(IOReturn));
	device->release();

	return kIOReturnUnsupported;
}

IOReturn
BCM2837USBPortPolicy::ResetPort(UInt32 portNum)
{
#pragma unused (portNum)
	return kIOReturnSuccess;
}

IOReturn
BCM2837USBPortPolicy::GetPortInformation(UInt32 portNum, UInt32 *info)
{
#pragma unused (portNum)
	if (info != NULL) {
		*info = 0;
	}
	return kIOReturnNoDevice;
}

/*
 * Called on the device's own _DO_PORT_REENUMERATE_THREAD
 * (IOUSBDevice::ProcessPortReEnumerateEntry(), which holds a reference to the
 * device for the duration), after _ReEnumerateDevice() has already closed
 * pipe zero and terminated the interfaces. See the header for the Apple
 * sequence this follows.
 */
IOReturn
BCM2837USBPortPolicy::ReEnumeratePort(UInt32 portNum, UInt32 options)
{
#pragma unused (portNum)
	IOUSBDevice *old = fDevice;
	BCM2837USBHostController *controller = fController;
	if (old == NULL || old->isInactive() || controller->isInactive()) {
		// AppleUSBHubPort::ReEnumeratePort() starts the same way
		// (AppleUSBHubPort.cpp:1381-1384, "if ( IsInactive() ) return
		// kIOReturnNotPermitted").
		return kIOReturnNotPermitted;
	}
	UInt8 address = old->GetAddress();
	// A controller recovery (DAR-352) that starts after this point rebuilds
	// the device table; DetachAddress()/ReEnumerateAddress() then refuse
	// rather than act on whatever device now has this address.
	uint32_t gen = controller->recoveryGeneration();

	// Terminating the device finalizes it, and its finalize() releases its
	// hub parent - this object. Keep both alive until this returns.
	retain();
	old->retain();

	// Detach the address in the device table before tearing the driver
	// down. A driver that re-queues a transfer from its "returned"
	// completion during teardown then gets kIOUSBEndpointNotFound, instead
	// of a new endpoint whose completion would later be delivered to the
	// freed driver.
	if (controller->DetachAddress(address, gen) == kIOReturnAborted) {
		// Recovery owns the old device's removal now.
		fDevice = NULL;
		old->release();
		release();
		return kIOReturnAborted;
	}

	// Like RemoveDevice() NULLing _portDevice (AppleUSBHubPort.cpp:1082-
	// 1086): once removal starts, this port has no device, so nothing here
	// dereferences the old one after it may have been freed.
	fDevice = NULL;

	controller->RemoveNub(old);
	old->release();

	// AppleUSBHubPort bails out here if it was stopped meanwhile
	// (AppleUSBHubPort.cpp:1407-1412, "Remove device can be synchronous ...
	// check to see if we have been stop()'s"). Same for the controller.
	if (controller->isInactive()) {
		release();
		return kIOReturnNotPermitted;
	}

	// The add half.
	IOReturn ret = controller->ReEnumerateAddress(address,
	    (options & kUSBAddExtraResetTimeMask) != 0, gen);
	if (ret != kIOReturnSuccess) {
		IOLog("BCM2837USBHostController: re-enumeration of device %u failed "
		    "(0x%x)\n", address, ret);
	}

	release();
	return ret;
}

UInt32
BCM2837USBPortPolicy::RequestExtraPower(UInt32 portNum, UInt32 type, UInt32 requestedPower)
{
#pragma unused (portNum, type, requestedPower)
	return 0;
}

IOReturn
BCM2837USBPortPolicy::ReturnExtraPower(UInt32 portNum, UInt32 type, UInt32 returnedPower)
{
#pragma unused (portNum, type, returnedPower)
	return kIOReturnSuccess;
}

uint32_t
BCM2837USBHostController::recoveryGeneration(void) const
{
	return fDWC2->recoveryGeneration();
}
