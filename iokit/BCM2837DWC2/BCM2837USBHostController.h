/*
 * BCM2837USBHostController - a real IOUSBFamily IOUSBControllerV2 whose UIM
 * is this project's existing BCM2837DWC2 driver.
 *
 * ============================== WHY / SCOPE ==============================
 *
 * BCM2837DWC2 enumerates the bus for real (BCM2837USBEnum.cpp) but keeps the
 * result in a private DWC2USBDevice table - not IOService nubs. Nothing can
 * match against a private table, so every driver above it has had to be
 * hand-constructed inside BCM2837DWC2::start() (see the "BRING-UP
 * SCAFFOLDING" comment at the SMSC9514Ethernet call site there). That is why
 * the real, vendored AppleUSBCDCECM driver could not be reached at all even
 * though QEMU's usb-net enumerates cleanly: an IOProviderClass=IOUSBInterface
 * personality has nothing to match (DAR-243/DAR-244).
 *
 * This class closes that gap by being the real thing IOUSBFamily expects: an
 * IOUSBControllerV2 whose UIM methods drive BCM2837DWC2. With it, real
 * IOUSBDevice/IOUSBInterface nubs get published and real Apple drivers match
 * against them through the normal IOKit path.
 *
 * ========================= WHY IOUSBControllerV2 ========================
 *
 * Not IOUSBController (V1), and the reason is concrete rather than stylistic:
 * IOUSBController::Read() (IOUSBController_Pipes.cpp:392-399) fails any
 * transfer with a non-zero reqCount when command->GetDMACommand() is NULL,
 * and IOUSBController::start() only populates a command's IODMACommand for
 * controllers that are IOUSBControllerV2 ("if (me2) command->SetDMACommand(
 * me2->GetNewDMACommand())", IOUSBController.cpp:548-551). A V1 controller
 * in this vintage of IOUSBFamily therefore cannot perform a single bulk or
 * interrupt read. V2 also hands the UIM the high-speed-hub/port pair for
 * split transactions, and V2::CreateDevice() takes the parent hub address and
 * port - both of which this board genuinely needs.
 *
 * Not IOUSBControllerV3: V3 adds nine more pure virtuals that are entirely a
 * power-management state machine (ResetControllerState, DozeController,
 * SaveControllerStateForSleep, ...) for a board that never sleeps, and V3
 * additionally suppresses IOUSBController::start()'s own CreateRootHubDevice
 * call, moving root-hub creation into a PM-driven path. Implementing that
 * would be fabricating behaviour, not porting it.
 *
 * ======================= THE REAL IMPEDANCE MISMATCH =====================
 *
 * IOUSBFamily's UIM API is asynchronous and command-based: UIMCreate*Transfer
 * is expected to QUEUE work and return immediately, with completion delivered
 * later via Complete(). BCM2837DWC2 is the opposite by design - polled,
 * synchronous, single-channel (see its header's "KNOWN LIMITATIONS").
 *
 * Bridging them by simply running each transfer synchronously inside
 * UIMCreate*Transfer does not work, and the reason is worth stating because
 * it is not obvious: a bulk IN read posted by a network driver is *supposed*
 * to stay outstanding until a packet arrives. A device with nothing to send
 * NAKs, which is normal flow control, not an error. A synchronous UIM would
 * either block the controller's workloop for the full NAK timeout on every
 * idle poll, or complete the read with zero bytes - which the driver reads as
 * "a packet arrived, it was empty", re-queues instantly, and busy-spins.
 *
 * So this class implements a real queue. UIMCreate*Transfer appends to a
 * per-endpoint FIFO and returns. An IOTimerEventSource on the controller's own
 * IOUSBWorkLoop drains those FIFOs, giving each pending transfer one
 * non-blocking attempt per tick (BCM2837DWC2::transferStage() with a zero
 * retry budget returns immediately on NAK) and re-queueing anything that did
 * not finish. Partial progress is remembered per transfer, so a multi-chunk
 * transfer that NAKs half way RESUMES rather than restarting - restarting
 * would re-send data with a stale data-toggle, which the device silently
 * drops as a duplicate (the same trap BCM2837DWC2::doTransferRetrying()'s own
 * header comment documents).
 *
 * Timer callouts run holding the workloop lock, which is the same lock
 * IOCommandGate takes, so completions are delivered in exactly the context
 * IOUSBFamily expects of a UIM - the context a real controller's interrupt
 * event source would deliver them in - and commandWakeup() for synchronous
 * transfers is legal there.
 *
 * ============================ ROOT HUB =================================
 *
 * The 12 root-hub simulation methods are implemented for real against HPRT0,
 * because IOUSBController::start() will not complete without them:
 * CreateRootHubDevice() calls GetRootHubDeviceDescriptor() and then runs a
 * full IOUSBDevice::start() against the simulated hub, which fetches its
 * device and configuration descriptors. Those requests never reach the UIM -
 * IOUSBRootHubDevice overrides DeviceRequest() and dispatches straight to
 * these methods (IOUSBRootHubDevice::DeviceRequestWorker) - so they are the
 * only implementation of them that exists.
 *
 * What the root hub does NOT do here is enumerate. That is AppleUSBHub's job,
 * and AppleUSBHub is not vendored into this kernel; BCM2837DWC2's own
 * enumerateBus() already walks the bus (including QEMU's downstream usb-hub)
 * and is what this controller publishes nubs from. The root hub device is
 * therefore published, correct, and unmatched - which is the honest state,
 * not a stub.
 */

