#include "BCM2837PWMAudio.h"
#include "BCM2837PWMMath.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <pexpert/pexpert.h>

extern "C" {
void _disable_preemption(void);
void _enable_preemption(void);
}

#define super IOAudioDevice
OSDefineMetaClassAndStructors(BCM2837PWMAudioDevice, IOAudioDevice)

#undef super
#define super IOAudioEngine
OSDefineMetaClassAndStructors(BCM2837PWMAudioEngine, IOAudioEngine)

enum {
	kRegSourceAd = 0x0c,
	kDefaultRate = 48000,
	kDefaultRng  = kPWMTargetRange,
	kBytesPerRingFrame = kPWMAudioChannels * sizeof(uint32_t),
	kBytesPerSampleFrame = kPWMAudioChannels * sizeof(int16_t),
	kVolumeMax   = 100,
};

#undef super
#define super IOAudioDevice

bool
BCM2837PWMAudioDevice::initHardware(IOService *provider)
{
	// Same opt-in as BCM2837PWM/BCM2837DMA (DARW-316): without audio=1 or
	// pwm-sim=1 (e.g. only pwm-test=1 started the PWM block) the engine would
	// wait for a DMA service that declined to start.
	uint32_t optin = 0, sim = 0;
	if (!((PE_parse_boot_argn("audio", &optin, sizeof(optin)) && optin) ||
	    (PE_parse_boot_argn("pwm-sim", &sim, sizeof(sim)) && sim))) {
		return false;
	}
	if (!super::initHardware(provider)) {
		return false;
	}
	setDeviceName("Raspberry Pi 3.5mm Output");
	setDeviceShortName("Headphones");
	setManufacturerName("Raspberry Pi");
	setDeviceCanBeDefault(kIOAudioDeviceCanBeDefaultOutput | kIOAudioDeviceCanBeSystemOutput);

	BCM2837PWMAudioEngine *engine = new BCM2837PWMAudioEngine;
	if (engine == NULL) {
		return false;
	}
	bool ok = engine->init(NULL) && activateAudioEngine(engine) == kIOReturnSuccess;
	engine->release();
	return ok;
}

#undef super
#define super IOAudioEngine

