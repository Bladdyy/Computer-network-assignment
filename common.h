
// Max package size.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <unistd.h>

#define MAX_MSG 64000
#define MAX_QUEUE 100
// Data package components.
typedef struct __attribute__ ((__packed__)) package{
    uint8_t id;
    uint64_t session_id;
    uint8_t protocol;
    uint64_t length;
    uint64_t pack_id;
    uint32_t byte_len;
} package;


// Creates pack with given data.
void create_pack(package* pack, uint8_t id, uint64_t sess_id, uint8_t prot, uint64_t len, uint64_t pack_id, uint32_t bit_len);


// Checks if malloc allocated spacer on 'pointer'.
int malloc_error(void* pointer);


// Creates port.
uint16_t read_port(char const *string, bool *error);


// Writing while tcp.
int tcp_write(int socket_fd, void *data, uint32_t size);


// Reading while tcp.
int tcp_read(int socket_fd, void* data, uint32_t size);