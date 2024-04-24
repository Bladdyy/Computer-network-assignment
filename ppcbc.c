#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include "common.h"
#include "protconst.h"


// Generates random session ID.
uint64_t gen_sess_id(){
    uint64_t sess = 0;
    srand(time(0));
    for (int i = 0; i < 64; i++){
        sess = sess * 2 + rand() % 2;
    }
    return sess;
}


// Calculates smaller value.
size_t min_msg(uint64_t len){
    if (len >= MAX_MSG){
        return MAX_MSG;
    }
    else{
        return len;
    }
}


// Creates server_address.
static struct sockaddr_in get_server_address(char const *host, uint16_t port, bool* error, int fam, int sock, int prot) {
    // Creating hints.
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = fam;
    hints.ai_socktype = sock;
    hints.ai_protocol = prot;

    // Getting address information.
    struct addrinfo *address_result;
    int errcode = getaddrinfo(host, NULL, &hints, &address_result);
    if (errcode != 0) {
        fprintf(stderr, "ERROR: Couldn't get address information.\n");
        *error = true;
    }

    // Updating socket address information.
    struct sockaddr_in send_address;
    send_address.sin_family = fam;
    send_address.sin_addr.s_addr =
            ((struct sockaddr_in *) (address_result->ai_addr))->sin_addr.s_addr;
    send_address.sin_port = htons(port); // Port from the command line
    freeaddrinfo(address_result);
    return send_address;
}


// Sends one package to server using UDP protocol.
int send_udp_pack(int socket_fd, int id, void *pack, size_t size, struct sockaddr_in server_address, char* msg, uint32_t msg_size, uint64_t pack_id){
    int code = 0;  // Return code.
    void* buffer;
    if (msg == NULL){  // No message, only package.
        buffer = malloc(size + sizeof(uint8_t));  // Buffer with package and message.
    }
    else{
        buffer = malloc(size + msg_size + sizeof(uint8_t));
    }
    if (malloc_error(buffer) == 1){
        return -1;
    }
    memcpy(buffer, &id, sizeof(uint8_t));
    memcpy(buffer + sizeof(uint8_t), pack, size);
    // Sending some part of message.
    if (msg != NULL){
        memcpy(buffer + sizeof(uint8_t) + size, msg + MAX_MSG * pack_id, msg_size);
    }

    // Sending buffer to server.
    ssize_t sent_length = sendto(socket_fd, buffer, sizeof(uint8_t) + size + msg_size, 0,
                                 (struct sockaddr *) &server_address, sizeof(server_address));

    if (sent_length < 0) {  // Couldn't send.
        code = 1;
    }
    else if ((size_t) sent_length != sizeof(uint8_t) + size + msg_size) {  // Couldn't send fully
        fprintf(stderr, "ERROR: Package was sent incompletely.\n");
    }
    free(buffer);
    return code;
}


// Receives package using UDP protocol.
int recv_udp_prot(int socket_fd, uint64_t sess_id){
    static char back[sizeof(data_msg)];
    struct sockaddr_in receive_address;
    socklen_t address_length = (socklen_t) sizeof(receive_address);
    ssize_t received_length = recvfrom(socket_fd, back, sizeof(data_msg), 0,
                                       (struct sockaddr *) &receive_address, &address_length);
    uint8_t id;
    uint64_t sess;
    memcpy(&id, back, sizeof(uint8_t));
    memcpy(&sess, back + sizeof(uint8_t), sizeof(uint64_t));
    if (received_length < 0){  // No message received.
        if (errno == EAGAIN){  // Timeout.
            return -4;
        }
        else{  // Error while reading message.
            fprintf(stderr, "ERROR: Couldn't receive message.\n");
            return -2;
        }
    }
    else if (sess_id != sess) {  // Session ID of message is not equal to client session ID.
        fprintf(stderr, "ERROR: Received message has wrong session ID\n");
        return -3;
    }
    return id;
}


