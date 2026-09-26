/* DAR-225 smoketest: the main thread's pthread_s must carry the real stack
 * top the kernel reported in apple[]'s main_stack= string (see
 * libsystem_pthread_main_stack_compat.c). Checked through
 * pthread_get_stackaddr_np() from libdispatch.dylib, the copy real
 * libdispatch's _dispatch_clear_stack() (third_party/libdispatch/src/
 * shims.h:270) calls. Prints PASS/FAIL on one line for boot_test.py. */
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

extern void *pthread_get_stackaddr_np(pthread_t);

int
main(int argc, char **argv, char **envp, char **apple)
{
	(void)argc; (void)argv; (void)envp;
	for (int i = 0; apple && apple[i]; i++)
		printf("pthread_main_stack_smoketest: apple[%d]=\"%s\"\n", i, apple[i]);
	int local = 0;
	uintptr_t top = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
	uintptr_t here = (uintptr_t)&local;

	printf("pthread_main_stack_smoketest: main=%d stackaddr=0x%lx frame=0x%lx\n",
	    pthread_main_np(), (unsigned long)top, (unsigned long)here);
	if (!pthread_main_np() || top == 0 || (top & 0xfff) != 0 ||
	    top <= here || top - here > (64UL << 20)) {
		printf("pthread_main_stack_smoketest: FAIL\n");
		return 1;
	}
	printf("pthread_main_stack_smoketest: PASS\n");
	return 0;
}
