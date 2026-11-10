#include <ninit/ipc_protocol.h>

#include <string.h>

static void
put_u16(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char) (value >> 8);
    p[1] = (unsigned char) value;
}

static void
put_u32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char) (value >> 24);
    p[1] = (unsigned char) (value >> 16);
    p[2] = (unsigned char) (value >> 8);
    p[3] = (unsigned char) value;
}

static void
put_u64(unsigned char *p, uint64_t value)
{
    put_u32(p, (uint32_t) (value >> 32));
    put_u32(p + 4, (uint32_t) value);
}

static uint16_t
get_u16(const unsigned char *p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | (uint16_t) p[1]);
}

static uint32_t
get_u32(const unsigned char *p)
{
    return ((uint32_t) p[0] << 24) |
           ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) |
           (uint32_t) p[3];
}

static uint64_t
get_u64(const unsigned char *p)
{
    return ((uint64_t) get_u32(p) << 32) | (uint64_t) get_u32(p + 4);
}

static int
valid_type(uint16_t type)
{
    return type >= NINIT_IPC_MSG_HELLO && type <= NINIT_IPC_MSG_ERROR;
}

int
ninit_ipc_encode(const struct ninit_ipc_header *header,
                 const void *payload,
                 unsigned char *output,
                 size_t output_capacity,
                 size_t *output_size)
{
    size_t required;

    if (header == NULL || output == NULL || output_size == NULL) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }

    if (header->version != NINIT_IPC_VERSION ||
        !valid_type(header->type) ||
        header->flags != 0 ||
        header->payload_size > NINIT_IPC_MAX_PAYLOAD ||
        (header->payload_size != 0 && payload == NULL)) {
        return NINIT_IPC_UNSUPPORTED;
    }

    required = NINIT_IPC_HEADER_SIZE + (size_t) header->payload_size;
    if (output_capacity < required) {
        return NINIT_IPC_BUFFER_TOO_SMALL;
    }

    put_u32(output, NINIT_IPC_MAGIC);
    put_u16(output + 4, header->version);
    put_u16(output + 6, header->type);
    put_u32(output + 8, header->flags);
    put_u32(output + 12, header->payload_size);
    put_u64(output + 16, header->request_id);

    if (header->payload_size != 0) {
        memcpy(output + NINIT_IPC_HEADER_SIZE, payload,
               (size_t) header->payload_size);
    }

    *output_size = required;
    return NINIT_IPC_OK;
}

int
ninit_ipc_decode(const unsigned char *packet,
                 size_t packet_size,
                 struct ninit_ipc_header *header,
                 const unsigned char **payload)
{
    uint32_t payload_size;

    if (packet == NULL || header == NULL || payload == NULL) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }

    if (packet_size < NINIT_IPC_HEADER_SIZE ||
        get_u32(packet) != NINIT_IPC_MAGIC) {
        return NINIT_IPC_INVALID_PACKET;
    }

    header->version = get_u16(packet + 4);
    header->type = get_u16(packet + 6);
    header->flags = get_u32(packet + 8);
    payload_size = get_u32(packet + 12);
    header->payload_size = payload_size;
    header->request_id = get_u64(packet + 16);

    if (header->version != NINIT_IPC_VERSION ||
        !valid_type(header->type)) {
        return NINIT_IPC_UNSUPPORTED;
    }

    if (header->flags != 0 ||
        payload_size > NINIT_IPC_MAX_PAYLOAD ||
        packet_size != NINIT_IPC_HEADER_SIZE + (size_t) payload_size) {
        return NINIT_IPC_INVALID_PACKET;
    }

    *payload = payload_size == 0 ? NULL : packet + NINIT_IPC_HEADER_SIZE;
    return NINIT_IPC_OK;
}
