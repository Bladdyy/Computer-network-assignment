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


// Gets length of the data in 'msg_list'.
unsigned long long get_length(msg_list *core){
    unsigned long long len = 0;
    while (core != NULL){
        len += core->length;
        core = core->next;
    }
    return len;
}


// Generates random session ID. TODO: random Unsigned long long.
unsigned long long gen_sess_id(){
    return 4;
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


// Creates port. TODO: RETURN ERROR (for example 1).
static uint16_t read_port(char const *string) {
    char* end_ptr;
    unsigned long port = strtoul(string, &end_ptr, 10);
    if ((port == ULONG_MAX && errno == ERANGE) || *end_ptr != 0 || port == 0 || port > UINT16_MAX) {
        fprintf(stderr,"ERROR: %s is not a valid port number.\n", string);
    }
    return (uint16_t) port;
}


// Creates server_address TODO: RETURN ERROR (for example 1).
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
    // If there was an error while reading characters.
    if (res < 0){
        fprintf(stderr, "ERROR: Problem %d while reading from stdin.\n", res);
        return 1;
    }
    else if (length > act->length){  // Length of data in the last node.
        act->length = length;
    }
    return 0;
}


// Sends one package to server using UDP protocol.
int send_udp_pack(int socket_fd, package *pack, size_t size, struct sockaddr_in server_address){
    int code = 0;  // Return code.
    ssize_t sent_length = sendto(socket_fd, pack, size, 0,
                                 (struct sockaddr *) &server_address, sizeof(server_address));
    if (sent_length < 0) {  // Couldn't send.
        fprintf(stderr, "ERROR: Package with id %d not sent.\n", pack->id);
        code = 1;
    }
    else if ((size_t) sent_length != sizeof(package)) {  // Couldn't send fully
        fprintf(stderr, "ERROR: Package with id %d not sent fully.\n", pack->id);
    }
    free(pack);
    return code;
}


// Receives package using UDP protocol.
int recv_udp_port(int socket_fd, unsigned long long sess_id){
    package* back = malloc(sizeof(package));  // Allocating space for new package.
    if (back == NULL){
        fprintf(stderr, "ERROR: Problem with allocation.\n");
        return -1;
    }
    struct sockaddr_in receive_address;
    // TODO: RECV FROM przyjmuje strukture?
    ssize_t received_length = recvfrom(socket_fd, back, sizeof(package), 0,
                                       (struct sockaddr *) &receive_address, (socklen_t *) sizeof(receive_address));
    if (received_length < 0 || sess_id != back->session_id) {
        free(back);
        return -2;
    }
    free(back);
    return back->id;
}


// Sends packages of data to server using UDP protocol.
int udp_conn(char const *host, uint16_t port, msg_list* core){
    unsigned long long sess_id = gen_sess_id();
    struct sockaddr_in server_address = get_server_address(host, port, AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        fprintf(stderr,"ERROR: Couldn't create a socket\n");
        return 1;
    }

    package *conn = malloc(sizeof(package));
    malloc_error(conn);
    create_pack(conn, 1, sess_id, 2, get_length(core), 0, 0, NULL);

    if (send_udp_pack(socket_fd, conn, sizeof(package), server_address) == 1) {
        return 1;
    }

    // Receive a message. Buffer should not be allocated on the stack.
    int back_id = recv_udp_port(socket_fd, sess_id);
    if (back_id == 3){
        printf("Connection dismissed by server.\n");
        return 0;
    }
    else if (back_id == 2){
        msg_list* act = core;
        unsigned long long pack_id = 0;
        while (act != NULL){
            package *data = malloc(sizeof(package));
            if (malloc_error(data) == 1){
                return 1;
            }
            create_pack(data, 4, sess_id, 2, 0, pack_id, act->length, act->msg);
            if (send_udp_pack(socket_fd, data, sizeof(package), server_address) == 1){
                return 1;
            }
            act = act->next;
            pack_id++;
        }
    }
    else{
        if (back_id != -2){
            fprintf(stderr, "ERROR: Wrong ID from server.\n");
        }
        return 1;
    }
    if (recv_udp_port(socket_fd, sess_id) != 7){
        fprintf(stderr, "ERROR: Didn't get RECV\n");
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
    uint16_t port = read_port(argv[3]);

    // Pointer to beginning of the list which contains data to send.
    msg_list *core = malloc(sizeof(msg_list));
    if (core == NULL){
        fprintf(stderr, "ERROR: Problem with allocation.\n");
        return 1;
    }
    if (get_stdin(core) == 1){
        free_msg(core);
        return 1;
    }

    if (strcmp(protocol, "udp") == 0){
        if(udp_conn(host, port, core) == 1){
            free_msg(core);
            return 1;
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
