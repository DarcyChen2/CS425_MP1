#include <arpa/inet.h>
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

#include "utils.h"

#define MAX_CLIENTS 10

void *process_client(void *p);
void write_to_clients(const char *message, const int msg_num, size_t size); 

static volatile int ready_go; // broadcast + msg count
static volatile int msg_count;
static volatile int serverSocket;
static volatile int endSession;
static volatile int n;
static volatile int clientsCount;
static volatile int clients[MAX_CLIENTS];
static volatile char* clients_name[MAX_CLIENTS];

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_1 = PTHREAD_MUTEX_INITIALIZER;
static struct addrinfo *addr_result;

/**
 * Signal handler for SIGINT.
 * Used to set flag to end server.
 */
void close_server() {
    endSession = 1;
    freeaddrinfo(addr_result);
    if (shutdown(serverSocket, SHUT_RDWR) != 0) {
        perror("shutdown():");
    }
    close(serverSocket);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != -1) {
            if (shutdown(clients[i], SHUT_RDWR) != 0) {
                perror("shutdown(): ");
            }
            close(clients[i]);
        }
    }    
    pthread_exit(NULL);
    exit(EXIT_SUCCESS);
}

/**
 * Cleanup function called in main after `run_server` exits.
 * Server ending clean up (such as shutting down clients) should be handled
 * here.
 */
void cleanup() {
    if (shutdown(serverSocket, SHUT_RDWR) != 0) {
        perror("shutdown():");
    }
    close(serverSocket);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != -1) {
            if (shutdown(clients[i], SHUT_RDWR) != 0) {
                perror("shutdown(): ");
            }
            close(clients[i]);
        }
    }
}

/**
 * Sets up a server connection.
 * Does not accept more than MAX_CLIENTS connections.  If more than MAX_CLIENTS
 * clients attempts to connects, simply shuts down
 * the new client and continues accepting.
 * Per client, a thread should be created and 'process_client' should handle
 * that client.
 * Makes use of 'endSession', 'clientsCount', 'client', and 'mutex'.
 *
 * port - port server will run on.
 *
 * If any networking call fails, the appropriate error is printed and the
 * function calls exit(1):
 *    - fprtinf to stderr for getaddrinfo
 *    - perror() for any other call
 */
void run_server(char *port) {

    for (int i = 0; i < MAX_CLIENTS; i++){
        clients[i] = -1;        
    }

    int retval;    
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        perror("socket failed.\n");
        exit(1);
        //exit_failure();
    }   

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(struct addrinfo)); 

    hints.ai_family     = AF_INET;
    hints.ai_socktype   = SOCK_STREAM;
    hints.ai_flags      = AI_PASSIVE;

    retval = getaddrinfo(NULL, port, &hints, &result);
    if (retval != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(retval)); // fprintf for getaddrinfo
        exit(1);
    }

    addr_result = result;

    // After a socket is closed the port enters a time-out state during which time it cannot be re-used
    // Disable it
    int optval = 1;
    // retval = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    retval = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (retval == -1) {
        perror("setsockopt()");
        exit(1);
    }

    if (bind(serverSocket, result->ai_addr, result->ai_addrlen) != 0) {
        perror("bind()");
        exit(1);
    }

    if (listen(serverSocket, MAX_CLIENTS) != 0) {
        perror("listen()");
        exit(1);
    }

    while (endSession == 0) {

        if (clientsCount < n) {

            struct sockaddr clientAddr;
            socklen_t clientAddrlen = sizeof(struct sockaddr);
            memset(&clientAddr, 0, sizeof(struct sockaddr));

            int client_fd;

            if((client_fd = accept(serverSocket, (struct sockaddr *) &clientAddr, &clientAddrlen)) < 0){
                perror("accept");
                exit(1);
            }
            if (endSession != 0) {
                fprintf(stderr, "accept():: Invalid argument\n");
                break;
            }

            // Put clientID into array
            intptr_t clientId = -1;
            pthread_mutex_lock(&mutex);
            for (int i = 0; i < MAX_CLIENTS; i++){
                if (clients[i] == -1) {
                    clients[i] = client_fd;
                    clientId = i;
                    break;
                }
            }
            clientsCount++;
            pthread_mutex_unlock(&mutex);

            pthread_t thread;
            pthread_create(&thread, NULL, (void *)process_client, (void *)clientId);

        }else if(ready_go == 0){

            int get_name = 1;
            for(int i = 0; i < n; i++){
                if(clients_name[i] == NULL){
                    get_name = 0;
                    break;
                }
            }
            if(get_name){
                ready_go = 1;
                /*broadcast "READY"*/
                printf("#ready!\n"); // debug info
                write_to_clients("READY", -1, 6);
            }    

        }


        
    } // End of while loop

    // Cleanup
    freeaddrinfo(result);
}

