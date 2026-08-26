/* Real Apple's OS_OBJECT_OBJC_CLASS_DECL() macro
 * (third_party/libdispatch/os/object_private.h) declares an extern
 * reference to a real Objective-C compiler-synthesized class symbol
 * (raw linker name "_OBJC_CLASS_$_<name>"). third_party/PureDarwin's
 * pd_xpc_object.c uses two of these --
 * OS_OBJECT_OBJC_CLASS_DECL(xpc_connection) in pd_xpc_object.c itself,
 * OS_OBJECT_OBJC_CLASS_DECL(xpc_object) in xpc_error.c/xpc_type.c -- but
 * only ever as an `isa` sentinel POINTER, compared by address
 * (`hdr->isa == &OS_xpc_connection_class`) to tell a connection object
 * apart from a generic xpc object during teardown. Confirmed via grep
 * across all of third_party/PureDarwin: neither symbol is ever
 * dereferenced as a real Objective-C class (no `objc_msgSend` against
 * it, no ObjC runtime exists on this target at all) -- so a minimal,
 * distinct, real data symbol under each exact raw linker name is
 * sufficient and correct, not a functional stand-in for anything.
 */
const char OS_xpc_connection_class_placeholder __asm__("_OBJC_CLASS_$_OS_xpc_connection") = 0;
const char OS_xpc_object_class_placeholder __asm__("_OBJC_CLASS_$_OS_xpc_object") = 0;
