#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>

// Constants
#define MAX_USERNAME_LEN 50
#define MAX_MESSAGE_LEN 500
#define MAX_ROOM_NAME_LEN 100
#define MAX_CLIENTS_PER_ROOM 20
#define MAX_ROOMS 50
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

// Message types
typedef enum {
    MSG_JOIN = 1,
    MSG_CREATE_ROOM,
    MSG_JOIN_ROOM,
    MSG_LEAVE_ROOM,
    MSG_MESSAGE,
    MSG_LIST_ROOMS,
    MSG_QUIT,
    MSG_WELCOME,
    MSG_ROOM_CREATED,
    MSG_ROOM_JOINED,
    MSG_ROOM_LEFT,
    MSG_ROOM_LIST,
    MSG_ERROR,
    MSG_BROADCAST
} message_type_t;

// Message structure
typedef struct {
    message_type_t type;
    char username[MAX_USERNAME_LEN];
    char content[MAX_MESSAGE_LEN];
    int room_id;
    int client_id;
    time_t timestamp;
} message_t;

// Client structure
typedef struct client {
    int socket_fd;
    int client_id;
    char username[MAX_USERNAME_LEN];
    int current_room_id;
    pthread_t thread_id;
    struct client* next;
} client_t;

// Room structure
typedef struct room {
    int room_id;
    char room_name[MAX_ROOM_NAME_LEN];
    client_t* clients;
    int client_count;
    pthread_mutex_t mutex;
    struct room* next;
} room_t;

// Server structure
typedef struct {
    int server_socket;
    room_t* rooms;
    client_t* clients;
    int next_room_id;
    int next_client_id;
    pthread_mutex_t rooms_mutex;
    pthread_mutex_t clients_mutex;
} server_t;

// Function prototypes
// Protocol functions
int send_message(int socket_fd, message_t* msg);
int receive_message(int socket_fd, message_t* msg);
void print_message(message_t* msg);

// Utility functions
void error_exit(const char* msg);
void* safe_malloc(size_t size);
void safe_free(void* ptr);
int create_socket();
void setup_server_socket(int socket_fd, int port);
void cleanup_client(client_t* client);
void cleanup_room(room_t* room);

// Thread-safe functions
void add_client_to_room(server_t* server, int room_id, client_t* client);
void remove_client_from_room(server_t* server, int room_id, client_t* client);
void broadcast_to_room(server_t* server, int room_id, message_t* msg, int exclude_client_id);
room_t* find_room(server_t* server, int room_id);
room_t* create_room(server_t* server, const char* room_name);
void list_rooms(server_t* server, int client_socket);

#endif // PROTOCOL_H
