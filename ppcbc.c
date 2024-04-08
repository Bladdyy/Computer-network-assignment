#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdbool.h>
#include <time.h>


// Max package size.
#define MAX_MSG 64000
#define MAX_WAIT 10
#define MAX_RETRANSMITS 3


// Stdin data storage.
typedef struct msg_list{
    char *msg;
    struct msg_list *next;
    unsigned int length;
} msg_list;


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


// Gets length of the data in 'msg_list'.
unsigned long long get_length(msg_list *core){
    unsigned long long len = 0;
    while (core != NULL){
        len += core->length;
        core = core->next;
    }
    return len;
}


// Generates random session ID.
unsigned long long gen_sess_id(){
    unsigned long long sess = 0;
    srand(time(0));
    for (int i = 0; i < 64; i++){
        sess = sess * 2 + rand() % 2;
    }
    return sess;
}


// Frees 'msg_list' structure.
void free_msg(msg_list* core){
    msg_list *prev;
    do {
        free(core->msg);
        prev = core;
        core = core->next;
        free(prev);
    } while (core != NULL);
}


// Checks if malloc allocated spacer on 'pointer'.
int malloc_error(void* pointer){
    if (pointer == NULL){  // If there was a problem allocating space.
        fprintf(stderr, "ERROR: Problem with allocation.\n");
        return 1;
    }
    return 0;
}


