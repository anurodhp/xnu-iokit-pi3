/* Real Apple libsyscall/wrappers/unix03/{mmap,munmap}.c (the real public
 * mmap(2)/munmap(2) wrappers around the private __mmap/__munmap raw
 * syscalls this project's build_simple_cmds.sh already generates) both
 * need mach/mach_init.h's mach_task_self() plus a private
 * "stack_logging_internal.h" header and its __syscall_logger global --
 * neither exists anywhere in this checkout (confirmed via find/grep
 * across all of third_party/Libc, not guessed) -- that optional VM
 * allocation stack-logging hook lives in a separate, not-pulled Apple
 * project. Real, documented mmap(2)/munmap(2) semantics preserved
 * exactly (MAP_PRIVATE/MAP_SHARED validation, len!=0 check, the real
 * MAP_UNIX03 flag the kernel's own mmap syscall handler expects for the
 * stricter conformance checks -- both copied directly from Apple's real
 * wrapper source, not reinvented), just without the stack-logging call
 * that source needs infrastructure this checkout doesn't have for --
 * per CLAUDE.md's source-priority rule (Apple real source first,
 * custom only as last resort): Apple's real file is what's actually
 * unbuildable here, not too complex to bother with. */
#include <sys/mman.h>
#include <errno.h>

extern void *__mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off);
extern int __munmap(void *addr, size_t len);
extern void cerror_nocancel(int);

void *mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off) {
    if (((flags & MAP_PRIVATE) != MAP_PRIVATE && (flags & MAP_SHARED) != MAP_SHARED) ||
        len == 0) {
        cerror_nocancel(EINVAL);
        return MAP_FAILED;
    }
    return __mmap(addr, len, prot, flags | MAP_UNIX03, fildes, off);
}

int munmap(void *addr, size_t len) {
    return __munmap(addr, len);
}
