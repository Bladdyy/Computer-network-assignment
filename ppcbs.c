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
#define MAX_RETRANSMITS 3



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
void to_default(unsigned long long *last, bool *udpr, unsigned long long *trials, bool *connected){
    *last = 0;
    *udpr = false;
    *connected = false;
    *trials = 0;
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


// Handles 'DATA' packages.
// 'sess_id' - ID current connection with client, 'unpack' - number of bites left to recieve from all packages,
// 'last' - ID of last received package, 'prot' - received package with information about 'msg', 'msg' - received bites.
// ACC send and check other things with retransmissions in server.
int DATA_handler(unsigned long long *unpack, unsigned long long *last, bool *udpr, bool *connected, unsigned long long *trials, package *prot,
                  int socket_fd, struct sockaddr_in client_address, socklen_t address_length, void* msg){
    int code = 0;
    package *to_send = malloc(sizeof(package));
    if (malloc_error(to_send) == 1){
        return 1;
    }
    // Checks if package's ID is correct.
    if ((*last < prot->pack_id && udpr) || (*last != prot->pack_id && !udpr)){
        create_pack(to_send, 6, prot->session_id, 0, 0, prot->pack_id,0);  // RJT
        to_default(last, udpr, trials, connected);  // Ends connection with client, so parameters are being set to default values.
        if (send_pack(socket_fd, to_send, client_address, address_length) == 1){ // Sends RJT.
            code = 1;
        }
    }
    // Retransmission of ACC.
    else if (*last >= prot->pack_id && udpr){
        create_pack(to_send, 5, prot->session_id, 0, 0, prot->pack_id, 0); // ACC
        if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sends ACC.
            code = 2;
        }
    }
    else{  // Protocol is correct.
        write(STDOUT_FILENO, msg, prot->bit_len);   // Writing message to stdout.
        *unpack -= prot->bit_len;  // Reduces the number of bites to read in the future.
        *last = *last + 1;  // Next package ID update.
        if (udpr){
            create_pack(to_send, 5, prot->session_id, 0, 0, prot->pack_id, 0); // ACC
            if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sends ACC.
                code = 2;
            }
        }
        if (*unpack == 0){  // If whole message is read.
            create_pack(to_send, 7, prot->session_id, 0, 0, 0, 0);  // RCVD
            to_default(last, udpr, trials, connected);  // Ends connection with client, so parameters are being set to default values.
            if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sends RCVD.
                code = 3;
            }
        }
    }
    free(to_send);
    return code;
}


// Handles 'CONN' packages.
// 'sess_id' - ID current connection with client, 'unpack' - number of bites left to recieve from all packages,
// 'last' - ID of last received package, 'prot' - received package with information about request to connect.
int CONN_handler(unsigned long long *sess_id, unsigned long long *unpack, bool *udpr, package *prot, int socket_fd, struct sockaddr_in client_address, socklen_t address_length, bool *connected){
    package *to_send = malloc(sizeof(package));  // Answer to the request.
    if (malloc_error(to_send) == 1){
        return 0;
    }
    if (!(*connected)){  // Server isn't currently holding any connection.
        // Creating new connection,
        *sess_id = prot->session_id;
        *unpack = prot->length;
        if (prot->protocol == 3){
            *udpr = true;
        }
        *udpr = prot->protocol;
        create_pack(to_send, 2, prot->session_id, 0, 0, 0, 0);  // CONNACC
        if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sending assent for connection.
            fprintf(stderr, "ERROR: Couldn't connect with the client.\n");
            return 1;  // Disconnect user.
        }
        return 2;  // Connected to new client.
    }
    else{  // Server was already connected with a client.
        // If connected client sent another 'CONN', and there are retransmissions.
        if (*sess_id == prot->session_id && *udpr){
            // Resend CONACC.
            create_pack(to_send, 2, prot->session_id, 0, 0, 0, 0);
            if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sending assent for connection.
                fprintf(stderr, "ERROR: Couldn't send another CONACC to current client.\n");
            }
        }
        else {  // Connected to the user using UDP.
            create_pack(to_send, 3, prot->session_id, 0, 0, 0, 0);  // CONNRJT
            if (send_pack(socket_fd, to_send, client_address, address_length) == 1){
                fprintf(stderr, "ERROR: Couldn't send CONNRJT.\n");
            }
            // If connected client sent another 'CONN', and there are no retransmissions.
            if (*sess_id == prot->session_id){
                fprintf(stderr, "ERROR: Connected client sent another CONN. \n");
                return 1;  // Disconnect user.
            }
            else{  // Different client wanted to connect.
                fprintf(stderr, "ERROR: There is a client already connected.\n");
            }
        }
    }
    return 0;
}


