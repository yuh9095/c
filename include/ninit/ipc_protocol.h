#ifndef NINIT_IPC_PROTOCOL_H
#define NINIT_IPC_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NINIT_IPC_MAGIC UINT32_C(0x4e495043) /* NIPC */
#define NINIT_IPC_VERSION UINT16_C(1)
#define NINIT_IPC_HEADER_SIZE 24U
#define NINIT_IPC_MAX_PAYLOAD (1024U * 1024U)
enum ninit_ipc_message_type { NINIT_IPC_MSG_HELLO=1,NINIT_IPC_MSG_HELLO_REPLY,NINIT_IPC_MSG_PING,NINIT_IPC_MSG_PONG,NINIT_IPC_MSG_STATUS,NINIT_IPC_MSG_STATUS_REPLY,NINIT_IPC_MSG_SUBMIT_JOB,NINIT_IPC_MSG_JOB_REPLY,NINIT_IPC_MSG_ERROR };
enum ninit_ipc_result { NINIT_IPC_OK=0,NINIT_IPC_INVALID_ARGUMENT=-1,NINIT_IPC_INVALID_PACKET=-2,NINIT_IPC_UNSUPPORTED=-3,NINIT_IPC_BUFFER_TOO_SMALL=-4, NINIT_IPC_IO_ERROR=-5, NINIT_IPC_CLOSED=-6 };
/* Host-order view; wire header is fixed-size and big-endian. */
struct ninit_ipc_header { uint16_t version,type; uint32_t flags,payload_size; uint64_t request_id; };
/* Payload must be NULL iff payload_size is zero and must not overlap output. */
int ninit_ipc_encode(const struct ninit_ipc_header *,const void *,unsigned char *,size_t,size_t *);
/* Returned payload aliases packet and remains valid only while packet is valid. */
int ninit_ipc_decode(const unsigned char *,size_t,struct ninit_ipc_header *,const unsigned char **);
#ifdef __cplusplus
}
#endif
#endif
