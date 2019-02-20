/**
* Chatroom Lab
* CS 241 - Fall 2018
*/


#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// #include "chat_window.h"
#include "utils.h"

static volatile int serverSocket;
static pthread_t threads[2];
static struct addrinfo *addr_result;

void *write_to_server(void *arg);
void *read_from_server(void *arg);
void close_program(int signal);

/**
 * Shuts down connection with 'serverSocket'.
 * Called by close_program upon SIGINT.
 */
void close_server_connection() {
    // Your code here
    freeaddrinfo(addr_result);
    if (shutdown(serverSocket, SHUT_RDWR) != 0) {
        perror(NULL);
    }
    close(serverSocket);
    //destroy_windows();
}

// void exit_failure() {
//     destroy_windows();
//     close_chat();
//     exit(1);
// }

/**
 * Sets up a connection to a chatroom server and returns
 * the file descriptor associated with the connection.
 *
 * host - Server to connect to.
 * port - Port to connect to server on.
 *
 * Returns integer of valid file descriptor, or exit(1) on failure.
 */
int connect_to_server(const char *host, const char *port) {

    int retval;     
    
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(serverSocket == -1){
        perror(NULL);
        //exit_failure();
        exit(1);
    }

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(struct addrinfo)); 

    hints.ai_family   = AF_INET; /* IPv4 only */
    hints.ai_socktype = SOCK_STREAM; /* TCP */



    retval = getaddrinfo(host, port, &hints, &result);
    if(retval != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(retval));
        freeaddrinfo(result);
        close(serverSocket);
        exit(1);
    }

    retval = connect(serverSocket, result->ai_addr, result->ai_addrlen);
    if(retval == -1){
        
        perror(NULL);
        freeaddrinfo(result);
        close(serverSocket);
        exit(1);
    }

    addr_result = result;
    return serverSocket;
}

typedef struct _thread_cancel_args {
    char **buffer;
    char **msg;
} thread_cancel_args;

/**
 * Cleanup routine in case the thread gets cancelled.
 * Ensure buffers are freed if they point to valid memory.
 */
void thread_cancellation_handler(void *arg) {
    printf("Cancellation handler\n");
    thread_cancel_args *a = (thread_cancel_args *)arg;
    char **msg = a->msg;
    char **buffer = a->buffer;
    if (*buffer) {
        free(*buffer);
        *buffer = NULL;
    }
    if (msg && *msg) {
        free(*msg);
        *msg = NULL;
    }
}


void read_message_from_input(char** buffer){

    // Allocate buffer if needed
    if (*buffer == NULL)
        *buffer = calloc(1, MSG_SIZE);
    else
        memset(*buffer, 0, MSG_SIZE);


    if (fgets(*buffer, MSG_SIZE, stdin) == NULL) {
        fprintf(stderr, "fgets():");
        exit(1);
    }
        
    char* ptr = *buffer;
    while (*ptr) {
        if (*ptr == '\n') {
            *ptr = '\0';
            break;
        }
        ptr++;
    }

    return;
}

/**
 * Reads bytes from user and writes them to server.
 *
 * arg - void* casting of char* that is the username of client.
 */
void *write_to_server(void *arg) {
    char *name = (char *)arg;
    char *buffer = NULL;
    char *msg = NULL;
    ssize_t retval = 1;

    thread_cancel_args cancel_args;
    cancel_args.buffer = &buffer;
    cancel_args.msg = &msg;
    // Setup thread cancellation handlers.
    // Read up on pthread_cancel, thread cancellation states,
    // pthread_cleanup_push for more!
    pthread_cleanup_push(thread_cancellation_handler, &cancel_args);

    while (retval > 0) {
        // read_message_from_screen(&buffer);
        read_message_from_input(&buffer);
        if (buffer == NULL)
            break;

        msg = create_message(name, buffer);
        size_t len = strlen(msg) + 1;

        retval = write_message_size(len, serverSocket);
        if (retval > 0)
            retval = write_all_to_socket(serverSocket, msg, len);

        free(msg);
        msg = NULL;
    }

    pthread_cleanup_pop(0);
    return 0;
}

/**
 * Reads bytes from the server and prints them to the user.
 *
 * arg - void* requriment for pthread_create function.
 */
void *read_from_server(void *arg) {
    // Silence the unused parameter warning.
    char *name = (char *)arg;
    ssize_t retval = 1;
    char *buffer = NULL;
    thread_cancel_args cancellation_args;
    cancellation_args.buffer = &buffer;
    cancellation_args.msg = NULL;
    pthread_cleanup_push(thread_cancellation_handler, &cancellation_args);
    int msg_count = 0;
    ssize_t msg_num = -1;

    while (retval > 0) {
        retval = get_message_size(serverSocket);
        if (retval > 0) {
            buffer = calloc(1, retval);
            retval = read_all_from_socket(serverSocket, buffer, retval);
            msg_num = get_msg_num(serverSocket);
            printf("msg_num is %lu\n", msg_num); // debug
        }

        if (retval > 0){
            if(msg_num == msg_count){
                int is_self = 1;
                unsigned int len = strlen(name);
                for(unsigned int i = 0; i < len; i++){
                    if(name[i] != buffer[i]){
                        is_self = 0;
                        break;
                    }
                }
                if(buffer[len] != ':'){
                    is_self = 0;
                }

                if(!is_self){

                    printf("%s\n", buffer);
                }
            }



        }



        free(buffer);
        buffer = NULL;
    }

    pthread_cleanup_pop(0);
    return 0;
}

/**
 * Signal handler used to close this client program.
 */
void close_program(int signal) {
    if (signal == SIGINT) {
        pthread_cancel(threads[0]);
        pthread_cancel(threads[1]);
        // close_chat();
        close_server_connection();
    }
}

int main(int argc, char **argv) {

    if(argc != 4){
        fprintf(stderr, "Usage: %s <name> <port> <n>\n", argv[0]);
        exit(1);
    }

    // Setup signal handler.
    signal(SIGINT, close_program);

    serverSocket = connect_to_server("sp19-cs425-g25-01.cs.illinois.edu", argv[2]);

    char *name = argv[1];

    size_t len = strlen(name) + 1;
    int retval = write_message_size(len, serverSocket);
    if(retval > 0){
        retval = write_all_to_socket(serverSocket, name, len);
    }

    char* buffer = calloc(1, 6);
    retval = get_message_size(serverSocket);
    if (retval > 0) {
        buffer = calloc(1, retval);
        retval = read_all_from_socket(serverSocket, buffer, retval);
    }

    if(!strncmp(buffer, "READY", 5)){
        printf("READY\n");
    }
    free(buffer);

    pthread_create(&threads[0], NULL, write_to_server, (void *)name);
    pthread_create(&threads[1], NULL, read_from_server, (void *)name);

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);

    return 0;
}


