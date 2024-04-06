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
#define MAX_WAIT 10

// Data package components.
typedef struct package{
    unsigned char id;
    unsigned long long session_id;
    unsigned char protocol;
    unsigned long long length;
    unsigned long long pack_id;
    unsigned long int bit_len;
} package;


// Ends connection with current client.
// Sets 'session ID', 'Bites to read' and 'last read package' to default values.
void to_default(unsigned long long *sess_id, unsigned long long *unpack, unsigned long long *last){
    *sess_id = -1;
    *unpack = -1;
    *last = -1;
}


// Creates pack with given data.
void create_pack(package* pack, unsigned char id, unsigned long long sess_id, unsigned char prot, unsigned long long len, unsigned long long pack_id, unsigned long bit_len){
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
static uint16_t read_port(char const *string, bool *error) {
    char *endptr;
    unsigned long port = strtoul(string, &endptr, 10);
    if ((port == ULONG_MAX && errno == ERANGE) || *endptr != 0 || port == 0 || port > UINT16_MAX) {
        fprintf(stderr,"ERROR: %s is not a valid port number.\n", string);
        *error = true;
    }
    return (uint16_t) port;
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
int CONNRJT_handler(int socket_fd, package *got, struct sockaddr_in client_address, socklen_t address_length){
    package *to_send = malloc(sizeof(package));
    if (malloc_error(to_send) == 1){
        return 1;
    }
    create_pack(to_send, 3, got->session_id, 0, 0, 0, 0);
    if (send_pack(socket_fd, to_send, client_address, address_length) == 1){
        return 1;
    }
    free(to_send);
    return 0;
}


// Handles 'DATA' packages.
// 'sess_id' - ID current connection with client, 'unpack' - number of bites left to recieve from all packages,
// 'last' - ID of last received package, 'prot' - received package with information about 'msg', 'msg' - received bites.
int DATA_handler(unsigned long long *sess_id, unsigned long long *unpack, unsigned long long *last, package *prot,
                 int socket_fd, struct sockaddr_in client_address, socklen_t address_length, void* msg){
    package *to_send = malloc(sizeof(package));
    if (malloc_error(to_send) == 1){
        return 1;
    }
    // Checks if package is next to receive and if it's session ID is correct.
    if (*sess_id != prot->session_id || *last + 1 != prot->pack_id){
        create_pack(to_send, 6, prot->session_id, 0, 0, prot->pack_id,0);  // RJT
        to_default(sess_id, unpack, last);  // Ends connection with client, so parameters are being set to default values.
        if (send_pack(socket_fd, to_send, client_address, address_length) == 1){ // Sends RJT.
            return 1;
        }
    }
    else{
        write(STDOUT_FILENO, msg, prot->bit_len);   // Writing message to stdout.
        *unpack -= prot->bit_len;  // Reduces the number of bites to read in the future.
        *last = *last + 1;  // Next package ID update.
        if (*unpack == 0){  // If whole message is read.
            create_pack(to_send, 7, prot->session_id, 0, 0, 0, 0);  // RCVD
            to_default(sess_id, unpack, last);  // Ends connection with client, so parameters are being set to default values.
            if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sends RCVD.
                return 1;
            }
        }
    }
    free(to_send);
    return 0;
}


// Handles 'CONN' packages.
// 'sess_id' - ID current connection with client, 'unpack' - number of bites left to recieve from all packages,
// 'last' - ID of last received package, 'prot' - received package with information about request to connect.
int CONN_handler(unsigned long long *sess_id, unsigned long long *unpack, package *prot, int socket_fd, struct sockaddr_in client_address, socklen_t address_length){
    package *to_send = malloc(sizeof(package));  // Answer to the request.
    if (malloc_error(to_send) == 1){
        return 1;
    }
    if (*sess_id == -1){  // Server isn't currently holding any connection.
        // Creating new connection,
        *sess_id = prot->session_id;
        *unpack = prot->length;
        create_pack(to_send, 2, prot->session_id, 0, 0, 0, 0);  // CONNACT
        if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sending assent for connection.
            fprintf(stderr, "ERROR: Couldn't connect with the client.\n");
            return 1;
        }
    }
    else{  // Server was already connected with a client.
        create_pack(to_send, 3, prot->session_id, 0, 0, 0, 0);  // CONNRJT
        if (send_pack(socket_fd, to_send, client_address, address_length) == 1){
            return 1;
        }
        if (*sess_id == prot->session_id){  // If connected client sent another 'CONN'.
            fprintf(stderr, "ERROR: Connected client sent another CONN. \n");
            return 1;
        }
        else{  // Different client wanted to connect.
            fprintf(stderr, "ERROR: There is a client already connected.\n");
        }
    }
    return 0;
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
    void *buff = malloc(sizeof(package) + MAX_MSG);  // Buffer.
    if (malloc_error(buff) == 0) {
        struct sockaddr_in client_address;
        socklen_t address_length = (socklen_t) sizeof(client_address);
        ssize_t received_length = recvfrom(socket_fd, buff, sizeof(package) + MAX_MSG, 0,
                                           (struct sockaddr *) &client_address, &address_length);

        if (received_length < 0) {
            // If there is a client connected but no message has been received in 'MAX_WAIT' seconds.
            if (errno == EAGAIN && sess_id != -1) {
                fprintf(stderr, "ERROR: Message receive timeout.\n");
            }
                // If there was an error receiving the message.
            else if (errno != EAGAIN) {
                fprintf(stderr, "ERROR: Couldn't receive message.\n");
            }
            to_default(&sess_id, &unpack, &last);
        } else {  // Got message.
            package *prot = malloc(sizeof(package));  // Package information.
            if (malloc_error(prot) == 0) {
                memcpy(prot, buff, sizeof(package));
                if (prot->protocol == 2) {  // UDP
                    if (prot->id == 1) {  // CONN
                        if (CONN_handler(&sess_id, &unpack, prot, socket_fd, client_address, address_length) == 1) {
                            fprintf(stderr, "ERROR: Ending connection with current client.\n");
                            to_default(&sess_id, &unpack, &last);
                        }
                        package *fake = malloc(sizeof(package));
                        create_pack(fake, 1, sess_id, 0, 0, 0, 0);
                        send_pack(socket_fd, fake, client_address, address_length);
                    }
                }
                free(buff);
            }
        }
    }
    else{
        to_default(&sess_id, &unpack, &last);
        fprintf(stderr, "ERROR: Ending connection with current client.");
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
    if (malloc_error(error) == 1){
        return 1;
    }
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

        // Setting timeout on the socket.
        struct timeval timeout;
        timeout.tv_sec = MAX_WAIT;
        timeout.tv_usec = 0;
        if (setsockopt (socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) < 0){
            fprintf(stderr, "ERROR: Couldn't set timeout on the socket.\n");
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