// config/Config.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <cstdlib>

class Config {
    std::unordered_map<std::string, std::string> values_;

public:
    void Load(const std::string& path = ".env") {
        // D'abord les variables d'environnement système
        // (priorité sur le fichier .env)
        std::ifstream file(path);
        if (!file.is_open()) return;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto sep = line.find('=');
            if (sep == std::string::npos) continue;
            std::string key = line.substr(0, sep);
            std::string val = line.substr(sep + 1);
            // Variable d'env système a priorité
            const char* env = std::getenv(key.c_str());
            values_[key] = env ? env : val;
        }
    }

    std::string Get(const std::string& key,
                    const std::string& default_val = "") const {
        // Vérifie d'abord les variables d'env système directement
        const char* env = std::getenv(key.c_str());
        if (env) return env;
        auto it = values_.find(key);
        return it != values_.end() ? it->second : default_val;
    }

    int GetInt(const std::string& key, int default_val = 0) const {
        std::string val = Get(key);
        if (val.empty()) return default_val;
        try { return std::stoi(val); }
        catch (...) { return default_val; }
    }
};