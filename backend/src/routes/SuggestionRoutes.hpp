// routes/SuggestionRoutes.hpp
#pragma once

#include "crow.h"
#include "../db/ConnectionPool.hpp"
#include "../db/QueryHelpers.hpp"
#include "../session/SessionManager.hpp"
#include "../middleware/RateLimiter.hpp"

class SuggestionRoutes {
public:
    static void Register(crow::App<crow::CORSHandler>& app,
                         ConnectionPool& pool,
                         SessionManager& sessions,
                         RateLimiter&    limiter)
    {
        // --------------------------------------------------------
        // GET /api/suggestions
        // ?status=pending&page=&limit=
        // has_voted calculé selon l'user connecté (optionnel)
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/suggestions")
        .methods("GET"_method)
        ([&pool, &sessions](const crow::request& req) {
            try {
                // User connecté optionnel — has_voted dépend de lui
                std::string token = ExtractSessionToken(
                    req.get_header_value("Cookie"));
                auto session = sessions.GetSession(token);
                int current_user_id = session ? session->user_id : -1;

                std::string status = "pending";
                if (req.url_params.get("status"))
                    status = req.url_params.get("status");

                static const std::vector<std::string> valid_status = {
                    "pending", "planned", "done", "rejected"
                };
                if (std::find(valid_status.begin(), valid_status.end(), status)
                        == valid_status.end())
                    return crow::response(400,
                        R"({"error":"Invalid status"})");

                int page  = 1;
                int limit = 20;
                if (req.url_params.get("page"))
                    page  = std::max(1,
                        std::stoi(req.url_params.get("page")));
                if (req.url_params.get("limit"))
                    limit = std::min(50, std::max(1,
                        std::stoi(req.url_params.get("limit"))));

                auto db = pool.Acquire();

                // LEFT JOIN sur suggestion_votes filtré par l'user courant
                // sv.user_id NOT NULL → a voté, NULL → n'a pas voté
                auto rows = QueryRows(db.get(),
                    "SELECT s.id, s.title, s.description, s.status, "
                    "       s.upvotes, s.created_at, "
                    "       u.username, "
                    "       sv.user_id AS has_voted "
                    "FROM suggestions s "
                    "JOIN users u ON u.id = s.user_id "
                    "LEFT JOIN suggestion_votes sv "
                    "       ON sv.suggestion_id = s.id "
                    "       AND sv.user_id = ? "
                    "WHERE s.status = ? "
                    "ORDER BY s.upvotes DESC, s.created_at DESC "
                    "LIMIT ? OFFSET ?",
                    {
                        std::to_string(current_user_id),
                        status,
                        std::to_string(limit),
                        std::to_string((page - 1) * limit)
                    });

                std::vector<crow::json::wvalue> suggestions;
                for (auto& r : rows) {
                    crow::json::wvalue s;
                    s["id"]          = std::stoi(r[0]);
                    s["title"]       = r[1];
                    s["description"] = r[2];
                    s["status"]      = r[3];
                    s["upvotes"]     = std::stoi(r[4].empty() ? "0" : r[4]);
                    s["created_at"]  = r[5];
                    s["username"]    = r[6];
                    s["has_voted"]   = !r[7].empty(); // NULL → false
                    suggestions.push_back(std::move(s));
                }

                crow::json::wvalue out;
                out["page"]        = page;
                out["limit"]       = limit;
                out["status"]      = status;
                out["suggestions"] = std::move(suggestions);
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // POST /api/suggestions  [auth requise]
        // Body : { "title": "...", "description": "..." }
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/suggestions")
        .methods("POST"_method)
        ([&pool, &sessions, &limiter](const crow::request& req) {
            if (limiter.Check(req).code == 429)
                return crow::response(429,
                    R"({"error":"Too many requests"})");

            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401,
                    R"({"error":"Not authenticated"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("title") || !body.has("description"))
                return crow::response(400,
                    R"({"error":"Missing title or description"})");

            std::string title       = json_str(body, "title");
            std::string description = json_str(body, "description");

            if (title.empty() || title.size() > 200)
                return crow::response(400,
                    R"({"error":"Title must be 1-200 chars"})");
            if (description.empty() || description.size() > 2000)
                return crow::response(400,
                    R"({"error":"Description must be 1-2000 chars"})");

            try {
                auto db = pool.Acquire();

                // Anti-spam : max 3 suggestions par user en attente
                auto pending = QueryRows(db.get(),
                    "SELECT COUNT(*) FROM suggestions "
                    "WHERE user_id = ? AND status = 'pending'",
                    {std::to_string(session->user_id)});
                if (!pending.empty() && std::stoi(pending[0][0]) >= 3)
                    return crow::response(429,
                        R"({"error":"Max 3 pending suggestions at a time"})");

                uint64_t sug_id = Execute(db.get(),
                    "INSERT INTO suggestions (user_id, title, description) "
                    "VALUES (?, ?, ?)",
                    {
                        std::to_string(session->user_id),
                        title,
                        description
                    });

                crow::json::wvalue out;
                out["id"]      = static_cast<int>(sug_id);
                out["message"] = "Suggestion created";
                return crow::response(201, out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // GET /api/suggestions/:id  — détail d'une suggestion
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/suggestions/<int>")
        .methods("GET"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            int current_user_id = session ? session->user_id : -1;

            try {
                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT s.id, s.title, s.description, s.status, "
                    "       s.upvotes, s.created_at, u.username, "
                    "       sv.user_id AS has_voted "
                    "FROM suggestions s "
                    "JOIN users u ON u.id = s.user_id "
                    "LEFT JOIN suggestion_votes sv "
                    "       ON sv.suggestion_id = s.id "
                    "       AND sv.user_id = ? "
                    "WHERE s.id = ? LIMIT 1",
                    {std::to_string(current_user_id), std::to_string(id)});

                if (rows.empty())
                    return crow::response(404,
                        R"({"error":"Suggestion not found"})");

                auto& r = rows[0];
                crow::json::wvalue out;
                out["id"]          = std::stoi(r[0]);
                out["title"]       = r[1];
                out["description"] = r[2];
                out["status"]      = r[3];
                out["upvotes"]     = std::stoi(r[4].empty() ? "0" : r[4]);
                out["created_at"]  = r[5];
                out["username"]    = r[6];
                out["has_voted"]   = !r[7].empty();
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // POST /api/suggestions/:id/vote  [auth requise]
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/suggestions/<int>/vote")
        .methods("POST"_method)
        ([&pool, &sessions, &limiter](const crow::request& req, int id) {
            if (limiter.Check(req).code == 429)
                return crow::response(429,
                    R"({"error":"Too many requests"})");

            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401,
                    R"({"error":"Not authenticated"})");

            try {
                auto db = pool.Acquire();

                // Vérifie que la suggestion existe
                auto check = QueryRows(db.get(),
                    "SELECT id FROM suggestions WHERE id = ? LIMIT 1",
                    {std::to_string(id)});
                if (check.empty())
                    return crow::response(404,
                        R"({"error":"Suggestion not found"})");

                // INSERT IGNORE — idempotent si déjà voté
                auto result = Execute(db.get(),
                    "INSERT IGNORE INTO suggestion_votes "
                    "(user_id, suggestion_id) VALUES (?, ?)",
                    {std::to_string(session->user_id), std::to_string(id)});

                // Recalcul upvotes seulement si le vote a bien été inséré
                if (result > 0) {
                    Execute(db.get(),
                        "UPDATE suggestions SET upvotes = "
                        "(SELECT COUNT(*) FROM suggestion_votes "
                        " WHERE suggestion_id = ?) "
                        "WHERE id = ?",
                        {std::to_string(id), std::to_string(id)});
                }

                return crow::response(200, R"({"message":"Voted"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // DELETE /api/suggestions/:id/vote  [auth requise]
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/suggestions/<int>/vote")
        .methods("DELETE"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401,
                    R"({"error":"Not authenticated"})");

            try {
                auto db = pool.Acquire();

                Execute(db.get(),
                    "DELETE FROM suggestion_votes "
                    "WHERE user_id = ? AND suggestion_id = ?",
                    {std::to_string(session->user_id), std::to_string(id)});

                Execute(db.get(),
                    "UPDATE suggestions SET upvotes = "
                    "(SELECT COUNT(*) FROM suggestion_votes "
                    " WHERE suggestion_id = ?) "
                    "WHERE id = ?",
                    {std::to_string(id), std::to_string(id)});

                return crow::response(200, R"({"message":"Vote removed"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // DELETE /api/suggestions/:id  [auth requise, owner ou admin]
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/suggestions/<int>")
        .methods("DELETE"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401,
                    R"({"error":"Not authenticated"})");

            try {
                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT user_id FROM suggestions WHERE id = ? LIMIT 1",
                    {std::to_string(id)});

                if (rows.empty())
                    return crow::response(404,
                        R"({"error":"Suggestion not found"})");

                bool is_owner = std::stoi(rows[0][0]) == session->user_id;
                if (!is_owner && !session->is_admin)
                    return crow::response(403,
                        R"({"error":"Forbidden"})");

                Execute(db.get(),
                    "DELETE FROM suggestions WHERE id = ?",
                    {std::to_string(id)});

                return crow::response(200,
                    R"({"message":"Suggestion deleted"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });
    }
};