/**
 * Broadcasts the message to all connected clients.
 *
 * message  - the message to send to all clients.
 * size     - length in bytes of message to send.
 */
void write_to_clients(const char *message, const int msg_num, size_t size) {
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != -1) {
            ssize_t retval = write_message_size(size, clients[i]);
            if (retval > 0) {
                retval = write_all_to_socket(clients[i], message, size);
                if(ready_go && msg_num != -1){
                    write_msg_num(msg_num, clients[i]);
                }
            }
            if (retval == -1) {
                perror("write(): ");
            }
        }
    }
    pthread_mutex_unlock(&mutex);
}

/**
 * Handles the reading to and writing from clients.
 *
 * p  - (void*)intptr_t index where clients[(intptr_t)p] is the file descriptor
 * for this client.
 *
 * Return value not used.
 */
void *process_client(void *p) {
    pthread_detach(pthread_self());
    intptr_t clientId = (intptr_t)p;
    ssize_t retval = 1;
    char *buffer = NULL;
    int msg_num = -1;

    while (retval > 0 && endSession == 0) {
        retval = get_message_size(clients[clientId]);
        if (retval > 0) {
            buffer = calloc(1, retval);
            retval = read_all_from_socket(clients[clientId], buffer, retval);
        }

        if (retval > 0){
            if(clients_name[clientId] == NULL){
                pthread_mutex_lock(&mutex);
                clients_name[clientId] = strdup(buffer);
                pthread_mutex_unlock(&mutex);
            }else{
                if(ready_go){
                    pthread_mutex_lock(&mutex_1);
                    msg_num = msg_count;
                    msg_count++;
                    pthread_mutex_unlock(&mutex_1);

                    write_to_clients(buffer, msg_num, retval);
                }
              
            }
        }


        free(buffer);
        buffer = NULL;
    }

    char *leave_client = (char *)clients_name[clientId];
    char *leave_msg = create_leave_message(leave_client);
    size_t msg_len = strlen(leave_msg) + 1;
    free(leave_client);

    printf("#User %d left\n", (int)clientId); // debug info

    close(clients[clientId]);

    pthread_mutex_lock(&mutex);
    clients[clientId] = -1;
    clientsCount--;
    clients_name[clientId] = NULL;
    pthread_mutex_unlock(&mutex);

    pthread_mutex_lock(&mutex_1);
    msg_num = msg_count;
    msg_count++;
    pthread_mutex_unlock(&mutex_1);
    write_to_clients(leave_msg, msg_num, msg_len);   
    free(leave_msg);

    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "%s <port> <n>\n", argv[0]);
        return -1;
    }

    struct sigaction act;
    memset(&act, '\0', sizeof(act));
    act.sa_handler = close_server;
    if (sigaction(SIGINT, &act, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    signal(SIGINT, close_server);
    n = atoi(argv[2]);
    ready_go = 0;
    run_server(argv[1]);
    cleanup();
    pthread_exit(NULL);
}
