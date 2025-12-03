#include "../common/protocol.h"

typedef struct {
    int client_socket;
    int client_id;
    int room_id;
    char username[MAX_USERNAME_LEN];
    char filepath[MAX_MESSAGE_LEN];
} file_handler_args_t;

server_t g_server;

void initialize_server() {
    init_crypto();
    
    g_server.server_socket = create_socket();
    g_server.rooms = NULL;
    g_server.clients = NULL;
    g_server.next_room_id = 1;
    g_server.next_client_id = 1;
    pthread_mutex_init(&g_server.rooms_mutex, NULL);
    pthread_mutex_init(&g_server.clients_mutex, NULL);
}

void cleanup_server() {
    pthread_mutex_lock(&g_server.rooms_mutex);
    room_t* room = g_server.rooms;
    while (room) {
        room_t* next = room->next;
        cleanup_room(room);
        room = next;
    }
    pthread_mutex_unlock(&g_server.rooms_mutex);

    // Cleanup all clients
    
    pthread_mutex_lock(&g_server.clients_mutex);
    client_t* client = g_server.clients;
    while (client) {
        client_t* next = client->next;
        cleanup_client(client);
        client = next;
    }
    pthread_mutex_unlock(&g_server.clients_mutex);

    // Destroy mutexes
    
    pthread_mutex_destroy(&g_server.rooms_mutex);
    pthread_mutex_destroy(&g_server.clients_mutex);

    close(g_server.server_socket);
    cleanup_crypto();
}

void* handle_file_transfer(void* arg) {
    file_handler_args_t* args = (file_handler_args_t*)arg;

    printf("[FILE] Đang nhận file từ %s...\n", args->username);

    // Receive file data from sender
    file_transfer_t ft;
    int chunks_received = 0;

    while (receive_file_transfer(args->client_socket, &ft) == 0) {
        chunks_received++;

        // Broadcast file chunk to all clients in room except sender
        room_t* room = find_room(&g_server, args->room_id);
        if (room) {
            pthread_mutex_lock(&room->mutex);
            client_t* current = room->clients;
            while (current) {
                if (current->client_id != args->client_id) {
                    send_file_transfer(current->socket_fd, &ft);
                }
                current = current->next;
            }
            pthread_mutex_unlock(&room->mutex);
        }

        // Check if last chunk
        if (ft.chunk_number >= ft.total_chunks - 1) {
            printf("[FILE] Đã nhận %d chunks từ %s\n", chunks_received, args->username);
            break;
        }
    }

    // Send completion notification to sender
    message_t complete;
    complete.type = MSG_FILE_COMPLETE;
    strcpy(complete.username, "SERVER");
    snprintf(complete.content, MAX_MESSAGE_LEN,
            "File %s đã được gửi thành công", args->filepath);
    send_message(args->client_socket, &complete);

    // Cleanup
    free(args);
    return NULL;
}