bool
BCM2837PWMAudioEngine::initHardware(IOService *provider)
{
	if (!super::initHardware(provider)) {
		return false;
	}
	fPWM = NULL;
	fDMA = NULL;
	fSim = false;
	fHaveChannel = false;
	fRate = kDefaultRate;
	fRng = kDefaultRng;
	fSampleBuffer = NULL;
	fRingMem = NULL;
	fRing = NULL;
	fCbMem = NULL;
	fCb = NULL;
	fSimThreadRunning = 0;
	fSimGen = 0;
	fLastPosition = 0;
	fDmaErrors = 0;
	fSimActive = false;
	fSimFires = 0;
	fVolume = kVolumeMax;
	fMute = false;
	fGainQ16 = 0x10000;
	buildGainTable();

	// provider is the audio device; its provider is the PWM block.
	IOService *dev = provider;
	fPWM = OSDynamicCast(BCM2837PWM, dev ? dev->getProvider() : NULL);
	if (fPWM == NULL) {
		IOLog("BCM2837PWMAudio: provider is not BCM2837PWM\n");
		return false;
	}
	fPWM->retain();
	fSim = fPWM->isSimulated();

	if (!fSim) {
		IOService *d = IOService::waitForMatchingService(IOService::serviceMatching("BCM2837DMA"),
		    5ULL * 1000 * 1000 * 1000);
		fDMA = OSDynamicCast(BCM2837DMA, d);
		if (fDMA == NULL) {
			IOLog("BCM2837PWMAudio: no BCM2837DMA\n");
			OSSafeReleaseNULL(d);
			return false;
		}
		// waitForMatchingService returned it retained; keep that reference.
	}

	// Output ring: one 32-bit PWM word per channel per frame.
	void *ringV = NULL, *cbV = NULL;
	uint32_t cbBus = 0, ringBus = 0;
	fRingMem = BCM2837DMA::allocateBuffer(kPWMAudioFrames * kBytesPerRingFrame, 32, &ringV, &ringBus);
	fCbMem = BCM2837DMA::allocateBuffer(kPWMAudioPeriods * sizeof(BCM2837DMAControlBlock), 32, &cbV, &cbBus);
	if (fRingMem == NULL || fCbMem == NULL) {
		IOLog("BCM2837PWMAudio: DMA buffer allocation failed\n");
		return false;
	}
	fRing = (uint32_t *)ringV;
	fRingBus = ringBus;
	fCb = (BCM2837DMAControlBlock *)cbV;
	fCbBus = cbBus;
	fillRingMidScale(0, kPWMAudioFrames);

	fSampleBufferSize = kPWMAudioFrames * kBytesPerSampleFrame;
	fSampleBuffer = IOMalloc(fSampleBufferSize);
	if (fSampleBuffer == NULL) {
		return false;
	}
	bzero(fSampleBuffer, fSampleBufferSize);

	setDescription("Raspberry Pi 3.5mm Output");
	setNumSampleFramesPerBuffer(kPWMAudioFrames);
	IOAudioSampleRate r441 = { 44100, 0 }, r48 = { 48000, 0 };
	setSampleRate(&r48);
	// The PWM FIFO (16 words = 8 frames) plus a DMA burst sit between the DMA
	// read pointer and the jack.
	setOutputSampleLatency(16);

	IOAudioStream *stream = new IOAudioStream;
	if (stream == NULL || !stream->initWithAudioEngine(this, kIOAudioStreamDirectionOutput, 1, "Output")) {
		OSSafeReleaseNULL(stream);
		return false;
	}
	stream->setSampleBuffer(fSampleBuffer, fSampleBufferSize);
	IOAudioStreamFormat fmt;
	bzero(&fmt, sizeof(fmt));
	fmt.fNumChannels = kPWMAudioChannels;
	fmt.fSampleFormat = kIOAudioStreamSampleFormatLinearPCM;
	fmt.fNumericRepresentation = kIOAudioStreamNumericRepresentationSignedInt;
	fmt.fBitDepth = 16;
	fmt.fBitWidth = 16;
	fmt.fAlignment = kIOAudioStreamAlignmentHighByte;
	fmt.fByteOrder = kIOAudioStreamByteOrderLittleEndian;
	fmt.fIsMixable = 1;
	fmt.fDriverTag = 0;
	stream->addAvailableFormat(&fmt, &r441, &r441);
	stream->addAvailableFormat(&fmt, &r48, &r48);
	stream->setFormat(&fmt);
	IOReturn r = addAudioStream(stream);
	stream->release();
	if (r != kIOReturnSuccess) {
		return false;
	}

	IOAudioLevelControl *vol = IOAudioLevelControl::createVolumeControl(kVolumeMax, 0, kVolumeMax,
	    (IOFixed)(-40 * 65536), 0, kIOAudioControlChannelIDAll, kIOAudioControlChannelNameAll, 0,
	    kIOAudioControlUsageOutput);
	if (vol) {
		vol->setValueChangeHandler((IOAudioControl::IntValueChangeHandler)&BCM2837PWMAudioEngine::volumeChanged, this);
		addDefaultAudioControl(vol);
		vol->release();
	}
	IOAudioToggleControl *mute = IOAudioToggleControl::createMuteControl(false, kIOAudioControlChannelIDAll,
	    kIOAudioControlChannelNameAll, 0, kIOAudioControlUsageOutput);
	if (mute) {
		mute->setValueChangeHandler((IOAudioControl::IntValueChangeHandler)&BCM2837PWMAudioEngine::muteChanged, this);
		addDefaultAudioControl(mute);
		mute->release();
	}

	IOLog("BCM2837PWMAudio: engine ready (%s), %u frames, %u periods\n", fSim ? "simulated" : "PWM+DMA",
	    (unsigned)kPWMAudioFrames, (unsigned)kPWMAudioPeriods);
	uint32_t test = 0;
	if (PE_parse_boot_argn("audio-selftest", &test, sizeof(test)) && test) {
		selfTest();
	}
	return true;
}

void
BCM2837PWMAudioEngine::free()
{
	fSimActive = false;
	for (int i = 0; i < 1000 && fSimThreadRunning; i++) {
		IOSleep(1);
	}
	if (fHaveChannel && fDMA) {
		fDMA->freeChannel(fChannel);
		fHaveChannel = false;
	}
	if (fRingMem) {
		fRingMem->complete();
		fRingMem->release();
		fRingMem = NULL;
	}
	if (fCbMem) {
		fCbMem->complete();
		fCbMem->release();
		fCbMem = NULL;
	}
	if (fSampleBuffer) {
		IOFree(fSampleBuffer, fSampleBufferSize);
		fSampleBuffer = NULL;
	}
	OSSafeReleaseNULL(fDMA);
	OSSafeReleaseNULL(fPWM);
	super::free();
}

