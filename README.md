# Chat Client-Server với Đa luồng và Đồng bộ hóa

Chương trình chat client-server được viết bằng C, sử dụng đa luồng (multithreading) và đồng bộ hóa (threading & synchronization).

## Tính năng

- **Chat nhiều phòng**: Hỗ trợ tạo và tham gia nhiều phòng chat
- **Đa luồng**: Server xử lý nhiều client đồng thời, client có thread riêng cho nhận/gửi tin nhắn
- **Đồng bộ hóa**: Sử dụng mutex để đảm bảo thread-safe
- **Tìm phòng theo ID**: Client có thể tìm và tham gia phòng theo ID
- **Username**: Client bắt đầu bằng việc nhập username

## Cấu trúc Project

```
chat_system/
├── server/
│   └── server.c          # Server chính
├── client/
│   └── client.c          # Client chính
├── common/
│   ├── protocol.h        # Định nghĩa protocol và cấu trúc
│   └── utils.c           # Utility functions
├── Makefile              # Build system
└── README.md             # Tài liệu này
```

## Cài đặt và Build

### Yêu cầu hệ thống

- GCC compiler
- POSIX threads (pthread)
- Linux/macOS

### Build

```bash
# Build tất cả
make all

# Hoặc build riêng lẻ
make chat_server
make chat_client

# Build với debug symbols
make debug

# Build optimized release
make release
```

## Sử dụng

### 1. Chạy Server

```bash
# Terminal 1
./chat_server
```

### 2. Chạy Client

```bash
# Terminal 2 (và các terminal khác cho nhiều client)
./chat_client

# Hoặc kết nối đến server khác
./chat_client 192.168.1.100 8080
```

## Các lệnh Client

| Lệnh                  | Mô tả                                |
| --------------------- | ------------------------------------ |
| `/join <username>`    | Đăng nhập với username               |
| `/create <room_name>` | Tạo phòng mới                        |
| `/room <room_id>`     | Tham gia phòng theo ID               |
| `/leave`              | Rời khỏi phòng hiện tại              |
| `/list`               | Liệt kê tất cả phòng                 |
| `/quit`               | Thoát chương trình                   |
| `<message>`           | Gửi tin nhắn (khi đã tham gia phòng) |

## Ví dụ sử dụng

### Client 1:

```
> /join alice
[12:34:56] Chào mừng alice đến với chat server!
> /create phong1
[12:34:57] Phòng phong1 đã được tạo với ID: 1
> /room 1
[12:34:58] Đã tham gia phòng phong1 (ID: 1)
> Xin chào mọi người!
[12:34:59] alice: Xin chào mọi người!
```

### Client 2:

```
> /join bob
[12:35:00] Chào mừng bob đến với chat server!
> /list
[12:35:01] Danh sách phòng: ID:1 Name:phong1 Members:1
> /room 1
[12:35:02] Đã tham gia phòng phong1 (ID: 1)
[12:35:02] SERVER: bob đã tham gia phòng
[12:35:02] alice: Xin chào mọi người!
> Chào alice!
[12:35:03] bob: Chào alice!
```

## Kiến trúc Threading

### Server

- **Main thread**: Chấp nhận kết nối mới
- **Client handler threads**: Mỗi client có 1 thread riêng
- **Mutex locks**: Đồng bộ hóa truy cập shared data

### Client

- **Main thread**: Xử lý input từ user
- **Receive thread**: Nhận messages từ server
- **Mutex**: Đồng bộ hóa socket operations

## Đồng bộ hóa

- **Room mutex**: Mỗi phòng có mutex riêng cho thread-safe broadcasting
- **Global mutex**: Bảo vệ danh sách rooms và clients
- **Socket mutex**: Client bảo vệ socket operations

## Protocol

### Message Types

- `MSG_JOIN`: Client đăng nhập
- `MSG_CREATE_ROOM`: Tạo phòng mới
- `MSG_JOIN_ROOM`: Tham gia phòng
- `MSG_LEAVE_ROOM`: Rời phòng
- `MSG_MESSAGE`: Gửi tin nhắn
- `MSG_LIST_ROOMS`: Liệt kê phòng
- `MSG_QUIT`: Thoát
- `MSG_BROADCAST`: Broadcast tin nhắn
- `MSG_ERROR`: Thông báo lỗi

## Testing

```bash
# Chạy test tự động
make test

# Test thủ công
# Terminal 1: ./chat_server
# Terminal 2: ./chat_client
# Terminal 3: ./chat_client
```

## Troubleshooting

### Lỗi thường gặp

1. **"Address already in use"**

   ```bash
   # Tìm và kill process đang dùng port
   lsof -i :8080
   kill -9 <PID>
   ```

2. **"Connection refused"**

   - Đảm bảo server đang chạy
   - Kiểm tra IP và port

3. **Thread errors**
   - Đảm bảo có pthread library
   - Kiểm tra compiler flags

### Debug

```bash
# Build với debug symbols
make debug

# Chạy với gdb
gdb ./chat_server
gdb ./chat_client
```

## Tác giả

Chương trình được phát triển như một bài tập về:

- Socket programming
- Multithreading với pthread
- Thread synchronization
- Client-Server architecture