void* handle_client(void* arg) {
    client_t* client = (client_t*)arg;
    message_t msg;

    printf("Client %s (ID: %d) đã kết nối\n", client->username, client->client_id);

    while (1) {
        if (receive_message(client->socket_fd, &msg) < 0) {
            break;
        }

        // Process message based on type
        
        switch (msg.type) {
            case MSG_JOIN: {
                strcpy(client->username, msg.username);

                message_t response;
                response.type = MSG_WELCOME;
                strcpy(response.username, "SERVER");
                snprintf(response.content, MAX_MESSAGE_LEN, 
                        "Chào mừng %s đến với chat server!", client->username);
                send_message(client->socket_fd, &response);
                break;
            }

            case MSG_CREATE_ROOM: {
                room_t* new_room = create_room(&g_server, msg.content);

                message_t response;
                response.type = MSG_ROOM_CREATED;
                strcpy(response.username, "SERVER");
                strcpy(response.content, new_room->room_name);
                response.room_id = new_room->room_id;
                send_message(client->socket_fd, &response);
                break;
            }

            case MSG_JOIN_ROOM: {
                room_t* room = find_room(&g_server, msg.room_id);

                message_t response;
                if (room) {
                    if (client->current_room_id != -1) {
                        remove_client_from_room(&g_server, client->current_room_id, client);
                    }

                    // Join new room
                    
                    add_client_to_room(&g_server, msg.room_id, client);

                    
                    // Nếu phòng đã bật mã hóa, gửi key cho client
                    if (room->encryption_enabled) {
                        send_room_key_to_client(client->socket_fd, room);
                    }
                    
                    response.type = MSG_ROOM_JOINED;
                    strcpy(response.username, "SERVER");
                    strcpy(response.content, room->room_name);
                    response.room_id = room->room_id;

                    // Notify other clients in room
                    
                    // Thông báo cho các client khác
                    message_t broadcast;
                    broadcast.type = MSG_BROADCAST;
                    broadcast.is_encrypted = 0;
                    broadcast.timestamp = time(NULL);
                    broadcast_to_room(&g_server, msg.room_id, &broadcast, client->client_id);
                    
                } else {
                    response.type = MSG_ERROR;
                    strcpy(response.username, "SERVER");
                    strcpy(response.content, "Phòng không tồn tại");
                }
                send_message(client->socket_fd, &response);
                break;
            }

            
            case MSG_ENABLE_ENCRYPTION: {
                if (client->current_room_id != -1) {
                    room_t* room = find_room(&g_server, client->current_room_id);
                    if (room) {
                        if (room->encryption_enabled) {
                            message_t response;
                            response.type = MSG_ERROR;
                            strcpy(response.username, "SERVER");
                            strcpy(response.content, "Phòng này đã được mã hóa rồi");
                            send_message(client->socket_fd, &response);
                        } else {
                            enable_room_encryption(&g_server, room);
                        }
                    }
                } else {
                    message_t response;
                    response.type = MSG_ERROR;
                    strcpy(response.username, "SERVER");
                    strcpy(response.content, "Bạn cần tham gia phòng trước");
                    send_message(client->socket_fd, &response);
                }
                break;
            }
            
            case MSG_LEAVE_ROOM: {
                if (client->current_room_id != -1) {
                    message_t broadcast;
                    broadcast.type = MSG_BROADCAST;
                    strcpy(broadcast.username, "SERVER");
                    snprintf(broadcast.content, MAX_MESSAGE_LEN, "%s đã rời khỏi phòng", client->username);
                    broadcast.is_encrypted = 0;
                    broadcast.timestamp = time(NULL);
                    broadcast_to_room(&g_server, client->current_room_id, &broadcast, client->client_id);

                    remove_client_from_room(&g_server, client->current_room_id, client);

                    message_t response;
                    response.type = MSG_ROOM_LEFT;
                    strcpy(response.username, "SERVER");
                    strcpy(response.content, "Đã rời khỏi phòng");
                    send_message(client->socket_fd, &response);
                }
                break;
            }

            case MSG_MESSAGE: {
                if (client->current_room_id != -1) {
                    room_t* room = find_room(&g_server, client->current_room_id);
                    if (room) {
                        // Broadcast message với timestamp và username
                        message_t broadcast = msg;
                        broadcast.type = MSG_BROADCAST;
                        strcpy(broadcast.username, client->username);
                        broadcast.timestamp = time(NULL);
                        broadcast.client_id = client->client_id;
                        broadcast.room_id = client->current_room_id;
                        broadcast_to_room(&g_server, client->current_room_id, &broadcast, -1);
                    }
                } else {
                    message_t response;
                    response.type = MSG_ERROR;
                    strcpy(response.username, "SERVER");
                    strcpy(response.content, "Bạn chưa tham gia phòng nào");
                    send_message(client->socket_fd, &response);
                }
                break;
            }

            case MSG_FILE_REQUEST: {
                if (client->current_room_id != -1) {
                    // Broadcast file notification to room
                    message_t notification;
                    notification.type = MSG_FILE_NOTIFICATION;
                    strcpy(notification.username, client->username);
                    snprintf(notification.content, MAX_MESSAGE_LEN,
                            "[FILE] %s đang gửi file: %s", client->username, msg.content);
                    notification.client_id = client->client_id;
                    broadcast_to_room(&g_server, client->current_room_id, &notification, client->client_id);

                    file_handler_args_t* args = (file_handler_args_t*)malloc(sizeof(file_handler_args_t));
                    args->client_socket = client->socket_fd;
                    args->client_id = client->client_id;
                    args->room_id = client->current_room_id;
                    strncpy(args->username, client->username, MAX_USERNAME_LEN - 1);
                    strncpy(args->filepath, msg.content, MAX_MESSAGE_LEN - 1);

                    pthread_t file_thread;
                    if (pthread_create(&file_thread, NULL, handle_file_transfer, args) != 0) {
                        perror("Failed to create file transfer thread");
                        free(args);

                        message_t response;
                        response.type = MSG_ERROR;
                        strcpy(response.username, "SERVER");
                        strcpy(response.content, "Lỗi xử lý file");
                        send_message(client->socket_fd, &response);
                    } else {
                        pthread_detach(file_thread);
                        printf("[FILE] Thread xử lý file đã được tạo cho %s\n", client->username);
                    }
                } else {
                    message_t response;
                    response.type = MSG_ERROR;
                    strcpy(response.username, "SERVER");
                    strcpy(response.content, "Bạn chưa tham gia phòng nào");
                    send_message(client->socket_fd, &response);
                }
                break;
            }

            case MSG_LIST_ROOMS: {
                list_rooms(&g_server, client->socket_fd);
                break;
            }

            case MSG_QUIT: {
                if (client->current_room_id != -1) {
                    message_t broadcast;
                    broadcast.type = MSG_BROADCAST;
                    strcpy(broadcast.username, "SERVER");
                    snprintf(broadcast.content, MAX_MESSAGE_LEN, "%s đã rời khỏi phòng", client->username);
                    broadcast.is_encrypted = 0;
                    broadcast.timestamp = time(NULL);
                    broadcast_to_room(&g_server, client->current_room_id, &broadcast, client->client_id);
                    remove_client_from_room(&g_server, client->current_room_id, client);
                }

                // Remove client from server's client list
                
                pthread_mutex_lock(&g_server.clients_mutex);
                if (g_server.clients == client) {
                    g_server.clients = client->next;
                } else {
                    client_t* current = g_server.clients;
                    while (current && current->next != client) {
                        current = current->next;
                    }
                    if (current) {
                        current->next = client->next;
                    }
                }
                pthread_mutex_unlock(&g_server.clients_mutex);

                printf("Client %s đã ngắt kết nối\n", client->username);
                
                cleanup_client(client);
                return NULL;
            }

            default:
                break;
        }
    }

    // Cleanup on disconnect
    
    if (client->current_room_id != -1) {
        remove_client_from_room(&g_server, client->current_room_id, client);
    }

    pthread_mutex_lock(&g_server.clients_mutex);
    if (g_server.clients == client) {
        g_server.clients = client->next;
    } else {
        client_t* current = g_server.clients;
        while (current && current->next != client) {
            current = current->next;
        }
        if (current) {
            current->next = client->next;
        }
    }
    pthread_mutex_unlock(&g_server.clients_mutex);

    cleanup_client(client);
    return NULL;
}

