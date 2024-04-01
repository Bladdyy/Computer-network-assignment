#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
//typedef struct package{
//    unsigned char id;
//    unsigned long long session_id;
//    unsigned char protocol;
//    unsigned long long length;
//} package;


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


// Reads stdin data. If successful sends data to server using established protocol.
// Function demands 3 arguments, communication protocol, server id and port id.
int main(int argc, char *argv[]) {
    if (argc != 4) {  // Checks for 3 arguments.
        fprintf(stderr, "ERROR: Expected arguments: %s <communication protocol> <server id> <port>\n", argv[0]);
        return 1;
    }
//    char const *protocol = argv[1];  // Communication protocol.
//    char const *host = argv[2];  // Server id.

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
        fprintf(stderr, "ERROR: Problem %d while reading from the file.\n", res);
    }
    else if (length > act->length){
        act->length = length;
    }
    free_msg(core);
    return 0;
}
