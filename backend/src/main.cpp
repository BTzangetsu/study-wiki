// main.cpp
#include "crow.h"
#include "crow/middlewares/cors.h"

#include "middleware/RateLimiter.hpp"
#include "config/Config.hpp"
#include "db/ConnectionPool.hpp"
#include "session/SessionManager.hpp"
#include "routes/AuthRoutes.hpp"
#include "routes/DocumentRoutes.hpp"
#include "routes/CommentRoutes.hpp"
#include "routes/SearchRoutes.hpp"
#include "routes/UserRoutes.hpp"
#include "routes/LeaderboardRoutes.hpp"
#include "routes/ReportRoutes.hpp"
#include "routes/AdminRoutes.hpp"
#include "routes/AdminRequestRoutes.hpp"
#include "routes/SuggestionRoutes.hpp"

#include <thread>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{true};
void signal_handler(int) { g_running = false; }

int main() {
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    Config cfg;
    cfg.Load();

    DBConfig db_cfg {
        cfg.Get("DB_HOST",     "127.0.0.1"),
        cfg.Get("DB_USER",     "study_wiki"),
        cfg.Get("DB_PASS",     "study_wiki_pwd"),
        cfg.Get("DB_NAME",     "study_wiki_db"),
        static_cast<unsigned int>(cfg.GetInt("DB_PORT", 3306))
    };
    ConnectionPool pool(db_cfg, cfg.GetInt("DB_POOL_SIZE", 10));

    SessionManager sessions;
    RateLimiter    rate_limiter(60.0, 1.0);

    StorageConfig storage {
        cfg.Get("STORAGE_ENDPOINT", ""),
        cfg.Get("STORAGE_BUCKET",   ""),
        cfg.Get("STORAGE_SECRET",   ""),
        cfg.GetInt("STORAGE_URL_TTL", 900)
    };

    // ← app déclaré 
    crow::App<crow::CORSHandler> app;

    auto& cors = app.get_middleware<crow::CORSHandler>();
    cors
        .global()
        .origin(cfg.Get("ALLOWED_ORIGIN", "http://localhost:3000"))
        .methods("GET"_method, "POST"_method,
                 "PATCH"_method, "DELETE"_method)
        .headers("Content-Type", "Cookie")
        .allow_credentials();

    // Rate limiter passé aux routes qui en ont besoin
    AuthRoutes::Register       (app, pool, sessions, rate_limiter);
    DocumentRoutes::Register   (app, pool, sessions, storage);
    CommentRoutes::Register    (app, pool, sessions);
    SearchRoutes::Register     (app, pool);
    UserRoutes::Register       (app, pool, sessions);
    LeaderboardRoutes::Register(app, pool);
    ReportRoutes::Register     (app, pool, sessions);
    AdminRoutes::Register      (app, pool, sessions);
    AdminRequestRoutes::Register(app, pool, sessions, rate_limiter);
    SuggestionRoutes::Register (app, pool, sessions, rate_limiter);

    CROW_ROUTE(app, "/api/health")
    .methods("GET"_method)
    ([]() {
        crow::json::wvalue out;
        out["status"] = "ok";
        return crow::response(out);
    });

    std::thread purge_thread([&sessions]() {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::hours(1));
            if (g_running) sessions.Purge();
        }
    });
    purge_thread.detach();

    int port        = cfg.GetInt("PORT", 8080);
    int concurrency = cfg.GetInt("THREADS",
        static_cast<int>(std::thread::hardware_concurrency()));

    app.port(port).concurrency(concurrency).run();
    return 0;
}