#ifndef _BCM2837_USB_HOST_CONTROLLER_H
#define _BCM2837_USB_HOST_CONTROLLER_H

#include <IOKit/usb/IOUSBControllerV2.h>
#include <IOKit/IOTimerEventSource.h>

#include "BCM2837DWC2.h"

/*
 * One queued transfer. Owned by the endpoint FIFO it is linked into; freed
 * only after its completion has been delivered.
 *
 * `pid` is the DATA0/DATA1/SETUP sequence value for THIS transfer, used only
 * by control stages, which drive the toggle explicitly per stage (SETUP,
 * then DATA1, then DATA1 for status). Bulk and interrupt transfers instead
 * point the DWC2 engine at the endpoint's own persistent toggle, because USB
 * requires a data endpoint's sequence bit to survive across transfers.
 *
 * `done` is what makes NAK-and-resume correct rather than corrupting: a
 * transfer that moved some bytes and then NAKed resumes at `done` with the
 * toggle the hardware left behind.
 */
struct DWC2UIMTransfer
{
	DWC2UIMTransfer     *next;
	IOUSBCompletion      completion;
	IOMemoryDescriptor  *memDesc;      // data source/sink; NULL for zero-length
	UInt8                inlineData[8]; // SETUP packets arrive as a raw pointer
	bool                 useInlineData;
	UInt32               length;
	UInt32               done;
	// Consecutive engine ticks this transfer has been attempted without
	// moving a single byte. Reset by any progress. This is what separates
	// "the device is NAKing because it has nothing for us yet", which is
	// normal and unbounded for an IN transfer, from "this transfer is wedged",
	// which has to fail eventually for control and OUT transfers.
	UInt32               stalledTicks;
	UInt8                pid;
	bool                 usePid;       // control stage: drive `pid`, not the endpoint toggle
	bool                 isIn;
};

/*
 * One endpoint the family has asked us to service. Keyed by (function
 * address, endpoint number, direction); control endpoints ignore direction
 * because a control endpoint is bidirectional by definition.
 *
 * `dep` is the DWC2-level endpoint descriptor, built once by
 * BCM2837DWC2::makeEndpoint() so that the split-transaction fields and the
 * data toggle both persist for the endpoint's whole life. Rebuilding it per
 * transfer would silently reset the toggle to DATA0 every time.
 */
struct DWC2UIMEndpoint
{
	DWC2UIMEndpoint     *next;
	UInt8                functionNumber;
	UInt8                endpointNumber;
	UInt8                direction;      // kUSBOut / kUSBIn / kUSBAnyDirn
	UInt8                transferType;   // kUSBControl / kUSBBulk / kUSBInterrupt
	DWC2Endpoint         dep;
	DWC2UIMTransfer     *head;
	DWC2UIMTransfer     *tail;
	bool                 retired;        // deleted while the engine was iterating
};

class BCM2837USBHostController : public IOUSBControllerV2
{
	OSDeclareDefaultStructors(BCM2837USBHostController);

public:
	virtual bool	init(OSDictionary *propTable) override;
	virtual void	free(void) override;

	/*
	 * IOUSBControllerV2 narrows IOService::start() back to protected, because
	 * every real Apple controller is instantiated by IOKit matching against a
	 * PCI nub rather than by hand. This one has no personality and no nub to
	 * match - BCM2837DWC2 constructs it directly once its own enumeration has
	 * finished - so the access has to be widened again here. Widening an
	 * inherited member's access in a derived class is the intended C++
	 * mechanism for exactly this, and it changes nothing about the override
	 * itself: this just forwards to super::start().
	 */
	virtual bool	start(IOService *provider) override;

