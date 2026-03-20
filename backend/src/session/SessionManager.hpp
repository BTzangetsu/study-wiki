// session/SessionManager.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <chrono>
#include "../utils/Crypto.hpp"

struct Session {
    int         user_id;
    std::string username;
    bool        is_admin;
    bool        is_super_admin;
    std::chrono::system_clock::time_point expires_at;
};

class SessionManager {
    std::unordered_map<std::string, Session> sessions_;
    mutable std::shared_mutex mutex_;

public:
    std::string CreateSession(int user_id,
                              const std::string& username,
                              bool is_admin = false,
                              bool is_super_admin = false,
                              size_t duration_hours = 24)
    {
        std::string token = GenerateRandomToken();
        Session s {
            user_id,
            username,
            is_admin,
            is_super_admin,
            std::chrono::system_clock::now() +
                std::chrono::hours(duration_hours)
        };
        std::unique_lock lock(mutex_);
        sessions_[token] = std::move(s);
        return token;
    }

    std::optional<Session> GetSession(const std::string& token) const {
        std::shared_lock lock(mutex_);
        auto it = sessions_.find(token);
        if (it == sessions_.end()) return std::nullopt;
        if (it->second.expires_at < std::chrono::system_clock::now())
            return std::nullopt;
        return it->second;
    }

    void DeleteSession(const std::string& token) {
        std::unique_lock lock(mutex_);
        sessions_.erase(token);
    }

    void DeleteSessionsForUser(int user_id) {
        std::unique_lock lock(mutex_);
        for (auto it = sessions_.begin(); it != sessions_.end(); ) {
            if (it->second.user_id == user_id)
                it = sessions_.erase(it);
            else
                ++it;
        }
    }

    void Purge() {
        auto now = std::chrono::system_clock::now();
        std::unique_lock lock(mutex_);
        for (auto it = sessions_.begin(); it != sessions_.end(); ) {
            if (it->second.expires_at < now)
                it = sessions_.erase(it);
            else
                ++it;
        }
    }
};