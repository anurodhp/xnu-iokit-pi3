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
 * non-blocking attempt when its endpoint is due (BCM2837DWC2::transferStage()
 * with a zero retry budget returns immediately on NAK) and re-queueing
 * anything that did not finish. "Due" is per endpoint: an interrupt endpoint
 * every bInterval, an idle bulk IN on a NAK back-off, everything else every
 * frame. The timer is armed only for the earliest due endpoint and not at all
 * when nothing is queued (DAR-370; before that it ticked every 1 ms for the
 * whole uptime). Partial progress is remembered per transfer, so a multi-chunk
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
#include <IOKit/usb/IOUSBHubPolicyMaker.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOInterruptEventSource.h>

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
	// Absolute time of the first attempt since this transfer last moved a
	// byte; 0 = not stalled. Reset by any progress. This is what separates
	// "the device is NAKing because it has nothing for us yet", which is
	// normal and unbounded for an IN transfer, from "this transfer is wedged",
	// which has to fail eventually for control and OUT transfers.
	uint64_t             stallStart;
	UInt32               xactErrors;    // consecutive XACTERRs on a parked chunk
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

	// Scheduling (DAR-370). The engine timer is armed for the earliest
	// nextDue of any endpoint with a queued transfer, and not at all when
	// none has one. See "THE QUEUE ENGINE'S SCHEDULE" in the .cpp.
	UInt32               periodUS;       // interrupt: bInterval as microseconds; 0 = not periodic
	UInt32               backoffUS;      // bulk IN: current NAK back-off step
	uint64_t             nextDue;        // absolute time of the next attempt; 0 = due now

	// Parked on a DWC2 async channel (DAR-370): the channel number, or -1.
	// While parked, the head transfer belongs to the hardware and the
	// timer engine leaves this endpoint alone. asyncReq is the chunk the
	// channel was armed for, which is what a short packet is measured
	// against.
	int                  asyncChan;
	UInt32               asyncReq;
};

