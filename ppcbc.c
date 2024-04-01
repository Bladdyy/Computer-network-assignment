#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netdb.h>

// Max package size.
#define MAX_MSG 64000

// Security check after allocation.
// 'pointer' is the pointer to newly allocated space.
#define MALLOC_ERR(pointer){ \
    if (pointer == NULL){\
        free_msg(core);\
        fprintf(stderr, "ERROR: Problem with allocation.\n");\
        return 1;\
    }\
}


// Package components.
typedef struct package{
    unsigned char id;
    unsigned long long session_id;
    unsigned char protocol;
    unsigned long long length;
} package;


// Stdin data storage.
typedef struct msg_list{
    char *msg;
    struct msg_list *next;
    unsigned int length;
} msg_list;


// Frees space allocated for stdin data storage.
// 'el' is the pointer to the beginning of the list.
void free_msg(msg_list *el){
    msg_list *prev;
    do {
        free(el->msg);
        prev = el;
        el = el->next;
        free(prev);
    } while (el != NULL);
}


static uint16_t read_port(char const *string) {
    char* end_ptr;
    unsigned long port = strtoul(string, &end_ptr, 10);
    if ((port == ULONG_MAX && errno == ERANGE) || *end_ptr != 0 || port == 0 || port > UINT16_MAX) {
        fprintf(stderr,"ERROR: %s is not a valid port number.\n", string);
    }
    return (uint16_t) port;
}

unsigned long long get_length(msg_list *core){
    unsigned long long len = 0;
    while (core != NULL){
        len += core->length;
        core = core->next;
    }
    return len;
}


// TODO: random Unsigned long long.
unsigned long long gen_sess_id(){
    return 4;
}
int send_udp(int socket_fd, char* buff, size_t size, struct sockaddr_in server_address){
    ssize_t sent_length = sendto(socket_fd, buff, size, 0,
                                 (struct sockaddr *) &server_address, sizeof(server_address));
    if (sent_length < 0) {
        fprintf(stderr, "ERROR: Conn not sent.\n");
        return 1;
    }
    else if ((size_t) sent_length != sizeof(package)) {
        fprintf(stderr, "ERROR: Conn not sent fully.\n");
    }
    return 0;
}

static struct sockaddr_in get_server_address(char const *host, uint16_t port, int fam, int sock, int prot) {
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
    }

    // Creating socket address information.
    struct sockaddr_in send_address;
    send_address.sin_family = fam;   // IPv4
    send_address.sin_addr.s_addr =       // IP address
            ((struct sockaddr_in *) (address_result->ai_addr))->sin_addr.s_addr;
    send_address.sin_port = htons(port); // port from the command line

    freeaddrinfo(address_result);

    return send_address;
}


int recv_prot(int socket_fd){
    package* back = malloc(sizeof(package));
    if (back == NULL){
        fprintf(stderr, "ERROR: Problem with allocation.\n");
        return -1;
    }
    struct sockaddr_in receive_address;

    // TODO: RECV FROM przyjmuje strukture?
    ssize_t received_length = recvfrom(socket_fd, back, sizeof(package), 0,
                                       (struct sockaddr *) &receive_address, (socklen_t *) sizeof(receive_address));
    if (received_length < 0) {
        free(back);
        fprintf(stderr, "ERROR: Couldn't receive message\n");
        return -1;
    }
    free(back);
    return back->id;
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
    uint16_t port = read_port(argv[3]);

    // Pointer to beginning of the list which contains data to send.
    msg_list *core = malloc(sizeof(msg_list));
    if (core == NULL){
        fprintf(stderr, "ERROR: Problem with allocation.\n");
        return 1;
    }
    core->next = NULL;
    core->msg = malloc(sizeof(char) * MAX_MSG);
    core->length = 0;
    MALLOC_ERR(core->msg)
    msg_list *act = core;
    char el;  // Read character.
    unsigned int length = 0;  // Number of characters written into current segment of data list.
    int res = (int) read(STDIN_FILENO, &el, 1);
    while (res > 0){  // As long as any character has been read, and it's not 'EOF'.
        (act->msg)[length] = el;  // Saving character in the buffer.
        length++;
        if (length == MAX_MSG - 1) {  // If current buffer is full.
            // Allocating new buffer.
            act->next = malloc(sizeof(msg_list));
            MALLOC_ERR(act->next)
            act = act->next;
            act->next = NULL;
            act->msg = malloc(sizeof(char) * MAX_MSG);
            MALLOC_ERR(act->next)
            act->length = length;
            length = 0;
        }
        res = (int) read(STDIN_FILENO, &el, 1);
    }
    // If there was an error while reading characters.
    if (res < 0){
        free_msg(core);
        fprintf(stderr, "ERROR: Problem %d while reading from the file.\n", res);
        return 1;
    }
    else if (length > act->length){  // Length of data in the last node.
        act->length = length;
    }
    if (strcmp(protocol, "udp") == 0){
        struct sockaddr_in server_address = get_server_address(host, port, AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
            free_msg(core);
            fprintf(stderr,"ERROR: Couldn't create a socket\n");
            return 1;
        }
        package *conn = malloc(sizeof(package));
        MALLOC_ERR(conn)
        conn->id = 1;
        conn->session_id = gen_sess_id();
        conn->protocol = 2;
        conn->length = get_length(core);
        ssize_t sent_length = sendto(socket_fd, conn, sizeof(package), 0,
                                     (struct sockaddr *) &server_address, sizeof(server_address));
        free(conn);
        if (sent_length < 0) {
            free_msg(core);
            fprintf(stderr, "ERROR: Conn not sent.\n");
            return 1;
        }
        else if ((size_t) sent_length != sizeof(package)) {
            fprintf(stderr, "ERROR: Conn not sent fully.\n");
        }


        // Receive a message. Buffer should not be allocated on the stack.
        int back_id = recv_prot(socket_fd);
        if (back_id == 3){
            free_msg(core);
            printf("Connection dismissed by server.\n");
            return 0;
        }
        else if (back_id == 2){
            act = core;
            while (act != NULL){
                if (send_udp(socket_fd, act->msg, act->length, server_address) == 1){
                    free_msg(core);
                    return 1;
                }
                act = act->next;
            }
        }
        else{
            if (back_id != -1){
                fprintf(stderr, "ERROR: Wrong ID from server.\n");
            }
            free_msg(core);
            return 1;
        }
        free_msg(core);
        if (recv_prot(socket_fd) != 7){
            fprintf(stderr, "ERROR: Didn't get RECV\n");
        }
    }
    else{
        free_msg(core);
        fprintf(stderr, "ERROR: Wrong protocol.\n");
        return 1;
    }


    free_msg(core);
    return 0;
}