// Receives ACC. Sets past accepts ID's to '2'.
int recv_ACC(int socket_fd, uint64_t sess_id, uint64_t pack_id){
    static char back[sizeof(data_msg)];  // Allocating space for new package.
    struct sockaddr_in receive_address;
    socklen_t address_length = (socklen_t) sizeof(receive_address);
    ssize_t received_length = recvfrom(socket_fd, back, sizeof(data_msg), 0,
                                       (struct sockaddr *) &receive_address, &address_length);
    uint8_t id;
    uint64_t sess;
    memcpy(&id, back, sizeof(uint8_t));
    memcpy(&sess, back + sizeof(uint8_t), sizeof(uint64_t));
    if (received_length < 0){  // No message received.
        if (errno == EAGAIN){  // Timeout.
            return -4;
        }
        else{  // Wrong message.
            return -2;
        }
    }
    else if (sess_id != sess) {  // Session ID of message is not equal to client session ID.
        fprintf(stderr, "ERROR: Received message has wrong session ID\n");
        return -3;
    }
    else if (id == 5){  // ACC received.
        uint64_t pack;
        memcpy(&pack, back + sizeof(uint8_t) + sizeof(uint64_t), sizeof(uint64_t));
        pack = be64toh(pack);
        if (pack_id > pack){  // Old ACC.
            return 2;
        }
        else if (pack_id < pack){  // Incorrect ACC.
            return -1;
        }
    }
    return id;
}


//  Tries to receive ACC.
int get_ACC(int socket_fd, uint64_t sess_id, uint64_t pack_id, uint32_t byte_len, data_msg *data, struct sockaddr_in server_address, char* msg){
    int back_id = recv_ACC(socket_fd, sess_id, pack_id);
    uint64_t trial = 0;
    while ((back_id == -4 || back_id == 2) && trial < MAX_RETRANSMITS){  // While receives past accepts or timeouts.
        if (back_id == -4){  // Timeout.
            trial++;
            // Retransmits.
            if (send_udp_pack(socket_fd, 4, data, sizeof(data_msg), server_address, msg, byte_len, pack_id) == 1){
                return 1;
            }
        }
        // Tries to receive ACC again.
        back_id = recv_ACC(socket_fd, sess_id, pack_id);
    }
    if (back_id == -4){  // Too many timeouts.
        fprintf(stderr, "ERROR: Too many message timeouts.\n");
        return 1;
    }
    else if (back_id < 0){  // Incorrect package.
        fprintf(stderr, "ERROR: Received message is incorrect.\n");
        return 1;
    }
    else if (back_id != 5){  // Correct package, but not ACC.
        fprintf(stderr, "ERROR: Received message has wrong package ID.\n");
        return 1;
    }
    return 0;
}


// Sends packages of data to server using UDP protocol.
int udp_conn(char* msg, uint64_t len, int socket_fd, struct sockaddr_in server_address, uint64_t sess_id, bool udpr){
    // Creating 'CONN' package.
    conn pack;
    if (udpr){
        create_conn(&pack, sess_id, 3, len);  // CONN UDPR.
    }
    else{
        create_conn(&pack, sess_id, 2, len);  // CONN UDP.
    }

    // Sending 'CONN' package.
    if (send_udp_pack(socket_fd,1, &pack, sizeof(conn), server_address, NULL, 0, 0) == 1) {
        return 1;
    }

    // Receive a message.
    uint64_t trial = 0;
    int back_id = recv_udp_prot(socket_fd, sess_id);

    // Retransmissions.
    while (back_id == -4 && udpr && trial < MAX_RETRANSMITS){
        if (send_udp_pack(socket_fd, 1, &pack, sizeof(conn), server_address, NULL, 0, 0) == 1) {
            return 1;
        }
        back_id = recv_udp_prot(socket_fd, sess_id);
        trial++;
    }

    if (back_id == 3){  // Received 'CONRJT'.
        fprintf(stderr, "ERROR: Couldn't connect with the server.\n");
        return 1;
    }
    else if (back_id == 2){  // Received 'CONACC'.
        uint64_t pack_id = 0;
        while (len != 0){  // Sending 'DATA" packages.
            data_msg data_pack;
            uint32_t byte_len = min_msg(len);
            create_data(&data_pack, sess_id, pack_id, byte_len);
            // Tries sending part of the message.
            if (send_udp_pack(socket_fd, 4, &data_pack, sizeof(data_msg), server_address, msg, byte_len, pack_id) == 1){
                return 1;
            }
            // Retransmissions.
            if (udpr && get_ACC(socket_fd, sess_id, pack_id, byte_len, &data_pack, server_address, msg) == 1){
                return 1;
            }
            len -= byte_len;
            pack_id++;

        }
        int recv;
        do{
            recv = recv_ACC(socket_fd, sess_id, pack_id);
        } while (recv == 2 && udpr);  // Receiving past accepts.
        if (recv == -4){
            fprintf(stderr, "ERROR: Message timeout. Didn't get RECV.\n");
            return 1;
        }
        else if (recv != 7){  // Waiting for 'RCVD' after sending all 'DATA'.
            fprintf(stderr, "ERROR: Didn't get RECV.\n");
            return 1;
        }
    }
    else if (back_id == -4){
        fprintf(stderr, "ERROR: Message timeout.\n");
        return 1;
    }
    else if (back_id >= 0){  //  Received other code.
        fprintf(stderr, "ERROR: Received message has wrong package ID.\n");
        return 1;
    }
    else{  // Error while receiving the message.
        fprintf(stderr, "ERROR: Received message is incorrect.\n");
        return 1;
    }
    return 0;
}


