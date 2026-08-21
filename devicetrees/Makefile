# Builds both device trees.
#
# CRITICAL: `dtc` here MUST be third_party/dtc-AppleDeviceTree's patched
# build, NOT a stock/homebrew dtc. XNU does not read flattened device tree
# (FDT) blobs at all - it reads Apple's own DeviceTree format, and the
# patched dtc's -O dtb emits that (see its AppleDeviceTree.c) rather than
# FDT. The two are trivially told apart by their first bytes: FDT starts
# with the magic d0 0d fe ed, an Apple DeviceTree starts with a node's
# property/child counts followed immediately by a "name" property.
#
# This is not hypothetical. This Makefile used to say a bare `dtc`, which
# silently picked up /opt/homebrew/bin/dtc and produced a perfectly valid
# FDT blob that the kernel cannot parse. The failure mode is nasty: the
# guest dies inside early device tree parsing, *before* console init, so
# the boot log contains only qemu-zhuowei's own loader chatter ("xnu",
# "DEBUG mmap ok ...") and then nothing at all -- indistinguishable at a
# glance from "the kernel hung". Hence the verify step below, which fails
# the build rather than shipping an unbootable blob.
#
# rpi3-realhw.dts is not a copy of rpi3.dts - it #includes it and
# overrides one property (see its header), so the C preprocessor pass is
# load-bearing. `-I.` lets that #include resolve when make is invoked from
# outside this directory.

DTC := ../third_party/dtc-AppleDeviceTree/dtc
DTS := rpi3 rpi3-realhw
DTBS := $(addsuffix .dtb,$(DTS))

all: $(DTBS)

%.dtb: %.dts rpi3.dts
	@test -x $(DTC) || { \
		echo "error: $(DTC) not found - run ./setup_third_party.sh" >&2; \
		exit 1; }
	clang -E -P -nostdinc -undef -I. -x assembler-with-cpp $< -o $*.p
	$(DTC) -O dtb -o $@ $*.p
	@# Reject an FDT blob (magic d00dfeed) outright - see header above.
	@if [ "$$(od -An -tx1 -N4 $@ | tr -d ' \n')" = "d00dfeed" ]; then \
		echo "error: $@ is an FDT blob, not an Apple DeviceTree." >&2; \
		echo "       $(DTC) is not the patched dtc-AppleDeviceTree build." >&2; \
		rm -f $@; exit 1; \
	fi

clean:
	rm -f $(DTBS) $(addsuffix .p,$(DTS))

.PHONY: all clean
