/*
 * compat/iohid/math.h -- DAR-245.
 *
 * IOHIDEvent.cpp:36 includes <math.h>. Apple builds kexts against
 * Kernel.framework, which ships its own math.h; the xnu tree has none
 * (-nostdinc). The only thing the file uses from it is NAN: 113 uses,
 * all through GET_EVENT_VALUE's Double variant (IOHIDEvent.cpp:1410,
 * macro in IOHIDEventMacroDefs.h). Verified by compiling it against an
 * empty math.h. The definition is the GNUC branch of Apple's own kernel
 * math.h (Xcode 12 MacOSX.sdk Kernel.framework/Headers/math.h:56-60).
 *
 * Reached only through the per-object -I on IOHIDFamily objects, so no
 * other kernel source can pick up a <math.h>.
 */
#ifndef _COMPAT_IOHID_MATH_H
#define _COMPAT_IOHID_MATH_H
#define NAN __builtin_nanf("0x7fc00000")
#endif