int main() {
    printf("=== CHAT SERVER ===\n");
    printf("Khởi động server trên port %d...\n", SERVER_PORT);

    printf("=== CHAT SERVER WITH END-TO-END ENCRYPTION ===\n");
    printf("Server đang khởi động...\n");
    printf("Hỗ trợ mã hóa AES-256-CBC\n");
    printf("Listening on port %d...\n\n", SERVER_PORT);
    
    initialize_server();
    setup_server_socket(g_server.server_socket, SERVER_PORT);

    printf("Server đã sẵn sàng chấp nhận kết nối!\n");
    printf("Nhấn Ctrl+C để dừng server\n\n");

    
    printf("✓ Server ready!\n");
    printf("Press Ctrl+C to stop\n\n");
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(g_server.server_socket,
                                 (struct sockaddr*)&client_addr, &client_len);

        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        // Create new client
        
        client_t* new_client = (client_t*)safe_malloc(sizeof(client_t));
        new_client->socket_fd = client_socket;
        new_client->client_id = g_server.next_client_id++;
        new_client->current_room_id = -1;
        strcpy(new_client->username, "");

        // Add client to server's client list
        
        pthread_mutex_lock(&g_server.clients_mutex);
        new_client->next = g_server.clients;
        g_server.clients = new_client;
        pthread_mutex_unlock(&g_server.clients_mutex);

        // Create thread for client
        
        if (pthread_create(&new_client->thread_id, NULL, handle_client, new_client) != 0) {
            perror("Thread creation failed");
            cleanup_client(new_client);
            continue;
        }

        // Detach thread
        
        pthread_detach(new_client->thread_id);
    }

    cleanup_server();
    return 0;
}