void
BCM2837PWMAudioEngine::fillRingMidScale(uint32_t firstFrame, uint32_t numFrames)
{
	if (fRing == NULL || firstFrame >= kPWMAudioFrames) {
		return;
	}
	if (firstFrame + numFrames > kPWMAudioFrames) {
		numFrames = kPWMAudioFrames - firstFrame;
	}
	uint32_t mid = fRng / 2;
	for (uint32_t i = 0; i < numFrames * kPWMAudioChannels; i++) {
		fRing[firstFrame * kPWMAudioChannels + i] = mid;
	}
}

// dB-linear volume: value 100 = 0 dB, value 0 = -40 dB, i.e. -0.4 dB per step
// (10^(-0.4/20) = 0.95499 = 62588 in Q16), the range the control declares. The
// table is built with integer arithmetic once (no FP outside the clip loop).
void
BCM2837PWMAudioEngine::buildGainTable()
{
	uint64_t g = 65536;
	for (int v = kVolumeMax; v >= 0; v--) {
		fGainTable[v] = (uint32_t)g;
		g = (g * 62588) >> 16;
	}
}

void
BCM2837PWMAudioEngine::updateGain()
{
	fGainQ16 = fMute ? 0 : fGainTable[fVolume < 0 ? 0 : (fVolume > kVolumeMax ? kVolumeMax : fVolume)];
}

IOReturn
BCM2837PWMAudioEngine::volumeChanged(OSObject *target, IOAudioControl *, SInt32, SInt32 newValue)
{
	BCM2837PWMAudioEngine *me = OSDynamicCast(BCM2837PWMAudioEngine, target);
	if (me == NULL || newValue < 0 || newValue > kVolumeMax) {
		return kIOReturnBadArgument;
	}
	me->fVolume = newValue;
	me->updateGain();
	return kIOReturnSuccess;
}

IOReturn
BCM2837PWMAudioEngine::muteChanged(OSObject *target, IOAudioControl *, SInt32, SInt32 newValue)
{
	BCM2837PWMAudioEngine *me = OSDynamicCast(BCM2837PWMAudioEngine, target);
	if (me == NULL) {
		return kIOReturnBadArgument;
	}
	me->fMute = newValue != 0;
	me->updateGain();
	return kIOReturnSuccess;
}

IOReturn
BCM2837PWMAudioEngine::performFormatChange(IOAudioStream *, const IOAudioStreamFormat *,
    const IOAudioSampleRate *rate)
{
	if (rate) {
		if (rate->fraction != 0 || (rate->whole != 44100 && rate->whole != 48000)) {
			return kIOReturnUnsupported;
		}
		// Applied by performAudioEngineStart(); the family stops the engine
		// around a configuration change.
		fRate = rate->whole;
	}
	return kIOReturnSuccess;
}

void
BCM2837PWMAudioEngine::dmaCallback(void *ref, uint32_t, uint32_t cs)
{
	// Only the last control block raises an interrupt, so this is once per
	// trip round the ring: the time stamp IOAudioFamily wants at each wrap.
	BCM2837PWMAudioEngine *me = (BCM2837PWMAudioEngine *)ref;
	if (cs & kDMACsError) {
		me->fDmaErrors++;           // reported at stop; the channel is not restarted
	}
	if (cs & kDMACsInt) {
		me->takeTimeStamp();
	}
}

// Simulated DMA: one time stamp per trip round the ring, from a thread that
// sleeps for the ring's duration. (A thread call and an event source on the
// device's work loop were both tried first and never fired in this build.)
void
BCM2837PWMAudioEngine::simThread(void *p0, wait_result_t)
{
	BCM2837PWMAudioEngine *me = (BCM2837PWMAudioEngine *)p0;
	const uint32_t gen = me->fSimGen;       // set before the thread starts
	const uint32_t ms = (uint32_t)((uint64_t)kPWMAudioFrames * 1000 / me->fRate);
	while (me->fSimActive && me->fSimGen == gen) {
		IOSleep(ms);
		if (!me->fSimActive || me->fSimGen != gen) {
			break;
		}
		me->fSimFires++;
		me->takeTimeStamp();
	}
	__sync_fetch_and_sub(&me->fSimThreadRunning, 1);
	me->release();
	thread_terminate(current_thread());
}

