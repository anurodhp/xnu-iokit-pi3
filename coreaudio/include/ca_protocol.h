/*
 * ca_protocol.h: private wire protocol between libaudiohal and audiod.
 * Plain (descriptor-free) Mach messages; request = msgh_id is the op, reply
 * goes to msgh_local_port (a send-once right made by the client's mach_msg).
 * Not Apple's protocol (Apple's coreaudiod protocol is private and unpublished);
 * this is project-defined. See PLAN.md.
 */
#ifndef CA_PROTOCOL_H
#define CA_PROTOCOL_H
#include <stdint.h>
#include <mach/mach.h>

#define CA_SERVICE_NAME   "org.puredarwin.coreaudiod"
#define CA_PROTO_VERSION  1

enum {
    CA_OP_OPEN       = 0x4341,   /* a = rate_hz, b = channels             -> id */
    CA_OP_WRITE      = 0x4342,   /* id, nframes, data (float32 interleaved) -> a = accepted, b = free frames */
    CA_OP_START      = 0x4343,   /* id */
    CA_OP_STOP       = 0x4344,   /* id */
    CA_OP_CLOSE      = 0x4345,   /* id */
    CA_OP_SET_VOLUME = 0x4346,   /* b = mask (CA_SET_VOLUME | CA_SET_MUTE); f = scalar 0..1, a = mute (0/1) */
    CA_OP_GET_INFO   = 0x4347,   /* -> rate, f = volume, a = mute, name */
};

#define CA_SET_VOLUME      1u
#define CA_SET_MUTE        2u
#define CA_MAX_STREAMS     16
#define CA_MIN_RATE        8000
#define CA_MAX_RATE        192000
#define CA_MAX_WRITE_BYTES 16384
#define CA_DEVICE_RATE     48000
#define CA_STREAM_RING_FRAMES 8192   /* per-stream ring in the daemon, in the stream's own rate */
#define CA_DEVICE_NAME     "Raspberry Pi 3.5mm Output"

typedef struct {
    mach_msg_header_t h;
    uint32_t version;
    uint32_t seq;          /* echoed in the reply */
    uint32_t id;           /* stream id (WRITE/START/STOP/CLOSE) */
    uint32_t a, b;
    float    f;
    uint32_t nframes;      /* WRITE: frames in data[] */
    uint32_t channels_check;/* WRITE: must equal the stream's channels */
    uint8_t  data[CA_MAX_WRITE_BYTES];
} ca_request_t;

typedef struct {
    mach_msg_header_t h;
    int32_t  kr;           /* kern_return_t */
    uint32_t version;
    uint32_t seq;
    uint32_t id;
    uint32_t a, b;
    float    f;
    uint32_t rate;
    char     name[40];
} ca_reply_t;

/* Receive buffers need room for the kernel-appended trailer. */
typedef struct { ca_request_t m; mach_msg_max_trailer_t t; } ca_request_rcv_t;
typedef struct { ca_reply_t m;   mach_msg_max_trailer_t t; } ca_reply_rcv_t;

/* Wire size of a request that carries `data_bytes` of PCM. */
#define CA_REQ_SIZE(data_bytes) ((mach_msg_size_t)(offsetof(ca_request_t, data) + (data_bytes)))
#endif
