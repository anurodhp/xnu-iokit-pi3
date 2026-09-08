#include "CDCECMEthernet.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/network/IONetworkMedium.h>

#define super IOEthernetController

OSDefineMetaClassAndStructors(CDCECMEthernet, IOEthernetController);

bool
CDCECMEthernet::init(OSDictionary *properties, CDCECMUSBTransport *transport)
{
	if (!super::init(properties)) {
		return false;
	}
	fTransport = transport;
	fInterface = NULL;
	fNetif = NULL;
	fLinkUp = false;
	fLinkTimer = NULL;
	fRxTimer = NULL;
	fRxBuffer = NULL;
	// Real Apple default (AppleUSBCDCECMControl.cpp's own
	// USBSetPacketFilter() initialization): directed + broadcast +
	// multicast, promiscuous off.
	fPacketFilter = kPacketTypeDirected | kPacketTypeBroadcast | kPacketTypeMulticast;
	bzero(fMediumTable, sizeof(fMediumTable));
	bzero(&fMACAddress, sizeof(fMACAddress));
	return true;
}

bool
CDCECMEthernet::createMediumTables(void)
{
	OSDictionary *dict = OSDictionary::withCapacity(kMediumCount);
	if (dict == NULL) {
		return false;
	}

	// See this header's note on why CDC-ECM has exactly one medium: no
	// speed/duplex is discoverable through this driver's control path.
	IONetworkMedium *medium = IONetworkMedium::medium(kIOMediumEthernetAuto, 0, 0, kMediumIndexAuto);
	bool ok = (medium != NULL) && IONetworkMedium::addMedium(dict, medium);
	if (ok) {
		fMediumTable[kMediumIndexAuto] = medium; // kept for setLinkStatus(); released in free()
		ok = publishMediumDictionary(dict);
	} else if (medium != NULL) {
		medium->release();
	}
	dict->release(); // publishMediumDictionary() retains what it needs

	return ok;
}

void
CDCECMEthernet::updateLinkStatus(void)
{
	if (fTransport == NULL) {
		return;
	}

	bool up = fLinkUp;
	if (!fTransport->pollNotification(&up)) {
		// Nothing queued this poll - the ordinary case, and (per
		// CDCECMUSBTransport::pollNotification()'s header comment) the
		// ONLY case QEMU's emulated device ever produces in CDC mode.
		// Leave fLinkUp exactly as enable() initialized it.
		return;
	}

	if (up == fLinkUp) {
		return;
	}
	fLinkUp = up;
	setLinkStatus(kIONetworkLinkValid | (fLinkUp ? kIONetworkLinkActive : 0),
	    fLinkUp ? fMediumTable[kMediumIndexAuto] : NULL);
}

void
CDCECMEthernet::linkTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
	CDCECMEthernet *self = OSDynamicCast(CDCECMEthernet, owner);
	if (self == NULL) {
		return;
	}
	self->updateLinkStatus();
	sender->setTimeoutMS(kLinkPollIntervalMS);
}

bool
CDCECMEthernet::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	if (fTransport == NULL) {
		IOLog("CDCECMEthernet: no USB transport\n");
		return false;
	}

	fTransport->getMACAddress(fMACAddress.bytes);
	IOLog("CDCECMEthernet: MAC address %02x:%02x:%02x:%02x:%02x:%02x\n",
	    fMACAddress.bytes[0], fMACAddress.bytes[1], fMACAddress.bytes[2],
	    fMACAddress.bytes[3], fMACAddress.bytes[4], fMACAddress.bytes[5]);

	// Apply the default filter now that the transport is confirmed ready -
	// see pushPacketFilter()'s header comment on why a failure here is
	// logged, not fatal.
	pushPacketFilter();

	if (!createMediumTables()) {
		IOLog("CDCECMEthernet: createMediumTables failed\n");
		return false;
	}

	IOWorkLoop *workLoop = getWorkLoop();
	if (workLoop == NULL) {
		IOLog("CDCECMEthernet: no workloop\n");
		return false;
	}
	fLinkTimer = IOTimerEventSource::timerEventSource(this, &CDCECMEthernet::linkTimerFired);
	if (fLinkTimer == NULL || workLoop->addEventSource(fLinkTimer) != kIOReturnSuccess) {
		IOLog("CDCECMEthernet: link timer setup failed\n");
		return false;
	}

	fRxBuffer = (uint8_t *)IOMalloc(kRxBufferSize);
	if (fRxBuffer == NULL) {
		IOLog("CDCECMEthernet: RX buffer allocation failed\n");
		return false;
	}
	fRxTimer = IOTimerEventSource::timerEventSource(this, &CDCECMEthernet::rxTimerFired);
	if (fRxTimer == NULL || workLoop->addEventSource(fRxTimer) != kIOReturnSuccess) {
		IOLog("CDCECMEthernet: RX timer setup failed\n");
		return false;
	}

	// Same real Apple convention SMSC9514Ethernet::start() cites - see its
	// header comment for the full IONetworkController::attachInterface()
	// rationale.
	if (!attachInterface((IONetworkInterface **)&fNetif, true)) {
		IOLog("CDCECMEthernet: attachInterface failed\n");
		return false;
	}

	registerService();
	return true;
}

