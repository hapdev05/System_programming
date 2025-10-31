
#ifndef CRYPTO_H
#define CRYPTO_H

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <string.h>
#include <stdio.h>

// Constants
#define AES_KEY_SIZE 32  // 256-bit key
#define AES_IV_SIZE 16   // 128-bit IV
#define AES_BLOCK_SIZE 16

// Cấu trúc lưu thông tin mã hóa của room
typedef struct {
    unsigned char key[AES_KEY_SIZE];
    unsigned char iv[AES_IV_SIZE];
} room_crypto_t;

// Khởi tạo OpenSSL
void init_crypto();

// Cleanup OpenSSL
void cleanup_crypto();

// Tạo key và IV ngẫu nhiên cho room
void generate_room_key(room_crypto_t* crypto);

// Mã hóa message
int encrypt_message(const unsigned char* plaintext, int plaintext_len,
                   const unsigned char* key, const unsigned char* iv,
                   unsigned char* ciphertext);

// Giải mã message
int decrypt_message(const unsigned char* ciphertext, int ciphertext_len,
                   const unsigned char* key, const unsigned char* iv,
                   unsigned char* plaintext);

// Chuyển key thành string hex để truyền qua mạng
void key_to_hex(const unsigned char* key, int key_len, char* hex_str);

// Chuyển string hex thành key
void hex_to_key(const char* hex_str, unsigned char* key, int key_len);

#endif // CRYPTO_H
