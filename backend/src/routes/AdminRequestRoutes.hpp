// routes/AdminRequestRoutes.hpp
#pragma once

#include "crow.h"
#include "../db/ConnectionPool.hpp"
#include "../db/QueryHelpers.hpp"
#include "../session/SessionManager.hpp"
#include "../middleware/RateLimiter.hpp"

class AdminRequestRoutes {
public:
    static void Register(crow::App<crow::CORSHandler>& app,
                         ConnectionPool& pool,
                         SessionManager& sessions,
                         RateLimiter& limiter)
    {
        // --------------------------------------------------------
        // POST /api/admin-requests
        // N'importe quel user connecté peut soumettre une demande.
        // Caché dans le front jusqu'à ce que tu actives la feature.
        // Body : { "reason": "..." }
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/admin-requests")
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

            // Déjà admin — pas besoin de demander
            if (session->is_admin)
                return crow::response(400,
                    R"({"error":"Already admin"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("reason"))
                return crow::response(400,
                    R"({"error":"Missing reason"})");

            std::string reason = std::string(body["reason"].s());
            if (reason.size() < 20 || reason.size() > 1000)
                return crow::response(400,
                    R"({"error":"Reason must be 20-1000 chars"})");

            try {
                auto db = pool.Acquire();

                // Vérifie qu'il n'y a pas déjà une demande en cours
                auto existing = QueryRows(db.get(),
                    "SELECT id, status FROM admin_requests "
                    "WHERE user_id = ? LIMIT 1",
                    {std::to_string(session->user_id)});

                if (!existing.empty()) {
                    std::string status = existing[0][1];
                    if (status == "pending")
                        return crow::response(409,
                            R"({"error":"Request already pending"})");
                    if (status == "approved")
                        return crow::response(409,
                            R"({"error":"Already approved"})");
                    // rejected → autorise une nouvelle demande
                    // on met à jour plutôt qu'insérer
                    Execute(db.get(),
                        "UPDATE admin_requests "
                        "SET reason = ?, status = 'pending', "
                        "reviewed_by = NULL, reviewed_at = NULL, "
                        "created_at = NOW() "
                        "WHERE user_id = ?",
                        {reason, std::to_string(session->user_id)});
                } else {
                    Execute(db.get(),
                        "INSERT INTO admin_requests (user_id, reason) "
                        "VALUES (?, ?)",
                        {std::to_string(session->user_id), reason});
                }

                return crow::response(201,
                    R"({"message":"Request submitted"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // GET /api/admin-requests  [super admin seulement]
        // Liste toutes les demandes en attente
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/admin-requests")
        .methods("GET"_method)
        ([&pool, &sessions](const crow::request& req) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session || !session->is_super_admin)
                return crow::response(403, R"({"error":"Forbidden"})");

            try {
                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT ar.id, ar.reason, ar.status, ar.created_at, "
                    "       u.username, u.id AS user_id, u.points, "
                    "       (SELECT COUNT(*) FROM documents "
                    "        WHERE user_id = u.id) AS doc_count "
                    "FROM admin_requests ar "
                    "JOIN users u ON u.id = ar.user_id "
                    "WHERE ar.status = 'pending' "
                    "ORDER BY ar.created_at ASC",
                    {});

                std::vector<crow::json::wvalue> requests;
                for (auto& r : rows) {
                    crow::json::wvalue req_obj;
                    req_obj["id"]         = std::stoi(r[0]);
                    req_obj["reason"]     = r[1];
                    req_obj["status"]     = r[2];
                    req_obj["created_at"] = r[3];
                    req_obj["username"]   = r[4];
                    req_obj["user_id"]    = std::stoi(r[5]);
                    req_obj["points"]     = std::stoi(r[6]);
                    req_obj["doc_count"]  = std::stoi(r[7]);
                    requests.push_back(std::move(req_obj));
                }

                crow::json::wvalue out;
                out["requests"] = std::move(requests);
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // PATCH /api/admin-requests/:id  [super admin seulement]
        // Body : { "status": "approved" | "rejected" }
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/admin-requests/<int>")
        .methods("PATCH"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session || !session->is_super_admin)
                return crow::response(403, R"({"error":"Forbidden"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("status"))
                return crow::response(400,
                    R"({"error":"Missing status"})");

            std::string status = std::string(body["status"].s());
            if (status != "approved" && status != "rejected")
                return crow::response(400,
                    R"({"error":"Invalid status"})");

            try {
                auto db = pool.Acquire();

                // Récupère le user_id associé à cette demande
                auto rows = QueryRows(db.get(),
                    "SELECT user_id FROM admin_requests "
                    "WHERE id = ? LIMIT 1",
                    {std::to_string(id)});

                if (rows.empty())
                    return crow::response(404,
                        R"({"error":"Request not found"})");

                int target_user_id = std::stoi(rows[0][0]);

                // Met à jour le statut de la demande
                Execute(db.get(),
                    "UPDATE admin_requests "
                    "SET status = ?, reviewed_by = ?, reviewed_at = NOW() "
                    "WHERE id = ?",
                    {status,
                     std::to_string(session->user_id),
                     std::to_string(id)});

                // Si approuvé → passe l'utilisateur admin
                if (status == "approved") {
                    Execute(db.get(),
                        "UPDATE users SET is_admin = 1 WHERE id = ?",
                        {std::to_string(target_user_id)});
                }

                return crow::response(200, R"({"message":"Updated"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // DELETE /api/admin-requests/:id  [super admin seulement]
        // Retire les droits admin à un utilisateur
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/admin/<int>/revoke")
        .methods("DELETE"_method)
        ([&pool, &sessions](const crow::request& req, int user_id) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session || !session->is_super_admin)
                return crow::response(403, R"({"error":"Forbidden"})");

            // Protection — ne peut pas se retirer ses propres droits
            if (user_id == session->user_id)
                return crow::response(400,
                    R"({"error":"Cannot revoke your own rights"})");

            try {
                auto db = pool.Acquire();

                // Vérifie que la cible n'est pas super admin
                auto rows = QueryRows(db.get(),
                    "SELECT is_super_admin FROM users "
                    "WHERE id = ? LIMIT 1",
                    {std::to_string(user_id)});

                if (rows.empty())
                    return crow::response(404,
                        R"({"error":"User not found"})");
                if (rows[0][0] == "1")
                    return crow::response(403,
                        R"({"error":"Cannot revoke super admin"})");

                Execute(db.get(),
                    "UPDATE users SET is_admin = 0 WHERE id = ?",
                    {std::to_string(user_id)});

                // Invalide ses sessions — il sera déconnecté
                sessions.DeleteSessionsForUser(user_id);

                return crow::response(200,
                    R"({"message":"Admin rights revoked"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });
    }
};