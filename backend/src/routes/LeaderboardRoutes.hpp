// routes/LeaderboardRoutes.hpp
#pragma once

#include "crow.h"
#include "../db/ConnectionPool.hpp"
#include "../db/QueryHelpers.hpp"

class LeaderboardRoutes {
public:
    static void Register(crow::App<crow::CORSHandler>& app,
                         ConnectionPool& pool)
    {
        // --------------------------------------------------------
        // GET /api/leaderboard/users
        // ?limit= (max 100)
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/leaderboard/users")
        .methods("GET"_method)
        ([&pool](const crow::request& req) {
            try {
                int limit = 20;
                if (req.url_params.get("limit"))
                    limit = std::min(100, std::max(1,
                        std::stoi(req.url_params.get("limit"))));

                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT u.id, u.username, u.points, "
                    "       sc.name AS school, "
                    "       COUNT(d.id) AS upload_count "
                    "FROM users u "
                    "LEFT JOIN schools sc  ON sc.id = u.school_id "
                    "LEFT JOIN documents d ON d.user_id = u.id "
                    "                     AND d.is_approved = 1 "
                    "WHERE u.is_active = 1 "
                    "GROUP BY u.id "
                    "ORDER BY u.points DESC "
                    "LIMIT ?",
                    {std::to_string(limit)});

                std::vector<crow::json::wvalue> users;
                int rank = 1;
                for (auto& r : rows) {
                    crow::json::wvalue u;
                    u["rank"]         = rank++;
                    u["id"]           = std::stoi(r[0]);
                    u["username"]     = r[1];
                    u["points"]       = std::stoi(r[2]);
                    u["school"]       = r[3];
                    u["upload_count"] = std::stoi(r[4]);
                    users.push_back(std::move(u));
                }

                crow::json::wvalue out;
                out["leaderboard"] = std::move(users);
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // GET /api/leaderboard/schools
        // Classement par nombre de documents + téléchargements totaux
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/leaderboard/schools")
        .methods("GET"_method)
        ([&pool](const crow::request& req) {
            try {
                int limit = 20;
                if (req.url_params.get("limit"))
                    limit = std::min(100, std::max(1,
                        std::stoi(req.url_params.get("limit"))));

                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT sc.id, sc.name, sc.city, "
                    "       COUNT(d.id)          AS doc_count, "
                    "       SUM(d.download_count) AS total_downloads, "
                    "       COUNT(DISTINCT d.user_id) AS contributor_count "
                    "FROM schools sc "
                    "LEFT JOIN documents d ON d.school_id = sc.id "
                    "                     AND d.is_approved = 1 "
                    "GROUP BY sc.id "
                    "ORDER BY doc_count DESC, total_downloads DESC "
                    "LIMIT ?",
                    {std::to_string(limit)});

                std::vector<crow::json::wvalue> schools;
                int rank = 1;
                for (auto& r : rows) {
                    crow::json::wvalue s;
                    s["rank"]              = rank++;
                    s["id"]                = std::stoi(r[0]);
                    s["name"]              = r[1];
                    s["city"]              = r[2];
                    s["doc_count"]         = std::stoi(r[3].empty() ? "0" : r[3]);
                    s["total_downloads"]   = std::stoi(r[4].empty() ? "0" : r[4]);
                    s["contributor_count"] = std::stoi(r[5].empty() ? "0" : r[5]);
                    schools.push_back(std::move(s));
                }

                crow::json::wvalue out;
                out["leaderboard"] = std::move(schools);
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });
    }
};