/*
 * The hub side of one published device's port (DAR-372).
 *
 * IOUSBDevice routes port-level operations through its hub parent, the
 * IOUSBHubPolicyMaker that AppleUSBHub would normally be
 * (IOUSBDevice::SetHubParent(); _HUBPARENT in IOUSBDevice.cpp). AppleUSBHub
 * is not vendored here (see the header comment above), so every device this
 * controller published had no hub parent. IOUSBDevice::SuspendDevice() then
 * reports a suspend that never happened (kIOUSBMessagePortHasBeenSuspended
 * with status success) and a resume that sends nothing at all
 * (IOUSBDevice.cpp:4036-4058). AppleUSBCDCECMData's disable() selects data
 * alternate setting 0, which has no endpoints, and suspends the device. It
 * gets its pipes back only when kIOUSBMessagePortHasBeenResumed arrives
 * (AppleUSBCDCECMData.cpp:2812-2819). Without that message, en0 stayed dead
 * after `ifconfig en0 down; ifconfig en0 up`.
 *
 * Selective suspend is not implemented: the port is never suspended on the
 * bus. Actually suspending it would need the UIM to stop polling the
 * device's endpoints first, because a hub does not forward tokens to a
 * suspended port. So this object answers the way AppleUSBHub itself does
 * when a suspend request did not leave the port suspended.
 * AppleUSBHub::SuspendPort() runs DoPortAction() inline
 * (third_party/IOUSBFamily/AppleUSBHub/Classes/AppleUSBHub.cpp:5277), which
 * then tells the device's clients kIOUSBMessagePortHasBeenResumed, also
 * inline (:3515-3522, "we are not suspended ... let our clients know").
 *
 *   SuspendPort(true)   delivers kIOUSBMessagePortHasBeenResumed to the
 *                       device's clients synchronously, on the caller's
 *                       thread, as above. Then returns kIOReturnUnsupported,
 *                       so IOUSBDevice also tells clients
 *                       kIOUSBMessagePortWasNotSuspended (asynchronously, from
 *                       its own thread call) and SuspendDevice(true) reports
 *                       the failure. Before this, it reported a suspend that
 *                       never happened as a success.
 *                       Consequence: AppleUSBCDCECMData re-selects its data
 *                       alternate setting inside its own disable(), so the
 *                       data interface's bulk pipes stay open while en0 is
 *                       down. No reads or writes are posted on them. Real
 *                       AppleUSBHub produces the same state on this path.
 *   SuspendPort(false)  the port is not suspended, so
 *                       kIOUSBDevicePortWasNotSuspended, as AppleUSBHubPort
 *                       answers (AppleUSBHubPort.cpp:1344-1351).
 *                       IOUSBDevice::SuspendDevice(false) still returns
 *                       success (IOUSBDevice.cpp:4047-4059).
 *
 * Installing a hub parent also routes IOUSBDevice's other port operations
 * here. Each keeps the result it had with no hub parent:
 *   ResetPort           kIOReturnSuccess. With no parent,
 *                       IOUSBDevice::_ResetDevice() skipped the port reset
 *                       and still returned success, recreated pipe zero and
 *                       sent kIOUSBMessagePortHasBeenReset
 *                       (IOUSBDevice.cpp:1347-1418). With this parent it does
 *                       the same, except that pipe zero is now aborted and
 *                       closed before being recreated, not overwritten while
 *                       still open. A real port reset would need re-addressing
 *                       through BCM2837DWC2's enumeration, which is not
 *                       implemented.
 *   GetPortInformation  kIOReturnNoDevice with *info = 0, the no-parent
 *                       result (IOUSBDevice.cpp:4233-4246). The one difference
 *                       is that GetDeviceInformation() now continues past the
 *                       call and records "not internal" from that zero.
 *   ReEnumeratePort     real (DAR-373), as AppleUSBHubPort::ReEnumeratePort()
 *                       does it: RemoveDevice() then add the device again
 *                       (AppleUSBHubPort.cpp:1378-1420).
 *                       Remove: detach it from the USB plane, wait up to 10 s
 *                       for it to stop being busy, then terminate it
 *                       synchronously (:1074-1150).
 *                       Add: BCM2837DWC2::reenumerateDevice() resets the port
 *                       and enumerates the device into the same table slot at
 *                       the same address. PublishEnumeratedDevice() then
 *                       creates a new IOUSBDevice, with a new one of these as
 *                       its hub parent.
 *                       Before DAR-373 this was the base class's
 *                       kIOReturnUnsupported: _ReEnumerateDevice() tore the
 *                       device down and nothing brought it back.
 *   Request/ReturnExtraPower  0 mA and success, what IOUSBDevice computes
 *                       with no parent. The base class would dereference
 *                       _device, the hub's own IOUSBHubDevice, which does
 *                       not exist here.
 *
 * Never started or registered. It is a plain object that IOUSBDevice
 * retains as its hub parent, not a node in the service or power planes.
 */
class BCM2837USBHostController;

class BCM2837USBPortPolicy : public IOUSBHubPolicyMaker
{
	OSDeclareDefaultStructors(BCM2837USBPortPolicy);

public:
	static BCM2837USBPortPolicy *withDevice(BCM2837USBHostController *controller,
	    IOUSBDevice *device);

	virtual bool		ConfigureHubDriver(void) override;
	virtual IOReturn	HubPowerChange(unsigned long powerStateOrdinal) override;
	virtual IOReturn	SuspendPort(UInt32 portNum, bool suspend) override;
	virtual IOReturn	ResetPort(UInt32 portNum) override;
	virtual IOReturn	GetPortInformation(UInt32 portNum, UInt32 *info) override;
	virtual IOReturn	ReEnumeratePort(UInt32 portNum, UInt32 options) override;
	virtual UInt32		RequestExtraPower(UInt32 portNum, UInt32 type, UInt32 requestedPower) override;
	virtual IOReturn	ReturnExtraPower(UInt32 portNum, UInt32 type, UInt32 returnedPower) override;

private:
	IOUSBDevice    *fDevice;      // not retained: it retains us (SetHubParent)
	BCM2837USBHostController *fController;  // not retained: outlives every device it publishes
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

