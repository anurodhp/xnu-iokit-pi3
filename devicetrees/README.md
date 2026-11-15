Raspberry Pi 3 Device Tree for XNU

Apple uses a different flattened device tree format than Linux. Therefore,
you need to compile this device tree with a special dtc that emits Apple's
ADT format instead of standard FDT — see darwin-on-arm/dtc-AppleDeviceTree.

Extended from FlorentRevest/xnu's devicetrees/rpi3.dts with an `sdhci` node
(Arasan SD/MMC controller) and the BCM2836/2837 interrupt controllers
(`local-intc`, `intc`). Register offsets sourced from upstream Linux
(arch/arm/boot/dts/broadcom/bcm283x.dtsi, bcm2837.dtsi) — see comments in
rpi3.dts for details and open questions (interrupt cell encoding is
unverified; GPIO pinmux for the SD slot vs onboard WiFi is not yet handled
anywhere in this tree, that still needs a GPIO alt-function node/driver).

Trees: `rpi3.dts` (QEMU), `rpi3-realhw.dts` (real hardware, includes
`rpi3.dts`), plus the `rpi3-v3dtest`/`rpi3-pwmtest` QEMU test variants. The
real-hardware tree serves the Pi 3 Model B and B+ alike: they share the SoC,
and the drivers identify the boards' different Wi-Fi, Bluetooth and Ethernet
chips themselves. `tools/make_realhw_sd_image.sh` writes the image
(`BOARD=rpi3bplus` only names it for the B+).
