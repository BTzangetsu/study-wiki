// routes/UserRoutes.hpp
#pragma once

#include "crow.h"
#include "../db/ConnectionPool.hpp"
#include "../db/QueryHelpers.hpp"
#include "../session/SessionManager.hpp"

class UserRoutes {
public:
    static void Register(crow::App<crow::CORSHandler>& app,
                         ConnectionPool& pool,
                         SessionManager& sessions)
    {
        // --------------------------------------------------------
        // GET /api/users/:id  — profil public
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/users/<int>")
        .methods("GET"_method)
        ([&pool](const crow::request&, int id) {
            try {
                auto db = pool.Acquire();

                auto urows = QueryRows(db.get(),
                    "SELECT id, username, points, created_at, "
                    "       sc.name AS school "
                    "FROM users u "
                    "LEFT JOIN schools sc ON sc.id = u.school_id "
                    "WHERE u.id = ? AND u.is_active = 1 LIMIT 1",
                    {std::to_string(id)});

                if (urows.empty())
                    return crow::response(404, R"({"error":"User not found"})");

                auto& u = urows[0];

                // Documents uploadés par cet utilisateur
                auto drows = QueryRows(db.get(),
                    "SELECT d.id, d.title, d.type, d.year, "
                    "       d.avg_rating, d.download_count, d.created_at, "
                    "       s.name AS subject "
                    "FROM documents d "
                    "LEFT JOIN subjects s ON s.id = d.subject_id "
                    "WHERE d.user_id = ? AND d.is_approved = 1 "
                    "ORDER BY d.created_at DESC LIMIT 20",
                    {std::to_string(id)});

                // Badges de l'utilisateur
                auto brows = QueryRows(db.get(),
                    "SELECT b.name, b.description, b.icon_key, ub.earned_at "
                    "FROM user_badges ub "
                    "JOIN badges b ON b.id = ub.badge_id "
                    "WHERE ub.user_id = ? "
                    "ORDER BY ub.earned_at DESC",
                    {std::to_string(id)});

                crow::json::wvalue out;
                out["id"]         = std::stoi(u[0]);
                out["username"]   = u[1];
                out["points"]     = std::stoi(u[2]);
                out["created_at"] = u[3];
                out["school"]     = u[4];

                std::vector<crow::json::wvalue> docs;
                for (auto& r : drows) {
                    crow::json::wvalue d;
                    d["id"]             = std::stoi(r[0]);
                    d["title"]          = r[1];
                    d["type"]           = r[2];
                    d["year"]           = r[3];
                    d["avg_rating"]     = std::stod(r[4].empty() ? "0" : r[4]);
                    d["download_count"] = std::stoi(r[5].empty() ? "0" : r[5]);
                    d["created_at"]     = r[6];
                    d["subject"]        = r[7];
                    docs.push_back(std::move(d));
                }
                out["documents"] = std::move(docs);

                std::vector<crow::json::wvalue> badges;
                for (auto& r : brows) {
                    crow::json::wvalue b;
                    b["name"]        = r[0];
                    b["description"] = r[1];
                    b["icon_key"]    = r[2];
                    b["earned_at"]   = r[3];
                    badges.push_back(std::move(b));
                }
                out["badges"] = std::move(badges);

                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // GET /api/users/:id/documents  — tous les docs d'un user
        // avec pagination
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/users/<int>/documents")
        .methods("GET"_method)
        ([&pool](const crow::request& req, int id) {
            try {
                int page  = 1;
                int limit = 20;
                if (req.url_params.get("page"))
                    page  = std::max(1, std::stoi(req.url_params.get("page")));
                if (req.url_params.get("limit"))
                    limit = std::min(50, std::max(1,
                        std::stoi(req.url_params.get("limit"))));

                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT d.id, d.title, d.type, d.year, "
                    "       d.avg_rating, d.download_count, d.created_at, "
                    "       s.name, sc.name "
                    "FROM documents d "
                    "LEFT JOIN subjects s  ON s.id  = d.subject_id "
                    "LEFT JOIN schools sc  ON sc.id = d.school_id "
                    "WHERE d.user_id = ? AND d.is_approved = 1 "
                    "ORDER BY d.created_at DESC "
                    "LIMIT ? OFFSET ?",
                    {
                        std::to_string(id),
                        std::to_string(limit),
                        std::to_string((page - 1) * limit)
                    });

                std::vector<crow::json::wvalue> docs;
                for (auto& r : rows) {
                    crow::json::wvalue d;
                    d["id"]             = std::stoi(r[0]);
                    d["title"]          = r[1];
                    d["type"]           = r[2];
                    d["year"]           = r[3];
                    d["avg_rating"]     = std::stod(r[4].empty() ? "0" : r[4]);
                    d["download_count"] = std::stoi(r[5].empty() ? "0" : r[5]);
                    d["created_at"]     = r[6];
                    d["subject"]        = r[7];
                    d["school"]         = r[8];
                    docs.push_back(std::move(d));
                }

                crow::json::wvalue out;
                out["page"]      = page;
                out["limit"]     = limit;
                out["documents"] = std::move(docs);
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });
    }
};