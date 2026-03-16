// middleware/RateLimiter.hpp
#pragma once

#include "crow.h"
#include <unordered_map>
#include <shared_mutex>
#include <chrono>
#include <string>

// ================================================================
// Token bucket par IP
// Chaque IP dispose d'un bucket de `capacity` tokens.
// Les tokens se régénèrent à raison de `refill_rate` par seconde.
// Chaque requête consomme 1 token.
// Bucket vide → 429 Too Many Requests.
//
// Valeurs par défaut : 60 requêtes, rechargé à 1/seconde
// → rafale max de 60 req, puis 1 req/sec en régime établi.
// ================================================================

struct TokenBucket {
    double   tokens;
    double   capacity;
    double   refill_rate;       // tokens par seconde
    std::chrono::steady_clock::time_point last_refill;

    TokenBucket(double cap, double rate)
        : tokens(cap), capacity(cap), refill_rate(rate),
          last_refill(std::chrono::steady_clock::now()) {}

    // Retourne true si la requête est autorisée
    bool Consume() {
        auto now     = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(
            now - last_refill).count();

        tokens = std::min(capacity, tokens + elapsed * refill_rate);
        last_refill = now;

        if (tokens >= 1.0) {
            tokens -= 1.0;
            return true;
        }
        return false;
    }
};

class RateLimiter {
public:
    // capacity     : tokens max dans le bucket (rafale max)
    // refill_rate  : tokens régénérés par seconde
    // cleanup_every: purge les IPs inactives toutes les N requêtes
    explicit RateLimiter(double capacity    = 60.0,
                         double refill_rate = 1.0,
                         int cleanup_every  = 10000)
        : capacity_(capacity),
          refill_rate_(refill_rate),
          cleanup_every_(cleanup_every),
          request_count_(0) {}

    // Retourne true si la requête est autorisée pour cette IP
    bool Allow(const std::string& ip) {
        {
            // Lecture : shared lock
            std::shared_lock read(mutex_);
            auto it = buckets_.find(ip);
            if (it != buckets_.end())
                return it->second.Consume();
        }
        // Bucket inexistant → création avec write lock
        std::unique_lock write(mutex_);
        // Double check après acquisition du write lock
        auto it = buckets_.find(ip);
        if (it != buckets_.end())
            return it->second.Consume();

        buckets_.emplace(ip, TokenBucket(capacity_, refill_rate_));
        return buckets_.at(ip).Consume();
    }

    // Middleware Crow — à appeler dans une lambda de route
    // ou via un before_handle global
    crow::response Check(const crow::request& req) {
        std::string ip = req.remote_ip_address;
        if (ip.empty()) ip = "unknown";

        MaybeCleanup();

        if (!Allow(ip)) {
            crow::response res(429);
            res.add_header("Retry-After", "1");
            res.write(R"({"error":"Too many requests"})");
            return res;
        }
        return crow::response(0); // 0 = "laisse passer"
    }

private:
    double      capacity_;
    double      refill_rate_;
    int         cleanup_every_;
    int         request_count_;

    std::unordered_map<std::string, TokenBucket> buckets_;
    mutable std::shared_mutex mutex_;

    // Supprime les buckets pleins (IPs inactives depuis longtemps)
    // pour éviter que la map grossisse indéfiniment
    void MaybeCleanup() {
        if (++request_count_ % cleanup_every_ != 0) return;

        std::unique_lock write(mutex_);
        for (auto it = buckets_.begin(); it != buckets_.end(); ) {
            // Bucket plein = IP inactive depuis assez longtemps
            // pour s'être rechargée complètement → on peut l'oublier
            if (it->second.tokens >= it->second.capacity)
                it = buckets_.erase(it);
            else
                ++it;
        }
    }
};