// Gets port number.
static uint16_t read_port(char const *string, bool *error) {
    char* end_ptr;
    unsigned long port = strtoul(string, &end_ptr, 10);
    if ((port == ULONG_MAX && errno == ERANGE) || *end_ptr != 0 || port == 0 || port > UINT16_MAX) {
        fprintf(stderr,"ERROR: %s is not a valid port number.\n", string);
        *error = true;
    }
    return (uint16_t) port;
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


// Saves data from standard input.
int get_stdin(msg_list* core){
    core->next = NULL;
    core->msg = malloc(sizeof(char) * MAX_MSG);
    core->length = 0;
    if (malloc_error(core->msg) == 1){
        return 1;
    }

    msg_list *act = core;  // Currently modified data buffer in message structure.
    char el;  // Read character.
    unsigned int length = 0;  // Number of characters written into current segment of data list.
    int res = (int) read(STDIN_FILENO, &el, 1);
    while (res > 0){  // As long as any character has been read, and it's not 'EOF'.
        (act->msg)[length] = el;  // Saving character in the buffer.
        length++;
        if (length == MAX_MSG - 1) {  // If current buffer is full.
            // Allocating new buffer.
            act->next = malloc(sizeof(msg_list));
            if (malloc_error(act->next) == 1){
                return 1;
            }
            act = act->next;
            act->next = NULL;
            act->msg = malloc(sizeof(char) * MAX_MSG);
            if (malloc_error(act->next) == 1){
                return 1;
            }
            act->length = length;
            length = 0;
        }
        res = (int) read(STDIN_FILENO, &el, 1);
    }
    if (res < 0){  // If there was an error while reading characters.
        fprintf(stderr, "ERROR: Problem %d while reading from stdin.\n", res);
        return 1;
    }
    else if (length > act->length){  // Length of data in the last node.
        act->length = length;
    }
    return 0;
}


// Sends one package to server using UDP protocol.
int send_udp_pack(int socket_fd, package *pack, struct sockaddr_in server_address, char* msg){
    int code = 0;  // Return code.
    void* buffer = malloc(sizeof(package) + pack->bit_len);  // Buffer with package and message.
    if (malloc_error(buffer) == 1){
        return -1;
    }

    // Copying data to buffer.
    memcpy(buffer, pack, sizeof(package));
    memcpy(buffer + sizeof(package), msg, pack->bit_len);

    // Sending buffer to server.
    ssize_t sent_length = sendto(socket_fd, buffer, sizeof(package) + pack->bit_len, 0,
                                 (struct sockaddr *) &server_address, sizeof(server_address));
    if (sent_length < 0) {  // Couldn't send.
        fprintf(stderr, "ERROR: Couldn't send Package with id %d.\n", pack->id);
        code = 1;
    }
    else if ((size_t) sent_length != sizeof(package) + pack->bit_len) {  // Couldn't send fully
        fprintf(stderr, "ERROR: Package with id %d was sent incompletely.\n", pack->id);
    }
    free(buffer);
    return code;
}


// Receives package using UDP protocol.
int recv_udp_prot(int socket_fd, unsigned long long sess_id){
    package *back = malloc(sizeof(package));  // Allocating space for new package.
    if (malloc_error(back) == 1){
        return -1;
    }
    struct sockaddr_in receive_address;
    socklen_t address_length = (socklen_t) sizeof(receive_address);
    ssize_t received_length = recvfrom(socket_fd, back, sizeof(package), 0,
                                       (struct sockaddr *) &receive_address, &address_length);
    unsigned char id = back->id;
    unsigned long long sess = back->session_id;
    free(back);
    if (received_length < 0){  // No message received.
        if (errno == EAGAIN){
            fprintf(stderr, "ERROR: Message timeout.\n");
            return -4;
        }
        else{
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
int recv_ACC(int socket_fd, unsigned long long sess_id, unsigned long long pack_id){
    package *back = malloc(sizeof(package));  // Allocating space for new package.
    if (malloc_error(back) == 1){
        return -1;
    }
    struct sockaddr_in receive_address;
    socklen_t address_length = (socklen_t) sizeof(receive_address);
    ssize_t received_length = recvfrom(socket_fd, back, sizeof(package), 0,
                                       (struct sockaddr *) &receive_address, &address_length);

    unsigned char id = back->id;
    unsigned long long sess = back->session_id;
    unsigned long long pack = back->pack_id;
    free(back);
    printf("%d id\n", id);
    if (received_length < 0){  // No message received.
        if (errno == EAGAIN){  // Timeout.
            return -4;
        }
        else{
            return -2;
        }
    }
    else if (sess_id != sess) {  // Session ID of message is not equal to client session ID.
        fprintf(stderr, "ERROR: Received message has wrong session ID\n");
        return -3;
    }
    else if (pack_id != pack && id == 5){  // Pack ID isn't correct, but ACC received.
        if (pack_id > pack){  // Old ACC.
            return 2;
        }
        else{  // Incorrect ACC.
            return -1;
        }
    }
    return id;
}

//  Tries to receive ACC.
int get_ACC(int socket_fd, unsigned long long sess_id, unsigned long long pack_id, package* data, struct sockaddr_in server_address, char* msg){
    int back_id = recv_ACC(socket_fd, sess_id, pack_id);
    unsigned long long trial = 0;
    while ((back_id == -4 || back_id == 2) && trial < MAX_RETRANSMITS){  // While receives past accepts or timeouts.
        if (back_id == -4){  // Timeout
            trial++;
            // Retransmits.
            if (send_udp_pack(socket_fd, data, server_address, msg) == 1){
                free(data);
                return 1;
            }
        }
        // Tries to receive ACC again.
        back_id = recv_ACC(socket_fd, sess_id, pack_id);
        printf("ACC %d\n", back_id);
    }
    printf("ACC %d\n", back_id);
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
int udp_conn(char const *host, uint16_t port, msg_list* core, int socket_fd, struct sockaddr_in server_address, unsigned long long sess_id, bool udpr){
    // Creating 'CONN' package.
    package *conn = malloc(sizeof(package));
    if (malloc_error(conn) == 1){
        return 1;
    }
    if (udpr){
        create_pack(conn, 1, sess_id, 3, get_length(core), 0, 0);  // CONN
    }
    else{
        create_pack(conn, 1, sess_id, 2, get_length(core), 0, 0);  // CONN
    }

    // Sending 'CONN' package.
    if (send_udp_pack(socket_fd, conn, server_address, NULL) == 1) {
        return 1;
    }

    // Receive a message.
    unsigned long long trial = 0;
    int back_id = recv_udp_prot(socket_fd, sess_id);

    // Retransmissions.
    while (back_id == -4 && udpr && trial < MAX_RETRANSMITS){
        if (send_udp_pack(socket_fd, conn, server_address, NULL) == 1) {
            return 1;
        }
        back_id = recv_udp_prot(socket_fd, sess_id);
        printf("CONN %d\n", back_id);
        trial++;
    }
    printf("CONN %d\n", back_id);
    if (back_id == 3){  // Received 'CONRJT'.
        printf("Connection dismissed by server.\n");
        return 0;
    }
    else if (back_id == 2){  // Received 'CONACC'.
        msg_list* act = core;
        unsigned long long pack_id = 0;
        while (act != NULL){  // Sending 'DATA" packages.
            package *data = malloc(sizeof(package));
            if (malloc_error(data) == 1){
                return 1;
            }

            create_pack(data, 4, sess_id, 2, 0, pack_id, act->length);
            // Tries sending part of the message.
            if (send_udp_pack(socket_fd, data, server_address, act->msg) == 1){
                free(data);
                return 1;
            }
            // Retransmissions.
            if (udpr && get_ACC(socket_fd, sess_id, pack_id, data, server_address, act->msg) == 1){
                free(data);
                return 1;
            }
            free(data);
            act = act->next;  // Next part of message.
            pack_id++;
        }
        printf("GET IN HERE\n");
        int recv;
        do{
            recv = recv_ACC(socket_fd, sess_id, pack_id);
            printf("recv %d\n", recv);
        } while (recv == 2 && udpr);  // Receiving past accepts.

        if (recv == -4){
            fprintf(stderr, "ERROR: Message timeout.\n");
            return 1;
        }
        else if (recv != 7){  // Waiting for 'RCVD' after sending all 'DATA'.
            fprintf(stderr, "ERROR: Didn't get RECV\n");
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


// Reads stdin data. If successful sends data to server using established protocol.
// Function demands 3 arguments, communication protocol, server id and port id.
int main(int argc, char *argv[]) {
    if (argc != 4) {  // Checks for 3 arguments.
        fprintf(stderr, "ERROR: Expected arguments: %s <communication protocol> <server id> <port>\n", argv[0]);
        return 1;
    }
    char const *protocol = argv[1];  // Communication protocol.
    char const *host = argv[2];  // Server id.
    bool *error = malloc(sizeof(bool));  // Catches errors.
    if (malloc_error(error) == 1){
        return 1;
    }

    *error = false;
    uint16_t port = read_port(argv[3], error);
    if (*error){  // There was an error getting port.
        free(error);
        return 1;
    }
    free(error);

    // Pointer to beginning of the list which contains data to send.
    msg_list *core = malloc(sizeof(msg_list));
    if (malloc_error(core) == 1){
        return 1;
    }
    if (get_stdin(core) == 1){  // Reading the message from stdin.
        free_msg(core);
        return 1;
    }
    if (get_length(core) == 0){  // Checking if message is not empty.
        fprintf(stderr, "ERROR: Won't send an empty message.\n");
        return 1;
    }
    unsigned long long sess_id = gen_sess_id();  // Generating session id.
    if (strcmp(protocol, "udp") == 0 || strcmp(protocol, "udpr") == 0){  // Sending the message using UDP protocol.
        bool udpr = false;  // Allows retransmissions.
        if (strcmp(protocol, "udpr") == 0){
            udpr = true;
        }
        error = malloc(sizeof(bool));  // Catches errors.
        if (malloc_error(error) == 1){
            return 1;
        }
        *error = false;
        struct sockaddr_in server_address = get_server_address(host, port, error, AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (*error){  // There was an error getting server address.
            free(error);
            return 1;
        }
        free(error);

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

        if (udp_conn(host, port, core, socket_fd, server_address, sess_id, udpr) == 1){
            free_msg(core);
            close(socket_fd);
            return 1;
        }
        close(socket_fd);
    }
    else{
        free_msg(core);
        fprintf(stderr, "ERROR: Wrong protocol.\n");
        return 1;
    }
    free_msg(core);
    return 0;
}
