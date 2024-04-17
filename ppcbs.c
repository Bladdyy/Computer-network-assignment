#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "common.h"
#include "protconst.h"

// Ends connection with current client.
// Sets 'session ID', 'Bites to read' and 'last read package' to default values.
void to_default(uint64_t *last, bool *udpr, uint64_t *trials, bool *connected){
    *last = 0;
    *udpr = false;
    *connected = false;
    *trials = 0;
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
int DATA_handler(uint64_t *unpack, uint64_t *last, bool *udpr, bool *connected, uint64_t *trials, package *prot,
                  int socket_fd, struct sockaddr_in client_address, socklen_t address_length, void* msg){
    package *to_send = malloc(sizeof(package));
    if (malloc_error(to_send) == 1){
        return 1;
    }
    uint64_t pack_id = be64toh(prot->pack_id);
    // Checks if package's ID is correct.
    if ((*last < pack_id && *udpr) || (*last != pack_id && !(*udpr))){
        fprintf(stderr, "ERROR: Client sent a package with wrong ID.\n");
        create_pack(to_send, 6, prot->session_id, 0, 0, pack_id,0);  // RJT
        to_default(last, udpr, trials, connected);  // Ends connection with client, so parameters are being set to default values.
        if (send_pack(socket_fd, to_send, client_address, address_length) == 1){ // Sends RJT.
            fprintf(stderr, "ERROR: Couldn't sent RJT.\n");
        }
    }
    // Retransmission of ACC.
    else if (*last > pack_id && *udpr){
        create_pack(to_send, 5, prot->session_id, 0, 0, pack_id, 0); // ACC
        if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sends ACC.
            fprintf(stderr, "ERROR: Couldn't send ACC\n");
        }
    }
    else{  // Protocol is correct.
        uint32_t byte_len = be32toh(prot->byte_len);
        if (write(STDOUT_FILENO, msg, byte_len) < 0){   // Writing message to stdout.
            to_default(last, udpr, trials, connected);
            free(to_send);
            fprintf(stderr, "ERROR: Couldn't write message. Disconnecting client.\n");
            return 1;
        }
        *unpack -= byte_len;  // Reduces the number of bites to read in the future.
        *last = *last + 1;  // Next package ID update.

        if (*udpr){  // Sending ACC.
            *trials = 0;
            create_pack(to_send, 5, prot->session_id, 0, 0, pack_id, 0); // ACC
            if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sends ACC.
                fprintf(stderr, "ERROR: Couldn't send ACC\n");
            }
        }
        if (*unpack == 0){  // If whole message is read.
            create_pack(to_send, 7, prot->session_id, 0, 0, 0, 0);  // RCVD
            to_default(last, udpr, trials, connected);  // Ends connection with client, so parameters are being set to default values.
            if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sends RCVD.
                fprintf(stderr, "ERROR: Couldn't send RECV\n");
            }
        }
    }
    free(to_send);
    return 0;
}


