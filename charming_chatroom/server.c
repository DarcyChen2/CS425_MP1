/**
* Chatroom Lab
* CS 241 - Fall 2018
*/

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

#define MAX_CLIENTS 8

void *process_client(void *p);

static volatile int serverSocket;
static volatile int endSession;

static volatile int clientsCount;
static volatile int clients[MAX_CLIENTS];

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
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

    //cleanup()
    // add any additional flags here you want.
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


// void exit_failure(){
//     close_server();
//     cleanup();
//     pthread_exit(NULL);
//     exit(1);
// }

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
        //exit_failure();
    }

    addr_result = result;

    // After a socket is closed the port enters a time-out state during which time it cannot be re-used
    // Disable it
    int optval = 1;
    retval = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    if (retval == -1) {
        perror("setsockopt()");
        exit(1);
        //exit_failure();
    }

    if (bind(serverSocket, result->ai_addr, result->ai_addrlen) != 0) {
        perror("bind()");
        exit(1);
        //exit_failure();
    }

    if (listen(serverSocket, MAX_CLIENTS) != 0) {
        perror("listen()");
        exit(1);
        //exit_failure();
    }

    while (endSession == 0) {

        if (clientsCount < MAX_CLIENTS) {

            struct sockaddr clientAddr;
            socklen_t clientAddrlen = sizeof(struct sockaddr);
            memset(&clientAddr, 0, sizeof(struct sockaddr));

            int client_fd;

            if((client_fd = accept(serverSocket, (struct sockaddr *) &clientAddr, &clientAddrlen)) < 0){
                perror("accept");
                exit(1);
                //exit_failure();                
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
void write_to_clients(const char *message, size_t size) {
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != -1) {
            ssize_t retval = write_message_size(size, clients[i]);
            if (retval > 0) {
                retval = write_all_to_socket(clients[i], message, size);
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

    while (retval > 0 && endSession == 0) {
        retval = get_message_size(clients[clientId]);
        if (retval > 0) {
            buffer = calloc(1, retval);
            retval = read_all_from_socket(clients[clientId], buffer, retval);
        }
        if (retval > 0)
            write_to_clients(buffer, retval);

        free(buffer);
        buffer = NULL;
    }

    printf("User %d left\n", (int)clientId);
    close(clients[clientId]);

    pthread_mutex_lock(&mutex);
    clients[clientId] = -1;
    clientsCount--;
    pthread_mutex_unlock(&mutex);

    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "%s <port>\n", argv[0]);
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
    run_server(argv[1]);
    cleanup();
    pthread_exit(NULL);
}
