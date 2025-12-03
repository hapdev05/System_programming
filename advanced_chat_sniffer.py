#!/usr/bin/env python3
"""
🧠 Smart Chat Sniffer (Full Text + Hex)
• Lắng nghe tất cả gói tin TCP port 8080
• Nhận biết JOIN/LEAVE để duy trì danh sách client trong room
• Chỉ log tin nhắn BROADCAST thật giữa các client
• Hiển thị text + full hex dump
"""

import struct
from datetime import datetime
from scapy.all import sniff, TCP

# --- Cấu hình ---
SERVER_PORT = 8080
MAX_USERNAME_LEN = 50
MAX_MESSAGE_LEN = 500
MAX_ENCRYPTED_LEN = 1024

# --- Kiểu message ---
JOIN_TYPES = {10, 11}     # có thể thay bằng giá trị thật trong server bạn
LEAVE_TYPES = {12, 13}
BROADCAST_TYPE = 14

# --- Biến toàn cục ---
seen_packets = set()      # tránh log trùng
room_clients = {}         # room_id -> set(username)

# --- Giải mã gói tin ---
def parse_message(data):
    try:
        if len(data) < 100:
            return None

        offset = 0
        msg_type = struct.unpack('<I', data[offset:offset+4])[0]; offset += 4
        username = data[offset:offset+MAX_USERNAME_LEN].split(b'\x00')[0].decode('utf-8', errors='ignore'); offset += MAX_USERNAME_LEN

        content_bytes = data[offset:offset+MAX_MESSAGE_LEN]
        content = content_bytes.split(b'\x00')[0].decode('utf-8', errors='ignore'); offset += MAX_MESSAGE_LEN

        encrypted_content = data[offset:offset+MAX_ENCRYPTED_LEN]; offset += MAX_ENCRYPTED_LEN
        encrypted_len = struct.unpack('<I', data[offset:offset+4])[0]; offset += 4
        is_encrypted = struct.unpack('<I', data[offset:offset+4])[0]; offset += 4
        room_id = struct.unpack('<i', data[offset:offset+4])[0]; offset += 4
        client_id = struct.unpack('<i', data[offset:offset+4])[0]; offset += 4

        total_payload = data[:offset]
        return {
            "type_num": msg_type,
            "username": username,
            "content": content,
            "room_id": room_id,
            "payload": total_payload,
        }
    except Exception:
        return None

# --- In hex dump ---
def full_hexdump(data: bytes):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hexpart = " ".join(f"{b:02x}" for b in chunk).ljust(48)
        ascii_part = "".join(chr(b) if 32 <= b <= 126 else '.' for b in chunk)
        lines.append(f"{i:04x}: {hexpart} | {ascii_part}")
    return "\n".join(lines)

# --- Cập nhật danh sách room ---
def handle_control_message(msg):
    room_id = msg["room_id"]
    username = msg["username"]

    if room_id not in room_clients:
        room_clients[room_id] = set()

    if msg["type_num"] in JOIN_TYPES:
        room_clients[room_id].add(username)
    elif msg["type_num"] in LEAVE_TYPES:
        room_clients[room_id].discard(username)

# --- Log tin nhắn thật ---
def log_broadcast(msg):
    if not msg["username"] or msg["username"] == "SERVER":
        return

    room_id = msg["room_id"]
    username = msg["username"]
    content = msg["content"]

    key = (username, content, room_id)
    if key in seen_packets:
        return
    seen_packets.add(key)

    # Đảm bảo phòng đã có
    if room_id not in room_clients:
        room_clients[room_id] = set()
    room_clients[room_id].add(username)

    receivers = ", ".join(sorted(u for u in room_clients[room_id] if u not in {username, "SERVER"}))
    if not receivers:
        receivers = "(no other clients)"

    ts = datetime.now().strftime("%H:%M:%S")
    print("═" * 100)
    print(f"[{ts}] {username} ➜ {receivers} | Room {room_id}")
    print(f"📄 Text:\n{content if content else '(no readable text)'}")
    print("📦 HEX DUMP:")
    print(full_hexdump(msg["payload"]))
    print("═" * 100 + "\n")

# --- Xử lý gói tin ---
def packet_handler(pkt):
    if TCP not in pkt or not pkt[TCP].payload:
        return

    # 🔒 chỉ lấy gói tin từ server gửi xuống client
    if pkt[TCP].sport != SERVER_PORT:
        return

    data = bytes(pkt[TCP].payload)
    if len(data) <= 100:
        return

    msg = parse_message(data)
    if not msg:
        return

    if msg["type_num"] in JOIN_TYPES or msg["type_num"] in LEAVE_TYPES:
        handle_control_message(msg)
        return

    if msg["type_num"] == BROADCAST_TYPE:
        log_broadcast(msg)
        return

# --- Main ---
def main():
    print("================================================================================")
    print("🧠 Smart Chat Sniffer — Log text + hex, auto track JOIN/LEAVE")
    print("================================================================================")
    try:
        sniff(filter=f"tcp port {SERVER_PORT}", prn=packet_handler, store=0, iface="lo")
    except KeyboardInterrupt:
        print("\n🛑 Stopped.")
    except PermissionError:
        print("❌ Run as root: sudo python3 smart_chat_sniffer_fulltexthex.py")

if __name__ == "__main__":
    main()

