/*
 * BCM2837USBHostController - IOUSBControllerV2 over BCM2837DWC2.
 * See BCM2837USBHostController.h for the design rationale; this file is the
 * implementation only.
 */

#include "BCM2837USBHostController.h"

#include <IOKit/IOLib.h>
#include <IOKit/usb/IOUSBDevice.h>
#include <IOKit/usb/IOUSBRootHubDevice.h>
#include <IOKit/usb/USB.h>
#include <IOKit/usb/USBHub.h>
#include <IOKit/usb/USBSpec.h>
#include <libkern/OSByteOrder.h>
#include <pexpert/pexpert.h>

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

// The queue engine's tick. 1ms is the USB full-speed frame time, so a pending
// bulk IN gets one attempt per frame - the same cadence a real interrupt-
// driven controller would service it at, without the interrupt.
#define kPollIntervalUS		1000

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
static inline UInt32 pollIntervalUS(void)
{
	return kPollIntervalUS * (UInt32)debug_cpu_performance_degradation_factor;
}

// Ticks a transfer may make no progress before it is failed. Control and
// bulk OUT get real ceilings because a stuck one must not hang a caller
// forever. IN transfers get NONE on purpose: an outstanding bulk/interrupt
// read with no data yet is not a fault, it is the entire point of the
// asynchronous API, and timing one out would break every network driver.
#define kControlStageTimeoutTicks	1000
#define kDataOutTimeoutTicks		5000

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
	fLastFrameNumber = 0;
	fFrameNumber64 = 0;
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
	// UIMFinalize() does the real teardown; this is the belt-and-braces path
	// for a controller that failed part-way through start().
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

	if (fStaging != NULL) {
		IOFree(fStaging, fStagingSize);
		fStaging = NULL;
		fStagingSize = 0;
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

	fEngineRunning = true;
	fPollTimer->setTimeoutUS(pollIntervalUS());

	IOLog("BCM2837USBHostController: UIM up on %s, %u enumerated device "
	      "address(es) reserved\n", fDWC2->getName(), reserved);

	return kIOReturnSuccess;
}

