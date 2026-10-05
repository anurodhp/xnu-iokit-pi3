/*
 * V3DQueue - the two-stage job pipeline of the V3D driver as a plain C++
 * state machine with no IOKit or register access, so that it can be driven
 * by a fake GPU in the host tests (tools/v3d_validate_test/test_queue.cpp).
 *
 * The hardware has two independent control-list threads: CT0 runs the
 * binner (tile lists), CT1 the renderer. A job is binned first, then
 * rendered; while job N renders, job N+1 may already be binning. Jobs
 * complete in submission order. This follows Linux's vc4_gem.c
 * (vc4_submit_next_bin_job / vc4_submit_next_render_job /
 * vc4_move_job_to_render) and vc4_irq.c (vc4_irq_finish_bin_job /
 * vc4_irq_finish_render_job / vc4_irq_reset).
 *
 * Locking is the caller's: every method must be called with the driver's job
 * lock held (it is an interrupt-level spinlock in the driver). Nothing in
 * here allocates or frees; finished jobs are handed back through the done
 * list so the caller can release them from thread context.
 */
#ifndef _V3DQUEUE_H
#define _V3DQUEUE_H

#include <stdint.h>
#include <stddef.h>

class V3DBuffer;

struct V3DJob {
	V3DJob *next;
	uint64_t seqno;
	uint32_t ct0ca, ct0ea;     /* binner list; equal => no binning work */
	uint32_t ct1ca, ct1ea;     /* render list */
	uint32_t boCount;
	V3DBuffer **bos;           /* retained by the job; boCount entries */
};

/* What the queue needs from the hardware layer. */
struct V3DHw {
	void *ctx;
	void (*startBin)(void *ctx, uint32_t ca, uint32_t ea);      /* writes CT0CA then CT0EA */
	void (*startRender)(void *ctx, uint32_t ca, uint32_t ea);   /* writes CT1CA then CT1EA */
	void (*flushCaches)(void *ctx);
	void (*flushTextureCaches)(void *ctx);
};

class V3DQueue {
public:
	explicit V3DQueue(const V3DHw &hw) : fHw(hw), fBinHead(NULL), fBinTail(NULL),
	    fRenderHead(NULL), fRenderTail(NULL), fDoneHead(NULL), fDoneTail(NULL),
	    fNextSeqno(0), fFinishedSeqno(0) {}

	bool idle() const { return fBinHead == NULL && fRenderHead == NULL; }
	bool binIdle() const { return fBinHead == NULL; }
	uint64_t finishedSeqno() const { return fFinishedSeqno; }
	uint64_t lastSeqno() const { return fNextSeqno; }
	const V3DJob *oldestInFlight() const { return fRenderHead ? fRenderHead : fBinHead; }
	const V3DJob *binHead() const { return fBinHead; }
	const V3DJob *renderHead() const { return fRenderHead; }

	/* Assigns j->seqno and queues it; starts it if the binner is free. */
	uint64_t submit(V3DJob *j)
	{
		j->seqno = ++fNextSeqno;
		bool kick = (fBinHead == NULL);
		append(&fBinHead, &fBinTail, j);
		if (kick) {
			startNextBin();
		}
		return j->seqno;
	}

	/* FLDONE: the binner finished the job at the head of the bin list. */
	void binDone()
	{
		V3DJob *j = fBinHead;
		if (j == NULL) {
			return;
		}
		popHead(&fBinHead, &fBinTail);
		moveToRender(j);
		startNextBin();
	}

	/* FRDONE: the renderer finished the job at the head of the render list. */
	void renderDone()
	{
		completeRenderHead();
		if (fRenderHead != NULL) {
			startNextRender();
		}
	}

