/*
 * Every MMAL message struct the VideoCore worker exchanges, for
 * mmal_abi_check.py: compiled once for 32-bit ARM Linux (AAPCS, the host
 * ABI the firmware's MMAL worker was written against, upstream's own
 * pointers) and once for this arm64 host (mmal_vc_msgs.h's 32-bit handles
 * and wire structs); the record layouts must be identical.
 */
#include "interface/mmal/vc/mmal_vc_msgs.h"
#include "interface/mmal/vc/mmal_vc_client_priv.h"
#define U(T) T *use_##T;
U(mmal_worker_msg_header) U(mmal_worker_version) U(mmal_worker_component_create) U(mmal_worker_component_create_reply)
U(mmal_worker_component_destroy) U(mmal_worker_port_info_get) U(mmal_worker_port_info_set) U(mmal_worker_port_info)
U(mmal_worker_reply) U(mmal_worker_port_action) U(mmal_worker_port_param_set) U(mmal_worker_port_param_get)
U(mmal_worker_port_param_get_reply) U(mmal_worker_buffer_from_host) U(mmal_worker_event_to_host) U(mmal_worker_stats)
U(mmal_worker_opaque_allocator) U(mmal_worker_get_core_stats_for_port) U(mmal_worker_get_core_stats_for_port_reply)
U(mmal_worker_consume_mem) U(mmal_worker_compact) U(mmal_worker_host_log) U(mmal_worker_lmk)
int sizes[] = { sizeof(mmal_worker_msg_header), sizeof(mmal_worker_component_create), sizeof(mmal_worker_port_info_set),
 sizeof(mmal_worker_port_info), sizeof(mmal_worker_port_action), sizeof(mmal_worker_buffer_from_host), sizeof(mmal_worker_event_to_host),
 sizeof(mmal_worker_port_param_get_reply), sizeof(mmal_worker_stats), sizeof(mmal_worker_get_core_stats_for_port_reply)};