IOReturn
BCM2837USBHostController::UIMFinalize(void)
{
	fEngineRunning = false;

	if (fPollTimer != NULL) {
		fPollTimer->cancelTimeout();
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

	const DWC2USBDevice *dev = NULL;
	for (uint32_t i = 0; i < fDWC2->deviceCount(); i++) {
		const DWC2USBDevice *candidate = fDWC2->deviceAt(i);
		if (candidate != NULL && candidate->address == functionNumber) {
			dev = candidate;
			break;
		}
	}
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
	ep->next = fEndpoints;
	fEndpoints = ep;

	return ep;
}

void
BCM2837USBHostController::returnTransfers(DWC2UIMEndpoint *ep, IOReturn status)
{
	DWC2UIMTransfer *xfer = ep->head;
	ep->head = NULL;
	ep->tail = NULL;

	while (xfer != NULL) {
		DWC2UIMTransfer *next = xfer->next;
		IOUSBCompletion completion = xfer->completion;
		UInt32 remaining = xfer->length - xfer->done;
		IOFree(xfer, sizeof(*xfer));
		Complete(completion, status, remaining);
		xfer = next;
	}
}

/*
 * Endpoints deleted from inside a completion routine cannot be freed
 * immediately - processQueues() is walking the very list they are on. They are
 * marked retired instead (which makes findEndpoint() ignore them at once) and
 * unlinked at the top of the next tick.
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
BCM2837USBHostController::serviceTransfer(DWC2UIMEndpoint *ep)
{
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
		if (moved > 0) {
			xfer->stalledTicks = 0;
		} else {
			xfer->stalledTicks++;
		}
		UInt32 limit = xfer->usePid ? kControlStageTimeoutTicks :
		    (xfer->isIn ? 0 : kDataOutTimeoutTicks);
		if (limit != 0 && xfer->stalledTicks >= limit) {
			status = kIOUSBTransactionTimeout;
			goto finished;
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

	xfer->stalledTicks = 0;

	// A short IN packet ends the transfer early; that is a normal, successful
	// result, not a truncation error.
	if (xfer->done < xfer->length && !(xfer->isIn && moved < chunk)) {
		return false;
	}

finished:
	{
		ep->head = xfer->next;
		if (ep->head == NULL) {
			ep->tail = NULL;
		}
		IOUSBCompletion completion = xfer->completion;
		UInt32 leftover = xfer->length - xfer->done;
		IOFree(xfer, sizeof(*xfer));
		Complete(completion, status, leftover);
	}
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

	// Keep a 32-bit frame counter extended to 64 bits. HFNUM wraps every
	// 16384 frames (~16s at full speed), far more often than anything here
	// samples it, so the wrap has to be accumulated rather than recomputed.
	uint32_t frame = fDWC2->frameNumber();
	if (frame < fLastFrameNumber) {
		fFrameNumber64 += 0x4000;
	}
	fLastFrameNumber = frame;

	for (DWC2UIMEndpoint *ep = fEndpoints; ep != NULL; ep = ep->next) {
		if (ep->retired) {
			continue;
		}
		for (int i = 0; i < kMaxTransfersPerTick; i++) {
			if (ep->head == NULL) {
				break;
			}
			if (!serviceTransfer(ep)) {
				break;
			}
		}
	}

	fInEngine = false;
}

void
BCM2837USBHostController::pollTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
	BCM2837USBHostController *me = OSDynamicCast(BCM2837USBHostController, owner);
	if (me == NULL) {
		return;
	}

	me->processQueues();

	if (me->fEngineRunning) {
		sender->setTimeoutUS(pollIntervalUS());
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
#pragma unused (speed, pollingRate, highSpeedHub, highSpeedPort)
	// The root hub's status-change endpoint is simulated, not real silicon:
	// there is no channel to program for it.
	if (functionAddress == fRootHubAddress) {
		return kIOReturnSuccess;
	}
	return (createEndpoint((UInt8)functionAddress, (UInt8)endpointNumber, direction,
	    kUSBInterrupt, maxPacketSize) != NULL) ? kIOReturnSuccess : kIOUSBEndpointNotFound;
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

	returnTransfers(ep, kIOUSBTransactionReturned);
	// Unlinked and freed on the next tick - see reapRetiredEndpoints().
	ep->retired = true;
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

	returnTransfers(ep, kIOUSBTransactionReturned);
	// USB 2.0 9.4.5: clearing a halt resets the endpoint's data toggle to
	// DATA0 on both sides. The device half is done by the CLEAR_FEATURE the
	// pipe layer sends; this is the host half.
	ep->dep.toggle = kHCPID_DATA0;
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
			Complete(completion, kIOUSBEndpointNotFound, bufferSize);
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

UInt64
BCM2837USBHostController::GetFrameNumber(void)
{
	return fFrameNumber64 + fDWC2->frameNumber();
}

UInt32
BCM2837USBHostController::GetFrameNumber32(void)
{
	return (UInt32)(fFrameNumber64 + fDWC2->frameNumber());
}

/*
 * PollInterrupts() is how the family asks a controller to drain its hardware
 * interrupt state outside the normal interrupt path. This controller has no
 * interrupt path - BCM2837DWC2 deliberately leaves GAHBCFG.GLBLINTRMSK clear
 * and polls - so the queue engine IS the interrupt handler, and running it
 * here is the correct equivalent rather than a no-op.
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
	return kIOReturnSuccess;
}

/*
 * A real controller calls these off its port-change interrupt to tell the hub
 * driver that a port changed. BCM2837DWC2 masks global interrupts by design
 * and enumerates once at boot, and there is no hub driver here to notify, so
 * there is genuinely nothing to signal. This is a real limitation - USB
 * hot-plug does not work in this kernel - not a placeholder for missing code.
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
	IOReturn err = CreateDevice(nub, dev->address, (UInt8)dev->maxPacket0, speed,
	    kUSB500mAAvailable, dev->parentHubAddr, dev->parentPort);
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

	// CreateDevice() attaches and starts the nub but deliberately does not
	// register it for matching - CreateRootHubDevice() does that separately
	// for the root hub. This is the equivalent step for a real device, and it
	// is what lets AppleUSBCDC's IOUSBDevice personality match.
	nub->registerService();

	return kIOReturnSuccess;
}