IOReturn
BCM2837PWMAudioEngine::performAudioEngineStart()
{
	uint32_t rng = 0, actual = 0;
	IOReturn r = fPWM->configure(fRate, &rng, &actual);
	if (r != kIOReturnSuccess) {
		IOLog("BCM2837PWMAudio: PWM configure(%u) failed %#x\n", fRate, r);
		return r;
	}
	fRng = rng;
	fillRingMidScale(0, kPWMAudioFrames);
	fLastPosition = 0;

	if (fSim) {
		clock_get_uptime(&fSimStartAbs);
		absolutetime_to_nanoseconds(fSimStartAbs, &fSimStartNs);
		takeTimeStamp(false);
		// A new generation: a thread from an earlier start (still asleep
		// out its period) sees the change and exits instead of doubling up.
		fSimGen++;
		fSimActive = true;
		__sync_fetch_and_add(&fSimThreadRunning, 1);
		retain();
		thread_t th = THREAD_NULL;
		if (kernel_thread_start(&BCM2837PWMAudioEngine::simThread, this, &th) != KERN_SUCCESS) {
			fSimActive = false;
			__sync_fetch_and_sub(&fSimThreadRunning, 1);
			release();
			return kIOReturnNoResources;
		}
		thread_deallocate(th);
		fPWM->startOutput();
		return kIOReturnSuccess;
	}

	// One control block per period, looping; only the last interrupts.
	const uint32_t periodBytes = kPWMAudioFramesPerPeriod * kBytesPerRingFrame;
	for (uint32_t p = 0; p < kPWMAudioPeriods; p++) {
		BCM2837DMAControlBlock *cb = &fCb[p];
		bzero(cb, sizeof(*cb));
		cb->ti = kDMATiSrcInc | kDMATiDestDreq | kDMATiWaitResp | kDMATiPerMap(kBCM2837PWMDmaPerMap);
		if (p == kPWMAudioPeriods - 1) {
			cb->ti |= kDMATiIntEn;
		}
		cb->srcBus = fRingBus + p * periodBytes;
		cb->dstBus = (uint32_t)kBCM2837PWMFifoBus;
		cb->length = periodBytes;
		cb->nextBus = fCbBus + ((p + 1) % kPWMAudioPeriods) * (uint32_t)sizeof(BCM2837DMAControlBlock);
	}
	r = fDMA->allocateChannel(&fChannel, &BCM2837PWMAudioEngine::dmaCallback, this, true);
	if (r != kIOReturnSuccess) {
		return r;
	}
	fHaveChannel = true;
	// Order contract (BCM2837PWM.h): ring primed, DMA started, then PWM on.
	r = fDMA->startChannel(fChannel, fCbBus);
	if (r != kIOReturnSuccess) {
		fDMA->freeChannel(fChannel);
		fHaveChannel = false;
		return r;
	}
	takeTimeStamp(false);
	r = fPWM->startOutput();
	if (r != kIOReturnSuccess) {
		fDMA->freeChannel(fChannel);
		fHaveChannel = false;
	}
	return r;
}

IOReturn
BCM2837PWMAudioEngine::performAudioEngineStop()
{
	if (fSim) {
		fSimActive = false;
	}
	if (fHaveChannel) {
		fDMA->freeChannel(fChannel);        // stops the DMA first
		fHaveChannel = false;
		if (fDmaErrors) {
			IOLog("BCM2837PWMAudio: %u DMA error interrupt(s) during playback\n", fDmaErrors);
			fDmaErrors = 0;
		}
	}
	fPWM->stopOutput();
	return kIOReturnSuccess;
}

UInt32
BCM2837PWMAudioEngine::getCurrentSampleFrame()
{
	if (fSim) {
		uint64_t nowNs, startNs = fSimStartNs;
		AbsoluteTime now;
		clock_get_uptime(&now);
		absolutetime_to_nanoseconds(now, &nowNs);
		return (UInt32)(((nowNs - startNs) * fRate / 1000000000ULL) % kPWMAudioFrames);
	}
	if (!fHaveChannel) {
		return 0;
	}
	// SOURCE_AD is read while the DMA may be moving between control blocks, so
	// an out-of-ring or backwards-by-a-little value is a glitch: keep the last
	// good position (the erase head must never move backwards within a loop,
	// or it erases audio that has not played yet).
	uint32_t src = fDMA->readChannelReg(fChannel, kRegSourceAd);
	if (src < fRingBus || src >= fRingBus + kPWMAudioFrames * kBytesPerRingFrame) {
		return fLastPosition;
	}
	uint32_t pos = (src - fRingBus) / kBytesPerRingFrame;
	uint32_t last = fLastPosition;
	if (pos < last && last - pos <= kPWMAudioFrames / 2) {
		return last;
	}
	fLastPosition = pos;
	return pos;
}

