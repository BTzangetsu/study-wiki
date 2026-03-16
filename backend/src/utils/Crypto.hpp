// utils/Crypto.hpp
#pragma once

#include <string>
#include <cstdint>
#include <mutex>
#include <random>
#include <fstream>

// ================================================================
// SHA-256 maison — zéro dépendance
// ================================================================

namespace sha256_impl {

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t ch  (uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
inline uint32_t maj (uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t sig0(uint32_t x) { return rotr(x,2)  ^ rotr(x,13) ^ rotr(x,22); }
inline uint32_t sig1(uint32_t x) { return rotr(x,6)  ^ rotr(x,11) ^ rotr(x,25); }
inline uint32_t gam0(uint32_t x) { return rotr(x,7)  ^ rotr(x,18) ^ (x >> 3);   }
inline uint32_t gam1(uint32_t x) { return rotr(x,17) ^ rotr(x,19) ^ (x >> 10);  }

} // namespace sha256_impl

struct SHA256Digest { uint8_t bytes[32]; };

inline SHA256Digest sha256(const uint8_t* data, size_t len) {
    using namespace sha256_impl;

    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };

    // Padding
    size_t padded = ((len + 8) / 64 + 1) * 64;
    std::vector<uint8_t> msg(padded, 0);
    memcpy(msg.data(), data, len);
    msg[len] = 0x80;
    uint64_t bit_len = static_cast<uint64_t>(len) * 8;
    for (int i = 0; i < 8; ++i)
        msg[padded - 1 - i] = static_cast<uint8_t>(bit_len >> (i * 8));

    // Compression
    for (size_t chunk = 0; chunk < padded; chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (msg[chunk+i*4]   << 24) | (msg[chunk+i*4+1] << 16) |
                   (msg[chunk+i*4+2] <<  8) |  msg[chunk+i*4+3];
        for (int i = 16; i < 64; ++i)
            w[i] = gam1(w[i-2]) + w[i-7] + gam0(w[i-15]) + w[i-16];

        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],
                 e=h[4],f=h[5],g=h[6],hh=h[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + sig1(e) + ch(e,f,g) + K[i] + w[i];
            uint32_t t2 = sig0(a) + maj(a,b,c);
            hh=g; g=f; f=e; e=d+t1;
            d=c;  c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    SHA256Digest digest;
    for (int i = 0; i < 8; ++i) {
        digest.bytes[i*4+0] = (h[i] >> 24) & 0xff;
        digest.bytes[i*4+1] = (h[i] >> 16) & 0xff;
        digest.bytes[i*4+2] = (h[i] >>  8) & 0xff;
        digest.bytes[i*4+3] =  h[i]        & 0xff;
    }
    return digest;
}

inline SHA256Digest sha256(const std::string& s) {
    return sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

inline std::string sha256_hex(const std::string& s) {
    auto d = sha256(s);
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (auto b : d.bytes) {
        out += hex[(b >> 4) & 0xF];
        out += hex[b & 0xF];
    }
    return out;
}

// HMAC-SHA256
inline SHA256Digest hmac_sha256(const std::string& key, const std::string& msg) {
    uint8_t k_pad[64] = {};
    if (key.size() > 64) {
        auto dk = sha256(key);
        memcpy(k_pad, dk.bytes, 32);
    } else {
        memcpy(k_pad, key.data(), key.size());
    }
    uint8_t i_pad[64], o_pad[64];
    for (int i = 0; i < 64; ++i) {
        i_pad[i] = k_pad[i] ^ 0x36;
        o_pad[i] = k_pad[i] ^ 0x5c;
    }
    std::vector<uint8_t> inner(64 + msg.size());
    memcpy(inner.data(), i_pad, 64);
    memcpy(inner.data() + 64, msg.data(), msg.size());
    auto inner_hash = sha256(inner.data(), inner.size());

    std::vector<uint8_t> outer(64 + 32);
    memcpy(outer.data(), o_pad, 64);
    memcpy(outer.data() + 64, inner_hash.bytes, 32);
    return sha256(outer.data(), outer.size());
}

// ================================================================
// Hash de mot de passe : PBKDF2-SHA256 maison (itéré 100 000x)
// ================================================================

inline std::string pbkdf2_sha256(const std::string& password,
                                  const std::string& salt,
                                  int iterations = 100000)
{
    // PBKDF2 block 1 : U1 = HMAC(password, salt || 0x00000001)
    std::string salt_block = salt + "\x00\x00\x00\x01";
    auto u = hmac_sha256(password, salt_block);

    uint8_t result[32];
    memcpy(result, u.bytes, 32);

    // Ui = HMAC(password, U(i-1))   XOR accumulé
    for (int i = 1; i < iterations; ++i) {
        std::string prev(reinterpret_cast<char*>(u.bytes), 32);
        u = hmac_sha256(password, prev);
        for (int j = 0; j < 32; ++j)
            result[j] ^= u.bytes[j];
    }

    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (auto b : result) {
        out += hex[(b >> 4) & 0xF];
        out += hex[b & 0xF];
    }
    return out;
}

// ================================================================
// Token aléatoire — mt19937 seedé une seule fois
// ================================================================

inline std::string GenerateRandomToken() {
    static std::mutex m;
    static std::mt19937 rng(std::random_device{}());
    static const char charset[] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    std::lock_guard<std::mutex> lock(m);
    std::string token;
    token.reserve(64);
    for (size_t i = 0; i < 64; ++i)
        token += charset[dist(rng)];
    return token;
}