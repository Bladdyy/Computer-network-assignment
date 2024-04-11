#include "common.h"


// Creates pack with given data.
void create_pack(package* pack, unsigned char id, unsigned long long sess_id, unsigned char prot, unsigned long long len, unsigned long long pack_id, unsigned long byte_len){
    pack = memset(pack, 0, sizeof(package));  // Initializing  memory.
    pack->id = id;
    pack->session_id = sess_id;
    pack->protocol = prot;
    pack->length = len;
    pack->pack_id = pack_id;
    pack->byte_len = byte_len;
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


int tcp_write(int socket_fd, void *data, unsigned long int size){
    unsigned long int to_write = size;
    unsigned long int written = 0;
    ssize_t done;
    while (to_write > 0){
        done = write(socket_fd, data + written, to_write);
        if (done <= 0){
            return 1;
        }
        written += done;
        to_write -= done;
    }
    return 0;
}


int tcp_read(int socket_fd, void* data, unsigned long int size){
    unsigned long int to_read = size;
    unsigned long int all_read = 0;
    ssize_t done;
    while (to_read > 0){
        done = read(socket_fd, data + all_read, to_read);
        if (done < 0){
            if (errno == EAGAIN){
                fprintf(stderr, "ERROR: Message timeout.\n");
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