
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

// Conn package components.
typedef struct __attribute__ ((__packed__)) conn{
    uint64_t session_id;
    uint8_t protocol;
    uint64_t length;
} conn;

// Base package components.
typedef struct __attribute__ ((__packed__)) base{
    uint64_t session_id;
} base;

// Data package components.
typedef struct __attribute__ ((__packed__)) data_msg{
    uint64_t session_id;
    uint64_t pack_id;
    uint32_t byte_len;
} data_msg;

// Data packages status components.
typedef struct __attribute__ ((__packed__)) status{
    uint64_t session_id;
    uint64_t pack_id;
} status;


void create_conn(conn *pack, uint64_t sess_id, uint8_t prot, uint64_t len);

// Creates base pack with given data.
void create_base(base *pack, uint64_t sess_id);

// Creates data pack with given data.
void create_data(data_msg *pack, uint64_t sess_id, uint64_t pack_id, uint32_t byte_len);

// Creates status pack with given data.
void create_status(status *pack, uint64_t sess_id, uint64_t pack_id);

// Checks if malloc allocated spacer on 'pointer'.
int malloc_error(void* pointer);


// Creates port.
uint16_t read_port(char const *string, bool *error);


// Writing while tcp.
int tcp_write(int socket_fd, void *data, uint32_t size);


// Reading while tcp.
int tcp_read(int socket_fd, void* data, uint32_t size);