//  UDP server work.
int udp_server(uint16_t port, int socket_fd){
    unsigned long long sess_id = 0;  // Session ID of currently connected client.
    unsigned long long unpack = 0;  // Number of bites left to receive.
    unsigned long long last = 0;  // ID of last received package.
    unsigned long long trials = 0;
    struct sockaddr_in client;
    bool connected = false;  // User connected.
    bool udpr = false;  // User uses UDPR.

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
        if (malloc_error(buff) == 0){
            struct sockaddr_in client_address;
            socklen_t address_length = (socklen_t) sizeof(client_address);
            ssize_t received_length = recvfrom(socket_fd, buff, sizeof(package) + MAX_MSG, 0,
                                               (struct sockaddr *) &client_address, &address_length);
            if (received_length < 0) {
                // If there is udpr client connected but no message has been received in 'MAX_WAIT' seconds.
                if (errno == EAGAIN && udpr){
                    if (trials < MAX_RETRANSMITS){
                        trials++;  // Another trial.
                        package *to_send = malloc(sizeof(package));
                        if (last > 0){  // Some data received.
                            // Resending ACC.
                            create_pack(to_send, 5, sess_id, 0, 0, last - 1, 0);
                            if (send_pack(socket_fd, to_send, client, sizeof(client))){
                                return 1;
                            }
                        }
                        // No data received yet.
                        else{
                            // Resending CONACC.
                            create_pack(to_send, 2, sess_id, 0, 0, 0, 0);
                            if (send_pack(socket_fd, to_send, client, sizeof(client))){
                                return 1;
                            }
                        }
                    }
                    else{
                        fprintf(stderr, "ERROR: Message timeout.\n");
                        to_default(&last, &udpr, &trials, &connected);
                    }

                }
                // If there was an error receiving the message.
                else if (errno != EAGAIN){
                    fprintf(stderr, "ERROR: Couldn't receive message.\n");
                    if (!udpr && connected){  // If client wasn't using UDP.
                        to_default(&last, &udpr, &trials, &connected);  // Disconnect.
                    }
                }
                // User with no UDP timeout.
                else if (errno == EAGAIN && connected){
                    fprintf(stderr, "ERROR: Message timeout.\n");
                    to_default(&last, &udpr, &trials, &connected);
                }
            }
            else{  // Got message.
                package *prot = malloc(sizeof(package));  // Package information.
                if (malloc_error(prot) == 0){
                    memcpy(prot, buff, sizeof(package));
                    if (udpr && sess_id == prot->session_id){  // Received message from connected UDPR user.
                        trials = 0;
                    }
                    if (prot->id == 1){  // CONN
                        int conn = CONN_handler(&sess_id, &unpack, &udpr, prot, socket_fd, client_address, address_length, &connected);
                        if (conn == 1){  // CONN error.
                            fprintf(stderr, "ERROR: Ending connection with current client.\n");
                            to_default(&last, &udpr, &trials, &connected);
                        }
                        else if (conn == 2){  // New user connected.
                            client = client_address;
                            connected = true;
                        }
                    }
                    else if (connected && prot->id == 4 && prot->session_id == sess_id){  // DATA and correct session ID.
                        char* msg = malloc(prot->bit_len);
                        if (malloc_error(msg) == 0){
                            memcpy(msg, buff + sizeof(package), prot->bit_len);
                            int data_code = DATA_handler(&unpack, &last, &udpr, &connected, &trials, prot, socket_fd, client_address, address_length, msg);
                            if (data_code != 0){
                                if (!udpr){
                                    fprintf(stderr, "ERROR: Ending connection with current client.\n");
                                    to_default(&last, &udpr, &trials, &connected);
                                }
                                else if (data_code == 1){
                                    fprintf(stderr, "ERROR: Couldn't receive next part of message.\n");
                                }
                                else if (data_code == 2){
                                    fprintf(stderr, "ERROR: Couldn't send ACC\n");
                                }
                                else{
                                    fprintf(stderr, "ERROR: Couldn't send RECV\n");
                                }
                            }
                            free(msg);
                        }
                        else {
                            fprintf(stderr, "ERROR: Couldn't allocate the memory for message.\n");
                        }
                    }
                    else if (connected && sess_id == prot->session_id){  // Currently connected client with wrong ID package.
                            to_default(&last, &udpr, &trials, &connected);  // Disconnecting user.
                    }
                    free(prot);
                }
                else {
                    fprintf(stderr, "ERROR: Couldn't allocate memory to get new protocol.\n");
                }
            }
            free(buff);
        }
        else{
            fprintf(stderr, "ERROR: Couldn't allocate memory to get new data.\n");
            return 1;
        }
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