	/*
	 * Publish a real IOUSBDevice nub for one device BCM2837DWC2 already
	 * enumerated. Called by BCM2837DWC2::start() after this controller has
	 * started, on the IOKit configuration thread - NOT on the USB workloop,
	 * which matters because IOUSBDevice::start() issues synchronous device
	 * requests and IOUSBFamily explicitly rejects those from the workloop
	 * thread (IOUSBController_Pipes.cpp:363-367).
	 */
	IOReturn	PublishEnumeratedDevice(const DWC2USBDevice *dev);

	// ---------------- UIM: initialisation ----------------
	virtual IOReturn	UIMInitialize(IOService *provider) override;
	virtual IOReturn	UIMFinalize(void) override;

	// ---------------- UIM: endpoint management ----------------
	virtual IOReturn	UIMCreateControlEndpoint(UInt8 functionNumber, UInt8 endpointNumber,
			    UInt16 maxPacketSize, UInt8 speed) override;
	virtual IOReturn	UIMCreateControlEndpoint(UInt8 functionNumber, UInt8 endpointNumber,
			    UInt16 maxPacketSize, UInt8 speed,
			    USBDeviceAddress highSpeedHub, int highSpeedPort) override;

	virtual IOReturn	UIMCreateBulkEndpoint(UInt8 functionNumber, UInt8 endpointNumber,
			    UInt8 direction, UInt8 speed, UInt8 maxPacketSize) override;
	virtual IOReturn	UIMCreateBulkEndpoint(UInt8 functionNumber, UInt8 endpointNumber,
			    UInt8 direction, UInt8 speed, UInt16 maxPacketSize,
			    USBDeviceAddress highSpeedHub, int highSpeedPort) override;

	virtual IOReturn	UIMCreateInterruptEndpoint(short functionAddress, short endpointNumber,
			    UInt8 direction, short speed, UInt16 maxPacketSize, short pollingRate) override;
	virtual IOReturn	UIMCreateInterruptEndpoint(short functionAddress, short endpointNumber,
			    UInt8 direction, short speed, UInt16 maxPacketSize, short pollingRate,
			    USBDeviceAddress highSpeedHub, int highSpeedPort) override;

	virtual IOReturn	UIMCreateIsochEndpoint(short functionAddress, short endpointNumber,
			    UInt32 maxPacketSize, UInt8 direction) override;
	virtual IOReturn	UIMCreateIsochEndpoint(short functionAddress, short endpointNumber,
			    UInt32 maxPacketSize, UInt8 direction,
			    USBDeviceAddress highSpeedHub, int highSpeedPort) override;

	virtual IOReturn	UIMAbortEndpoint(short functionNumber, short endpointNumber,
			    short direction) override;
	virtual IOReturn	UIMDeleteEndpoint(short functionNumber, short endpointNumber,
			    short direction) override;
	virtual IOReturn	UIMClearEndpointStall(short functionNumber, short endpointNumber,
			    short direction) override;

	// ---------------- UIM: transfers ----------------
	// The IOUSBCommand* forms are what IOUSBController's transaction layer
	// actually calls; the IOUSBCompletion forms are the older API the family
	// still declares pure and therefore still has to be defined.
	virtual IOReturn	UIMCreateControlTransfer(short functionNumber, short endpointNumber,
			    IOUSBCompletion completion, void *CBP, bool bufferRounding,
			    UInt32 bufferSize, short direction) override;
	virtual IOReturn	UIMCreateControlTransfer(short functionNumber, short endpointNumber,
			    IOUSBCompletion completion, IOMemoryDescriptor *CBP, bool bufferRounding,
			    UInt32 bufferSize, short direction) override;
	virtual IOReturn	UIMCreateControlTransfer(short functionNumber, short endpointNumber,
			    IOUSBCommand *command, void *CBP, bool bufferRounding,
			    UInt32 bufferSize, short direction) override;
	virtual IOReturn	UIMCreateControlTransfer(short functionNumber, short endpointNumber,
			    IOUSBCommand *command, IOMemoryDescriptor *CBP, bool bufferRounding,
			    UInt32 bufferSize, short direction) override;

	virtual IOReturn	UIMCreateBulkTransfer(short functionNumber, short endpointNumber,
			    IOUSBCompletion completion, IOMemoryDescriptor *CBP, bool bufferRounding,
			    UInt32 bufferSize, short direction) override;
	virtual IOReturn	UIMCreateBulkTransfer(IOUSBCommand *command) override;

