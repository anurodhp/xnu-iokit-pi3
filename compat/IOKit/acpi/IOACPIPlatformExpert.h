/*
 * <IOKit/acpi/IOACPIPlatformExpert.h> -- minimal ARM/device-tree stand-in.
 *
 * WHY THIS FILE EXISTS
 *
 * IOGraphics-585's IOFramebuffer.cpp `#include`s this header
 * unconditionally (IOGraphicsFamily/IOFramebuffer.cpp:42, inside a
 * `#pragma clang diagnostic` push/pop that silences
 * -Winconsistent-missing-override for it). IOACPIFamily is not open
 * source -- Apple ships only its headers in the Kernel framework -- so,
 * exactly as for the sibling IOACPIPlatformDevice.h in this directory,
 * there is no real source to pin in third_party/ and no upstream
 * equivalent to fall back to under CLAUDE.md's source-priority rule.
 *
 * WHAT IOFramebuffer ACTUALLY USES IT FOR
 *
 * One function, and only on machines with an ACPI lid switch:
 * IOFramebuffer::readClamshellState() (IOFramebuffer.cpp:11805-11846)
 * looks up the ACPI device named "PNP0C0D" (the ACPI-defined lid device),
 * metaCast()s it to IOACPIPlatformDevice, and evaluates its "_LID" object
 * to learn whether the laptop lid is open. That is Intel-Mac firmware
 * plumbing: a Raspberry Pi has no ACPI namespace, no PNP0C0D device, and
 * no lid. `IOService::getMatchingServices(nameMatching("PNP0C0D"))`
 * returns an empty iterator on this board, so the whole body is
 * correctly inert -- gIOFBLastClamshellState simply keeps its
 * "lid open" default, which is the right answer for a board with no lid.
 *
 * The class IOFramebuffer names is IOACPIPlatformDevice, not
 * IOACPIPlatformExpert; only the header's NAME is what it asks for. So
 * this file deliberately declares nothing of its own and just pulls in
 * the sibling stand-in that already declares the exact surface
 * IOFramebuffer calls (evaluateInteger()), rather than adding a second,
 * differently-guessed copy of the same class. Real Apple's
 * IOACPIPlatformExpert.h likewise includes IOACPIPlatformDevice.h; the
 * IOACPIPlatformExpert class itself is referenced nowhere in
 * IOGraphics-585 (checked with grep over the whole pinned checkout, not
 * assumed -- the only ACPI identifiers in the entire family are the
 * three IOACPIPlatformDevice references in readClamshellState()).
 *
 * If a future platform here ever grows real ACPI (it will not -- this is
 * a Raspberry Pi), this file is the thing to delete, not extend.
 */

#ifndef _IOKIT_ACPI_IOACPIPLATFORMEXPERT_H_COMPAT
#define _IOKIT_ACPI_IOACPIPLATFORMEXPERT_H_COMPAT

#include <IOKit/acpi/IOACPIPlatformDevice.h>

#endif /* _IOKIT_ACPI_IOACPIPLATFORMEXPERT_H_COMPAT */
