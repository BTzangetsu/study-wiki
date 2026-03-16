// db/ConnectionPool.hpp
#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <mysql/mysql.h>

struct DBConfig {
    std::string host;
    std::string user;
    std::string password;
    std::string database;
    unsigned int port = 3306;
};

// RAII : la connexion se remet dans le pool automatiquement à la destruction
class PooledConnection {
public:
    PooledConnection(MYSQL* conn, std::queue<MYSQL*>& pool,
                     std::mutex& mutex, std::condition_variable& cv)
        : conn_(conn), pool_(pool), mutex_(mutex), cv_(cv) {}

    ~PooledConnection() {
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.push(conn_);
        cv_.notify_one();
    }

    MYSQL* get() { return conn_; }
    MYSQL* operator->() { return conn_; }

    // non copiable, déplaçable
    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;

private:
    MYSQL*                    conn_;
    std::queue<MYSQL*>&       pool_;
    std::mutex&               mutex_;
    std::condition_variable&  cv_;
};

class ConnectionPool {
public:
    ConnectionPool(const DBConfig& config, size_t pool_size = 10)
        : config_(config)
    {
        for (size_t i = 0; i < pool_size; ++i)
            pool_.push(CreateConnection());
    }

    ~ConnectionPool() {
        while (!pool_.empty()) {
            mysql_close(pool_.front());
            pool_.pop();
        }
    }

    // Bloque jusqu'à ce qu'une connexion soit disponible
    PooledConnection Acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]{ return !pool_.empty(); });
        MYSQL* conn = pool_.front();
        pool_.pop();
        // Ping pour reconnecter si la connexion a été coupée (timeout MariaDB)
        if (mysql_ping(conn) != 0) {
            mysql_close(conn);
            conn = CreateConnection();
        }
        return PooledConnection(conn, pool_, mutex_, cv_);
    }

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

private:
    MYSQL* CreateConnection() {
        MYSQL* conn = mysql_init(nullptr);
        if (!conn) throw std::runtime_error("mysql_init failed");
        if (!mysql_real_connect(conn,
                config_.host.c_str(),
                config_.user.c_str(),
                config_.password.c_str(),
                config_.database.c_str(),
                config_.port, nullptr, 0))
        {
            std::string err = mysql_error(conn);
            mysql_close(conn);
            throw std::runtime_error("MariaDB connect failed: " + err);
        }
        // Reconnexion automatique activée
        bool reconnect = true;
        mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);
        return conn;
    }

    DBConfig                 config_;
    std::queue<MYSQL*>       pool_;
    std::mutex               mutex_;
    std::condition_variable  cv_;
};