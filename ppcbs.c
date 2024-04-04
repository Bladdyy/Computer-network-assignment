#include <sys/socket.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define MAX_MSG 64000
// TODO: MALLOC ERROR.


// Data package components.
typedef struct package{
    unsigned char id;
    unsigned long long session_id;
    unsigned char protocol;
    unsigned long long length;
    unsigned long long pack_id;
    unsigned long int bit_len;
} package;


// Creates pack with given data.
void create_pack(package* pack, unsigned char id, unsigned long long sess_id, unsigned char prot, unsigned long long len, unsigned long long pack_id, unsigned long bit_len){
    pack->id = id;
    pack->session_id = sess_id;
    pack->protocol = prot;
    pack->length = len;
    pack->pack_id = pack_id;
    pack->bit_len = bit_len;
}


// Sends 'to_send' package to client.
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


// Handles 'CONNRJT' packages.
void CONNRJT_handler(int socket_fd, package *got, struct sockaddr_in client_address, socklen_t address_length){
    package *to_send = malloc(sizeof(package));
    create_pack(to_send, 3, got->session_id, 0, 0, 0, 0);
    send_pack(socket_fd, to_send, client_address, address_length);
    free(to_send);
}


// Handles 'DATA' packages.
// 'sess_id' - ID current connection with client, 'unpack' - number of bites left to recieve from all packages,
// 'last' - ID of last received package, 'prot' - received package with information about 'msg', 'msg' - received bites.
void DATA_handler(unsigned long long *sess_id, unsigned long long *unpack, unsigned long long *last, package *prot,
                  int socket_fd, struct sockaddr_in client_address, socklen_t address_length, void* msg){
    package *to_send = malloc(sizeof(package));
    bool change = false;  // Allows to set parameters to default values.

    // Checks if package is next to receive and if it's session ID is correct.
    if (!(*sess_id == prot->session_id && *last + 1 == prot->pack_id)){
        create_pack(to_send, 6, prot->session_id, 0, 0, prot->pack_id,0);  // RJT
        send_pack(socket_fd, to_send, client_address, address_length);  // Sends RJT.
        change = true;  // Ends connection with client, so parameters are being set to default values.
    }
    else{
        write(STDOUT_FILENO, msg, prot->bit_len);   // Writing message to stdout.
        *unpack -= prot->bit_len;  // Reduces the number of bites to read in the future.
        if (*unpack == 0){  // If whole message is read.
            create_pack(to_send, 7, prot->session_id, 0, 0, 0, 0);  // RCVD
            send_pack(socket_fd, to_send, client_address, address_length);  // Sends RCVD.
            change = true;  // Ends connection with client, so parameters are being set to default values.
        }
    }
    if (change){  // Sets parameters to default value.
        *sess_id = -1;
        *unpack = -1;
        *last = -1;
    }
    free(to_send);
}


// Handles 'CONN' packages.
// 'sess_id' - ID current connection with client, 'unpack' - number of bites left to recieve from all packages,
// 'last' - ID of last received package, 'prot' - received package with information about request to connect.
void CONN_handler(unsigned long long *sess_id, unsigned long long *unpack, package *prot, int socket_fd, struct sockaddr_in client_address, socklen_t address_length){
    package *to_send = malloc(sizeof(package));  // Answer to the request.
    if (*sess_id == -1){  // Server isn't currently holding any connection.
        // Creating new connection,
        *sess_id = prot->session_id;
        *unpack = prot->length;
        create_pack(to_send, 2, prot->session_id, 0, 0, 0, 0);  // CONNACT
        int code = send_pack(socket_fd, to_send, client_address, address_length);  // Sending assent for connection.
        if (code == 1){  // If sending failed.
            fprintf(stderr, "ERROR: Couldn't connect with the client.\n");
            // Setting parameters to default values.
            *sess_id = -1;
            *unpack = -1;
        }
    }
    else{  // Server was already connected with a client.
        create_pack(to_send, 3, prot->session_id, 0, 0, 0, 0);  // CONNRJT
        send_pack(socket_fd, to_send, client_address, address_length);
        if (*sess_id == prot->session_id){  // If connected client sent another 'CONN'.
            fprintf(stderr, "ERROR: Connected client sent another CONN.\n");
            *sess_id = -1;
            *unpack = -1;
        }
        else{  // Different client wanted to connect.
            fprintf(stderr, "ERROR: There is a client already connected.\n");
        }
    }
}


