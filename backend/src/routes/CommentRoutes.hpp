// routes/CommentRoutes.hpp
#pragma once

#include "crow.h"
#include "../db/ConnectionPool.hpp"
#include "../db/QueryHelpers.hpp"
#include "../session/SessionManager.hpp"

class CommentRoutes {
public:
    static void Register(crow::App<crow::CORSHandler>& app,
                         ConnectionPool& pool,
                         SessionManager& sessions)
    {
        // --------------------------------------------------------
        // GET /api/documents/:id/comments
        // Retourne l'arbre complet : racines + leurs enfants récursifs
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents/<int>/comments")
        .methods("GET"_method)
        ([&pool](const crow::request&, int doc_id) {
            try {
                auto db = pool.Acquire();

                // On charge tous les commentaires du document en une seule
                // requête puis on reconstruit l'arbre en mémoire —
                // plus efficace que des requêtes récursives multiples
                auto rows = QueryRows(db.get(),
                    "SELECT c.id, c.parent_id, c.user_id, u.username, "
                    "       c.content, c.is_deleted, c.created_at, c.updated_at, "
                    "       COUNT(cl.id) AS like_count "
                    "FROM comments c "
                    "JOIN users u ON u.id = c.user_id "
                    "LEFT JOIN comment_likes cl ON cl.comment_id = c.id "
                    "WHERE c.document_id = ? "
                    "GROUP BY c.id "
                    "ORDER BY c.created_at ASC",
                    {std::to_string(doc_id)});

                // Construit les noeuds
                // node_map : id -> wvalue index dans flat_nodes
                std::unordered_map<int, crow::json::wvalue> node_map;
                std::vector<int> root_ids;

                for (auto& r : rows) {
                    int  id         = std::stoi(r[0]);
                    bool is_deleted = r[5] == "1";

                    crow::json::wvalue node;
                    node["id"]         = id;
                    node["parent_id"]  = r[1].empty() ? -1 : std::stoi(r[1]);
                    node["user_id"]    = std::stoi(r[2]);
                    node["username"]   = r[3];
                    // Masque le contenu si supprimé mais garde le noeud
                    // pour ne pas casser les threads enfants
                    node["content"]    = is_deleted ? "[deleted]" : r[4];
                    node["is_deleted"] = is_deleted;
                    node["created_at"] = r[6];
                    node["updated_at"] = r[7];
                    node["like_count"] = std::stoi(r[8].empty() ? "0" : r[8]);
                    node["replies"]    = std::vector<crow::json::wvalue>{};

                    node_map[id] = std::move(node);

                    if (r[1].empty()) root_ids.push_back(id);
                }

                // Attache chaque noeud à son parent
                // On parcourt dans l'ordre created_at ASC donc les parents
                // existent toujours avant leurs enfants
                for (auto& [id, node] : node_map) {
                    int parent_id = node["parent_id"].i();
                    if (parent_id != -1 && node_map.count(parent_id)) {
                        node_map[parent_id]["replies"].push_back(
                            crow::json::wvalue(node));
                    }
                }

                // Assemble les racines
                std::vector<crow::json::wvalue> roots;
                for (int rid : root_ids)
                    roots.push_back(std::move(node_map[rid]));

                crow::json::wvalue out;
                out["comments"] = std::move(roots);
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // POST /api/documents/:id/comments  [auth requise]
        // Body : { "content": "...", "parent_id": 42 (optionnel) }
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents/<int>/comments")
        .methods("POST"_method)
        ([&pool, &sessions](const crow::request& req, int doc_id) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("content"))
                return crow::response(400, R"({"error":"Missing content"})");

            std::string content = body["content"].s();
            if (content.empty() || content.size() > 2000)
                return crow::response(400,
                    R"({"error":"Content must be 1-2000 chars"})");

            try {
                auto db = pool.Acquire();

                // Vérifie que le document existe
                auto doc = QueryRows(db.get(),
                    "SELECT id FROM documents WHERE id = ? AND is_approved = 1 LIMIT 1",
                    {std::to_string(doc_id)});
                if (doc.empty())
                    return crow::response(404, R"({"error":"Document not found"})");

                // Si parent_id fourni, vérifie qu'il appartient bien à ce document
                std::string parent_id = "";
                if (body.has("parent_id")) {
                    int pid = static_cast<int>(body["parent_id"].i());
                    auto parent = QueryRows(db.get(),
                        "SELECT id FROM comments "
                        "WHERE id = ? AND document_id = ? AND is_deleted = 0 LIMIT 1",
                        {std::to_string(pid), std::to_string(doc_id)});
                    if (parent.empty())
                        return crow::response(404,
                            R"({"error":"Parent comment not found"})");
                    parent_id = std::to_string(pid);
                }

                uint64_t comment_id = Execute(db.get(),
                    "INSERT INTO comments "
                    "(document_id, user_id, parent_id, content) "
                    "VALUES (?, ?, ?, ?)",
                    {
                        std::to_string(doc_id),
                        std::to_string(session->user_id),
                        parent_id,  // chaîne vide → NULL en base via le helper
                        content
                    });

                // +2 points pour un commentaire
                GamificationService::AddPoints(db.get(), session->user_id, 2,
                    "comment_posted", static_cast<int>(comment_id));
                GamificationService::CheckAndAwardBadges(db.get(), session->user_id);

                crow::json::wvalue out;
                out["id"]      = static_cast<int>(comment_id);
                out["message"] = "Comment posted";
                return crow::response(201, out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // PATCH /api/comments/:id  [auth requise, owner only]
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/comments/<int>")
        .methods("PATCH"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("content"))
                return crow::response(400, R"({"error":"Missing content"})");

            std::string content = body["content"].s();
            if (content.empty() || content.size() > 2000)
                return crow::response(400,
                    R"({"error":"Content must be 1-2000 chars"})");

            try {
                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT user_id FROM comments "
                    "WHERE id = ? AND is_deleted = 0 LIMIT 1",
                    {std::to_string(id)});

                if (rows.empty())
                    return crow::response(404, R"({"error":"Comment not found"})");

                if (std::stoi(rows[0][0]) != session->user_id)
                    return crow::response(403, R"({"error":"Forbidden"})");

                Execute(db.get(),
                    "UPDATE comments SET content = ? WHERE id = ?",
                    {content, std::to_string(id)});

                return crow::response(200, R"({"message":"Updated"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // DELETE /api/comments/:id  [auth requise, owner ou admin]
        // Soft delete : contenu masqué, noeud conservé pour les replies
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/comments/<int>")
        .methods("DELETE"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT user_id FROM comments WHERE id = ? LIMIT 1",
                    {std::to_string(id)});

                if (rows.empty())
                    return crow::response(404, R"({"error":"Comment not found"})");

                bool is_owner = std::stoi(rows[0][0]) == session->user_id;
                if (!is_owner && !session->is_admin)
                    return crow::response(403, R"({"error":"Forbidden"})");

                Execute(db.get(),
                    "UPDATE comments SET is_deleted = 1, content = '' WHERE id = ?",
                    {std::to_string(id)});

                return crow::response(200, R"({"message":"Deleted"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // POST /api/comments/:id/like  [auth requise]
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/comments/<int>/like")
        .methods("POST"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db = pool.Acquire();

                auto rows = QueryRows(db.get(),
                    "SELECT id FROM comments "
                    "WHERE id = ? AND is_deleted = 0 LIMIT 1",
                    {std::to_string(id)});
                if (rows.empty())
                    return crow::response(404, R"({"error":"Comment not found"})");

                // INSERT IGNORE : silencieux si déjà liké
                Execute(db.get(),
                    "INSERT IGNORE INTO comment_likes (user_id, comment_id) "
                    "VALUES (?, ?)",
                    {std::to_string(session->user_id), std::to_string(id)});

                return crow::response(200, R"({"message":"Liked"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // DELETE /api/comments/:id/like  [auth requise]
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/comments/<int>/like")
        .methods("DELETE"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db = pool.Acquire();
                Execute(db.get(),
                    "DELETE FROM comment_likes "
                    "WHERE user_id = ? AND comment_id = ?",
                    {std::to_string(session->user_id), std::to_string(id)});

                return crow::response(200, R"({"message":"Unliked"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });
    }
};