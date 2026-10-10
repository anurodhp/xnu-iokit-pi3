/*
 * IOAudioFamily device and engine for the Pi 3's 3.5mm jack (PWM0/PWM1,
 * DMA-fed). DARW-315. Hardware layers: BCM2837PWM (DARW-314), BCM2837DMA
 * (DARW-313).
 *
 * Data path. The HAL (or a test tool through IOAudioEngineUserClient) writes
 * 32-bit float frames; IOAudioFamily mixes them in the float mix buffer and
 * calls clipOutputSamples(). That writes the same frames twice, at the same
 * frame index:
 *   - the stream's sample buffer: signed 16-bit stereo, the format the engine
 *     advertises, and
 *   - the DMA ring: one 32-bit PWM data word per channel per frame (DAT =
 *     0..RNG, BCM2837PWMMath.h), which a looping DMA control-block chain
 *     copies into the PWM FIFO, one control block per period, each raising a
 *     completion interrupt that calls takeTimeStamp().
 * getCurrentSampleFrame() is the DMA channel's current source address.
 *
 * Floating point in the kernel. FP/NEON is enabled at EL1 (osfmk/arm64/
 * start.s:367,937 set CPACR_EL1.FPEN). A context switch saves only the
 * callee-saved d8-d15 and FPCR (osfmk/arm64/cswitch.s:87-101) and an EL1
 * exception saves only general registers (locore.s:193-365), so v0-v7/v16-v31
 * are not preserved across a switch. Every FP section here, and the float
 * mixer in the IOAudioFamily fork, therefore runs with preemption disabled
 * and is bounded by the clip size (at most 8192 floats). An interrupt handler
 * on the same CPU that itself used NEON could still clobber them; none here
 * does, and xnu's NEON asm (e.g. lz4) is not reached from interrupt context.
 *
 * pwm-sim=1 boot-arg: BCM2837PWM keeps its registers in RAM and this engine
 * replaces the DMA ring with a timer that advances the position and calls
 * takeTimeStamp() once per period, so the whole family path can be exercised
 * on QEMU, which has no PWM block (DARW-318).
 */
#ifndef BCM2837PWMAUDIO_H
#define BCM2837PWMAUDIO_H

#include <IOKit/audio/IOAudioDefines.h>
#include <IOKit/audio/IOAudioDevice.h>
#include <IOKit/audio/IOAudioEngine.h>
#include <IOKit/audio/IOAudioStream.h>
#include <IOKit/audio/IOAudioLevelControl.h>
#include <IOKit/audio/IOAudioToggleControl.h>
#include <IOKit/IOTimerEventSource.h>
#include <kern/thread_call.h>
#include <kern/thread.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

#include "BCM2837PWM.h"
#include "BCM2837DMA.h"

// Ring geometry: 4096 frames (85 ms at 48 kHz) in 8 periods of 512 frames.
#define kPWMAudioFrames          4096
#define kPWMAudioPeriods         8
#define kPWMAudioFramesPerPeriod (kPWMAudioFrames / kPWMAudioPeriods)
#define kPWMAudioChannels        2

class BCM2837PWMAudioDevice : public IOAudioDevice
{
	OSDeclareDefaultStructors(BCM2837PWMAudioDevice)

public:
	virtual bool initHardware(IOService *provider) APPLE_KEXT_OVERRIDE;
};

class BCM2837PWMAudioEngine : public IOAudioEngine
{
	OSDeclareDefaultStructors(BCM2837PWMAudioEngine)

public:
	virtual bool initHardware(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free() APPLE_KEXT_OVERRIDE;
	virtual IOReturn performAudioEngineStart() APPLE_KEXT_OVERRIDE;
	virtual IOReturn performAudioEngineStop() APPLE_KEXT_OVERRIDE;
	virtual UInt32 getCurrentSampleFrame() APPLE_KEXT_OVERRIDE;
	virtual IOReturn performFormatChange(IOAudioStream *stream, const IOAudioStreamFormat *format,
	    const IOAudioSampleRate *rate) APPLE_KEXT_OVERRIDE;
	virtual IOReturn clipOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame,
	    UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *stream) APPLE_KEXT_OVERRIDE;
	virtual IOReturn eraseOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame,
	    UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *stream) APPLE_KEXT_OVERRIDE;
	virtual bool driverDesiresHiResSampleIntervals(void) { return false; }

private:
	static IOReturn volumeChanged(OSObject *target, IOAudioControl *control, SInt32 oldValue, SInt32 newValue);
	static IOReturn muteChanged(OSObject *target, IOAudioControl *control, SInt32 oldValue, SInt32 newValue);
	static void dmaCallback(void *ref, uint32_t channel, uint32_t cs);
	static void simThread(void *p0, wait_result_t w);
	volatile uint32_t fSimThreadRunning;
	void updateGain();
	void buildGainTable();
	static IOReturn selfTestStartAction(OSObject *owner, void *, void *, void *, void *);
	static IOReturn selfTestStopAction(OSObject *owner, void *, void *, void *, void *);
	void selfTest();
	void selfTestRun(bool ok);
	static void selfTestThread(void *p0, wait_result_t w);
	bool selfTestOk;
	void fillRingMidScale(uint32_t firstFrame, uint32_t numFrames);

	BCM2837PWM              *fPWM;
	BCM2837DMA              *fDMA;
	bool                     fSim;
	uint32_t                 fRate;
	uint32_t                 fRng;
	uint32_t                 fChannel;
	bool                     fHaveChannel;

	void                    *fSampleBuffer;     // s16 stereo, kPWMAudioFrames frames
	UInt32                   fSampleBufferSize;
	IOBufferMemoryDescriptor *fRingMem;          // PWM data words, kPWMAudioFrames * 2 * 4 bytes
	uint32_t                *fRing;
	uint32_t                 fRingBus;
	IOBufferMemoryDescriptor *fCbMem;            // kPWMAudioPeriods control blocks
	BCM2837DMAControlBlock  *fCb;
	uint32_t                 fCbBus;

	volatile bool            fSimActive;
	volatile uint32_t        fSimFires;
	uint64_t                 fSimStartNs;
	AbsoluteTime             fSimStartAbs;

	uint32_t                 fGainTable[101];    // Q16, index = volume value
	volatile uint32_t        fSimGen;
	volatile uint32_t        fLastPosition;
	volatile uint32_t        fDmaErrors;
	volatile uint32_t        fGainQ16;           // 0..65536, volume * mute
	SInt32                   fVolume;
	bool                     fMute;
};

#endif
