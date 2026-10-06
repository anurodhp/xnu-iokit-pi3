/*
 * Transport-independent core of the libdrm shim; see v3d_drm.h.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "v3d_drm.h"
#include "xf86drm.h"
#include "BCM2837V3DUAPI.h"

/* DRM ioctl numbers (low byte of the request), from Linux's drm.h and
 * vc4_drm.h: DRM_COMMAND_BASE is 0x40. Checked against Mesa's headers by
 * tools/v3d_validate_test/abi_check.c. */
enum {
	NR_GEM_CLOSE = 0x09,
	NR_GET_CAP = 0x0c,
	NR_VC4_SUBMIT_CL = 0x40 + 0x00,
	NR_VC4_WAIT_SEQNO = 0x40 + 0x01,
	NR_VC4_WAIT_BO = 0x40 + 0x02,
	NR_VC4_CREATE_BO = 0x40 + 0x03,
	NR_VC4_MMAP_BO = 0x40 + 0x04,
	NR_VC4_CREATE_SHADER_BO = 0x40 + 0x05,
	NR_VC4_GET_HANG_STATE = 0x40 + 0x06,
	NR_VC4_GET_PARAM = 0x40 + 0x07,
	NR_VC4_SET_TILING = 0x40 + 0x08,
	NR_VC4_GET_TILING = 0x40 + 0x09,
	NR_VC4_LABEL_BO = 0x40 + 0x0a,
	NR_VC4_GEM_MADVISE = 0x40 + 0x0b,
	NR_VC4_PERFMON_FIRST = 0x40 + 0x0c,
	NR_VC4_PERFMON_LAST = 0x40 + 0x0e,
};
#define IOC_NR(r) ((unsigned)((r) & 0xff))
#define IOC_TYPE(r) ((unsigned)(((r) >> 8) & 0xff))

/* Layouts of the ioctl structs handled in userland (Linux drm.h / vc4_drm.h). */
struct drm_gem_close_s { uint32_t handle; uint32_t pad; };
struct drm_get_cap_s { uint64_t capability; uint64_t value; };
struct drm_vc4_mmap_bo_s { uint32_t handle; uint32_t flags; uint64_t offset; };
struct drm_vc4_tiling_s { uint32_t handle; uint32_t flags; uint64_t modifier; };
struct drm_vc4_madvise_s { uint32_t handle; uint32_t madv; uint32_t retained; uint32_t pad; };
struct drm_vc4_label_s { uint32_t handle; uint32_t len; uint64_t name; };
struct drm_vc4_hang_state_s {
	uint64_t bo;
	uint32_t bo_count;
	uint32_t start_bin, start_render;
	uint32_t ct0ca, ct0ea;
	uint32_t ct1ca, ct1ea;
	uint32_t ct0cs, ct1cs;
	uint32_t ct0ra0, ct1ra0;
	uint32_t bpca, bpcs;
	uint32_t bpoa, bpos;
	uint32_t vpmbase;
	uint32_t dbge;
	uint32_t fdbgo;
	uint32_t fdbgb;
	uint32_t fdbgr;
	uint32_t fdbgs;
	uint32_t errstat;
	uint32_t pad[16];
};

#define DRM_CAP_SYNCOBJ_VALUE 0x13        /* DRM_CAP_SYNCOBJ */

/* ---- IOReturn -> errno ---- */

/* IOKit's values (iokit/IOKit/IOReturn.h: sys_iokit | sub_iokit_common | n
 * = 0xe0000000 | n), written out so this core builds without IOKit headers;
 * the host test compares this table against the SDK's IOReturn.h. */
