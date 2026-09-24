/*
 * Minimal driver for the BCM2836/2837 "local_intc" ARM-local peripheral
 * block (Broadcom's QA7 datasheet; Linux: drivers/irqchip/irq-bcm2836.c,
 * DT node "local_intc"/"local-intc" here). This is the top of the
 * interrupt tree for this board - it has no interrupt-parent of its own,
 * unlike "intc" (the "armctrl-ic" global peripheral IRQ mux), which
 * cascades into this controller's "GPU IRQ" bit (see BCM2837
 * InterruptController.h's own header comment).
 *
 * SCOPE, deliberately narrow: this project's "intc" driver only needs
 * local-intc for exactly one thing - to be a real, phandle-addressable
 * IOInterruptController that IODTInterruptControllerName()-based lookup
 * (iokit/Kernel/IODeviceTreeSupport.cpp) can resolve "intc"'s
 * interrupt-parent to, so BCM2837InterruptController::start()'s
 * registerInterrupt() call against its own provider succeeds instead of
 * failing outright (confirmed empirically: 0xe00002be before this driver
 * existed). This does NOT implement the per-CPU timer/mailbox IRQ sources
 * (LOCAL_TIMER_INT_CONTROLN/LOCAL_MAILBOX_INT_CONTROLN) - those are a
 * separate, already-otherwise-working concern (IOCPUInterruptController,
 * iokit/Kernel/IOCPU.cpp, registers under the fixed well-known symbol
 * "IOPlatformInterruptController" via a different, non-DT-based
 * mechanism - this kernel already reaches idle taking scheduler-tick
 * interrupts through whatever that path actually is). Only vector 8 (the
 * GPU IRQ passthrough bit in LOCAL_IRQ_PENDINGN(0)) is wired here.
 *
 * UNVERIFIED / explicitly flagged, not guessed past: whether a real
 * ARM64 IRQ exception actually reaches this class's handleInterrupt() at
 * all. BCM2837InterruptController.h already flags the identical gap for
 * itself ("how a raw ARM64 IRQ exception actually reaches this class's
 * handleInterrupt() ... pexpert-level wiring ... not something IOKit-
 * level code here can set up on its own") - this driver inherits that
 * same open question, now one level further up the chain. Getting
 * registerInterrupt() to *succeed* (this driver's actual goal) is
 * independent of whether interrupts *fire* in practice - if
 * BCM2837SDHCI's PIO waits later time out waiting for a real interrupt
 * that never arrives, that is the next concrete, separate thing to
 * investigate (likely a pexpert/osfmk exception-vector question, not an
 * IOKit-level one), not something to solve speculatively here.
 */

#ifndef _BCM2837_LOCAL_INTC_H
#define _BCM2837_LOCAL_INTC_H

#include <IOKit/IOInterruptController.h>
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>

class BCM2837LocalIntc : public IOInterruptController
{
	OSDeclareDefaultStructors(BCM2837LocalIntc);

private:
	IOMemoryMap *fRegMap;
	volatile uint32_t *fRegBase;

	// Backlog item 2: one-shot latch + last-seen-value for the "pending0
	// had a bit set other than the GPU IRQ" case handleInterrupt() used to
	// silently ignore - see that function's own comment for the full
	// reasoning (this can't self-clear since QA7's per-core IRQ Source
	// register isn't ack'd here the way BCM2837InterruptController's
	// peripheral pending registers are; a source setting one of these bits
	// gets no chance to be serviced/cleared, so a real occurrence spins
	// forever - flagging it once is strictly better than the prior total
	// silence, without needing to implement mailbox/local-timer handling).
	bool fLoggedUnexpectedPending;
	uint32_t fLastUnexpectedPending;

	enum {
		kNumVectors = 32, // one bank, matches LOCAL_IRQ_PENDINGN(0)'s width

		kRegGPURouting     = 0x0C / sizeof(uint32_t),
		// Core0 Timers interrupt control (QA7 rev3.4 REG_TIMERCONTROL) -
		// gates whether each of the 4 ARM generic timer comparator
		// sources (CNTPSIRQ/CNTPNSIRQ/CNTHPIRQ/CNTVIRQ, bits 0-3) is
		// masked, routed as IRQ (bits 0-3 set), or routed as FIQ (bits
		// 4-7 set) - see start()'s own comment for why this is written
		// explicitly (backlog item 3).
		kRegTimerControl0  = 0x40 / sizeof(uint32_t),
		kRegIRQPending0    = 0x60 / sizeof(uint32_t), // core 0 only - see header
		// DAR-375: core 0 mailbox 0, used as its IPI (as Linux's
		// irq-bcm2836.c does). QA7: LOCAL_MAILBOX_INT_CONTROL0 (bit 0 =
		// mailbox 0 IRQ enable), core 0 mailbox 0 write-set and read /
		// write-1-to-clear. Same offsets in QEMU's model
		// (hw/intc/bcm2836_control.c: REG_MBOXCONTROL 0x50, REG_MBOX0_WR
		// 0x80, REG_MBOX0_RDCLR 0xc0).
		kRegMailboxControl0 = 0x50 / sizeof(uint32_t),
		kRegMailbox0Set0    = 0x80 / sizeof(uint32_t),
		kRegMailbox0Clr0    = 0xC0 / sizeof(uint32_t),

		kVectorMailbox0 = 4, // bit 4 of LOCAL_IRQ_PENDINGN(n) (QEMU: IRQ_MAILBOX0)
		kVectorGPUIRQ = 8, // bit 8 of LOCAL_IRQ_PENDINGN(n) - real HW bit position
	};

	// DAR-375: the one instance, and the CPU's IPI handler
	// (cpu_signal_handler, from ml_processor_register()), so BCM2837CPU
	// can raise and receive a real IPI through core 0's mailbox 0.
	static BCM2837LocalIntc *sInstance;
	static void (*sIPIHandler)(void);

public:
	/*
	 * DAR-375 IPI support. BCM2837CPU hands over its IPI handler at
	 * initCPU() (which runs before this driver starts) and raises IPIs
	 * with sendIPI(). sendIPI() is a no-op until this driver has started.
	 * start() sends one IPI once the mailbox is live, so a signal posted
	 * in the gap is delivered then.
	 */
	static void setIPIHandler(void (*handler)(void));
	static bool sendIPI(void);

	virtual IOService *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;

	virtual bool init(OSDictionary *propTable) APPLE_KEXT_OVERRIDE;
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	virtual IOInterruptAction getInterruptHandlerAddress(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn handleInterrupt(void *refCon, IOService *nub, int source) APPLE_KEXT_OVERRIDE;

	virtual bool vectorCanBeShared(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual void initVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual int  getVectorType(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual void disableVectorHard(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual void enableVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
};

#endif /* _BCM2837_LOCAL_INTC_H */