IOReturn
BCM2837PWMAudioEngine::clipOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame,
    UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *)
{
	if (mixBuf == NULL || sampleBuf == NULL || streamFormat == NULL ||
	    streamFormat->fNumChannels != kPWMAudioChannels || streamFormat->fBitDepth != 16 ||
	    firstSampleFrame >= kPWMAudioFrames || numSampleFrames > kPWMAudioFrames - firstSampleFrame) {
		return kIOReturnBadArgument;
	}
	const float *mix = (const float *)mixBuf + firstSampleFrame * kPWMAudioChannels;
	int16_t *out = (int16_t *)sampleBuf + firstSampleFrame * kPWMAudioChannels;
	uint32_t *ring = fRing + firstSampleFrame * kPWMAudioChannels;
	const uint32_t n = numSampleFrames * kPWMAudioChannels;
	const int32_t gain = (int32_t)fGainQ16;
	const uint32_t rng = fRng;

	// Kernel FP registers are not preserved across a context switch.
	_disable_preemption();
	for (uint32_t i = 0; i < n; i++) {
		float f = mix[i];
		if (f > 1.0f) {
			f = 1.0f;
		} else if (f < -1.0f) {
			f = -1.0f;
		}
		int32_t s = (int32_t)(f * 32767.0f);
		s = (s * gain) >> 16;
		out[i] = (int16_t)s;
		ring[i] = pwmDatFromS16((int16_t)s, rng);
	}
	_enable_preemption();
	return kIOReturnSuccess;
}

IOReturn
BCM2837PWMAudioEngine::eraseOutputSamples(const void *, void *sampleBuf, UInt32 firstSampleFrame,
    UInt32 numSampleFrames, const IOAudioStreamFormat *, IOAudioStream *)
{
	if (sampleBuf == NULL || firstSampleFrame >= kPWMAudioFrames ||
	    numSampleFrames > kPWMAudioFrames - firstSampleFrame) {
		return kIOReturnBadArgument;
	}
	bzero((int16_t *)sampleBuf + firstSampleFrame * kPWMAudioChannels,
	    numSampleFrames * kBytesPerSampleFrame);
	fillRingMidScale(firstSampleFrame, numSampleFrames);
	return kIOReturnSuccess;
}