#define IOR(n) ((int)(0xe0000000u | (n)))
int
v3d_drm_ioreturn_to_errno(int r)
{
	if (r == 0) {
		return 0;
	}
	switch (r) {
	case IOR(0x2bc): return EIO;            // kIOReturnError
	case IOR(0x2bd): return ENOMEM;         // kIOReturnNoMemory
	case IOR(0x2be): return ENOMEM;         // kIOReturnNoResources
	case IOR(0x2c0): return ENODEV;         // kIOReturnNoDevice
	case IOR(0x2c1): return EACCES;         // kIOReturnNotPrivileged
	case IOR(0x2c2): return EINVAL;         // kIOReturnBadArgument
	case IOR(0x2c5): return EBUSY;          // kIOReturnExclusiveAccess
	case IOR(0x2c7): return ENOSYS;         // kIOReturnUnsupported
	case IOR(0x2c8): return EFAULT;         // kIOReturnVMError
	case IOR(0x2d5): return EBUSY;          // kIOReturnBusy
	case IOR(0x2d6): return ETIME;          // kIOReturnTimeout
	case IOR(0x2d8): return EBUSY;          // kIOReturnNotReady
	case IOR(0x2d9): return ENXIO;          // kIOReturnNotAttached
	case IOR(0x2db): return ENOSPC;         // kIOReturnNoSpace
	case IOR(0x2e2): return EPERM;          // kIOReturnNotPermitted
	case IOR(0x2eb): return EINTR;          // kIOReturnAborted
	case IOR(0x2f0): return ENOENT;         // kIOReturnNotFound
	default: return EIO;
	}
}

/* ---- per-descriptor state ---- */

#define MAX_FDS 64
#define MAX_BOS 1025        /* handle = index + 1, as in the kext */

struct bo_state {
	uint8_t used;
	uint32_t size;
	uint64_t modifier;
};

struct mapping {
	void *addr;
	size_t len;
	uint32_t handle;
};
#define MAX_MAPS 4096