// Handles 'CONN' packages.
// 'sess_id' - ID current connection with client, 'unpack' - number of bites left to recieve from all packages,
// 'last' - ID of last received package, 'prot' - received package with information about request to connect.
int CONN_handler(uint64_t *sess_id, uint64_t *unpack, bool *udpr, package *prot, int socket_fd, struct sockaddr_in client_address, socklen_t address_length, bool *connected){
    package *to_send = malloc(sizeof(package));  // Answer to the request.
    if (malloc_error(to_send) == 1){
        return 0;
    }
    if (!(*connected)){  // Server isn't currently holding any connection.
        // Creating new connection,
        *sess_id = prot->session_id;
        *unpack = be64toh(prot->length);
        if (prot->protocol == 3){
            *udpr = true;
        }
        else if (prot->protocol != 2){  // Not UDP/UDPr.
            fprintf(stderr, "ERROR: Client tried to connect using wrong protocol.\n");
            free(to_send);
            return 1;
        }
        create_pack(to_send, 2, prot->session_id, 0, 0, 0, 0);  // CONNACC
        if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sending assent for connection.
            fprintf(stderr, "ERROR: Couldn't connect with the client.\n");
            free(to_send);
            return 1;  // Disconnect user.
        }
        free(to_send);
        return 2;  // Connected to new client.
    }
    else{  // Server was already connected with a client.
        if (*sess_id != prot->session_id){
            fprintf(stderr, "ERROR: Another client tried to connect.\n");
            create_pack(to_send, 3, prot->session_id, 0, 0, 0, 0);  // CONRJT.
            if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sending CONRJT.
                fprintf(stderr, "ERROR: Couldn't send another CONRJT that client.\n");
            }
        }
        // If connected client sent another 'CONN', and there are retransmissions.
        else if (*udpr){
            // Resend CONACC.
            create_pack(to_send, 2, prot->session_id, 0, 0, 0, 0);
            if (send_pack(socket_fd, to_send, client_address, address_length) == 1){  // Sending assent for connection.
                fprintf(stderr, "ERROR: Couldn't send another CONACC to current client.\n");
            }
        }
        else {  // Connected to the user using UDP.
            fprintf(stderr, "ERROR: Connected client sent another CONN. \n");
            create_pack(to_send, 3, prot->session_id, 0, 0, 0, 0);  // CONRJT
            if (send_pack(socket_fd, to_send, client_address, address_length) == 1){
                fprintf(stderr, "ERROR: Couldn't send CONRJT.\n");
            }
            free(to_send);
            return 1;
        }
    }
    free(to_send);
    return 0;
}


