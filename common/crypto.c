#include "crypto.h"
#include <stdlib.h>

void init_crypto() {
    OpenSSL_add_all_algorithms();
}

void cleanup_crypto() {
    EVP_cleanup();
}

void generate_room_key(room_crypto_t* crypto) {
    // Tạo key và IV ngẫu nhiên
    RAND_bytes(crypto->key, AES_KEY_SIZE);
    RAND_bytes(crypto->iv, AES_IV_SIZE);
}

int encrypt_message(const unsigned char* plaintext, int plaintext_len,
                   const unsigned char* key, const unsigned char* iv,
                   unsigned char* ciphertext) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    
    int len;
    int ciphertext_len;
    
    // Khởi tạo encryption
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    // Mã hóa plaintext
    if (EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len = len;
    
    // Finalize encryption
    if (EVP_EncryptFinal_ex(ctx, ciphertext + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    return ciphertext_len;
}

int decrypt_message(const unsigned char* ciphertext, int ciphertext_len,
                   const unsigned char* key, const unsigned char* iv,
                   unsigned char* plaintext) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    
    int len;
    int plaintext_len;
    
    // Khởi tạo decryption
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    // Giải mã ciphertext
    if (EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    plaintext_len = len;
    
    // Finalize decryption
    if (EVP_DecryptFinal_ex(ctx, plaintext + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    plaintext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    return plaintext_len;
}

void key_to_hex(const unsigned char* key, int key_len, char* hex_str) {
    for (int i = 0; i < key_len; i++) {
        sprintf(hex_str + (i * 2), "%02x", key[i]);
    }
    hex_str[key_len * 2] = '\0';
}

void hex_to_key(const char* hex_str, unsigned char* key, int key_len) {
    for (int i = 0; i < key_len; i++) {
        sscanf(hex_str + (i * 2), "%2hhx", &key[i]);
    }
}