struct v3d_fd {
	int fd;
	dev_t dev;               /* identity of the descriptor's file: dup()ed copies match */
	ino_t ino;
	struct v3d_transport t;
	pthread_mutex_t lock;
	struct bo_state bo[MAX_BOS];
	struct mapping maps[MAX_MAPS];
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct v3d_fd *g_fds[MAX_FDS];

/*
 * Mesa's DRM winsys dup()s the descriptor it is given
 * (vc4_drm_winsys.c: os_dupfd_cloexec) and calls drmIoctl() on the copy, so
 * descriptors are matched by the identity of the file behind them, not by
 * number. Each v3d_drm_open() descriptor is an unlinked temporary file, which
 * has a unique (st_dev, st_ino) that every dup() shares.
 */
static struct v3d_fd *
fd_lookup(int fd)
{
	struct stat st;
	struct v3d_fd *r = NULL;
	if (fstat(fd, &st) != 0) {
		return NULL;
	}
	pthread_mutex_lock(&g_lock);
	for (int i = 0; i < MAX_FDS; i++) {
		if (g_fds[i] != NULL && g_fds[i]->dev == st.st_dev && g_fds[i]->ino == st.st_ino) {
			r = g_fds[i];
			break;
		}
	}
	pthread_mutex_unlock(&g_lock);
	return r;
}

int
v3d_drm_open_transport(const struct v3d_transport *t)
{
	char path[] = "/tmp/.v3d_drm_XXXXXX";
	int fd = mkstemp(path);
	if (fd < 0) {
		return -1;
	}
	unlink(path);
	struct stat st;
	struct v3d_fd *f = calloc(1, sizeof(*f));
	if (f == NULL || fstat(fd, &st) != 0) {
		free(f);
		close(fd);
		errno = ENOMEM;
		return -1;
	}
	f->fd = fd;
	f->dev = st.st_dev;
	f->ino = st.st_ino;
	f->t = *t;
	pthread_mutex_init(&f->lock, NULL);
	pthread_mutex_lock(&g_lock);
	for (int i = 0; i < MAX_FDS; i++) {
		if (g_fds[i] == NULL) {
			g_fds[i] = f;
			pthread_mutex_unlock(&g_lock);
			return fd;
		}
	}
	pthread_mutex_unlock(&g_lock);
	free(f);
	close(fd);
	errno = EMFILE;
	return -1;
}

int
v3d_drm_close(int fd)
{
	struct v3d_fd *f = NULL;
	pthread_mutex_lock(&g_lock);
	for (int i = 0; i < MAX_FDS; i++) {
		if (g_fds[i] != NULL && g_fds[i]->fd == fd) {      /* the original, not a dup */
			f = g_fds[i];
			g_fds[i] = NULL;
			break;
		}
	}
	pthread_mutex_unlock(&g_lock);
	if (f == NULL) {
		errno = EBADF;
		return -1;
	}
	for (int i = 0; i < MAX_MAPS; i++) {
		if (f->maps[i].addr != NULL && f->t.unmap_bo != NULL) {
			f->t.unmap_bo(f->t.ctx, f->maps[i].handle, (uint64_t)(uintptr_t)f->maps[i].addr);
		}
	}
	if (f->t.close != NULL) {
		f->t.close(f->t.ctx);
	}
	pthread_mutex_destroy(&f->lock);
	free(f);
	return close(fd);
}

/* ---- helpers ---- */

static int
fail(int ioret)
{
	errno = v3d_drm_ioreturn_to_errno(ioret);
	return -1;
}

static int
fail_errno(int e)
{
	errno = e;
	return -1;
}

/* Struct goes in and the kernel's updated copy comes back out. */
static int
call_inout(struct v3d_fd *f, uint32_t sel, void *arg, size_t size)
{
	size_t out = size;
	int r = f->t.call_struct(f->t.ctx, sel, arg, size, arg, &out);
	return r == 0 ? 0 : fail(r);
}

static int
call_in(struct v3d_fd *f, uint32_t sel, const void *arg, size_t size)
{
	int r = f->t.call_struct(f->t.ctx, sel, arg, size, NULL, NULL);
	return r == 0 ? 0 : fail(r);
}

static struct bo_state *
bo_get(struct v3d_fd *f, uint32_t handle)
{
	if (handle == 0 || handle >= MAX_BOS || !f->bo[handle].used) {
		return NULL;
	}
	return &f->bo[handle];
}

static void
bo_note_created(struct v3d_fd *f, uint32_t handle, uint32_t size)
{
	if (handle != 0 && handle < MAX_BOS) {
		pthread_mutex_lock(&f->lock);
		f->bo[handle].used = 1;
		f->bo[handle].size = size;
		f->bo[handle].modifier = 0;       /* DRM_FORMAT_MOD_LINEAR... see tiling below */
		pthread_mutex_unlock(&f->lock);
	}
}

/*
 * V3D_DRM_DEBUG=1: describe a submission the kernel rejected (to stderr),
 * since the kernel's validators do not log why. Enough to rebuild the case
 * on the host (tools/v3d_validate_test).
 */
static void
debug_surf(const char *name, const struct v3d_submit_rcl_surface *s)
{
	if (s->hindex != ~0u)
		fprintf(stderr, "  %-16s hindex %u offset %#x bits %#x flags %#x\n",
		    name, s->hindex, s->offset, s->bits, s->flags);
}

static void
debug_rejected_submit(struct v3d_fd *f, const struct v3d_submit_cl *a, int err)
{
	fprintf(stderr, "v3d_drm: SUBMIT_CL rejected (errno %d)\n", err);
	fprintf(stderr, "  bin_cl %u B, shader_rec %u B x%u, uniforms %u B, %u BOs\n",
	    a->bin_cl_size, a->shader_rec_size, a->shader_rec_count, a->uniforms_size, a->bo_handle_count);
	fprintf(stderr, "  %ux%u tiles x %u..%u y %u..%u flags %#x clear %#x/%#x z %#x s %u\n",
	    a->width, a->height, a->min_x_tile, a->max_x_tile, a->min_y_tile, a->max_y_tile,
	    a->flags, a->clear_color[0], a->clear_color[1], a->clear_z, a->clear_s);
	debug_surf("color_read", &a->color_read);
	debug_surf("color_write", &a->color_write);
	debug_surf("zs_read", &a->zs_read);
	debug_surf("zs_write", &a->zs_write);
	debug_surf("msaa_color_write", &a->msaa_color_write);
	debug_surf("msaa_zs_write", &a->msaa_zs_write);
	const uint32_t *h = (const uint32_t *)(uintptr_t)a->bo_handles;
	for (uint32_t i = 0; h != NULL && i < a->bo_handle_count && i < 32; i++) {
		pthread_mutex_lock(&f->lock);
		struct bo_state *b = bo_get(f, h[i]);
		uint32_t size = b ? b->size : 0;
		pthread_mutex_unlock(&f->lock);
		fprintf(stderr, "  bo[%u] handle %u size %u\n", i, h[i], size);
	}
	const uint8_t *cl = (const uint8_t *)(uintptr_t)a->bin_cl;
	if (cl != NULL && a->bin_cl_size != 0) {
		fprintf(stderr, "  bin_cl head:");
		for (uint32_t i = 0; i < a->bin_cl_size && i < 64; i++)
			fprintf(stderr, " %02x", cl[i]);
		fprintf(stderr, "\n");
	}
}

/* ---- drmIoctl ---- */

int
drmIoctl(int fd, unsigned long request, void *arg)
{
	struct v3d_fd *f = fd_lookup(fd);
	if (f == NULL) {
		return fail_errno(EBADF);
	}
	if (IOC_TYPE(request) != 'd') {
		return fail_errno(ENOTTY);
	}
	unsigned nr = IOC_NR(request);
	if (arg == NULL && nr != NR_VC4_PERFMON_FIRST) {
		return fail_errno(EFAULT);
	}

	switch (nr) {
	case NR_VC4_SUBMIT_CL: {
		int r = call_inout(f, kV3DMethodSubmitCL, arg, sizeof(struct v3d_submit_cl));
		if (r != 0 && getenv("V3D_DRM_DEBUG") != NULL)
			debug_rejected_submit(f, arg, errno);
		return r;
	}
	case NR_VC4_WAIT_SEQNO:
		return call_in(f, kV3DMethodWaitSeqno, arg, sizeof(struct v3d_wait_seqno));
	case NR_VC4_WAIT_BO:
		return call_in(f, kV3DMethodWaitBO, arg, sizeof(struct v3d_wait_bo));
	case NR_VC4_CREATE_BO: {
		int r = call_inout(f, kV3DMethodCreateBO, arg, sizeof(struct v3d_create_bo));
		if (r == 0) {
			struct v3d_create_bo *c = arg;
			bo_note_created(f, c->handle, c->size);
		}
		return r;
	}
	case NR_VC4_CREATE_SHADER_BO: {
		int r = call_inout(f, kV3DMethodCreateShaderBO, arg, sizeof(struct v3d_create_shader_bo));
		if (r == 0) {
			struct v3d_create_shader_bo *c = arg;
			bo_note_created(f, c->handle, c->size);
		}
		return r;
	}
	case NR_VC4_MMAP_BO: {
		struct drm_vc4_mmap_bo_s *m = arg;
		pthread_mutex_lock(&f->lock);
		struct bo_state *b = bo_get(f, m->handle);
		pthread_mutex_unlock(&f->lock);
		if (b == NULL) {
			return fail_errno(ENOENT);
		}
		/* The token is just the handle, page-aligned so that code which
		 * checks "offset & (pagesize-1)" stays happy. */
		m->offset = (uint64_t)m->handle << 12;
		return 0;
	}
	case NR_VC4_GET_PARAM:
		return call_inout(f, kV3DMethodGetParam, arg, sizeof(struct v3d_get_param));
	case NR_VC4_GET_HANG_STATE: {
		struct drm_vc4_hang_state_s *h = arg;
		struct v3d_hang_state k;
		size_t out = sizeof(k);
		memset(&k, 0, sizeof(k));
		int r = f->t.call_struct(f->t.ctx, kV3DMethodGetHangState, NULL, 0, &k, &out);
		if (r != 0) {
			return fail(r);
		}
		if (!k.valid) {
			return fail_errno(ENOENT);
		}
		memset(h, 0, sizeof(*h));
		h->bo_count = 0;                    /* the kext does not keep BO snapshots */
		h->start_bin = k.start_bin; h->start_render = k.start_render;
		h->ct0ca = k.ct0ca; h->ct0ea = k.ct0ea; h->ct1ca = k.ct1ca; h->ct1ea = k.ct1ea;
		h->ct0cs = k.ct0cs; h->ct1cs = k.ct1cs; h->ct0ra0 = k.ct0ra0; h->ct1ra0 = k.ct1ra0;
		h->bpca = k.bpca; h->bpcs = k.bpcs; h->bpoa = k.bpoa; h->bpos = k.bpos;
		h->vpmbase = k.vpmbase; h->dbge = k.dbge; h->fdbgo = k.fdbgo; h->fdbgb = k.fdbgb;
		h->fdbgr = k.fdbgr; h->fdbgs = k.fdbgs; h->errstat = k.errstat;
		return 0;
	}
	case NR_VC4_SET_TILING:
	case NR_VC4_GET_TILING: {
		/* Tiling modifiers are bookkeeping for buffer sharing; nothing in
		 * the hardware path depends on them, so they live here. */
		struct drm_vc4_tiling_s *t = arg;
		int ret = 0;
		pthread_mutex_lock(&f->lock);
		struct bo_state *b = bo_get(f, t->handle);
		if (b == NULL) {
			ret = fail_errno(ENOENT);
		} else if (nr == NR_VC4_SET_TILING) {
			b->modifier = t->modifier;
		} else {
			t->modifier = b->modifier;
		}
		pthread_mutex_unlock(&f->lock);
		return ret;
	}
	case NR_VC4_LABEL_BO:
		return 0;
	case NR_VC4_GEM_MADVISE: {
		struct drm_vc4_madvise_s *m = arg;
		m->retained = 1;                    /* nothing is ever purged */
		return 0;
	}
	case NR_GEM_CLOSE: {
		struct drm_gem_close_s *c = arg;
		uint64_t in = c->handle;
		pthread_mutex_lock(&f->lock);
		struct bo_state *b = bo_get(f, c->handle);
		if (b != NULL) {
			memset(b, 0, sizeof(*b));
		}
		pthread_mutex_unlock(&f->lock);
		if (b == NULL) {
			return fail_errno(ENOENT);
		}
		int r = f->t.call_scalar(f->t.ctx, kV3DMethodCloseBO, &in, 1, NULL, NULL);
		return r == 0 ? 0 : fail(r);
	}
	case NR_GET_CAP: {
		struct drm_get_cap_s *c = arg;
		c->value = 0;
		return 0;
	}
	case NR_VC4_PERFMON_FIRST:
	case NR_VC4_PERFMON_FIRST + 1:
	case NR_VC4_PERFMON_LAST:
		return fail_errno(ENOSYS);
	default:
		return fail_errno(ENOTTY);
	}
}

int
drmGetCap(int fd, uint64_t capability, uint64_t *value)
{
	struct drm_get_cap_s c = { capability, 0 };
	/* 0x0c with type 'd': same request value Linux's DRM_IOCTL_GET_CAP has
	 * once the size/direction bits are added; the core only looks at type
	 * and number. */
	int r = drmIoctl(fd, ('d' << 8) | NR_GET_CAP, &c);
	if (r == 0) {
		*value = c.value;
	}
	return r;
}

/* ---- mappings ---- */

void *
v3d_drm_mmap(int fd, size_t length, uint64_t offset)
{
	struct v3d_fd *f = fd_lookup(fd);
	uint32_t handle = (uint32_t)(offset >> 12);
	if (f == NULL) {
		errno = EBADF;
		return (void *)-1;
	}
	pthread_mutex_lock(&f->lock);
	struct bo_state *b = bo_get(f, handle);
	uint32_t size = b ? b->size : 0;
	pthread_mutex_unlock(&f->lock);
	if (b == NULL || (offset & 0xfff) != 0 || length == 0 || length > size) {
		errno = b == NULL ? ENOENT : EINVAL;
		return (void *)-1;
	}
	uint64_t addr = 0, mapped = 0;
	int r = f->t.map_bo(f->t.ctx, handle, &addr, &mapped);
	if (r != 0) {
		errno = v3d_drm_ioreturn_to_errno(r);
		return (void *)-1;
	}
	pthread_mutex_lock(&f->lock);
	for (int i = 0; i < MAX_MAPS; i++) {
		if (f->maps[i].addr == NULL) {
			f->maps[i].addr = (void *)(uintptr_t)addr;
			f->maps[i].len = length;
			f->maps[i].handle = handle;
			pthread_mutex_unlock(&f->lock);
			return (void *)(uintptr_t)addr;
		}
	}
	pthread_mutex_unlock(&f->lock);
	f->t.unmap_bo(f->t.ctx, handle, addr);
	errno = ENOMEM;
	return (void *)-1;
}

int
v3d_drm_munmap(int fd, void *addr, size_t length)
{
	struct v3d_fd *f = fd_lookup(fd);
	(void)length;
	if (f == NULL) {
		errno = EBADF;
		return -1;
	}
	pthread_mutex_lock(&f->lock);
	for (int i = 0; i < MAX_MAPS; i++) {
		if (f->maps[i].addr == addr) {
			uint32_t h = f->maps[i].handle;
			f->maps[i].addr = NULL;
			pthread_mutex_unlock(&f->lock);
			int r = f->t.unmap_bo(f->t.ctx, h, (uint64_t)(uintptr_t)addr);
			return r == 0 ? 0 : fail(r);
		}
	}
	pthread_mutex_unlock(&f->lock);
	errno = EINVAL;
	return -1;
}

/* ---- libdrm entry points Mesa references but this port cannot provide ---- */

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle) { (void)fd; (void)flags; (void)handle; errno = ENOSYS; return -1; }
int drmSyncobjDestroy(int fd, uint32_t handle) { (void)fd; (void)handle; errno = ENOSYS; return -1; }
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd) { (void)fd; (void)handle; (void)sync_file_fd; errno = ENOSYS; return -1; }
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd) { (void)fd; (void)handle; (void)sync_file_fd; errno = ENOSYS; return -1; }
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle) { (void)fd; (void)prime_fd; (void)handle; errno = ENOSYS; return -1; }
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd) { (void)fd; (void)handle; (void)flags; (void)prime_fd; errno = ENOSYS; return -1; }