//  UDP server lifetime.
int udp_server(int socket_fd){
    uint64_t sess_id = 0;  // Session ID of currently connected client.
    uint64_t unpack = 0;  // Number of bites left to receive.
    uint64_t last = 0;  // ID of last received package.
    uint64_t trials = 0;
    struct sockaddr_in client;
    bool connected = false;  // User connected.
    bool udpr = false;  // User uses UDPR.

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
                if (udpr && errno == EAGAIN){
                    if (trials < MAX_RETRANSMITS){
                        trials++;  // Another trial.
                        package *to_send = malloc(sizeof(package));
                        if (malloc_error(to_send) == 1){
                            to_default(&last, &udpr, &trials, &connected);
                        }
                        else if (last > 0){  // Some data received.
                            // Resending ACC.
                            create_pack(to_send, 5, sess_id, 0, 0, last - 1, 0);
                            if (send_pack(socket_fd, to_send, client, sizeof(client))){
                                fprintf(stderr, "ERROR: Couldn't resend ACC.\n");
                            }
                        }
                        // No data received yet.
                        else{
                            // Resending CONACC.
                            create_pack(to_send, 2, sess_id, 0, 0, 0, 0);
                            if (send_pack(socket_fd, to_send, client, sizeof(client))){
                                fprintf(stderr, "ERROR: Couldn't resend CONNACC.\n");
                            }
                        }
                        free(to_send);
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
                        uint32_t byte_len = be32toh(prot->byte_len);
                        char* msg = malloc(byte_len);
                        if (malloc_error(msg) == 0){
                            memcpy(msg, buff + sizeof(package), byte_len);
                            DATA_handler(&unpack, &last, &udpr, &connected, &trials, prot, socket_fd, client_address, address_length, msg);
                            free(msg);
                        }
                    }
                    else if (!connected || prot->session_id != sess_id){
                        fprintf(stderr, "ERROR: Not connected user tried to send a package.\n");
                    }
                    else{  // Currently connected client with wrong ID package.
                        fprintf(stderr, "ERROR: Currently connected client sent package with incorrect ID.\n");
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


// Disconnecting currently connected user from the server.
void tcp_disconnect(bool *connected, bool *conacc, uint64_t *pack_id, int socket_fd){
    *connected = false;
    *conacc = false;
    *pack_id = 0;
    close(socket_fd);
}


// Reads data.
int tcp_data(int socket_fd, uint32_t len, uint64_t *size){
    char* buffer = malloc(len);
    if (malloc_error(buffer) == 1){
        return 1;
    }
    if (tcp_read(socket_fd, buffer, len) == 1){  // Gets data to write.
        free(buffer);
        return 1;
    }
    if (write(STDOUT_FILENO, buffer, len) < 0){  // Writes data on stdout.
        free(buffer);
        fprintf(stderr, "ERROR: Couldn't write received message.\n");
        return 1;
    }
    *size -= len;  // Lessens size of data to read.
    free(buffer);
    return 0;
}


// Handles getting new packages. Size is a pointer to size left of message.
// Demanded is the ID of package that server wants to recieve. Pack_id is the id of next package with data to recieve.
int tcp_handle(int socket_fd, uint64_t *sess_id, uint64_t *size, int demanded, uint64_t pack_id){
    package *pack = malloc(sizeof(package));
    if (malloc_error(pack) == -1){
        return 1;
    }
    if (tcp_read(socket_fd, pack, sizeof(package)) == 1){  // Reads new package information.
        free(pack);
        return 1;
    }
    // Saves information.
    uint8_t id = pack->id;
    uint64_t session = pack->session_id;
    uint64_t current = be64toh(pack->pack_id);
    uint64_t length = be64toh(pack->length);
    uint8_t prot = pack->protocol;
    uint32_t byte_len = be32toh(pack->byte_len);
    free(pack);

    if (demanded == id){  // ID of received package matches demanded ID.
        if (demanded == 1){  // CONN.
            if (prot != 3){  // Not TCP.
                fprintf(stderr, "ERROR: Wrong protocol.\n");
            }
            else{  // Connected new user succesfully.
                *sess_id = session;
                *size = length;
            }
        }
        else if (*sess_id == session && pack_id == current){  // DATA.
            if (tcp_data(socket_fd, byte_len, size) == 1){  // Handles newly received data.
                return 1;
            }
            return 0;
        }
        else{  // DATA but with wrong parameters.
            if (*sess_id != session){  // Incorrect session ID.
                fprintf(stderr, "ERROR: Wrong session id in DATA package.\n");
            }
            else{  // Incorrect pack ID.
                fprintf(stderr, "ERROR: Wrong data id in DATA package.\n");
            }
            package *send_to = malloc(sizeof(package));
            if (malloc_error(send_to) == 0){
                create_pack(send_to, 6, *sess_id, 0, 0, pack_id, 0);  // RJT.
                if (tcp_write(socket_fd, send_to, sizeof(package)) == 1){
                    fprintf(stderr, "ERROR: Couldn't send RJT.\n");
                }
                free(send_to);
            }
            return 1;
        }
    }
    else{  // ID doesn't match demanded ID.
        fprintf(stderr, "ERROR: Wrong package id.\n");
        return 1;
    }

    return 0;
}



// TCP server lifetime.
int tcp_server(int socket_fd){
    bool connected = false;          // Connected to any user.
    bool conacc = false;             // Accepted connection from connected user.
    int client_fd;                   // Connected user's socket.
    uint64_t size;         // Size of client's message.
    uint64_t sess_id;      // Client's session ID.
    uint64_t pack_id = 0;  // ID of next package.
    for (;;){
        if (!connected){  // Connecting with the new client.
            struct sockaddr_in client_address;
            client_fd = accept(socket_fd, (struct sockaddr*) &client_address, &((socklen_t){sizeof(client_address)}));
            if (client_fd < 0){
                fprintf(stderr, "ERROR: Couldn't connect with the client.\n");
            }
            else{
                connected = true;

                // Setting timeout on the socket.
                struct timeval timeout;
                timeout.tv_sec = MAX_WAIT;
                timeout.tv_usec = 0;
                if (setsockopt (client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) < 0){
                    tcp_disconnect(&connected, &conacc, &pack_id, client_fd);
                    fprintf(stderr, "ERROR: Couldn't set timeout on the socket.\n");
                }
            }
        }
        else{  // User connected to server.
            if (!conacc){  // User not permitted to send yet.
                int receive = tcp_handle(client_fd, &sess_id, &size, 1, 0);  // Receive CONN.
                if (receive == 1){  // Message receive problem.
                    tcp_disconnect(&connected, &conacc, &pack_id, client_fd);
                }
                else{  // CONN received.
                    package *to_send = malloc(sizeof(package));
                    if (malloc_error(to_send) == 1){
                        tcp_disconnect(&connected, &conacc, &pack_id, client_fd);
                    }
                    else{
                        conacc = true;
                        create_pack(to_send, 2, sess_id, 0, 0, 0, 0);
                        if (tcp_write(client_fd, to_send, sizeof(package)) == 1){  // Send CONACC.
                            tcp_disconnect(&connected, &conacc, &pack_id, client_fd);
                            fprintf(stderr, "ERROR: Couldn't send CONACC\n");
                        }
                        free(to_send);
                    }
                }
            }
            else{  // User permitted to send.
                int read = tcp_handle(client_fd, &sess_id, &size, 4, pack_id);  // Receive new data.
                if (read == 1) {  // Message receive problem.
                    tcp_disconnect(&connected, &conacc, &pack_id, client_fd);
                }
                else{  // Message got.
                    pack_id++;
                    if (size == 0){  // If whole message was read.
                        package *to_send = malloc(sizeof(package));
                        if (malloc_error(to_send) == 0){
                            create_pack(to_send, 7, sess_id, 0, 0, 0, 0);   // RCVD.
                            int write = tcp_write(client_fd, to_send, sizeof(package));  // Send RCVD.
                            free(to_send);
                            if (write == 1){
                                fprintf(stderr, "ERROR: Couldn't send recv\n");
                            }
                        }
                        tcp_disconnect(&connected, &conacc, &pack_id, client_fd);
                    }
                }
            }
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

    int sock;  // Setting for the socket.
    if (strcmp(protocol, "udp") == 0){  // UDP/UDPr
        sock = SOCK_DGRAM;
    }
    else{  // TCP
        sock = SOCK_STREAM;
    }

    // Create socket.
    int socket_fd = socket(AF_INET, sock, 0);
    if (socket_fd < 0) {  // There was an error creating a socket.
        fprintf(stderr,"ERROR: Couldn't create a socket\n");
        return 1;
    }

    // Bind the socket to an address.
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);

    // Binding.
    if (bind(socket_fd, (struct sockaddr *) &server_address, (socklen_t) sizeof(server_address)) < 0) {
        fprintf(stderr, "ERROR: Couldn't bind the socket.\n");
        return 1;
    }

    if (strcmp(protocol, "udp") == 0){  // Communication protocol is UDP.

        // Setting timeout on the socket.
        struct timeval timeout;
        timeout.tv_sec = MAX_WAIT;
        timeout.tv_usec = 0;
        if (setsockopt (socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) < 0){
            fprintf(stderr, "ERROR: Couldn't set timeout on the socket.\n");
        }

        // Setting up UDP server.
        if (udp_server(socket_fd) == 1){
            close(socket_fd);
            return 1;
        }
        close(socket_fd);
    }
    else if (strcmp(protocol, "tcp") == 0){
        // Listening.
        if (listen(socket_fd, MAX_QUEUE) < 0){
            fprintf(stderr, "ERROR: Couldn't listen.\n");
            return 1;
        }
        socklen_t length = (socklen_t) sizeof(server_address);
        if (getsockname(socket_fd, (struct sockaddr*) &server_address, &length) < 0){
            fprintf(stderr, "ERROR: Couldn't find the port to listen on.\n");
        }

        // Setting up TCP server.
        if (tcp_server(socket_fd) == 1){
            close(socket_fd);
            return 1;
        }
        close(socket_fd);
    }
    else{  // Wrong protocol.
        fprintf(stderr, "ERROR: Wrong protocol.\n");
        return 1;
    }

    return 0;
}