// Reads and validates new protocol. Returns ID of validated protocol.
int tcp_read_prot(int socket_fd, uint64_t sess_id){
    static char pack[sizeof(uint8_t) + sizeof(base)];
    if (tcp_read(socket_fd, pack, sizeof(uint8_t) + sizeof(base)) == 1){
        return -1;
    }
    uint64_t session;
    uint8_t id;
    memcpy(&id, pack, sizeof(uint8_t));
    memcpy(&session, pack + sizeof(uint8_t), sizeof(uint64_t));
    if (sess_id != session) {  // Checking session ID.
        fprintf(stderr, "ERROR: Message with wrong session ID\n");
        return -1;
    }
    return id;
}


// Sends packages of data using TCP protocol.
int tcp_conn(char *msg, uint64_t len, int socket_fd, uint64_t sess_id){
    static char data[sizeof(uint8_t) + sizeof(conn)];
    uint8_t id = 1;
    conn pack;
    create_conn(&pack, sess_id, 3, len);  // CONN.
    memcpy(data, &id, sizeof(uint8_t));
    memcpy(data + sizeof(uint8_t), &pack, sizeof(conn));
    if (tcp_write(socket_fd, data, sizeof(uint8_t) + sizeof(conn)) == 1){  // Sending CONN.
        fprintf(stderr, "ERROR: Couldn't send message.\n");
        return 1;
    }
    int read = tcp_read_prot(socket_fd, sess_id);  // Receiving CONNACC.
    if (read == -1){  // Message receive problem.
        return 1;
    }
    else if (read != 2){  // Received ID doesn't match CONACC.
        fprintf(stderr, "ERROR: Wrong package ID, didn't receive CONNACC.\n");
        return 1;
    }

    id = 4;
    data_msg data_pack;  // DATA.
    uint32_t max_size = min_msg(len);
    void* buffer = malloc(sizeof(uint8_t) + sizeof(data_msg) + max_size);  // DATA + message.
    if (malloc_error(buffer) == 1){
        return 1;
    }
    uint64_t pack_id = 0;
    while (len != 0){  // Sending whole package in portions
        uint32_t byte_len = min_msg(len);
        create_data(&data_pack, sess_id, pack_id, byte_len);     // Creating new package of data.
        memset(buffer,0, sizeof(uint8_t) + sizeof(data_msg) + max_size);
        memcpy(buffer, &id, sizeof(uint8_t));
        memcpy(buffer + sizeof(uint8_t), &data_pack, sizeof(data_msg));       // Copying new package to buffer.
        memcpy(buffer + sizeof(uint8_t) + sizeof(data_msg), msg + pack_id * MAX_MSG, byte_len);  // Copying message to buffer.
        if (tcp_write(socket_fd, buffer, sizeof(uint8_t) + sizeof(data_msg) + byte_len) == 1){      // Sending DATA + message.
            fprintf(stderr, "ERROR: Couldn't send message.\n");
            free(buffer);
            return 1;
        }
        len -= byte_len;        // Bytes sent.
        pack_id++;              // Next pack.
    }
    free(buffer);
    read = tcp_read_prot(socket_fd, sess_id);  // Read RCVD.
    if (read == -1){  // Message receive problem.
        return 1;
    }
    else if (read != 7){  // Received ID doesn't match RCVD.
        fprintf(stderr, "ERROR: Wrong package ID, didn't receive RCVD.\n");
        return 1;
    }
    return 0;
}


