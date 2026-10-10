/*
 * ca_proto_test: speaks the raw audiod protocol (ca_protocol.h) and checks that the daemon rejects what it must:
 * wrong tokens, bad sizes and counts, bad rates and channel counts, bad volume, wrong version, ops on a closed stream.
 * Prints "ca_proto_test: PASS" (exit 0) or the failing check.
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <mach/mach.h>
#include "ca_protocol.h"

extern mach_port_t bootstrap_port;
extern kern_return_t bootstrap_look_up(mach_port_t, const char *, mach_port_t *);
extern mach_port_t mach_reply_port(void);

static mach_port_t g_svc, g_reply;
static ca_request_rcv_t g_buf;
static uint32_t g_seq;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("ca_proto_test: FAIL line %d: %s ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* Returns the transport status; the reply's kr is in r->kr. data_bytes is what is actually sent. */
static kern_return_t rpc(uint32_t op, uint32_t version, const ca_request_t *in, size_t data_bytes, ca_reply_t *r)
{
    ca_request_t *rq = &g_buf.m;
    memcpy(rq, in, offsetof(ca_request_t, data) + data_bytes);
    rq->version = version;
    rq->seq = ++g_seq;
    rq->h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
    rq->h.msgh_remote_port = g_svc;
    rq->h.msgh_local_port = g_reply;
    rq->h.msgh_size = CA_REQ_SIZE(data_bytes);
    rq->h.msgh_id = (mach_msg_id_t)op;
    kern_return_t kr = mach_msg(&rq->h, MACH_SEND_MSG | MACH_RCV_MSG | MACH_RCV_TIMEOUT | MACH_SEND_TIMEOUT,
        rq->h.msgh_size, sizeof(g_buf), g_reply, 20000, MACH_PORT_NULL);
    if (kr == KERN_SUCCESS) *r = *(ca_reply_t *)&g_buf;
    return kr;
}

static int32_t op(uint32_t o, uint32_t id, uint32_t token, uint32_t a, uint32_t b, float f, uint32_t nframes, uint32_t ch,
                  const void *data, size_t data_bytes, ca_reply_t *r)
{
    ca_request_t rq;
    memset(&rq, 0, sizeof(rq));
    rq.id = id; rq.token = token; rq.a = a; rq.b = b; rq.f = f; rq.nframes = nframes; rq.channels_check = ch;
    if (data_bytes) memcpy(rq.data, data, data_bytes);
    kern_return_t kr = rpc(o, CA_PROTO_VERSION, &rq, data_bytes, r);
    if (kr != KERN_SUCCESS) { printf("ca_proto_test: transport error 0x%x for op 0x%x\n", kr, o); fails++; return -1; }
    return r->kr;
}

