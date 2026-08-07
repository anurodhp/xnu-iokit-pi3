all: rpi3.dts
	clang -E -P -nostdinc -undef -x assembler-with-cpp rpi3.dts -orpi3.p
	dtc -O dtb -o rpi3.dtb rpi3.p