// Reads stdin data. If successful sends data to server using established protocol.
// Function demands 3 arguments, communication protocol, server id and port id.
int main(int argc, char *argv[]) {
    if (argc != 4) {  // Checks for 3 arguments.
        fprintf(stderr, "ERROR: Expected arguments: %s <communication protocol> <server id> <port>\n", argv[0]);
        return 1;
    }
    char const *protocol = argv[1];  // Communication protocol.
    char const *host = argv[2];  // Server id.
    bool error = false;
    uint16_t port = read_port(argv[3], &error);
    if (error){  // There was an error getting port.
        return 1;
    }

    char* msg = NULL;  // Whole message.
    size_t size = 0;
    // Tries to read message.
    long long code = getdelim(&msg, &size, EOF, stdin);
    if (code == -1) {  // Error while reading message.
        free(msg);
        fprintf(stderr, "Couldn't read message.\n");
        return 1;
    }
    if (code == 0){  // Empty message.
        free(msg);
        fprintf(stderr, "Empty message won't be send.\n");
        return 1;
    }

    uint64_t sess_id = gen_sess_id();  // Generating session id.
    int sock, prot;  // Protocol settings.
    if (strcmp(protocol, "tcp") == 0){  // TCP.
        sock = SOCK_STREAM;
        prot = IPPROTO_TCP;
    }
    else{  // UDP/UDPr.
        sock = SOCK_DGRAM;
        prot = IPPROTO_UDP;
    }
    error = false;
    struct sockaddr_in server_address = get_server_address(host, port, &error, AF_INET, sock, prot);
    if (error){  // There was an error getting server address.
        free(msg);
        return 1;
    }

    int socket_fd = socket(AF_INET, sock, 0);
    if (socket_fd < 0) {  // There was an error creating a socket.
        fprintf(stderr,"ERROR: Couldn't create a socket\n");
        free(msg);
        return 1;
    }

    // Setting timeout on the socket.
    struct timeval timeout;
    timeout.tv_sec = MAX_WAIT;
    timeout.tv_usec = 0;
    if (setsockopt (socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) < 0){
        fprintf(stderr, "ERROR: Couldn't set timeout on the socket.\n");
        return 1;
    }

    if (strcmp(protocol, "udp") == 0 || strcmp(protocol, "udpr") == 0){  // Sending the message using UDP protocol.
        bool udpr = false;  // Allows retransmissions.
        if (strcmp(protocol, "udpr") == 0){
            udpr = true;
        }
        // Sending messages to the server.
        if (udp_conn(msg, code, socket_fd, server_address, sess_id, udpr) == 1){
            free(msg);
            close(socket_fd);
            return 1;
        }
        close(socket_fd);
    }
    else if (strcmp(protocol, "tcp") == 0){
        // Connecting to the server.
        if (connect(socket_fd, (struct sockaddr *) &server_address, (socklen_t) sizeof(server_address)) < 0) {
            free(msg);
            fprintf(stderr, "ERROR: Couldn't connect to the server.");
            return 1;
        }
        // Sending message to the server.
        if (tcp_conn(msg, code, socket_fd, sess_id) == 1){
            free(msg);
            close(socket_fd);
            return 1;
        }
        close(socket_fd);
    }
    else{
        free(msg);
        fprintf(stderr, "ERROR: Wrong protocol.\n");
        return 1;
    }
    free(msg);
    return 0;
}