int main(void)
{
    ca_reply_t r;
    float pcm[200];
    memset(pcm, 0, sizeof pcm);
    for (int i = 0; i < 5; i++) {
        if (bootstrap_look_up(bootstrap_port, CA_SERVICE_NAME, &g_svc) == KERN_SUCCESS) break;
        usleep(200000);
    }
    if (!g_svc) { printf("ca_proto_test: FAIL cannot look up %s\n", CA_SERVICE_NAME); return 1; }
    g_reply = mach_reply_port();

    /* argument validation */
    CHECK(op(CA_OP_OPEN, 0, 0, 5, 2, 0, 0, 0, NULL, 0, &r) == KERN_INVALID_ARGUMENT, "rate 5");
    CHECK(op(CA_OP_OPEN, 0, 0, 1000000, 2, 0, 0, 0, NULL, 0, &r) == KERN_INVALID_ARGUMENT, "rate 1e6");
    CHECK(op(CA_OP_OPEN, 0, 0, 48000, 3, 0, 0, 0, NULL, 0, &r) == KERN_INVALID_ARGUMENT, "3 channels");
    {   /* wrong protocol version */
        ca_request_t rq; memset(&rq, 0, sizeof rq); rq.a = 48000; rq.b = 2;
        CHECK(rpc(CA_OP_OPEN, CA_PROTO_VERSION + 1, &rq, 0, &r) == KERN_SUCCESS && r.kr == KERN_INVALID_ARGUMENT, "version");
    }
    CHECK(op(0x1234, 0, 0, 0, 0, 0, 0, 0, NULL, 0, &r) == KERN_INVALID_ARGUMENT, "unknown op");

    /* a real stream */
    CHECK(op(CA_OP_OPEN, 0, 0, 44100, 1, 0, 0, 0, NULL, 0, &r) == KERN_SUCCESS, "open 44100/1");
    uint32_t id = r.id, tok = r.token;
    CHECK(id != 0 && tok != 0, "id %u token %u", id, tok);

    /* token / id checks */
    CHECK(op(CA_OP_WRITE, id, tok ^ 1, 0, 0, 0, 10, 1, pcm, 40, &r) == KERN_INVALID_NAME, "wrong token write");
    CHECK(op(CA_OP_WRITE, id + 1000, tok, 0, 0, 0, 10, 1, pcm, 40, &r) == KERN_INVALID_NAME, "wrong id write");
    CHECK(op(CA_OP_START, id, tok ^ 1, 0, 0, 0, 0, 0, NULL, 0, &r) == KERN_INVALID_NAME, "wrong token start");
    CHECK(op(CA_OP_CLOSE, id, tok ^ 1, 0, 0, 0, 0, 0, NULL, 0, &r) == KERN_INVALID_NAME, "wrong token close");

    /* WRITE shape checks */
    CHECK(op(CA_OP_WRITE, id, tok, 0, 0, 0, 100, 1, pcm, 400, &r) == KERN_SUCCESS && r.a == 100, "good write accepted %u", r.a);
    CHECK(op(CA_OP_WRITE, id, tok, 0, 0, 0, 100, 1, pcm, 200, &r) == KERN_INVALID_ARGUMENT, "declared 100 frames, sent 50");
    CHECK(op(CA_OP_WRITE, id, tok, 0, 0, 0, 10, 2, pcm, 40, &r) == KERN_INVALID_ARGUMENT, "channels mismatch");
    CHECK(op(CA_OP_WRITE, id, tok, 0, 0, 0, 0, 1, NULL, 0, &r) == KERN_SUCCESS && r.a == 0 && r.b > 0, "zero-frame poll, room %u", r.b);

    /* volume validation, restore afterwards */
    CHECK(op(CA_OP_GET_INFO, 0, 0, 0, 0, 0, 0, 0, NULL, 0, &r) == KERN_SUCCESS && r.rate == CA_DEVICE_RATE, "info");
    float vol = r.f; uint32_t mute = r.a;
    CHECK(op(CA_OP_SET_VOLUME, 0, 0, 0, 0, 0.5f, 0, 0, NULL, 0, &r) == KERN_INVALID_ARGUMENT, "empty mask");
    CHECK(op(CA_OP_SET_VOLUME, 0, 0, 0, CA_SET_VOLUME, 2.0f, 0, 0, NULL, 0, &r) == KERN_INVALID_ARGUMENT, "volume 2.0");
    CHECK(op(CA_OP_SET_VOLUME, 0, 0, mute, CA_SET_VOLUME, vol, 0, 0, NULL, 0, &r) == KERN_SUCCESS, "restore volume");

    /* close, then the stream must be gone */
    CHECK(op(CA_OP_CLOSE, id, tok, 0, 0, 0, 0, 0, NULL, 0, &r) == KERN_SUCCESS, "close");
    CHECK(op(CA_OP_WRITE, id, tok, 0, 0, 0, 10, 1, pcm, 40, &r) == KERN_INVALID_NAME, "write after close");
    CHECK(op(CA_OP_CLOSE, id, tok, 0, 0, 0, 0, 0, NULL, 0, &r) == KERN_INVALID_NAME, "double close");

    /* stream table limit */
    uint32_t ids[CA_MAX_STREAMS + 2], toks[CA_MAX_STREAMS + 2];
    int opened = 0;
    for (int i = 0; i < CA_MAX_STREAMS + 2; i++) {
        int32_t k = op(CA_OP_OPEN, 0, 0, 48000, 2, 0, 0, 0, NULL, 0, &r);
        if (k == KERN_SUCCESS) { ids[opened] = r.id; toks[opened] = r.token; opened++; }
        else { CHECK(k == KERN_RESOURCE_SHORTAGE, "open #%d returned %d", i, k); break; }
    }
    CHECK(opened <= CA_MAX_STREAMS && opened >= CA_MAX_STREAMS - 1, "opened %d streams", opened);   /* a draining stream may still hold a slot */
    for (int i = 0; i < opened; i++) op(CA_OP_CLOSE, ids[i], toks[i], 0, 0, 0, 0, 0, NULL, 0, &r);

    printf(fails ? "ca_proto_test: %d FAILED\n" : "ca_proto_test: PASS\n", fails);
    return fails != 0;
}
