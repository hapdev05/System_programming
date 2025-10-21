#include "protocol.h"

void error_exit(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (ptr == NULL) {
        error_exit("Memory allocation failed");
    }
    return ptr;
}

void safe_free(void* ptr) {
    if (ptr != NULL) {
        free(ptr);
    }
}

int create_socket() {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        error_exit("Socket creation failed");
    }
    return socket_fd;
}

void setup_server_socket(int socket_fd, int port) {
    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        error_exit("setsockopt failed");
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        error_exit("Bind failed");
    }

    if (listen(socket_fd, 10) < 0) {
        error_exit("Listen failed");
    }
}

int send_message(int socket_fd, message_t* msg) {
    if (send(socket_fd, msg, sizeof(message_t), 0) < 0) {
        return -1;
    }
    return 0;
}

int receive_message(int socket_fd, message_t* msg) {
    ssize_t bytes_received = recv(socket_fd, msg, sizeof(message_t), 0);
    if (bytes_received <= 0) {
        return -1;
    }
    return 0;
}

void print_message(message_t* msg) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    switch (msg->type) {
        case MSG_WELCOME:
            printf("[%s] Chào mừng %s!\n", time_str, msg->username);
            break;
        case MSG_ROOM_CREATED:
            printf("[%s] Phòng %s đã được tạo với ID: %d\n", time_str, msg->content, msg->room_id);
            break;
        case MSG_ROOM_JOINED:
            printf("[%s] Đã tham gia phòng %s (ID: %d)\n", time_str, msg->content, msg->room_id);
            break;
        case MSG_ROOM_LEFT:
            printf("[%s] Đã rời khỏi phòng\n", time_str);
            break;
        case MSG_BROADCAST:
            printf("[%s] %s: %s\n", time_str, msg->username, msg->content);
            break;
        case MSG_ERROR:
            printf("[%s] Lỗi: %s\n", time_str, msg->content);
            break;
        case MSG_ROOM_LIST:
            printf("[%s] Danh sách phòng: %s\n", time_str, msg->content);
            break;
        default:
            printf("[%s] %s\n", time_str, msg->content);
            break;
    }
}

void cleanup_client(client_t* client) {
    if (client) {
        close(client->socket_fd);
        safe_free(client);
    }
}

void cleanup_room(room_t* room) {
    if (room) {
        pthread_mutex_destroy(&room->mutex);
        safe_free(room);
    }
}

// Thread-safe room management functions
void add_client_to_room(server_t* server, int room_id, client_t* client) {
    room_t* room = find_room(server, room_id);
    if (!room) return;

    pthread_mutex_lock(&room->mutex);
    
    // Add client to room's client list
    client->next = room->clients;
    room->clients = client;
    room->client_count++;
    client->current_room_id = room_id;
    
    pthread_mutex_unlock(&room->mutex);
}

void remove_client_from_room(server_t* server, int room_id, client_t* client) {
    room_t* room = find_room(server, room_id);
    if (!room) return;

    pthread_mutex_lock(&room->mutex);
    
    // Remove client from room's client list
    if (room->clients == client) {
        room->clients = client->next;
    } else {
        client_t* current = room->clients;
        while (current && current->next != client) {
            current = current->next;
        }
        if (current) {
            current->next = client->next;
        }
    }
    room->client_count--;
    client->current_room_id = -1;
    
    pthread_mutex_unlock(&room->mutex);
}

void broadcast_to_room(server_t* server, int room_id, message_t* msg, int exclude_client_id) {
    room_t* room = find_room(server, room_id);
    if (!room) return;

    pthread_mutex_lock(&room->mutex);
    
    client_t* current = room->clients;
    while (current) {
        if (current->client_id != exclude_client_id) {
            send_message(current->socket_fd, msg);
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&room->mutex);
}

room_t* find_room(server_t* server, int room_id) {
    pthread_mutex_lock(&server->rooms_mutex);
    
    room_t* current = server->rooms;
    while (current) {
        if (current->room_id == room_id) {
            pthread_mutex_unlock(&server->rooms_mutex);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&server->rooms_mutex);
    return NULL;
}

room_t* create_room(server_t* server, const char* room_name) {
    pthread_mutex_lock(&server->rooms_mutex);
    
    room_t* new_room = (room_t*)safe_malloc(sizeof(room_t));
    new_room->room_id = server->next_room_id++;
    strncpy(new_room->room_name, room_name, MAX_ROOM_NAME_LEN - 1);
    new_room->room_name[MAX_ROOM_NAME_LEN - 1] = '\0';
    new_room->clients = NULL;
    new_room->client_count = 0;
    pthread_mutex_init(&new_room->mutex, NULL);
    
    new_room->next = server->rooms;
    server->rooms = new_room;
    
    pthread_mutex_unlock(&server->rooms_mutex);
    return new_room;
}

void list_rooms(server_t* server, int client_socket) {
    pthread_mutex_lock(&server->rooms_mutex);
    
    message_t response;
    response.type = MSG_ROOM_LIST;
    strcpy(response.username, "SERVER");
    strcpy(response.content, "");
    
    char room_list[BUFFER_SIZE] = "";
    room_t* current = server->rooms;
    
    while (current) {
        char room_info[200];
        snprintf(room_info, sizeof(room_info), "ID:%d Name:%s Members:%d\n", 
                current->room_id, current->room_name, current->client_count);
        strcat(room_list, room_info);
        current = current->next;
    }
    
    if (strlen(room_list) == 0) {
        strcpy(response.content, "Không có phòng nào");
    } else {
        strncpy(response.content, room_list, MAX_MESSAGE_LEN - 1);
        response.content[MAX_MESSAGE_LEN - 1] = '\0';
    }
    
    pthread_mutex_unlock(&server->rooms_mutex);
    
    send_message(client_socket, &response);
}