	/*
	 * The hardware was reset (hang). Fails the job that was on it and
	 * restarts the rest: with a job in the render stage that job was the hung
	 * one, and whatever is in the bin stage has to be run again from the
	 * start; with nothing rendering, the bin job was the hung one. If the
	 * hardware could not be brought back (`hwOk` false) everything still
	 * queued is failed so no waiter blocks forever.
	 */
	void resetDone(bool hwOk)
	{
		if (fRenderHead != NULL) {
			completeRenderHead();
		} else if (fBinHead != NULL) {
			V3DJob *j = fBinHead;
			popHead(&fBinHead, &fBinTail);
			finish(j);
		}
		if (hwOk) {
			startNextRender();
			startNextBin();
		} else {
			failAll();
		}
	}

	/* Completes everything queued, in order, without touching the hardware. */
	void failAll()
	{
		for (;;) {
			V3DJob *j = fRenderHead;
			if (j != NULL) {
				popHead(&fRenderHead, &fRenderTail);
			} else if ((j = fBinHead) != NULL) {
				popHead(&fBinHead, &fBinTail);
			} else {
				break;
			}
			finish(j);
		}
	}

	/* Finished jobs, oldest first; NULL when there are none. */
	V3DJob *takeDone()
	{
		V3DJob *j = fDoneHead;
		fDoneHead = fDoneTail = NULL;
		return j;
	}

	/* Everything still queued or finished-but-unreaped (shutdown). */
	V3DJob *takeAll()
	{
		V3DJob *lists[3] = { fBinHead, fRenderHead, fDoneHead };
		V3DJob *tails[3] = { fBinTail, fRenderTail, fDoneTail };
		fBinHead = fBinTail = fRenderHead = fRenderTail = fDoneHead = fDoneTail = NULL;
		V3DJob *head = NULL, *tail = NULL;
		for (int i = 0; i < 3; i++) {
			if (lists[i] != NULL) {
				if (tail != NULL) {
					tail->next = lists[i];
				} else {
					head = lists[i];
				}
				tail = tails[i];
			}
		}
		return head;
	}

private:
	static void popHead(V3DJob **head, V3DJob **tail)
	{
		V3DJob *j = *head;
		if (j != NULL) {
			*head = j->next;
			if (*head == NULL) {
				*tail = NULL;
			}
			j->next = NULL;
		}
	}

	static void append(V3DJob **head, V3DJob **tail, V3DJob *j)
	{
		j->next = NULL;
		if (*tail != NULL) {
			(*tail)->next = j;
		} else {
			*head = j;
		}
		*tail = j;
	}

	/* A finished job becomes visible to waiters, then waits for reaping. */
	void finish(V3DJob *j)
	{
		fFinishedSeqno = j->seqno;
		append(&fDoneHead, &fDoneTail, j);
	}

	void completeRenderHead()
	{
		V3DJob *j = fRenderHead;
		if (j != NULL) {
			popHead(&fRenderHead, &fRenderTail);
			finish(j);
		}
	}

	void startNextBin()
	{
		for (;;) {
			V3DJob *j = fBinHead;
			if (j == NULL) {
				return;
			}
			fHw.flushCaches(fHw.ctx);
			if (j->ct0ca != j->ct0ea) {
				fHw.startBin(fHw.ctx, j->ct0ca, j->ct0ea);
				return;
			}
			/* nothing to bin: straight to the render stage */
			popHead(&fBinHead, &fBinTail);
			moveToRender(j);
		}
	}

	void startNextRender()
	{
		V3DJob *j = fRenderHead;
		if (j != NULL) {
			fHw.flushTextureCaches(fHw.ctx);
			fHw.startRender(fHw.ctx, j->ct1ca, j->ct1ea);
		}
	}

	void moveToRender(V3DJob *j)
	{
		bool wasEmpty = (fRenderHead == NULL);
		append(&fRenderHead, &fRenderTail, j);
		if (wasEmpty) {
			startNextRender();
		}
	}

	V3DHw fHw;
	V3DJob *fBinHead, *fBinTail;
	V3DJob *fRenderHead, *fRenderTail;
	V3DJob *fDoneHead, *fDoneTail;
	uint64_t fNextSeqno;
	volatile uint64_t fFinishedSeqno;
};

#endif /* _V3DQUEUE_H */
