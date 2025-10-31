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
            printf("[%s] 🔐 Phòng %s đã được tạo với ID: %d (Đã mã hóa)\n", 
                   time_str, msg->content, msg->room_id);
            break;
        case MSG_ROOM_JOINED:
            printf("[%s] 🔐 Đã tham gia phòng %s (ID: %d) - Tin nhắn được mã hóa\n", 
                   time_str, msg->content, msg->room_id);
            break;
        case MSG_ROOM_LEFT:
            printf("[%s] Đã rời khỏi phòng\n", time_str);
            break;
        case MSG_BROADCAST:
            if (msg->is_encrypted) {
                printf("[%s] %s: %s\n", time_str, msg->username, msg->content);
            } else {
                printf("[%s] %s: %s\n", time_str, msg->username, msg->content);
            }
            break;
        case MSG_ERROR:
            printf("[%s] ❌ Lỗi: %s\n", time_str, msg->content);
            break;
        case MSG_ROOM_LIST:
            printf("[%s] Danh sách phòng:\n%s", time_str, msg->content);
            break;
        case MSG_ROOM_KEY:
            printf("[%s] 🔑 Đã nhận key mã hóa cho phòng %d\n", time_str, msg->room_id);
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

// Encryption helper functions
void send_room_key_to_client(int client_socket, room_t* room) {
    message_t key_msg;
    memset(&key_msg, 0, sizeof(message_t));
    
    key_msg.type = MSG_ROOM_KEY;
    key_msg.room_id = room->room_id;
    strcpy(key_msg.username, "SERVER");
    
    // Chuyển key và IV sang hex
    key_to_hex(room->crypto.key, AES_KEY_SIZE, key_msg.room_key_hex);
    key_to_hex(room->crypto.iv, AES_IV_SIZE, key_msg.room_iv_hex);
    
    send_message(client_socket, &key_msg);
}

int encrypt_message_content(message_t* msg, const room_crypto_t* crypto) {
    int plaintext_len = strlen(msg->content);
    
    int encrypted_len = encrypt_message(
        (unsigned char*)msg->content, 
        plaintext_len,
        crypto->key, 
        crypto->iv, 
        msg->encrypted_content
    );
    
    if (encrypted_len < 0) {
        return -1;
    }
    
    msg->encrypted_len = encrypted_len;
    msg->is_encrypted = 1;
    
    // Xóa plaintext
    memset(msg->content, 0, MAX_MESSAGE_LEN);
    
    return 0;
}

int decrypt_message_content(message_t* msg, const room_crypto_t* crypto) {
    unsigned char plaintext[MAX_MESSAGE_LEN];
    
    int plaintext_len = decrypt_message(
        msg->encrypted_content, 
        msg->encrypted_len,
        crypto->key, 
        crypto->iv, 
        plaintext
    );
    
    if (plaintext_len < 0) {
        return -1;
    }
    
    plaintext[plaintext_len] = '\0';
    strncpy(msg->content, (char*)plaintext, MAX_MESSAGE_LEN - 1);
    msg->content[MAX_MESSAGE_LEN - 1] = '\0';
    
    return 0;
}

// Thread-safe room management functions
void add_client_to_room(server_t* server, int room_id, client_t* client) {
    room_t* room = find_room(server, room_id);
    if (!room) return;

    pthread_mutex_lock(&room->mutex);
    
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
    
    // Tạo key mã hóa cho room
    generate_room_key(&new_room->crypto);
    
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
        snprintf(room_info, sizeof(room_info), "🔐 ID:%d Name:%s Members:%d\n", 
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
