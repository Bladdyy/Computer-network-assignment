#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>


// Data package components.
typedef struct package{
    unsigned char id;
    unsigned long long session_id;
    unsigned char protocol;
    unsigned long long length;
    unsigned long long pack_id;
    unsigned long int bit_len;
    char* data;
} package;


// Creates pack with given data.
void create_pack(package* pack, unsigned char id, unsigned long long sess_id, unsigned char prot, unsigned long long len, unsigned long long pack_id, unsigned long bit_len, char* data){
    pack->id = id;
    pack->session_id = sess_id;
    pack->protocol = prot;
    pack->length = len;
    pack->pack_id = pack_id;
    pack->bit_len = bit_len;
    pack->data = data;
}


int send_pack(int socket_fd, package *to_send, struct sockaddr_in client_address, socklen_t address_length){
    int code = 0;
    ssize_t sent_length = sendto(socket_fd, to_send, sizeof(package), 0,
                                 (struct sockaddr *) &client_address, address_length);
    if (sent_length < 0) {  // Couldn't send.
        fprintf(stderr, "ERROR: Couldn't send Package with id %d.\n", to_send->id);
        code = 1;
    }
    else if ((size_t) sent_length != sizeof(package)) {  // Couldn't send fully
        fprintf(stderr, "ERROR: Package with id %d was sent incompletely.\n", to_send->id);
        code = 1;
    }
    return code;
}


void DATA_handler(unsigned long long *sess_id, unsigned long long *unpack, unsigned long long *last, package *prot, int socket_fd, struct sockaddr_in client_address, socklen_t address_length){
    package *to_send = malloc(sizeof(package));
    int change = 0;
    if (!(*sess_id == prot->session_id && *last + 1 == prot->pack_id)){
        create_pack(to_send, 6, prot->session_id, 0, 0, prot->pack_id,0, NULL);
        send_pack(socket_fd, to_send, client_address, address_length);
        change = 1;
    }
    else{
        if (*unpack == 0){
            create_pack(to_send, 7, prot->session_id, 0, 0, 0, 0, NULL);
            send_pack(socket_fd, to_send, client_address, address_length);
            change = 1;
            write(STDOUT_FILENO, prot->data, prot->bit_len);
        }
    }
    if (change == 1){
        *sess_id = -1;
        *unpack = -1;
        *last = -1;
    }
    free(to_send);
}

void CONN_handler(unsigned long long *sess_id, unsigned long long *unpack, package *prot, int socket_fd, struct sockaddr_in client_address, socklen_t address_length){
    package *to_send = malloc(sizeof(package));
    if (*sess_id == -1){
        *sess_id = prot->session_id;
        *unpack = prot->length;
        create_pack(to_send, 2, prot->session_id, 0, 0, 0,0, NULL);
        int code = send_pack(socket_fd, to_send, client_address, address_length);
        if (*sess_id == prot->session_id && code == 1){
            *sess_id = -1;
            *unpack = -1;
        }
    }
    else{
        create_pack(to_send, 3, prot->session_id, 0, 0, 0,0, NULL);
        send_pack(socket_fd, to_send, client_address, address_length);
        if (*sess_id == prot->session_id){
            *sess_id = -1;
            *unpack = -1;
        }
    }
    free(to_send);
}
// Creates port. TODO: RETURN ERROR (for example 1).
static uint16_t read_port(char const *string) {
    char *endptr;
    unsigned long port = strtoul(string, &endptr, 10);
    if ((port == ULONG_MAX && errno == ERANGE) || *endptr != 0 || port == 0 || port > UINT16_MAX) {
        fprintf(stderr,"ERROR: %s is not a valid port number.\n", string);
    }
    return (uint16_t) port;
}

int udp_server(uint16_t port, int socket_fd){
    unsigned long long sess_id = -1;
    unsigned long long unpack = -1;
    unsigned long long last = -1;
    // Bind the socket to an address.
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);
    if (bind(socket_fd, (struct sockaddr *) &server_address, (socklen_t) sizeof(server_address)) < 0) {
        fprintf(stderr, "ERROR: Couldn't bind the socket.\n");
        return 1;
    }
    for (;;) {
        // Receive a message. Buffer should not be allocated on the stack.
        package *prot = malloc(sizeof(package));
        struct sockaddr_in client_address;
        socklen_t address_length = (socklen_t) sizeof(client_address);
        size_t received_length = recvfrom(socket_fd, prot, sizeof(package), 0,
                                   (struct sockaddr *) &client_address, &address_length);
        if (received_length < 0) {
            fprintf(stderr, "ERROR: Couldn't receive message.\n");
        }
        else if (received_length != sizeof(package)){
            fprintf(stderr, "ERROR: Couldn't receive whole message.\n");
        }
        else if (prot->id == 1 && prot->protocol == 2){
            CONN_handler(&sess_id, &unpack, prot, socket_fd, client_address, address_length);
        }
        else if (prot->id == 3 && prot->session_id == sess_id){
            DATA_handler(&sess_id, &unpack, &last, prot, socket_fd, client_address, address_length);
        }
        free(prot);
    }
    return 0;
}


int main(int argc, char *argv[]) {
    if (argc != 3) {  // Checks for 2 arguments.
        fprintf(stderr, "ERROR: Expected arguments: %s <communication protocol> <port>\n", argv[0]);
        return 1;
    }
    char const *protocol = argv[1];  // Communication protocol.
    uint16_t port = read_port(argv[2]);
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        fprintf(stderr,"ERROR: Couldn't create a socket\n");
        return 1;
    }
    if (strcmp(protocol, "udp") == 0){
        if (udp_server(port, socket_fd) == 1){
            close(socket_fd);
            return 1;
        }
        close(socket_fd);
    }
    else{
        fprintf(stderr, "ERROR: Wrong protocol.\n");
        return 1;
    }

    return 0;
}