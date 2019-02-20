
#pragma once

/**
 * The largest size the message can be that a client
 * sends to the server.
 */
#define MSG_SIZE (256)


typedef struct node { 
    char *data; 
  
    // Lower values indicate higher priority 
    int priority; 
  
    struct node* next; 
  
} Node; 

Node* newNode(char *d, int p);

char *peek(Node** head); 

void pop(Node** head);

void push(Node** head, char *d, int p);

int isEmpty(Node** head); 

/**
 * Builds a message in the form of
 * <name>: <message>\n
 *
 * Returns a char* to the created message on the heap
 */
char* create_message (char *name, char *message);


char *create_leave_message(char *name);

/**
 * Read the first four bytes from socket and transform it into ssize_t
 * 
 * Returns the size of the incomming message,
 * 0 if socket is disconnected, -1 on failure
 */
ssize_t get_message_size(int socket);


/**
 * Writes the bytes of size to the socket
 *
 * Returns the number of bytes successfully written,
 * 0 if socket is disconnected, or -1 on failure
 */
ssize_t write_message_size(size_t size, int socket);


ssize_t get_msg_num(int socket);


ssize_t write_msg_num(size_t size, int socket);

/**
 * Attempts to read all count bytes from socket into buffer.
 * Assumes buffer is large enough.
 *
 * Returns the number of bytes read, 0 if socket is disconnected,
 * or -1 on failure.
 */
ssize_t read_all_from_socket (int socket, char *buffer, size_t count);


/**
 * Attempts to write all count bytes from buffer to socket.
 * Assumes buffer contains at least count bytes.
 *
 * Returns the number of bytes written, 0 if socket is disconnected,
 * or -1 on failure.
 */
ssize_t write_all_to_socket (int socket, const char *buffer, size_t count);