	virtual IOReturn	UIMCreateInterruptTransfer(short functionNumber, short endpointNumber,
			    IOUSBCompletion completion, IOMemoryDescriptor *CBP, bool bufferRounding,
			    UInt32 bufferSize, short direction) override;
	virtual IOReturn	UIMCreateInterruptTransfer(IOUSBCommand *command) override;

	virtual IOReturn	UIMCreateIsochTransfer(short functionAddress, short endpointNumber,
			    IOUSBIsocCompletion completion, UInt8 direction, UInt64 frameStart,
			    IOMemoryDescriptor *pBuffer, UInt32 frameCount,
			    IOUSBIsocFrame *pFrames) override;

	// ---------------- UIM: bus/frame ----------------
	virtual UInt32		GetBandwidthAvailable(void) override;
	virtual UInt64		GetFrameNumber(void) override;
	virtual UInt32		GetFrameNumber32(void) override;
	virtual void		PollInterrupts(IOUSBCompletionAction safeAction = 0) override;

	// ---------------- UIM: root hub simulation ----------------
	virtual void		UIMRootHubStatusChange(void) override;
	virtual void		UIMRootHubStatusChange(bool abort) override;

	virtual IOReturn	GetRootHubDeviceDescriptor(IOUSBDeviceDescriptor *desc) override;
	virtual IOReturn	GetRootHubDescriptor(IOUSBHubDescriptor *desc) override;
	virtual IOReturn	SetRootHubDescriptor(OSData *buffer) override;
	virtual IOReturn	GetRootHubConfDescriptor(OSData *desc) override;
	virtual IOReturn	GetRootHubStringDescriptor(UInt8 index, OSData *desc) override;
	virtual IOReturn	GetRootHubStatus(IOUSBHubStatus *status) override;
	virtual IOReturn	SetRootHubFeature(UInt16 wValue) override;
	virtual IOReturn	ClearRootHubFeature(UInt16 wValue) override;
	virtual IOReturn	GetRootHubPortStatus(IOUSBHubPortStatus *status, UInt16 port) override;
	virtual IOReturn	SetRootHubPortFeature(UInt16 wValue, UInt16 port) override;
	virtual IOReturn	ClearRootHubPortFeature(UInt16 wValue, UInt16 port) override;
	virtual IOReturn	GetRootHubPortState(UInt8 *state, UInt16 port) override;
	virtual IOReturn	SetHubAddress(UInt16 wValue) override;

private:
	BCM2837DWC2         *fDWC2;
	IOTimerEventSource  *fPollTimer;
	DWC2UIMEndpoint     *fEndpoints;
	UInt8               *fStaging;        // bounce buffer between IOMemoryDescriptor and DWC2
	UInt32               fStagingSize;
	UInt16               fRootHubAddress;
	bool                 fEngineRunning;
	bool                 fInEngine;      // re-entrancy guard for processQueues()
	UInt32               fLastFrameNumber;
	UInt64               fFrameNumber64;

	// Root-hub interrupt (status change) pipe, endpoint 1. Nothing in this
	// kernel opens it - AppleUSBHub is not vendored - but a queued read must
	// still be tracked so an abort can return it rather than leak it.
	IOUSBCompletion      fRootHubIntCompletion;
	bool                 fRootHubIntPending;

	DWC2UIMEndpoint    *findEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction);
	DWC2UIMEndpoint    *createEndpoint(UInt8 functionNumber, UInt8 endpointNumber,
			    UInt8 direction, UInt8 transferType, UInt16 maxPacketSize);
	void			reapRetiredEndpoints(void);
	void			returnTransfers(DWC2UIMEndpoint *ep, IOReturn status);

	IOReturn		enqueueTransfer(DWC2UIMEndpoint *ep, IOUSBCompletion completion,
			    IOMemoryDescriptor *memDesc, const void *inlineData, UInt32 length,
			    bool isIn, bool usePid, UInt8 pid);

	// Returns true if the head transfer completed (successfully or not) and
	// its completion has been delivered; false if it is still pending.
	bool			serviceTransfer(DWC2UIMEndpoint *ep);
	void			processQueues(void);
	static void		pollTimerFired(OSObject *owner, IOTimerEventSource *sender);

	IOReturn		queueControlStage(short functionNumber, short endpointNumber,
			    IOUSBCompletion completion, IOMemoryDescriptor *memDesc,
			    const void *rawSetup, UInt32 bufferSize, short direction);
	IOReturn		queueDataTransfer(short functionNumber, short endpointNumber,
			    UInt8 transferType, IOUSBCompletion completion,
			    IOMemoryDescriptor *memDesc, UInt32 bufferSize, short direction);
};

#endif /* _BCM2837_USB_HOST_CONTROLLER_H */
