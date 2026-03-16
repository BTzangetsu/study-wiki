#pragma once

#include<iostream>
#include<string>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include "../utils/Crypto.hpp"

// SessionManager.hpp
struct Session {
    int user_id;
    std::string username;
    std::string token;
    //time dedicated before session expires
    std::chrono::system_clock::time_point expires_after;
};

class SessionManager {
    std::unordered_map<int, Session> sessions_;
    std::shared_mutex mutex_;  // plusieurs lectures simultanées OK

public:
    std::string CreateSession(int user_id, const std::string& username,size_t duration_hours = 24) {
        std::string token = GenerateRandomToken();  // 64 bytes random hex
        Session s { user_id, username, token, std::chrono::system_clock::now() + std::chrono::hours(duration_hours) };
        std::unique_lock lock(mutex_);  // lock en écriture
        sessions_[user_id] = s;
        return token;
    }

    std::optional<Session> GetSession(int user_id) {
        std::shared_lock lock(mutex_);  // lock en lecture seulement
        auto it = sessions_.find(user_id);
        if (it == sessions_.end()) return std::nullopt;
        if (it->second.expires_after < std::chrono::system_clock::now()) return std::nullopt;
        return it->second;
    }

    void DeleteSession(int user_id) {
        std::unique_lock lock(mutex_);
        sessions_.erase(user_id);
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
};