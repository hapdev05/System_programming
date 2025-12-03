#include "../common/protocol.h"
#include <signal.h>
#ifdef _WIN32
    #include <direct.h>
    #define mkdir _mkdir
#else
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

typedef struct {
    int socket_fd;
    char username[MAX_USERNAME_LEN];
    int current_room_id;
    room_crypto_t current_room_crypto;
    int has_room_key;
    int encryption_enabled;  // Flag để biết room có bật mã hóa không
    pthread_t receive_thread;
    pthread_t input_thread;
    pthread_mutex_t socket_mutex;
    int running;
    int receiving_file;
} client_data_t;

client_data_t g_client;

void* receive_messages(void* arg) {
    (void)arg;
    message_t msg;

    while (g_client.running) {
        pthread_mutex_lock(&g_client.socket_mutex);
        if (g_client.receiving_file) {
            pthread_mutex_unlock(&g_client.socket_mutex);
            sleep(1);
            continue;
        }
        pthread_mutex_unlock(&g_client.socket_mutex);

        if (receive_message(g_client.socket_fd, &msg) < 0) {
            if (g_client.running) {
                printf("\nKết nối đến server bị ngắt!\n");
            }
            break;
        }

        // Update client state based on message type
        if (msg.type == MSG_ROOM_JOINED) {
            g_client.current_room_id = msg.room_id;
        } else if (msg.type == MSG_ROOM_LEFT) {
            g_client.current_room_id = -1;
        } else if (msg.type == MSG_FILE_NOTIFICATION) {
            print_message(&msg);
            printf("Đang nhận file...\n");

#ifdef _WIN32
            _mkdir("downloads");
#else
            mkdir("downloads", 0755);
#endif

            pthread_mutex_lock(&g_client.socket_mutex);
            g_client.receiving_file = 1;
            pthread_mutex_unlock(&g_client.socket_mutex);

            if (receive_file(g_client.socket_fd, "downloads") < 0) {
                printf("Lỗi nhận file!\n");
            } else {
                printf("File đã được lưu vào thư mục: downloads/\n");
            }

            // *** THÊM: Kết thúc nhận file ***
            pthread_mutex_lock(&g_client.socket_mutex);
            g_client.receiving_file = 0;
            pthread_mutex_unlock(&g_client.socket_mutex);

            continue;
        }

        print_message(&msg);
    }

    return NULL;
}

