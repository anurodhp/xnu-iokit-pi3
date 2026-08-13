/* Proves Apple's real, unmodified libsyscall syscall-stub generator
 * (third_party/xnu-7195/libsyscall/xcodescripts/create-syscalls.pl,
 * run against custom/SYS.h + syscalls.master, output copied into
 * libsyscall_smoketest/ - see build_libsyscall_smoketest.sh) produces
 * working arm64 code on this project's toolchain, linked against real
 * Csu crt0 (same as tools/userland_staging/crt0_smoketest.c already
 * verified) instead of hand-written syscall asm like tools/init_binary/
 * init.c uses. No libc beyond cerror_stub.c's minimal errno stand-in.
 */
extern int getpid(void);
extern long write(int fd, const void *buf, unsigned long count);
extern void _exit(int code) __attribute__((noreturn));

static void putstr(const char *s) {
	unsigned long n = 0;
	while (s[n]) n++;
	write(1, s, n);
}

static void putdec(unsigned int v) {
	char buf[16];
	int i = 15;
	buf[i--] = '\0';
	if (v == 0) {
		buf[i--] = '0';
	}
	while (v != 0) {
		buf[i--] = (char)('0' + (v % 10));
		v /= 10;
	}
	putstr(&buf[i + 1]);
}

int main(int argc, char **argv) {
	(void)argc; (void)argv;
	putstr("libsyscall_smoketest: real Apple syscall stubs work, pid=");
	putdec((unsigned int)getpid());
	putstr("\n");
	return 0;
}

void exit(int code) {
	_exit(code);
}
