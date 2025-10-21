#include "../common/protocol.h"

// Global server instance
server_t g_server;

void initialize_server() {
    g_server.server_socket = create_socket();
    g_server.rooms = NULL;
    g_server.clients = NULL;
    g_server.next_room_id = 1;
    g_server.next_client_id = 1;
    pthread_mutex_init(&g_server.rooms_mutex, NULL);
    pthread_mutex_init(&g_server.clients_mutex, NULL);
}

void cleanup_server() {
    // Cleanup all rooms
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
}

void* handle_client(void* arg) {
    client_t* client = (client_t*)arg;
    message_t msg;
    
    printf("Client %s (ID: %d) đã kết nối\n", client->username, client->client_id);
    
    while (1) {
        if (receive_message(client->socket_fd, &msg) < 0) {
            printf("Client %s đã ngắt kết nối\n", client->username);
            break;
        }
        
        // Process message based on type
        switch (msg.type) {
            case MSG_JOIN: {
                // Client joining with username
                strcpy(client->username, msg.username);
                
                message_t response;
                response.type = MSG_WELCOME;
                strcpy(response.username, "SERVER");
                snprintf(response.content, MAX_MESSAGE_LEN, "Chào mừng %s đến với chat server!", client->username);
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
                    // Leave current room if any
                    if (client->current_room_id != -1) {
                        remove_client_from_room(&g_server, client->current_room_id, client);
                    }
                    
                    // Join new room
                    add_client_to_room(&g_server, msg.room_id, client);
                    
                    response.type = MSG_ROOM_JOINED;
                    strcpy(response.username, "SERVER");
                    strcpy(response.content, room->room_name);
                    response.room_id = room->room_id;
                    
                    // Notify other clients in room
                    message_t broadcast;
                    broadcast.type = MSG_BROADCAST;
                    strcpy(broadcast.username, "SERVER");
                    snprintf(broadcast.content, MAX_MESSAGE_LEN, "%s đã tham gia phòng", client->username);
                    broadcast_to_room(&g_server, msg.room_id, &broadcast, client->client_id);
                } else {
                    response.type = MSG_ERROR;
                    strcpy(response.username, "SERVER");
                    strcpy(response.content, "Phòng không tồn tại");
                }
                send_message(client->socket_fd, &response);
                break;
            }
            
            case MSG_LEAVE_ROOM: {
                if (client->current_room_id != -1) {
                    message_t broadcast;
                    broadcast.type = MSG_BROADCAST;
                    strcpy(broadcast.username, "SERVER");
                    snprintf(broadcast.content, MAX_MESSAGE_LEN, "%s đã rời khỏi phòng", client->username);
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
                    message_t broadcast;
                    broadcast.type = MSG_BROADCAST;
                    strcpy(broadcast.username, client->username);
                    strcpy(broadcast.content, msg.content);
                    broadcast_to_room(&g_server, client->current_room_id, &broadcast, -1);
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
                // Remove client from current room
                if (client->current_room_id != -1) {
                    message_t broadcast;
                    broadcast.type = MSG_BROADCAST;
                    strcpy(broadcast.username, "SERVER");
                    snprintf(broadcast.content, MAX_MESSAGE_LEN, "%s đã rời khỏi phòng", client->username);
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
    
    initialize_server();
    setup_server_socket(g_server.server_socket, SERVER_PORT);
    
    printf("Server đã sẵn sàng chấp nhận kết nối!\n");
    printf("Nhấn Ctrl+C để dừng server\n\n");
    
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
