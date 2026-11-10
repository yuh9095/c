#include <ninit/ipc_protocol.h>
#include <string.h>
static void p16(unsigned char*p,uint16_t v){p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v;}
static void p32(unsigned char*p,uint32_t v){p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;}
static void p64(unsigned char*p,uint64_t v){p32(p,(uint32_t)(v>>32));p32(p+4,(uint32_t)v);}
static uint16_t g16(const unsigned char*p){return(uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static uint32_t g32(const unsigned char*p){return((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3];}
static uint64_t g64(const unsigned char*p){return((uint64_t)g32(p)<<32)|(uint64_t)g32(p+4);}
static int valid_type(uint16_t t){return t>=NINIT_IPC_MSG_HELLO&&t<=NINIT_IPC_MSG_ERROR;}
int ninit_ipc_encode(const struct ninit_ipc_header*h,const void*payload,unsigned char*out,size_t cap,size_t*out_size){
 size_t need;if(!h||!out||!out_size)return NINIT_IPC_INVALID_ARGUMENT;
 if(h->version!=NINIT_IPC_VERSION||!valid_type(h->type)||h->flags||h->payload_size>NINIT_IPC_MAX_PAYLOAD||(h->payload_size&&!payload))return NINIT_IPC_UNSUPPORTED;
 need=NINIT_IPC_HEADER_SIZE+(size_t)h->payload_size;if(cap<need)return NINIT_IPC_BUFFER_TOO_SMALL;
 p32(out,NINIT_IPC_MAGIC);p16(out+4,h->version);p16(out+6,h->type);p32(out+8,h->flags);p32(out+12,h->payload_size);p64(out+16,h->request_id);
 if(h->payload_size)memcpy(out+NINIT_IPC_HEADER_SIZE,payload,h->payload_size);*out_size=need;return NINIT_IPC_OK;
}
int ninit_ipc_decode(const unsigned char*packet,size_t size,struct ninit_ipc_header*h,const unsigned char**payload){
 uint32_t n;if(!packet||!h||!payload)return NINIT_IPC_INVALID_ARGUMENT;
 if(size<NINIT_IPC_HEADER_SIZE||g32(packet)!=NINIT_IPC_MAGIC)return NINIT_IPC_INVALID_PACKET;
 h->version=g16(packet+4);h->type=g16(packet+6);h->flags=g32(packet+8);n=g32(packet+12);h->payload_size=n;h->request_id=g64(packet+16);
 if(h->version!=NINIT_IPC_VERSION||!valid_type(h->type))return NINIT_IPC_UNSUPPORTED;
 if(h->flags||n>NINIT_IPC_MAX_PAYLOAD||size!=NINIT_IPC_HEADER_SIZE+(size_t)n)return NINIT_IPC_INVALID_PACKET;
 *payload=n?packet+NINIT_IPC_HEADER_SIZE:NULL;return NINIT_IPC_OK;
}
