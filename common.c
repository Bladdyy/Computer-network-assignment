#include "common.h"


// Creates pack with given data.
void create_pack(package* pack, unsigned char id, unsigned long long sess_id, unsigned char prot, unsigned long long len, unsigned long long pack_id, unsigned long bit_len){
    pack = memset(pack, 0, sizeof(package));  // Initializing  memory.
    pack->id = id;
    pack->session_id = sess_id;
    pack->protocol = prot;
    pack->length = len;
    pack->pack_id = pack_id;
    pack->bit_len = bit_len;
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