void
CDCECMEthernet::stop(IOService *provider)
{
	if (fLinkTimer != NULL) {
		fLinkTimer->cancelTimeout();
		IOWorkLoop *wl = getWorkLoop();
		if (wl != NULL) {
			wl->removeEventSource(fLinkTimer);
		}
		fLinkTimer->release();
		fLinkTimer = NULL;
	}

	if (fRxTimer != NULL) {
		fRxTimer->cancelTimeout();
		IOWorkLoop *wl = getWorkLoop();
		if (wl != NULL) {
			wl->removeEventSource(fRxTimer);
		}
		fRxTimer->release();
		fRxTimer = NULL;
	}
	if (fRxBuffer != NULL) {
		IOFree(fRxBuffer, kRxBufferSize);
		fRxBuffer = NULL;
	}

	if (fNetif != NULL) {
		detachInterface(fNetif);
		fNetif = NULL;
	}
	super::stop(provider);
}

void
CDCECMEthernet::free(void)
{
	if (fLinkTimer != NULL) {
		fLinkTimer->cancelTimeout();
		fLinkTimer->release();
		fLinkTimer = NULL;
	}
	if (fRxTimer != NULL) {
		fRxTimer->cancelTimeout();
		fRxTimer->release();
		fRxTimer = NULL;
	}
	if (fRxBuffer != NULL) {
		IOFree(fRxBuffer, kRxBufferSize);
		fRxBuffer = NULL;
	}
	for (UInt32 i = 0; i < kMediumCount; i++) {
		if (fMediumTable[i] != NULL) {
			fMediumTable[i]->release();
			fMediumTable[i] = NULL;
		}
	}
	super::free();
}

IOReturn
CDCECMEthernet::getHardwareAddress(IOEthernetAddress *addrP)
{
	*addrP = fMACAddress;
	return kIOReturnSuccess;
}

IOReturn
CDCECMEthernet::setHardwareAddress(const void * /*addr*/, UInt32 /*addrBytes*/)
{
	// CDC-ECM defines no way to reprogram the device's station address -
	// unlike SMSC's MAC CSRs, there is no register this could write to.
	// Real Apple failure semantics for "unsupported", not a silent no-op.
	return kIOReturnUnsupported;
}

IOReturn
CDCECMEthernet::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
	if (group != gIONetworkFilterGroup) {
		*filters = 0;
		return kIOReturnSuccess;
	}
	*filters = kIOPacketFilterUnicast | kIOPacketFilterBroadcast |
	    kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
	return kIOReturnSuccess;
}

void
CDCECMEthernet::pushPacketFilter(void)
{
	if (fTransport == NULL) {
		return;
	}
	if (!fTransport->setPacketFilter(fPacketFilter)) {
		// Not fatal - see CDCECMUSBTransport::setPacketFilter()'s header
		// comment: QEMU's emulated device does not implement this
		// request at all (confirmed against dev-network.c), and packet
		// delivery there is unconditional at the netdev layer regardless,
		// so there is nothing more to do here than note it.
		IOLog("CDCECMEthernet: SetEthernetPacketFilter(%#x) not applied by "
		      "the device (not fatal - see driver header)\n", fPacketFilter);
	}
}

IOReturn
CDCECMEthernet::setMulticastMode(bool active)
{
	fPacketFilter = active ? (fPacketFilter | kPacketTypeAllMulticast)
	                       : (fPacketFilter & ~(uint16_t)kPacketTypeAllMulticast);
	pushPacketFilter();
	return kIOReturnSuccess;
}

IOReturn
CDCECMEthernet::setPromiscuousMode(bool active)
{
	fPacketFilter = active ? (fPacketFilter | kPacketTypePromiscuous)
	                       : (fPacketFilter & ~(uint16_t)kPacketTypePromiscuous);
	pushPacketFilter();
	return kIOReturnSuccess;
}

