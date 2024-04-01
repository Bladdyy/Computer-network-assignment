#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define MAX_MSG 64000
#define MALLOC_ERR(pointer){ \
    if (core->msg == NULL){\
        free_msg(core);\
        fprintf(stderr, "ERROR: Problem with allocation.\n");\
        return 1;\
    }\
}

typedef struct package{
    unsigned char id;
    unsigned long long session_id;
    unsigned char protocol;
    unsigned long long length;
} package;

typedef struct msg_list{
    char *msg;
    struct msg_list *next;
} msg_list;


void free_msg(msg_list *el){
    msg_list *prev;
    do {
        free(el->msg);
        prev = el;
        el = el->next;
        free(prev);
    } while (el != NULL);
}


int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "ERROR: Expected arguments: %s <communication protocol> <server id> <port>\n", argv[0]);
        return 1;
    }
    char const *mode = argv[1];
    char const *host = argv[2];

    // Msg read
    msg_list *core = malloc(sizeof(msg_list));
    if (core == NULL){
        fprintf(stderr, "ERROR: Problem with allocation.\n");
        return 1;
    }
    core->next = NULL;
    core->msg = malloc(sizeof(char) * MAX_MSG);
    MALLOC_ERR(core->msg)
    msg_list *act = core;
    char el;
    unsigned int length = 0;
    int res = (int) read(STDIN_FILENO, &el, 1);
    while (res > 0){
        (act->msg)[length] = el;
        length++;
        if (length == MAX_MSG - 1) {
            act->next = malloc(sizeof(msg_list));
            MALLOC_ERR(act->next)
            act = act->next;
            act->next = NULL;
            act->msg = malloc(sizeof(char) * MAX_MSG);
            MALLOC_ERR(act->next)
            length = 0;
        }
        res = (int) read(STDIN_FILENO, &el, 1);
    }
    if (res < 0){
        fprintf(stderr, "ERROR: Problem %d while reading from the file.\n", res);
    }
}
