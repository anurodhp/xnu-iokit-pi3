/* DAR-251 smoketest for libiokituser.dylib's real IOKitLib API: serialize a
 * matching dictionary (IOCFSerialize, what every IOService* call does
 * first), then get the master port and enumerate IONetworkInterface
 * services by name and BSD name. Prints one PASS/FAIL line. */
#include <stdio.h>
#include <mach/mach.h>
#include <pthread.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFSerialize.h>

/* InterfaceNamer runs its IOKit setup on its own pthread
 * (ifnamer.c:4008) and adds the notification source to that thread's
 * CFRunLoopGetCurrent(). */
static volatile int thread_done;
static void *
runloop_thread(void *arg)
{
	(void)arg;
	CFRunLoopRef rl = CFRunLoopGetCurrent();
	printf("iokitlib_smoketest: CFRunLoopGetCurrent on a created thread -> %p (main %p)\n",
	    (void *)rl, (void *)CFRunLoopGetMain());
	thread_done = rl ? 1 : 2;
	return NULL;
}

int
main(void)
{
	int fail = 0;
	CFMutableDictionaryRef m = IOServiceMatching("IONetworkInterface");
	printf("iokitlib_smoketest: IOServiceMatching -> %p\n", (void *)m);
	if (!m) {
		printf("iokitlib_smoketest: FAIL\n");
		return 1;
	}
	for (int bin = 0; bin <= 1; bin++) {
		CFDataRef d = IOCFSerialize(m, bin ? kIOCFSerializeToBinary : 0);
		printf("iokitlib_smoketest: IOCFSerialize(%s) -> %ld bytes\n",
		    bin ? "binary" : "xml", d ? (long)CFDataGetLength(d) : -1L);
		if (!d) fail = 1; else CFRelease(d);
	}

	mach_port_t master = MACH_PORT_NULL;
	kern_return_t kr = IOMasterPort(MACH_PORT_NULL, &master);
	printf("iokitlib_smoketest: IOMasterPort kr=0x%x port=0x%x\n", kr, master);
	if (kr != KERN_SUCCESS) fail = 1;

	io_iterator_t it = IO_OBJECT_NULL;
	kr = IOServiceGetMatchingServices(master, m, &it);	/* consumes m */
	printf("iokitlib_smoketest: IOServiceGetMatchingServices kr=0x%x\n", kr);
	if (kr != KERN_SUCCESS) fail = 1;
	int n = 0;
	io_object_t o;
	while (it && (o = IOIteratorNext(it))) {
		io_name_t name;
		char bsd[64] = "?";
		IORegistryEntryGetName(o, name);
		CFTypeRef b = IORegistryEntryCreateCFProperty(o, CFSTR("BSD Name"), kCFAllocatorDefault, 0);
		if (b && CFGetTypeID(b) == CFStringGetTypeID())
			CFStringGetCString(b, bsd, sizeof(bsd), kCFStringEncodingUTF8);
		if (b) CFRelease(b);
		printf("iokitlib_smoketest: interface %s (%s)\n", name, bsd);
		IOObjectRelease(o);
		n++;
	}
	if (it) IOObjectRelease(it);
	if (n == 0) fail = 1;

	/* What configd's InterfaceNamer does next (ifnamer.c:3749, 3832). */
	IONotificationPortRef np = IONotificationPortCreate(master);
	mach_port_t wake = np ? IONotificationPortGetMachPort(np) : MACH_PORT_NULL;
	mach_port_type_t type = 0;
	kern_return_t tkr = mach_port_type(mach_task_self(), wake, &type);
	printf("iokitlib_smoketest: IONotificationPortCreate -> %p wakePort=0x%x type kr=0x%x type=0x%x\n",
	    (void *)np, wake, tkr, type);
	CFMachPortRef cmp = CFMachPortCreate(NULL, NULL, NULL, NULL);
	printf("iokitlib_smoketest: CFMachPortCreate -> %p\n", (void *)cmp);
	CFRunLoopSourceRef rls = np ? IONotificationPortGetRunLoopSource(np) : NULL;
	printf("iokitlib_smoketest: IONotificationPortGetRunLoopSource -> %p\n", (void *)rls);
	if (!np || !rls) fail = 1;

	pthread_t t;
	if (pthread_create(&t, NULL, runloop_thread, NULL) != 0) fail = 1;
	while (!thread_done) { }
	if (thread_done != 1) fail = 1;
	printf("iokitlib_smoketest: %s\n", fail ? "FAIL" : "PASS");
	return fail;
}
