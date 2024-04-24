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
int send_pack(uint8_t id, int socket_fd, void *to_send, size_t size, struct sockaddr_in client_address, socklen_t address_length){
    int code = 0;
    void *buffer = malloc(size + sizeof(uint8_t));
    if (malloc_error(buffer) == 1){
        return 1;
    }
    memcpy(buffer, &id, sizeof(uint8_t));
    memcpy(buffer + sizeof(uint8_t), to_send, size);
    ssize_t sent_length = sendto(socket_fd, buffer, size + sizeof(uint8_t), 0,
                                 (struct sockaddr *) &client_address, address_length);

    if (sent_length < 0) {  // Couldn't send.
        fprintf(stderr, "ERROR: Couldn't send Package.\n");
        code = 1;
    }
    else if ((size_t) sent_length != size + sizeof(uint8_t)) {  // Couldn't send fully
        fprintf(stderr, "ERROR: Package was sent incompletely.\n");
        code = 1;
    }
    free(buffer);
    return code;
}


// Handles 'DATA' packages.
// 'sess_id' - ID current connection with client, 'unpack' - number of bites left to recieve from all packages,
// 'last' - ID of last received package, 'prot' - received package with information about 'msg', 'msg' - received bites.
// ACC send and check other things with retransmissions in server.
int DATA_handler(void* msg, uint64_t *unpack, uint64_t *last, bool *udpr, bool *connected, uint64_t *trials, data_msg prot,
                  int socket_fd, struct sockaddr_in client_address, socklen_t address_length){
    status to_send;
    // Checks if package's ID is correct.
    if ((*last < prot.pack_id && *udpr) || (*last != prot.pack_id && !(*udpr))){
        fprintf(stderr, "ERROR: Client sent a package with wrong ID.\n");
        create_status(&to_send, prot.session_id, prot.pack_id);  // RJT
        to_default(last, udpr, trials, connected);  // Ends connection with client, so parameters are being set to default values.
        if (send_pack(6, socket_fd, &to_send, sizeof(status), client_address, address_length) == 1){ // Sends RJT.
            fprintf(stderr, "ERROR: Couldn't sent RJT.\n");
        }
    }
    else if (!(*last > prot.pack_id && *udpr)){  // Protocol is correct.
        if (write(STDOUT_FILENO, msg, prot.byte_len) < 0){   // Writing message to stdout.
            fflush(stdout);
            to_default(last, udpr, trials, connected);
            fprintf(stderr, "ERROR: Couldn't write message. Disconnecting client.\n");
            return 1;
        }
        fflush(stdout);
        *unpack -= prot.byte_len;  // Reduces the number of bites to read in the future.
        *last = *last + 1;  // Next package ID update.

        if (*udpr){  // Sending ACC.
            *trials = 0;
            create_status(&to_send,  prot.session_id, prot.pack_id); // ACC
            if (send_pack(5, socket_fd, &to_send, sizeof(status), client_address, address_length) == 1){  // Sends ACC.
                fprintf(stderr, "ERROR: Couldn't send ACC\n");
            }
        }
        if (*unpack == 0){  // If whole message is read.
            base rcvd;
            create_base(&rcvd, prot.session_id);  // RCVD
            to_default(last, udpr, trials, connected);  // Ends connection with client, so parameters are being set to default values.
            if (send_pack(7, socket_fd, &rcvd, sizeof(base), client_address, address_length) == 1){  // Sends RCVD.
                fprintf(stderr, "ERROR: Couldn't send RECV\n");
            }
        }
    }
    return 0;
}