/* libsync.h */
int sync_wait(int fd, int timeout) { (void)fd; (void)timeout; errno = ENOSYS; return -1; }
int sync_accumulate(const char *name, int *fd1, int fd2) { (void)name; (void)fd1; (void)fd2; errno = ENOSYS; return -1; }

drmVersionPtr
drmGetVersion(int fd)
{
	if (fd_lookup(fd) == NULL) {
		return NULL;
	}
	drmVersionPtr v = calloc(1, sizeof(*v));
	if (v != NULL) {
		v->version_major = 0;
		v->version_minor = 1;
		v->name_len = 3;
		v->name = strdup("vc4");
		v->desc_len = 0;
		v->desc = strdup("");
	}
	return v;
}

void
drmFreeVersion(drmVersionPtr v)
{
	if (v != NULL) {
		free(v->name);
		free(v->desc);
		free(v->date);
		free(v);
	}
}

/* ---- mmap()/munmap() redirection (include/v3d_mmap_hook.h) ---- */

void *
v3d_drm_mmap_hook(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
	if (fd >= 0 && fd_lookup(fd) != NULL) {
		return v3d_drm_mmap(fd, len, (uint64_t)offset);
	}
	return mmap(addr, len, prot, flags, fd, offset);      /* the real one: this file is built without the hook */
}

int
v3d_drm_munmap_hook(void *addr, size_t len)
{
	pthread_mutex_lock(&g_lock);
	for (int i = 0; i < MAX_FDS; i++) {
		struct v3d_fd *f = g_fds[i];
		if (f == NULL) {
			continue;
		}
		for (int k = 0; k < MAX_MAPS; k++) {
			if (f->maps[k].addr == addr) {
				int fd = f->fd;
				pthread_mutex_unlock(&g_lock);
				return v3d_drm_munmap(fd, addr, len);
			}
		}
	}
	pthread_mutex_unlock(&g_lock);
	return munmap(addr, len);
}