// Boot-arg audio-selftest=1. Checks, without userland: (1) the float -> s16 ->
// PWM word conversion at known points, incl. clipping, (2) volume/mute gain,
// (3) that the engine starts, advances its time stamps, and stops. In
// simulated mode (QEMU) the time stamps come from the timer; on hardware from
// the DMA ring's interrupt, so (3) doubles as the first real DMA test.
void
BCM2837PWMAudioEngine::selfTest()
{
	bool ok = true;
	enum { kN = 8 };
	float mix[kN * 2];

	IOAudioStreamFormat fmt;
	bzero(&fmt, sizeof(fmt));
	fmt.fNumChannels = 2;
	fmt.fBitDepth = 16;

	_disable_preemption();
	const float pts[kN] = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 2.0f, -2.0f, 0.25f };
	for (int i = 0; i < kN; i++) {
		mix[i * 2] = pts[i];
		mix[i * 2 + 1] = -pts[i];
	}
	_enable_preemption();

	fRng = kDefaultRng;
	fGainQ16 = 0x10000;
	if (clipOutputSamples(mix, fSampleBuffer, 0, kN, &fmt, NULL) != kIOReturnSuccess) {
		ok = false;
	}
	const int16_t *s = (const int16_t *)fSampleBuffer;
	const int16_t want[kN] = { 0, 16383, -16383, 32767, -32767, 32767, -32767, 8191 };
	for (int i = 0; i < kN; i++) {
		if (s[i * 2] != want[i] || s[i * 2 + 1] != -want[i] ||
		    fRing[i * 2] != pwmDatFromS16(want[i], fRng) ||
		    fRing[i * 2 + 1] != pwmDatFromS16((int16_t)-want[i], fRng)) {
			IOLog("BCM2837PWMAudio: selftest frame %d: got %d/%d ring %u/%u, want %d\n", i, s[i * 2],
			    s[i * 2 + 1], fRing[i * 2], fRing[i * 2 + 1], want[i]);
			ok = false;
		}
	}
	fVolume = 50;
	updateGain();
	clipOutputSamples(mix, fSampleBuffer, 0, kN, &fmt, NULL);
	// value 50 = -20 dB = 0.1 (Q16 6554 +- table rounding)
	int expect50 = (32767 * (int)fGainQ16) >> 16;
	if (s[3 * 2] != expect50 || fGainQ16 < 6400 || fGainQ16 > 6700) {
		IOLog("BCM2837PWMAudio: selftest volume 50: got %d want %d gain %u\n", s[3 * 2], expect50, fGainQ16);
		ok = false;
	}
	fMute = true;
	updateGain();
	clipOutputSamples(mix, fSampleBuffer, 0, kN, &fmt, NULL);
	if (s[3 * 2] != 0 || fRing[3 * 2] != fRng / 2) {
		IOLog("BCM2837PWMAudio: selftest mute: got %d ring %u\n", s[3 * 2], fRing[3 * 2]);
		ok = false;
	}
	fMute = false;
	fVolume = kVolumeMax;
	updateGain();

	// The engine run must not happen inside initHardware() (it runs on the
	// device's work loop), and must not sit in a thread-call worker blocked in
	// IOSleep() (the sim time stamps are thread calls too): use a thread of
	// its own, holding a reference on the engine.
	selfTestOk = ok;
	retain();
	thread_t th = THREAD_NULL;
	if (kernel_thread_start(&BCM2837PWMAudioEngine::selfTestThread, this, &th) != KERN_SUCCESS) {
		release();
		ok = false;
	} else {
		thread_deallocate(th);
		return;
	}
	IOLog("BCM2837PWMAudio: selftest %s\n", ok ? "PASS" : "FAIL");
}

void
BCM2837PWMAudioEngine::selfTestThread(void *p0, wait_result_t)
{
	BCM2837PWMAudioEngine *me = (BCM2837PWMAudioEngine *)p0;
	IOSleep(1000);          // let the device finish start()
	me->selfTestRun(me->selfTestOk);
	me->release();
	thread_terminate(current_thread());
}

// Engine start / time stamps / stop (see selfTest()).
void
BCM2837PWMAudioEngine::selfTestRun(bool ok)
{
	// IOAudioEngine::performFlush() (driven by its erase timer) stops a running
	// engine that has no active user client, so stand in for one.
	// Engine operations belong on the work loop, as the user-client path does.
	incrementActiveUserClients();
	IOReturn r = getCommandGate() ? getCommandGate()->runAction(&BCM2837PWMAudioEngine::selfTestStartAction) : startAudioEngine();
	if (r != kIOReturnSuccess) {
		IOLog("BCM2837PWMAudio: selftest startAudioEngine %#x\n", r);
		decrementActiveUserClients();
		ok = false;
	} else {
		IOSleep(400);
		const IOAudioEngineStatus *st = getStatus();
		uint32_t loops = st ? st->fCurrentLoopCount : 0;
		decrementActiveUserClients();
		if (getCommandGate()) {
			getCommandGate()->runAction(&BCM2837PWMAudioEngine::selfTestStopAction);
		} else {
			stopAudioEngine();
		}
		// 400 ms is 4 trips round the 85 ms ring at 48 kHz.
		IOLog("BCM2837PWMAudio: selftest engine ran 400 ms, loop count %u (sim timer fired %u)\n", loops, fSimFires);
		if (loops < 3) {
			ok = false;
		}
	}
	IOLog("BCM2837PWMAudio: selftest %s\n", ok ? "PASS" : "FAIL");
}

IOReturn
BCM2837PWMAudioEngine::selfTestStartAction(OSObject *owner, void *, void *, void *, void *)
{
	BCM2837PWMAudioEngine *me = OSDynamicCast(BCM2837PWMAudioEngine, owner);
	return me ? me->startAudioEngine() : kIOReturnBadArgument;
}

IOReturn
BCM2837PWMAudioEngine::selfTestStopAction(OSObject *owner, void *, void *, void *, void *)
{
	BCM2837PWMAudioEngine *me = OSDynamicCast(BCM2837PWMAudioEngine, owner);
	return me ? me->stopAudioEngine() : kIOReturnBadArgument;
}
