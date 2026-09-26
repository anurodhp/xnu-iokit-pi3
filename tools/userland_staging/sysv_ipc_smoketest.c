/* sysv_ipc_smoketest.c (DAR-407): proves SysV shm/sem/msg work end to end
 * on this kernel, through libsystem_kernel.dylib's real syscall wrappers.
 *
 * Before DAR-407, config/MASTER.arm64.bcm2837's BSD_BASE had no
 * sysv_shm/sysv_sem/sysv_msg, so syscalls 251-265 were nosys() and the
 * first call raised SIGSYS (bsd/kern/subr_xxx.c:183-188). The shm section
 * replays cairo's MIT-SHM probe exactly (third_party/cairo/src/
 * cairo-xlib-surface-shm.c:373-406: shmget(IPC_PRIVATE, 0x1000,
 * IPC_CREAT|0600), shmat(id, NULL, 0), shmctl(IPC_RMID) BEFORE shmdt),
 * then checks that a fork()ed child's write through the attachment is
 * visible to the parent (the mapping is really shared, not copied).
 * semctl() is variadic, so it exercises Apple's own
 * libsyscall/wrappers/varargs_wrappers.s shim, not a plain stub.
 *
 * Prints "SYSV_IPC_SMOKETEST: PASS" and exits 0, or names the first
 * failing call with errno and exits 1. A SIGSYS kill means the running
 * kernel still lacks the option.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/wait.h>

#define FAIL(what) do { \
	printf("SYSV_IPC_SMOKETEST: FAIL %s errno=%d\n", (what), errno); \
	return 1; \
} while (0)

static int
test_shm(void)
{
	int id = shmget(IPC_PRIVATE, 0x1000, IPC_CREAT | 0600);
	if (id == -1) FAIL("shmget");
	char *p = shmat(id, NULL, 0);
	if (p == (char *)-1) FAIL("shmat");

	strcpy(p, "parent");
	pid_t pid = fork();
	if (pid < 0) FAIL("fork");
	if (pid == 0) {
		/* Child inherits the attachment (kern_fork.c shmfork). */
		if (strcmp(p, "parent") != 0) exit(2);
		strcpy(p, "child-wrote-this");
		exit(0);
	}
	int st = 0;
	if (waitpid(pid, &st, 0) != pid) FAIL("waitpid");
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
		printf("SYSV_IPC_SMOKETEST: FAIL child status=0x%x\n", st);
		return 1;
	}
	if (strcmp(p, "child-wrote-this") != 0) {
		printf("SYSV_IPC_SMOKETEST: FAIL shm not shared, parent sees \"%s\"\n", p);
		return 1;
	}

	struct shmid_ds ds;
	if (shmctl(id, IPC_STAT, &ds) == -1) FAIL("shmctl(IPC_STAT)");
	if (ds.shm_segsz != 0x1000 || ds.shm_nattch != 1) {
		printf("SYSV_IPC_SMOKETEST: FAIL IPC_STAT segsz=%lu nattch=%lu\n",
		    (unsigned long)ds.shm_segsz, (unsigned long)ds.shm_nattch);
		return 1;
	}
	/* cairo's order: remove first, then detach. */
	if (shmctl(id, IPC_RMID, NULL) == -1) FAIL("shmctl(IPC_RMID)");
	if (shmdt(p) == -1) FAIL("shmdt");
	if (shmat(id, NULL, 0) != (void *)-1 || errno != EINVAL) {
		printf("SYSV_IPC_SMOKETEST: FAIL shmat after RMID+shmdt did not EINVAL (errno=%d)\n", errno);
		return 1;
	}
	printf("SYSV_IPC_SMOKETEST: shm ok (id=%d, fork-shared, IPC_STAT, RMID)\n", id);
	return 0;
}

static int
test_sem(void)
{
	int id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
	if (id == -1) FAIL("semget");
	union semun arg;
	arg.val = 3;
	if (semctl(id, 0, SETVAL, arg) == -1) FAIL("semctl(SETVAL)");
	struct sembuf op = { 0, -2, 0 };
	if (semop(id, &op, 1) == -1) FAIL("semop");
	int v = semctl(id, 0, GETVAL);
	if (v != 1) {
		printf("SYSV_IPC_SMOKETEST: FAIL semctl(GETVAL)=%d, want 1 (errno=%d)\n", v, errno);
		return 1;
	}
	if (semctl(id, 0, IPC_RMID) == -1) FAIL("semctl(IPC_RMID)");
	printf("SYSV_IPC_SMOKETEST: sem ok (id=%d, SETVAL 3, semop -2, GETVAL 1)\n", id);
	return 0;
}

struct smoke_msg {
	long mtype;
	char mtext[32];
};

