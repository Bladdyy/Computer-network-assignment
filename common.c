#include "common.h"


// Creates pack with given data.
void create_pack(package* pack, uint8_t id, uint64_t sess_id, uint8_t prot, uint64_t len, uint64_t pack_id, uint32_t byte_len){
    pack = memset(pack, 0, sizeof(package));  // Initializing  memory.
    pack->id = id;
    pack->session_id = sess_id;
    pack->protocol = prot;
    pack->length = htobe64(len);
    pack->pack_id = htobe64(pack_id);
    pack->byte_len = htobe32(byte_len);
}


// Checks if malloc allocated spacer on 'pointer'.
int malloc_error(void* pointer){
    if (pointer == NULL){  // If there was a problem allocating space.
        fprintf(stderr, "ERROR: Problem with allocation.\n");
        return 1;
    }
    return 0;
}


// Creates port.
uint16_t read_port(char const *string, bool *error) {
    char *endptr;
    unsigned long port = strtoul(string, &endptr, 10);
    if ((port == ULONG_MAX && errno == ERANGE) || *endptr != 0 || port == 0 || port > UINT16_MAX) {
        fprintf(stderr,"ERROR: %s is not a valid port number.\n", string);
        *error = true;
    }
    return (uint16_t) port;
}


// Sends messages using TCP protocol.
int tcp_write(int socket_fd, void *data, uint32_t size){
    uint32_t to_write = size;  // Size to write.
    uint32_t written = 0;      // Already sent bytes.
    ssize_t done;
    while (to_write > 0){
        done = write(socket_fd, data + written, to_write);
        if (done <= 0){  // Error while sending.
            return 1;
        }
        written += done;
        to_write -= done;
    }
    return 0;
}


// Receives messages using TCP protocol.
int tcp_read(int socket_fd, void* data, uint32_t size){
    uint32_t to_read = size;  // Size to read.
    uint32_t all_read = 0;    // Already read bytes.
    ssize_t done;
    while (to_read > 0){
        done = read(socket_fd, data + all_read, to_read);
        if (done <= 0){  // Error while reading.
            if (errno == EAGAIN){
                fprintf(stderr, "ERROR: Message timeout.\n");
            }
            else if (done == 0){
                fprintf(stderr, "ERROR: Client already closed the socket.\n");
            }
            else{
                fprintf(stderr, "ERROR: Couldn't read message.\n");
            }
            return 1;
        }
        all_read += done;
        to_read -= done;
    }
    return 0;
}