	/*
	 * The add half of BCM2837USBPortPolicy::ReEnumeratePort() (DAR-373):
	 * re-enumerate the device at `address` on the bus, then publish it
	 * again. Same thread rules as PublishEnumeratedDevice(); the caller has
	 * already terminated the old nub.
	 */
	IOReturn	ReEnumerateAddress(UInt8 address, bool extraResetDelay);
	// The first step of re-enumeration: mark the address gone in
	// BCM2837DWC2's table before the old IOUSBDevice is torn down.
	void		DetachAddress(UInt8 address);

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
	uint64_t             fArmedDeadline; // absolute time fPollTimer is armed for; 0 = parked

	// GetFrameNumber()'s 64-bit extension of HFNUM. See sampleFrameNumber().
	UInt64               fFrameNumber64;
	uint64_t             fFrameSampleTime;
	IOSimpleLock        *fFrameLock;

	// Engine wake-up accounting for the usb-engine-stats boot-arg (DAR-370).
	bool                 fEngineStats;
	UInt32               fEngineWakeups;
	UInt32               fEngineAttempts;
	uint64_t             fEngineStatsSince;
	UInt32               fAsyncEvents;       // async completions delivered by interrupt
	UInt32               fAsyncPolled;       // ... found by the engine's poll instead

	// usb-reenum-test boot-arg (DAR-373 verification): see UIMInitialize().
	thread_call_t        fReenumTestCall;
	UInt32               fReenumTestMask;
	static void		reenumTestFired(thread_call_param_t owner, thread_call_param_t);
	static IOReturn		purgeAddressGated(OSObject *owner, void *arg0, void *, void *, void *);

	// Poked by BCM2837DWC2::interruptOccurred() when a parked channel halts.
	// Runs asyncEventFired() on this controller's workloop.
	IOInterruptEventSource *fAsyncEvent;
	bool                 fAsyncEventDeferred;  // doorbell rang inside an fInEngine section

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
	void			completeHead(DWC2UIMEndpoint *ep, IOReturn status);

	// Parked async bulk IN (DAR-370). See "PARKED BULK IN" in the .cpp.
	bool			startAsync(DWC2UIMEndpoint *ep);
	void			kickAsync(void);
	void			leaveEngine(void);
	void			collectAllAsync(void);
	bool			collectAsync(DWC2UIMEndpoint *ep, bool abort);
	static void		asyncEventFired(OSObject *owner, IOInterruptEventSource *sender, int count);

	IOReturn		enqueueTransfer(DWC2UIMEndpoint *ep, IOUSBCompletion completion,
			    IOMemoryDescriptor *memDesc, const void *inlineData, UInt32 length,
			    bool isIn, bool usePid, UInt8 pid);

	// Returns true if the head transfer completed (successfully or not) and
	// its completion has been delivered; false if it is still pending.
	bool			serviceTransfer(DWC2UIMEndpoint *ep, bool *progressed);
	void			processQueues(void);
	void			scheduleNext(DWC2UIMEndpoint *ep, uint64_t now, bool progressed);
	void			armEngine(uint64_t deadline);
	void			rearmEngine(void);
	UInt64			sampleFrameNumber(void);
	static void		pollTimerFired(OSObject *owner, IOTimerEventSource *sender);

	IOReturn		queueControlStage(short functionNumber, short endpointNumber,
			    IOUSBCompletion completion, IOMemoryDescriptor *memDesc,
			    const void *rawSetup, UInt32 bufferSize, short direction);
	IOReturn		queueDataTransfer(short functionNumber, short endpointNumber,
			    UInt8 transferType, IOUSBCompletion completion,
			    IOMemoryDescriptor *memDesc, UInt32 bufferSize, short direction);
};

#endif /* _BCM2837_USB_HOST_CONTROLLER_H */