IOReturn
CDCECMEthernet::setMulticastList(IOEthernetAddress * /*addrs*/, UInt32 /*count*/)
{
	// setMulticastMode(true) already puts the filter in "all multicast"
	// mode - same real reasoning as SMSC9514Ethernet::setMulticastList():
	// no narrower per-address hash filter exists here to program instead.
	return kIOReturnSuccess;
}

IOReturn
CDCECMEthernet::enable(IONetworkInterface *interface)
{
	fInterface = interface;

	/*
	 * Optimistic initial state. CDC-ECM reports link state via an
	 * unsolicited NETWORK_CONNECTION notification (CDC120 6.3.1), polled
	 * for in updateLinkStatus() - but nothing requires a compliant device
	 * to have sent one yet by the time the interface is asked to come up,
	 * and QEMU's emulated device never sends one at all in CDC mode (see
	 * CDCECMUSBTransport::pollNotification()'s header comment for the
	 * exact dev-network.c citation). Treating "no notification yet" as
	 * "down" would leave en0 permanently link-down under QEMU, so the
	 * initial state is optimistic - only a REAL notification (real
	 * hardware) is ever allowed to move it from here.
	 */
	fLinkUp = true;
	setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, fMediumTable[kMediumIndexAuto]);

	if (fLinkTimer != NULL) {
		fLinkTimer->setTimeoutMS(kLinkPollIntervalMS);
	}
	if (fRxTimer != NULL) {
		fRxTimer->setTimeoutMS(kRxPollIntervalMS);
	}
	return kIOReturnSuccess;
}

IOReturn
CDCECMEthernet::disable(IONetworkInterface * /*interface*/)
{
	if (fLinkTimer != NULL) {
		fLinkTimer->cancelTimeout();
	}
	if (fRxTimer != NULL) {
		fRxTimer->cancelTimeout();
	}
	fLinkUp = false;
	fInterface = NULL;
	return kIOReturnSuccess;
}

UInt32
CDCECMEthernet::outputPacket(mbuf_t m, void * /*param*/)
{
	if (fTransport == NULL) {
		freePacket(m);
		return kIOReturnOutputDropped;
	}

	size_t frameLen = 0;
	for (mbuf_t n = m; n != NULL; n = mbuf_next(n)) {
		frameLen += mbuf_len(n);
	}
	if (frameLen == 0) {
		freePacket(m);
		return kIOReturnOutputDropped;
	}

	// No TX header/trailer to add - see this file's header comment. Just
	// gather the mbuf chain into one contiguous frame; the transport
	// handles USB-layer framing (short packet / trailing ZLP).
	uint8_t *buf = (uint8_t *)IOMalloc(frameLen);
	if (buf == NULL) {
		freePacket(m);
		return kIOReturnOutputDropped;
	}
	size_t off = 0;
	for (mbuf_t n = m; n != NULL; n = mbuf_next(n)) {
		size_t len = mbuf_len(n);
		bcopy(mbuf_data(n), buf + off, len);
		off += len;
	}

	uint32_t sent = fTransport->bulkOut(buf, (uint32_t)frameLen);
	IOFree(buf, frameLen);
	freePacket(m);

	return (sent == frameLen) ? (UInt32)kIOReturnOutputSuccess : (UInt32)kIOReturnOutputDropped;
}

void
CDCECMEthernet::inputPacketAvailable(void *frameData, uint32_t frameLen)
{
	if (fInterface == NULL) {
		return;
	}
	mbuf_t m = allocatePacket(frameLen);
	if (m == NULL) {
		return;
	}
	mbuf_copyback(m, 0, frameLen, frameData, MBUF_DONTWAIT);
	fInterface->inputPacket(m, frameLen);
}

void
CDCECMEthernet::pollReceive(void)
{
	if (fTransport == NULL || fRxBuffer == NULL || fInterface == NULL) {
		return;
	}

	// One call is exactly one frame - see this file's header comment on
	// why CDC-ECM's bulk framing needs no unpacking loop the way SMSC's
	// RX_STS-prefixed, multi-frame-per-transfer framing does. 0 back is
	// the ordinary "nothing queued this tick" case, not a failure.
	uint32_t actual = fTransport->bulkIn(fRxBuffer, kRxBufferSize);
	if (actual == 0) {
		return;
	}
	inputPacketAvailable(fRxBuffer, actual);
}

void
CDCECMEthernet::rxTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
	CDCECMEthernet *self = OSDynamicCast(CDCECMEthernet, owner);
	if (self == NULL) {
		return;
	}
	self->pollReceive();
	sender->setTimeoutMS(kRxPollIntervalMS);
}