static int
test_msg(void)
{
	int id = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
	if (id == -1) FAIL("msgget");
	struct smoke_msg m = { 7, "hello-msgq" };
	if (msgsnd(id, &m, sizeof(m.mtext), 0) == -1) FAIL("msgsnd");
	struct smoke_msg r;
	memset(&r, 0, sizeof(r));
	ssize_t n = msgrcv(id, &r, sizeof(r.mtext), 7, 0);
	if (n != (ssize_t)sizeof(m.mtext) || r.mtype != 7 || strcmp(r.mtext, "hello-msgq") != 0) {
		printf("SYSV_IPC_SMOKETEST: FAIL msgrcv n=%ld type=%ld text=\"%s\" errno=%d\n",
		    (long)n, r.mtype, r.mtext, errno);
		return 1;
	}
	if (msgctl(id, IPC_RMID, NULL) == -1) FAIL("msgctl(IPC_RMID)");
	printf("SYSV_IPC_SMOKETEST: msg ok (id=%d, send/recv type 7)\n", id);
	return 0;
}

/* DAR-407 regression: shm_committed must return to its starting value
 * after every create+remove. With the kernel on 4K pages under 16K user
 * maps, upstream's shm_deallocate_segment() over-uncommitted and the
 * SECOND small shmget() failed ENOMEM (see bsd/kern/sysv_shm.c's DAR-407
 * comments in the fork). Cycle mixed sizes, including cairo's 0x1000. */
static int
test_shm_accounting(void)
{
	static const size_t sizes[] = { 0x1000, 0x2000, 0x4000, 0x10000, 0x1000, 0x3000 };
	for (int round = 0; round < 8; round++) {
		for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
			int id = shmget(IPC_PRIVATE, sizes[i], IPC_CREAT | 0600);
			if (id == -1) {
				printf("SYSV_IPC_SMOKETEST: FAIL shmget(0x%zx) round %d errno=%d\n",
				    sizes[i], round, errno);
				return 1;
			}
			if (shmctl(id, IPC_RMID, NULL) == -1) FAIL("shmctl(IPC_RMID) in accounting loop");
		}
	}
	printf("SYSV_IPC_SMOKETEST: shm accounting ok (48 create/remove cycles)\n");
	return 0;
}

/* The legacy multiplexers: Apple's varargs_wrappers.s shims (semsys/msgsys/
 * shmsys) over syscalls 251-253. `which` indexes semcalls[]/msgcalls[]/
 * shmcalls[] (bsd/kern/sysv_sem.c:124-127, sysv_msg.c:93-96,
 * sysv_shm.c:127-131): 1 = semget, 1 = msgget, 3 = shmget. xnu-7195's
 * _msgsys lacked its ARM64_STACK_EPILOG and fell through into _semsys
 * (fixed in the fork, matching upstream) -- that would hand back a
 * semaphore id here, which the msgsnd/msgrcv round-trip then rejects. */
static int
test_multiplexers(void)
{
	int sid = semsys(1, IPC_PRIVATE, 1, IPC_CREAT | 0600);
	if (sid == -1) FAIL("semsys(semget)");
	if (semctl(sid, 0, GETVAL) != 0) FAIL("semctl(GETVAL) on semsys id");
	if (semctl(sid, 0, IPC_RMID) == -1) FAIL("semctl(IPC_RMID) on semsys id");

	int qid = msgsys(1, IPC_PRIVATE, IPC_CREAT | 0600);
	if (qid == -1) FAIL("msgsys(msgget)");
	struct smoke_msg m = { 9, "via-msgsys" }, r;
	memset(&r, 0, sizeof(r));
	if (msgsnd(qid, &m, sizeof(m.mtext), IPC_NOWAIT) == -1) FAIL("msgsnd on msgsys id");
	if (msgrcv(qid, &r, sizeof(r.mtext), 9, IPC_NOWAIT) != (ssize_t)sizeof(r.mtext) ||
	    strcmp(r.mtext, "via-msgsys") != 0) FAIL("msgrcv on msgsys id");
	if (msgctl(qid, IPC_RMID, NULL) == -1) FAIL("msgctl(IPC_RMID) on msgsys id");

	int hid = shmsys(3, IPC_PRIVATE, 0x2000, IPC_CREAT | 0600);
	if (hid == -1) FAIL("shmsys(shmget)");
	struct shmid_ds ds;
	if (shmctl(hid, IPC_STAT, &ds) == -1 || ds.shm_segsz != 0x2000)
		FAIL("shmctl(IPC_STAT) on shmsys id");
	if (shmctl(hid, IPC_RMID, NULL) == -1) FAIL("shmctl(IPC_RMID) on shmsys id");
	printf("SYSV_IPC_SMOKETEST: semsys/msgsys/shmsys ok\n");
	return 0;
}

int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (test_shm() || test_shm_accounting() || test_sem() || test_msg() ||
	    test_multiplexers())
		return 1;
	printf("SYSV_IPC_SMOKETEST: PASS\n");
	return 0;
}
