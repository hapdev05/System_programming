#include "../common/protocol.h"

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
    
    pthread_mutex_lock(&g_server.clients_mutex);
    client_t* client = g_server.clients;
    while (client) {
        client_t* next = client->next;
        cleanup_client(client);
        client = next;
    }
    pthread_mutex_unlock(&g_server.clients_mutex);
    
    pthread_mutex_destroy(&g_server.rooms_mutex);
    pthread_mutex_destroy(&g_server.clients_mutex);
    
    close(g_server.server_socket);
    cleanup_crypto();
}

void* handle_client(void* arg) {
    client_t* client = (client_t*)arg;
    message_t msg;

        printf("Client %s (ID: %d) đã kết nối\n", client->username, client->client_id);

    while (1) {
        if (receive_message(client->socket_fd, &msg) < 0) {
            break;
        }
        
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
                    
                    add_client_to_room(&g_server, msg.room_id, client);
                    
                    // Nếu phòng đã bật mã hóa, gửi key cho client
                    if (room->encryption_enabled) {
                        send_room_key_to_client(client->socket_fd, room);
                    }
                    
                    response.type = MSG_ROOM_JOINED;
                    strcpy(response.username, "SERVER");
                    strcpy(response.content, room->room_name);
                    response.room_id = room->room_id;
                    
                    // Thông báo cho các client khác
                    message_t broadcast;
                    broadcast.type = MSG_BROADCAST;
                    strcpy(broadcast.username, "SERVER");
                    snprintf(broadcast.content, MAX_MESSAGE_LEN, "%s đã tham gia phòng", client->username);
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
            
            default:
                break;
        }
    }
    
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
    printf("=== CHAT SERVER WITH END-TO-END ENCRYPTION ===\n");
    printf("Server đang khởi động...\n");
    printf("Hỗ trợ mã hóa AES-256-CBC\n");
    printf("Listening on port %d...\n\n", SERVER_PORT);
    
    initialize_server();
    setup_server_socket(g_server.server_socket, SERVER_PORT);
    
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
        
        client_t* new_client = (client_t*)safe_malloc(sizeof(client_t));
        new_client->socket_fd = client_socket;
        new_client->client_id = g_server.next_client_id++;
        new_client->current_room_id = -1;
        strcpy(new_client->username, "");
        
        pthread_mutex_lock(&g_server.clients_mutex);
        new_client->next = g_server.clients;
        g_server.clients = new_client;
        pthread_mutex_unlock(&g_server.clients_mutex);
        
        if (pthread_create(&new_client->thread_id, NULL, handle_client, new_client) != 0) {
            perror("Thread creation failed");
            cleanup_client(new_client);
            continue;
        }
        
        pthread_detach(new_client->thread_id);
    }
    
    cleanup_server();
    return 0;
}
