// routes/AdminRoutes.hpp
#pragma once

#include "crow.h"
#include "../db/ConnectionPool.hpp"
#include "../db/QueryHelpers.hpp"
#include "../session/SessionManager.hpp"

class AdminRoutes {
public:
    static void Register(crow::App<crow::CORSHandler>& app,
                         ConnectionPool& pool,
                         SessionManager& sessions)
    {
        // --------------------------------------------------------
        // Helper lambda — vérifie admin, factorise la vérification
        // --------------------------------------------------------
        auto require_admin = [&sessions](const crow::request& req)
            -> std::optional<Session>
        {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session || !session->is_admin) return std::nullopt;
            return session;
        };

        // --------------------------------------------------------
        // GET /api/admin/reports?status=pending&page=&limit=
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/admin/reports")
        .methods("GET"_method)
        ([&pool, require_admin](const crow::request& req) {
            if (!require_admin(req))
                return crow::response(403, R"({"error":"Forbidden"})");

            try {
                std::string status = "pending";
                if (req.url_params.get("status"))
                    status = req.url_params.get("status");

                int page  = 1;
                int limit = 20;
                if (req.url_params.get("page"))
                    page  = std::max(1, std::stoi(req.url_params.get("page")));
                if (req.url_params.get("limit"))
                    limit = std::min(100, std::max(1,
                        std::stoi(req.url_params.get("limit"))));

                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT r.id, r.reason, r.details, r.status, r.created_at, "
                    "       r.document_id, r.comment_id, "
                    "       u.username AS reporter, "
                    "       d.title    AS doc_title "
                    "FROM reports r "
                    "JOIN users u        ON u.id = r.user_id "
                    "LEFT JOIN documents d ON d.id = r.document_id "
                    "WHERE r.status = ? "
                    "ORDER BY r.created_at ASC "
                    "LIMIT ? OFFSET ?",
                    {status,
                     std::to_string(limit),
                     std::to_string((page - 1) * limit)});

                std::vector<crow::json::wvalue> reports;
                for (auto& r : rows) {
                    crow::json::wvalue rep;
                    rep["id"]          = std::stoi(r[0]);
                    rep["reason"]      = r[1];
                    rep["details"]     = r[2];
                    rep["status"]      = r[3];
                    rep["created_at"]  = r[4];
                    rep["document_id"] = r[5].empty() ? -1 : std::stoi(r[5]);
                    rep["comment_id"]  = r[6].empty() ? -1 : std::stoi(r[6]);
                    rep["reporter"]    = r[7];
                    rep["doc_title"]   = r[8];
                    reports.push_back(std::move(rep));
                }

                crow::json::wvalue out;
                out["page"]    = page;
                out["limit"]   = limit;
                out["reports"] = std::move(reports);
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // GET /api/admin/stats
        CROW_ROUTE(app, "/api/admin/stats")
        .methods("GET"_method)
        ([&pool, require_admin](const crow::request& req) {
            if (!require_admin(req))
                return crow::response(403, R"({"error":"Forbidden"})");
            try {
                auto db = pool.Acquire();
                auto r = QueryRows(db.get(),
                    "SELECT "
                    " (SELECT COUNT(*) FROM reports     WHERE status='pending')      AS pending_reports, "
                    " (SELECT COUNT(*) FROM documents   WHERE is_approved=0)         AS pending_documents, "
                    " (SELECT COUNT(*) FROM suggestions WHERE status='pending')      AS pending_suggestions, "
                    " (SELECT COUNT(*) FROM users       WHERE is_active=1)           AS total_users, "
                    " (SELECT COUNT(*) FROM documents   WHERE is_approved=1)         AS total_documents, "
                    " (SELECT COUNT(*) FROM users       WHERE DATE(created_at)=CURDATE()) AS new_users_today, "
                    " (SELECT COUNT(*) FROM documents   WHERE DATE(created_at)=CURDATE()) AS new_docs_today ",
                    {});

                crow::json::wvalue out;
                if (!r.empty()) {
                    out["pending_reports"]      = std::stoi(r[0][0]);
                    out["pending_documents"]    = std::stoi(r[0][1]);
                    out["pending_suggestions"]  = std::stoi(r[0][2]);
                    out["total_users"]          = std::stoi(r[0][3]);
                    out["total_documents"]      = std::stoi(r[0][4]);
                    out["new_users_today"]      = std::stoi(r[0][5]);
                    out["new_docs_today"]       = std::stoi(r[0][6]);
                }
                return crow::response(out);
            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // PATCH /api/admin/suggestions/:id
        CROW_ROUTE(app, "/api/admin/suggestions/<int>")
        .methods("PATCH"_method)
        ([&pool, require_admin](const crow::request& req, int id) {
            if (!require_admin(req))
                return crow::response(403, R"({"error":"Forbidden"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("status"))
                return crow::response(400, R"({"error":"Missing status"})");

            std::string status = body["status"].s();
            static const std::vector<std::string> valid = {
                "planned", "done", "rejected", "pending"
            };
            if (std::find(valid.begin(), valid.end(), status) == valid.end())
                return crow::response(400, R"({"error":"Invalid status"})");

            try {
                auto db = pool.Acquire();
                Execute(db.get(),
                    "UPDATE suggestions SET status = ? WHERE id = ?",
                    {status, std::to_string(id)});
                return crow::response(200, R"({"message":"Updated"})");
            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // PATCH /api/admin/reports/:id
        // Body : { "status": "reviewed" | "dismissed" }
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/admin/reports/<int>")
        .methods("PATCH"_method)
        ([&pool, require_admin](const crow::request& req, int id) {
            if (!require_admin(req))
                return crow::response(403, R"({"error":"Forbidden"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("status"))
                return crow::response(400, R"({"error":"Missing status"})");

            std::string status = body["status"].s();
            if (status != "reviewed" && status != "dismissed")
                return crow::response(400, R"({"error":"Invalid status"})");

            try {
                auto db = pool.Acquire();
                Execute(db.get(),
                    "UPDATE reports SET status = ? WHERE id = ?",
                    {status, std::to_string(id)});
                return crow::response(200, R"({"message":"Report updated"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // PATCH /api/admin/documents/:id
        // Body : { "is_approved": 0 | 1 }
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/admin/documents/<int>")
        .methods("PATCH"_method)
        ([&pool, require_admin](const crow::request& req, int id) {
            if (!require_admin(req))
                return crow::response(403, R"({"error":"Forbidden"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("is_approved"))
                return crow::response(400, R"({"error":"Missing is_approved"})");

            int approved = static_cast<int>(body["is_approved"].i());
            if (approved != 0 && approved != 1)
                return crow::response(400, R"({"error":"is_approved must be 0 or 1"})");

            try {
                auto db = pool.Acquire();
                Execute(db.get(),
                    "UPDATE documents SET is_approved = ? WHERE id = ?",
                    {std::to_string(approved), std::to_string(id)});
                return crow::response(200, R"({"message":"Document updated"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // DELETE /api/admin/documents/:id
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/admin/documents/<int>")
        .methods("DELETE"_method)
        ([&pool, require_admin](const crow::request& req, int id) {
            if (!require_admin(req))
                return crow::response(403, R"({"error":"Forbidden"})");

            try {
                auto db = pool.Acquire();
                Execute(db.get(),
                    "DELETE FROM documents WHERE id = ?",
                    {std::to_string(id)});
                return crow::response(200, R"({"message":"Document deleted"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // DELETE /api/admin/comments/:id
        // Hard delete admin — supprime le noeud et tous ses enfants
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/admin/comments/<int>")
        .methods("DELETE"_method)
        ([&pool, require_admin](const crow::request& req, int id) {
            if (!require_admin(req))
                return crow::response(403, R"({"error":"Forbidden"})");

            try {
                auto db = pool.Acquire();
                // ON DELETE CASCADE dans le schéma supprime les enfants
                Execute(db.get(),
                    "DELETE FROM comments WHERE id = ?",
                    {std::to_string(id)});
                return crow::response(200, R"({"message":"Comment deleted"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // PATCH /api/admin/users/:id
        // Body : { "is_active": 0 | 1 }  — bannir / débannir
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/admin/users/<int>")
        .methods("PATCH"_method)
        ([&pool, &sessions, require_admin](const crow::request& req, int id) {
            if (!require_admin(req))
                return crow::response(403, R"({"error":"Forbidden"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("is_active"))
                return crow::response(400, R"({"error":"Missing is_active"})");

            int active = static_cast<int>(body["is_active"].i());
            if (active != 0 && active != 1)
                return crow::response(400,
                    R"({"error":"is_active must be 0 or 1"})");

            try {
                auto db = pool.Acquire();

                // Vérifie que la cible n'est pas admin
                auto rows = QueryRows(db.get(),
                    "SELECT is_admin FROM users WHERE id = ? LIMIT 1",
                    {std::to_string(id)});
                if (rows.empty())
                    return crow::response(404, R"({"error":"User not found"})");
                if (rows[0][0] == "1")
                    return crow::response(403,
                        R"({"error":"Cannot ban an admin"})");

                Execute(db.get(),
                    "UPDATE users SET is_active = ? WHERE id = ?",
                    {std::to_string(active), std::to_string(id)});

                // Si ban : invalide toutes les sessions actives de cet user
                if (active == 0)
                    sessions.DeleteSessionsForUser(id);

                return crow::response(200, R"({"message":"User updated"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });
    }
};