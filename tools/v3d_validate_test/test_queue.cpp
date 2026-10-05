/*
 * Host test of V3DQueue (iokit/BCM2837V3D/V3DQueue.h) against a fake GPU
 * that enforces the hardware's constraints: one job on the binner thread
 * (CT0), one on the render thread (CT1), a job must be binned before it
 * renders, jobs complete in order. Random submit / bin-done / render-done /
 * reset events; after each step the invariants are checked.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

#include "V3DQueue.h"

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct FakeGpu {
	// what the hardware is running right now (by the CA/EA the queue gave it)
	bool binBusy = false, renderBusy = false;
	uint32_t binCa = 0, renderCa = 0;
	int cacheFlushes = 0, texFlushes = 0;
	int violations = 0;
};

static void startBin(void *c, uint32_t ca, uint32_t ea)
{
	FakeGpu *g = (FakeGpu *)c;
	if (g->binBusy || ca == ea) g->violations++;        // would clobber a running list / empty list
	g->binBusy = true; g->binCa = ca;
}
static void startRender(void *c, uint32_t ca, uint32_t ea)
{
	FakeGpu *g = (FakeGpu *)c;
	if (g->renderBusy || ca == ea) g->violations++;
	g->renderBusy = true; g->renderCa = ca;
}
static void flushC(void *c) { ((FakeGpu *)c)->cacheFlushes++; }
static void flushT(void *c) { ((FakeGpu *)c)->texFlushes++; }

struct Harness {
	FakeGpu gpu;
	V3DQueue q;
	std::vector<V3DJob *> all;           // by seqno - 1
	std::vector<int> state;              // 0 queued, 1 binned, 2 done(ok)/failed
	std::vector<uint64_t> done_order;
	Harness() : q(V3DHw{ &gpu, startBin, startRender, flushC, flushT }) {}

	V3DJob *newJob(bool hasBin)
	{
		V3DJob *j = (V3DJob *)calloc(1, sizeof(V3DJob));
		uint32_t id = (uint32_t)all.size() + 1;
		j->ct0ca = 0x1000 * id; j->ct0ea = hasBin ? j->ct0ca + 0x40 : j->ct0ca;
		j->ct1ca = 0x80000 + 0x1000 * id; j->ct1ea = j->ct1ca + 0x20;
		all.push_back(j);
		state.push_back(0);
		return j;
	}

	// which job does the hardware think it is running? (by address, since
	// addresses are unique per job)
	V3DJob *binJobOnHw() { for (V3DJob *j : all) if (gpu.binBusy && j->ct0ca == gpu.binCa && j->ct0ca != j->ct0ea) return j; return nullptr; }
	V3DJob *renderJobOnHw() { for (V3DJob *j : all) if (gpu.renderBusy && j->ct1ca == gpu.renderCa) return j; return nullptr; }

	void reap()
	{
		for (V3DJob *j = q.takeDone(); j; j = j->next) {
			done_order.push_back(j->seqno);
			state[j->seqno - 1] = 2;
		}
	}

	void checkInvariants(const char *what)
	{
		CHECK(gpu.violations == 0, "%s: hardware constraint violated (%d)", what, gpu.violations);
		// hardware activity matches the queue heads
		const V3DJob *bh = q.binHead(), *rh = q.renderHead();
		if (gpu.binBusy) {
			CHECK(bh && bh->ct0ca == gpu.binCa, "%s: binner runs something that is not the bin head", what);
		} else {
			CHECK(bh == nullptr, "%s: a job waits at the bin head but the binner is idle", what);
		}
		if (gpu.renderBusy) {
			CHECK(rh && rh->ct1ca == gpu.renderCa, "%s: renderer runs something that is not the render head", what);
		} else {
			CHECK(rh == nullptr, "%s: render head waiting but renderer idle", what);
		}
		// a job on the render stage has been through the bin stage
		if (rh) CHECK(state[rh->seqno - 1] == 0 || state[rh->seqno - 1] == 1, "%s: render head already finished", what);
		// finished seqno monotonic and consistent with the done order
		for (size_t i = 1; i < done_order.size(); i++) CHECK(done_order[i] > done_order[i - 1], "%s: done out of order", what);
	}
};

static uint32_t rng_s = 12345;
static uint32_t rnd() { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 17; rng_s ^= rng_s << 5; return rng_s; }

static void
run_random(int steps, bool with_resets)
{
	Harness h;
	uint64_t failed_by_reset = 0;
	for (int i = 0; i < steps; i++) {
		switch (rnd() % 8) {
		case 0: case 1: case 2: {
			V3DJob *j = h.newJob(rnd() % 4 != 0);
			h.q.submit(j);
			break;
		}
		case 3: case 4:
			if (h.gpu.binBusy) {                     // FLDONE
				h.gpu.binBusy = false;
				const V3DJob *bh = h.q.binHead();
				if (bh) h.state[bh->seqno - 1] = 1;
				h.q.binDone();
			}
			break;
		case 5: case 6:
			if (h.gpu.renderBusy) {                  // FRDONE
				h.gpu.renderBusy = false;
				h.q.renderDone();
			}
			break;
		case 7:
			if (with_resets && rnd() % 6 == 0 && !h.q.idle()) {
				uint64_t before = h.q.finishedSeqno();
				bool hwOk = rnd() % 5 != 0;
				h.gpu.binBusy = h.gpu.renderBusy = false;          // power cycle clears both threads
				h.q.resetDone(hwOk);
				failed_by_reset += h.q.finishedSeqno() - before;
				if (!hwOk) CHECK(h.q.idle(), "reset with dead hardware must drain the queue");
			}
			break;
		}
		h.reap();
		h.checkInvariants("random");
	}
	// drain: complete everything, no more submissions
	for (int guard = 0; guard < 100000 && !h.q.idle(); guard++) {
		if (h.gpu.binBusy) {
			h.gpu.binBusy = false;
			const V3DJob *bh = h.q.binHead();
			if (bh) h.state[bh->seqno - 1] = 1;
			h.q.binDone();
		} else if (h.gpu.renderBusy) {
			h.gpu.renderBusy = false;
			h.q.renderDone();
		} else {
			CHECK(false, "stuck: queue not idle but hardware idle (bin head %p, render head %p)",
			    (const void *)h.q.binHead(), (const void *)h.q.renderHead());
			break;
		}
		h.reap();
		h.checkInvariants("drain");
	}
	h.reap();
	CHECK(h.q.idle(), "queue idle after drain");
	CHECK(h.done_order.size() == h.all.size(), "every job completed exactly once: %zu done of %zu", h.done_order.size(), h.all.size());
	CHECK(h.q.finishedSeqno() == h.all.size(), "finished seqno %llu, submitted %zu", (unsigned long long)h.q.finishedSeqno(), h.all.size());
	for (size_t i = 0; i < h.state.size(); i++) CHECK(h.state[i] == 2, "job %zu never completed", i + 1);
	printf("random(%s): %zu jobs, %llu completed by reset\n", with_resets ? "resets" : "no resets",
	    h.all.size(), (unsigned long long)failed_by_reset);
	for (V3DJob *j : h.all) free(j);
}

static void
test_basic_pipeline()
{
	Harness h;
	V3DJob *a = h.newJob(true), *b = h.newJob(true);
	h.q.submit(a);
	CHECK(h.gpu.binBusy && h.gpu.binCa == a->ct0ca, "job A starts binning");
	h.q.submit(b);
	CHECK(h.gpu.binCa == a->ct0ca, "job B waits behind A on the binner");
	h.gpu.binBusy = false; h.q.binDone();                 // A binned
	CHECK(h.gpu.renderBusy && h.gpu.renderCa == a->ct1ca, "A renders");
	CHECK(h.gpu.binBusy && h.gpu.binCa == b->ct0ca, "B starts binning while A renders (pipelining)");
	h.gpu.binBusy = false; h.q.binDone();                 // B binned, but A still rendering
	CHECK(h.gpu.renderCa == a->ct1ca, "B does not start rendering until A is done");
	h.gpu.renderBusy = false; h.q.renderDone();           // A rendered
	CHECK(h.q.finishedSeqno() == 1, "A finished");
	CHECK(h.gpu.renderBusy && h.gpu.renderCa == b->ct1ca, "B renders next");
	h.gpu.renderBusy = false; h.q.renderDone();
	CHECK(h.q.finishedSeqno() == 2 && h.q.idle(), "both finished, queue idle");
	h.reap();
	CHECK(h.done_order.size() == 2, "both reaped");
	for (V3DJob *j : h.all) free(j);
}

static void
test_render_only_job()
{
	Harness h;
	V3DJob *a = h.newJob(false);        // no binner work: goes straight to rendering
	h.q.submit(a);
	CHECK(!h.gpu.binBusy && h.gpu.renderBusy && h.gpu.renderCa == a->ct1ca, "render-only job renders at once");
	V3DJob *b = h.newJob(true);
	h.q.submit(b);
	CHECK(h.gpu.binBusy && h.gpu.binCa == b->ct0ca, "next job bins while the render-only job renders");
	h.gpu.renderBusy = false; h.q.renderDone();
	CHECK(h.q.finishedSeqno() == 1, "render-only job finished");
	for (V3DJob *j : h.all) free(j);
}

static void
test_reset()
{
	Harness h;
	V3DJob *a = h.newJob(true), *b = h.newJob(true), *c = h.newJob(true);
	h.q.submit(a); h.q.submit(b); h.q.submit(c);
	h.gpu.binBusy = false; h.q.binDone();                 // A rendering, B binning, C queued
	// hang: power cycle
	h.gpu.binBusy = h.gpu.renderBusy = false;
	h.q.resetDone(true);
	CHECK(h.q.finishedSeqno() == 1, "the hung render job (A) is failed");
	CHECK(h.gpu.binBusy && h.gpu.binCa == b->ct0ca, "B's binning restarts from the beginning");
	CHECK(!h.gpu.renderBusy, "nothing to render yet");
	// hang again with only a bin job in flight
	h.gpu.binBusy = false;
	h.q.resetDone(true);
	CHECK(h.q.finishedSeqno() == 2, "a hung bin job with an empty render stage is failed too");
	CHECK(h.gpu.binBusy && h.gpu.binCa == c->ct0ca, "C starts");
	// dead hardware
	h.gpu.binBusy = false;
	h.q.resetDone(false);
	CHECK(h.q.idle() && h.q.finishedSeqno() == 3, "dead hardware drains everything");
	h.reap();
	CHECK(h.done_order.size() == 3, "all three reaped");
	for (V3DJob *j : h.all) free(j);
}

static void
test_take_all()
{
	Harness h;
	V3DJob *a = h.newJob(true), *b = h.newJob(true), *c = h.newJob(true);
	h.q.submit(a); h.q.submit(b); h.q.submit(c);
	h.gpu.binBusy = false; h.q.binDone();                 // a in render, b in bin, c in bin
	h.gpu.renderBusy = false; h.q.renderDone();           // a done
	int n = 0;
	for (V3DJob *j = h.q.takeAll(); j; j = j->next) n++;
	CHECK(n == 3, "takeAll returns every job exactly once (%d)", n);
	CHECK(h.q.idle(), "idle after takeAll");
	for (V3DJob *j : h.all) free(j);
}

int
main(int argc, char **argv)
{
	int steps = argc > 1 ? atoi(argv[1]) : 200000;
	test_basic_pipeline();
	test_render_only_job();
	test_reset();
	test_take_all();
	run_random(steps, false);
	run_random(steps, true);
	printf(g_fail ? "queue tests: %d FAILED\n" : "queue tests: all passed\n", g_fail);
	return g_fail != 0;
}