// Handles 'CONN' packages.
// 'sess_id' - ID current connection with client, 'unpack' - number of bites left to recieve from all packages,
// 'last' - ID of last received package, 'prot' - received package with information about request to connect.
int CONN_handler(conn recv, uint64_t *sess_id, uint64_t *unpack, bool *udpr, int socket_fd, struct sockaddr_in client_address, socklen_t address_length, bool *connected){
    base to_send;
    if (!(*connected)){  // Server isn't currently holding any connection.
        // Creating new connection,
        if (recv.protocol == 3){
            *udpr = true;
        }
        else if (recv.protocol != 2){  // Not UDP/UDPr.
            fprintf(stderr, "ERROR: Client tried to connect using wrong protocol.\n");
            return 1;
        }
        *sess_id = recv.session_id;
        *unpack = recv.length;
        create_base(&to_send, recv.session_id);  // CONNACC
        if (send_pack(2, socket_fd, &to_send, sizeof(base), client_address, address_length) == 1){  // Sending assent for connection.
            fprintf(stderr, "ERROR: Couldn't connect with the client.\n");
            return 1;  // Disconnect user.
        }
        return 2;  // Connected to new client.
    }
    else{  // Server was already connected with a client.
        if (*sess_id != recv.session_id){
            fprintf(stderr, "ERROR: Another client tried to connect.\n");
            create_base(&to_send, recv.session_id);  // CONRJT.
            if (send_pack(3, socket_fd, &to_send, sizeof(base), client_address, address_length) == 1){  // Sending CONRJT.
                fprintf(stderr, "ERROR: Couldn't send another CONRJT that client.\n");
            }
        }
        else if (!*udpr){  // Connected to the user using UDP.
            fprintf(stderr, "ERROR: Connected client sent another CONN. \n");
            create_base(&to_send, recv.session_id);  // CONRJT
            if (send_pack(3, socket_fd, &to_send, sizeof(base), client_address, address_length) == 1){  // Sending CONRJT.
                fprintf(stderr, "ERROR: Couldn't send CONRJT.\n");
            }
            return 1;
        }
    }
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
        static char buff[BUFFOR_SIZE + sizeof(data_msg)];  // Buffer for protocol ID.
        struct sockaddr_in client_address;
        socklen_t address_length = (socklen_t) sizeof(client_address);
        ssize_t received_length = recvfrom(socket_fd, buff, sizeof(data_msg) + BUFFOR_SIZE, 0,
                                           (struct sockaddr *) &client_address, &address_length);
        if (received_length < 0) {
            // If there is udpr client connected but no message has been received in 'MAX_WAIT' seconds.
            if (udpr && errno == EAGAIN){
                if (trials < MAX_RETRANSMITS){
                    trials++;  // Another trial.
                    if (last > 0){  // Some data received.
                        status to_send;  // ACC
                        create_status(&to_send, sess_id, last - 1);
                        if (send_pack(5, socket_fd, &to_send, sizeof(status), client, sizeof(client))){
                            fprintf(stderr, "ERROR: Couldn't resend ACC.\n");
                        }
                    }
                    else{  // No data received yet.
                        base to_send;
                        create_base(&to_send, sess_id);
                        if (send_pack(2, socket_fd, &to_send, sizeof(base), client, sizeof(client))){
                            fprintf(stderr, "ERROR: Couldn't resend CONNACC.\n");
                        }
                    }
                }
                else{  // Too many retransmissions.
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
            uint8_t id;
            memcpy(&id, buff, sizeof(uint8_t));
            if (id == 1){  // CONN
                conn received;
                memcpy(&received, buff + sizeof(uint8_t), sizeof(conn));
                received.session_id = received.session_id;
                received.length = be64toh(received.length);
                int conn = CONN_handler(received, &sess_id, &unpack, &udpr, socket_fd, client_address, address_length, &connected);

                if (conn == 1){  // CONN error.
                    fprintf(stderr, "ERROR: Ending connection with current client.\n");
                    to_default(&last, &udpr, &trials, &connected);
                }
                else if (conn == 2){  // New user connected.
                    client = client_address;
                    connected = true;
                }
            }
            else if (connected && id == 4){  // DATA.
                data_msg received;
                memcpy(&received, buff + sizeof(uint8_t), sizeof(data_msg));
                received.session_id = received.session_id;
                received.pack_id = be64toh(received.pack_id);
                received.byte_len = be32toh(received.byte_len);
                if (sess_id == received.session_id){
                    char* msg = malloc(received.byte_len);
                    if (malloc_error(msg) == 0){
                        memcpy(msg, buff + sizeof(data_msg) + sizeof(uint8_t), received.byte_len);
                        DATA_handler(msg, &unpack, &last, &udpr, &connected, &trials, received, socket_fd, client_address, address_length);
                        free(msg);
                    }
                }
                else{
                    fprintf(stderr, "ERROR: Different user tried to send package.\n");
                    status to_send;
                    create_status(&to_send, received.session_id, received.pack_id);  // RJT
                    if (send_pack(6, socket_fd, &to_send, sizeof(status), client_address, address_length) == 1){ // Sends RJT.
                        fprintf(stderr, "ERROR: Couldn't sent RJT.\n");
                    }
                }
            }
            else if (!connected){
                fprintf(stderr, "ERROR: Not connected user tried to send a package.\n");
            }
            else{  // Currently connected client with wrong ID package.
                fprintf(stderr, "ERROR: Currently connected client sent package with incorrect ID.\n");
                to_default(&last, &udpr, &trials, &connected);  // Disconnecting user.
            }
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
        fflush(stdout);
        free(buffer);
        fprintf(stderr, "ERROR: Couldn't write received message.\n");
        return 1;
    }
    fflush(stdout);
    *size -= len;  // Lessens size of data to read.
    free(buffer);
    return 0;
}


// Handles getting new packages. Size is a pointer to size left of message.
// Demanded is the ID of package that server wants to recieve. Pack_id is the id of next package with data to recieve.
int tcp_handle(int socket_fd, uint64_t *sess_id, uint64_t *size, uint8_t demanded, uint64_t pack_id){
    uint8_t pack;
    if (tcp_read(socket_fd, &pack, sizeof(uint8_t)) == 1){  // Reads new package information.
        return 1;
    }
    if (demanded == pack){  // ID of received package matches demanded ID.
        if (demanded == 1){  // CONN.
            conn received;
            if (tcp_read(socket_fd, &received, sizeof(conn)) == 1){  // Reads new package information.
                return 1;
            }
            if (received.protocol != 3){  // Not TCP.
                fprintf(stderr, "ERROR: Wrong protocol.\n");
                return 1;
            }
            else{  // Connected new user succesfully.
                *sess_id = received.session_id;
                *size = be64toh(received.length);
            }
        }
        else{  // DATA.
            data_msg received;
            if (tcp_read(socket_fd, &received, sizeof(data_msg)) == 1){  // Reads new package information.
                return 1;
            }
            received.pack_id = be64toh(received.pack_id);
            received.byte_len = be32toh(received.byte_len);
            if (*sess_id == received.session_id && pack_id == received.pack_id){
                if (tcp_data(socket_fd, received.byte_len, size) == 1){  // Handles newly received data.
                    return 1;
                }
                return 0;
            }
            else{  // DATA but with wrong parameters.
                if (*sess_id != received.session_id){  // Incorrect session ID.
                    fprintf(stderr, "ERROR: Wrong session id in DATA package.\n");
                }
                else{  // Incorrect pack ID.
                    fprintf(stderr, "ERROR: Wrong data id in DATA package.\n");
                }
                static char to_send[sizeof(uint8_t) + sizeof(status)];
                uint8_t id = 6;
                status rjt;
                create_status(&rjt, received.session_id, received.pack_id);
                memcpy(to_send, &id, sizeof(uint8_t));
                memcpy(to_send + sizeof(uint8_t), &rjt, sizeof(status));
                if (tcp_write(socket_fd, to_send, sizeof(uint8_t) + sizeof(status)) == 1){  // Send RJT
                    fprintf(stderr, "ERROR: Couldn't send RJT.\n");
                }
                return 1;
            }
        }
    }
    else{  // Received ID doesn't match demanded ID.
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
                    static char to_send[sizeof(uint8_t) + sizeof(base)];
                    conacc = true;
                    uint8_t id = 2;
                    base acc;
                    create_base(&acc, sess_id);
                    memcpy(to_send, &id, sizeof(uint8_t));
                    memcpy(to_send + sizeof(uint8_t), &acc, sizeof(base));
                    if (tcp_write(client_fd, to_send, sizeof(uint8_t) + sizeof(base)) == 1){  // Send CONACC.
                        tcp_disconnect(&connected, &conacc, &pack_id, client_fd);
                        fprintf(stderr, "ERROR: Couldn't send CONACC\n");
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
                        static char to_send[sizeof(uint8_t) + sizeof(base)];
                        uint8_t id = 7;
                        base rcvd;
                        create_base(&rcvd, sess_id);
                        memcpy(to_send, &id, sizeof(uint8_t));
                        memcpy(to_send + sizeof(uint8_t), &rcvd, sizeof(base));
                        int write = tcp_write(client_fd, to_send, sizeof(uint8_t) + sizeof(base));  // Send RCVD.
                        if (write == 1){
                            fprintf(stderr, "ERROR: Couldn't send recv\n");
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
    bool error = false;
    uint16_t port = read_port(argv[2], &error);
    if (error){  // There was an error getting port.
        return 1;
    }

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