// Creates port.
static uint16_t read_port(char const *string, bool *error) {
    char *endptr;
    unsigned long port = strtoul(string, &endptr, 10);
    if ((port == ULONG_MAX && errno == ERANGE) || *endptr != 0 || port == 0 || port > UINT16_MAX) {
        fprintf(stderr,"ERROR: %s is not a valid port number.\n", string);
        *error = true;
    }
    return (uint16_t) port;
}


//  UDP server work.
int udp_server(uint16_t port, int socket_fd){
    unsigned long long sess_id = -1;  // Session ID of currently connected client.
    unsigned long long unpack = -1;  // Number of bites left to receive.
    unsigned long long last = -1;  // ID of last received package.
    // Bind the socket to an address.
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);

    // There was an error binding.
    if (bind(socket_fd, (struct sockaddr *) &server_address, (socklen_t) sizeof(server_address)) < 0) {
        fprintf(stderr, "ERROR: Couldn't bind the socket.\n");
        return 1;
    }

    // Handling clients.
    for (;;) {
        void *buff = malloc(sizeof(package) + MAX_MSG);  // Buffer.
        struct sockaddr_in client_address;
        socklen_t address_length = (socklen_t) sizeof(client_address);
        ssize_t received_length = recvfrom(socket_fd, buff, sizeof(package) + MAX_MSG, 0,
                                          (struct sockaddr *) &client_address, &address_length);

        if (received_length < 0) {
            fprintf(stderr, "ERROR: Couldn't receive message.\n");
        }
        else{  // Got message.
            package *prot = malloc(sizeof(package));  // Package information.
            memcpy(prot, buff, sizeof(package));
            if (prot->protocol == 2){  // UDP
                if (prot->id == 1){  // CONN
                    CONN_handler(&sess_id, &unpack, prot, socket_fd, client_address, address_length);
                }
                else if (prot->id == 4 && prot->session_id == sess_id){  // DATA and correct session ID.
                    char* msg = malloc(prot->bit_len);
                    memcpy(msg, buff + sizeof(package), prot->bit_len);
                    DATA_handler(&sess_id, &unpack, &last, prot, socket_fd, client_address, address_length, msg);
                    free(msg);
                }
                else{
                    if (sess_id != -1 && prot->session_id != sess_id){  // Bad session ID.
                        fprintf(stderr, "ERROR: Wrong session ID.");
                        CONNRJT_handler(socket_fd, prot, client_address, address_length);
                    }
                    else{  // Bad package ID.
                        fprintf(stderr, "ERROR: Wrong package ID.");
                        CONNRJT_handler(socket_fd, prot, client_address, address_length);
                    }

                }
                free(prot);
            }
            else{  // NOT UDP
                fprintf(stderr, "ERROR: Client demanded wrong protocol.\n");
                CONNRJT_handler(socket_fd, prot, client_address, address_length);
            }
        }
        free(buff);
    }
    return 0;
}


// Creates server with specified protocol.
int main(int argc, char *argv[]) {
    if (argc != 3) {  // Checks for 2 arguments.
        fprintf(stderr, "ERROR: Expected arguments: %s <communication protocol> <port>\n", argv[0]);
        return 1;
    }
    char const *protocol = argv[1];  // Communication protocol.
    bool *error = malloc(sizeof(bool));
    *error = false;
    uint16_t port = read_port(argv[2], error);
    if (*error){  // There was an error getting port.
        free(error);
        return 1;
    }
    free(error);


    if (strcmp(protocol, "udp") == 0){  // Communication protocol is UDP.
        int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {  // There was an error creating a socket.
            fprintf(stderr,"ERROR: Couldn't create a socket\n");
            return 1;
        }
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