void* handle_input(void* arg) {
    (void)arg;
    char input[BUFFER_SIZE];
    char command[50];
    char content[MAX_MESSAGE_LEN];

    printf("\n=== CHAT CLIENT ===\n");
    printf("Các lệnh có sẵn:\n");
    printf("  /join <username>     - Đăng nhập với username\n");
    printf("  /create <room_name>  - Tạo phòng mới\n");
    printf("  /room <room_id>      - Tham gia phòng theo ID\n");
    printf("  /encrypt             - BẬT mã hóa cho phòng hiện tại\n");
    printf("  /leave               - Rời khỏi phòng hiện tại\n");
    printf("  /list                - Liệt kê tất cả phòng\n");
    printf("  /sendfile <filepath> - Gửi file vào phòng hiện tại\n");
    printf("  /quit                - Thoát chương trình\n");
    printf("  <message>            - Gửi tin nhắn (khi đã tham gia phòng)\n\n");

    printf("  <message>            - Gửi tin nhắn\n\n");
    while (g_client.running) {
        printf("> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        // Remove newline

        input[strcspn(input, "\n")] = 0;

        if (strlen(input) == 0) {
            continue;
        }

        // Parse command

        if (input[0] == '/') {
            sscanf(input, "%s %[^\n]", command, content);

            message_t msg;
            memset(&msg, 0, sizeof(message_t));

            pthread_mutex_lock(&g_client.socket_mutex);

            if (strcmp(command, "/join") == 0) {
                if (strlen(content) == 0) {
                    printf("Vui lòng nhập username!\n");
                    pthread_mutex_unlock(&g_client.socket_mutex);
                    continue;
                }

                msg.type = MSG_JOIN;
                strncpy(msg.username, content, MAX_USERNAME_LEN - 1);
                msg.username[MAX_USERNAME_LEN - 1] = '\0';
                strcpy(g_client.username, msg.username);

            } else if (strcmp(command, "/create") == 0) {
                if (strlen(content) == 0) {
                    printf("Vui lòng nhập tên phòng!\n");
                    pthread_mutex_unlock(&g_client.socket_mutex);
                    continue;
                }

                msg.type = MSG_CREATE_ROOM;
                strncpy(msg.content, content, MAX_MESSAGE_LEN - 1);
                msg.content[MAX_MESSAGE_LEN - 1] = '\0';

            } else if (strcmp(command, "/room") == 0) {
                int room_id = atoi(content);
                if (room_id <= 0) {
                    printf("Vui lòng nhập ID phòng hợp lệ!\n");
                    pthread_mutex_unlock(&g_client.socket_mutex);
                    continue;
                }

                msg.type = MSG_JOIN_ROOM;
                msg.room_id = room_id;


            } else if (strcmp(command, "/encrypt") == 0) {
                if (g_client.current_room_id == -1) {
                    printf("❌ Bạn cần tham gia phòng trước!\n");
                    pthread_mutex_unlock(&g_client.socket_mutex);
                    continue;
                }

                if (g_client.encryption_enabled) {
                    printf("ℹ️  Phòng này đã được mã hóa rồi!\n");
                    pthread_mutex_unlock(&g_client.socket_mutex);
                    continue;
                }

                msg.type = MSG_ENABLE_ENCRYPTION;
                printf("🔒 Đang bật mã hóa cho phòng...\n");

            } else if (strcmp(command, "/leave") == 0) {
                msg.type = MSG_LEAVE_ROOM;

            } else if (strcmp(command, "/list") == 0) {
                msg.type = MSG_LIST_ROOMS;

            } else if (strcmp(command, "/sendfile") == 0) {
                if (g_client.current_room_id == -1) {
                    printf("Bạn cần tham gia một phòng trước khi gửi file!\n");
                    pthread_mutex_unlock(&g_client.socket_mutex);
                    continue;
                }

                if (strlen(content) == 0) {
                    printf("Vui lòng nhập đường dẫn file!\n");
                    pthread_mutex_unlock(&g_client.socket_mutex);
                    continue;
                }

                FILE* test_file = fopen(content, "rb");
                if (!test_file) {
                    printf("Không thể mở file: %s\n", content);
                    pthread_mutex_unlock(&g_client.socket_mutex);
                    continue;
                }
                fclose(test_file);

                // *** THÊM: Đánh dấu đang gửi file ***
                g_client.receiving_file = 1;

                // Send file request
                msg.type = MSG_FILE_REQUEST;
                strncpy(msg.content, content, MAX_MESSAGE_LEN - 1);
                msg.content[MAX_MESSAGE_LEN - 1] = '\0';

                if (send_message(g_client.socket_fd, &msg) < 0) {
                    printf("Lỗi gửi yêu cầu file!\n");
                    g_client.receiving_file = 0;
                    pthread_mutex_unlock(&g_client.socket_mutex);
                    continue;
                }

                sleep(1);

                // Send file data
                if (send_file(g_client.socket_fd, content, g_client.current_room_id, g_client.username) < 0) {
                    printf("Lỗi gửi file!\n");
                } else {
                    printf("File đã được gửi thành công!\n");
                }

                // *** THÊM: Kết thúc gửi file ***
                g_client.receiving_file = 0;
                pthread_mutex_unlock(&g_client.socket_mutex);
                continue;
            } else if (strcmp(command, "/quit") == 0) {
                msg.type = MSG_QUIT;
                g_client.running = 0;

            } else {
                printf("Lệnh không hợp lệ: %s\n", command);
                pthread_mutex_unlock(&g_client.socket_mutex);
                continue;
            }

            if (send_message(g_client.socket_fd, &msg) < 0) {
                printf("Lỗi gửi tin nhắn!\n");
            }

            pthread_mutex_unlock(&g_client.socket_mutex);

            if (strcmp(command, "/quit") == 0) {
                break;
            }

        } else {
            // Regular message
            if (g_client.current_room_id == -1) {
                printf("Bạn cần tham gia một phòng trước khi gửi tin nhắn!\n");
                continue;
            }

            message_t msg;
            memset(&msg, 0, sizeof(message_t));
            msg.type = MSG_MESSAGE;
            strncpy(msg.content, input, MAX_MESSAGE_LEN - 1);
            msg.content[MAX_MESSAGE_LEN - 1] = '\0';


            // Kiểm tra xem có cần mã hóa không
            if (g_client.encryption_enabled && g_client.has_room_key) {
                // Mã hóa message
                if (encrypt_message_content(&msg, &g_client.current_room_crypto) != 0) {
                    printf("❌ Lỗi mã hóa tin nhắn!\n");
                    continue;
                }
            } else {
                // Gửi plaintext
                msg.is_encrypted = 0;
            }

            pthread_mutex_lock(&g_client.socket_mutex);
            if (send_message(g_client.socket_fd, &msg) < 0) {
                printf("Lỗi gửi tin nhắn!\n");
            }
            pthread_mutex_unlock(&g_client.socket_mutex);
        }
    }

    return NULL;
}

void cleanup_client_data() {
    g_client.running = 0;

    if (g_client.socket_fd != -1) {
        close(g_client.socket_fd);
    }

    pthread_mutex_destroy(&g_client.socket_mutex);
}

void signal_handler(int sig) {
    (void)sig;
    printf("\nĐang thoát...\n");
    cleanup_client_data();
    exit(0);
}

int main(int argc, char* argv[]) {
    const char* server_ip = "127.0.0.1";
    int server_port = SERVER_PORT;

    if (argc >= 2) {
        server_ip = argv[1];
    }
    if (argc >= 3) {
        server_port = atoi(argv[2]);
    }

    // Khởi tạo crypto library
    init_crypto();

    // Khởi tạo client
    g_client.socket_fd = -1;
    g_client.current_room_id = -1;
    g_client.has_room_key = 0;
    g_client.encryption_enabled = 0;
    g_client.running = 1;
    g_client.receiving_file = 0;
    pthread_mutex_init(&g_client.socket_mutex, NULL);

    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Kết nối đến server
    g_client.socket_fd = create_socket();

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        error_exit("Invalid server address");
    }

    if (connect(g_client.socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        error_exit("Connection failed");
    }

    printf("Đã kết nối đến server %s:%d\n", server_ip, server_port);

    // Tạo threads
    if (pthread_create(&g_client.receive_thread, NULL, receive_messages, NULL) != 0) {
        error_exit("Failed to create receive thread");
    }

    if (pthread_create(&g_client.input_thread, NULL, handle_input, NULL) != 0) {
        error_exit("Failed to create input thread");
    }

    // Đợi threads kết thúc
    pthread_join(g_client.receive_thread, NULL);
    pthread_join(g_client.input_thread, NULL);

    cleanup_client_data();
    cleanup_crypto();

    return 0;
}
