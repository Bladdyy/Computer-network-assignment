
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
typedef struct package{
    unsigned char id;
    unsigned long long session_id;
    unsigned char protocol;
    unsigned long long length;
    unsigned long long pack_id;
    unsigned long int byte_len;
} package;


// Creates pack with given data.
void create_pack(package* pack, unsigned char id, unsigned long long sess_id, unsigned char prot, unsigned long long len, unsigned long long pack_id, unsigned long bit_len);


// Checks if malloc allocated spacer on 'pointer'.
int malloc_error(void* pointer);


// Creates port.
uint16_t read_port(char const *string, bool *error);


// Writing while tcp.
int tcp_write(int socket_fd, void *data, unsigned long int size);


// Reading while tcp.
int tcp_read(int socket_fd, void* data, unsigned long int size);