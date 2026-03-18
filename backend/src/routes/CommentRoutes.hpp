// routes/CommentRoutes.hpp
#pragma once

#include "crow.h"
#include "../db/ConnectionPool.hpp"
#include "../db/QueryHelpers.hpp"
#include "../session/SessionManager.hpp"
#include "../services/GamificationService.hpp"
#include <functional>
#include <unordered_map>

class CommentRoutes {
public:
    static void Register(crow::App<crow::CORSHandler>& app,
                         ConnectionPool& pool,
                         SessionManager& sessions)
    {
        CROW_ROUTE(app, "/api/documents/<int>/comments")
        .methods("GET"_method)
        ([&pool](const crow::request&, int doc_id) {
            try {
                auto db = pool.Acquire();

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

                // Structure intermédiaire — sépare les données
                // de la hiérarchie pour éviter les problèmes
                // de wvalue non copiable
                struct CommentNode {
                    int              id;
                    int              parent_id;
                    std::string      username;
                    std::string      content;
                    bool             is_deleted;
                    std::string      created_at;
                    std::string      updated_at;
                    int              like_count;
                    std::vector<int> children;
                };

                std::unordered_map<int, CommentNode> node_map;
                std::vector<int> root_ids;

                for (auto& r : rows) {
                    int  id         = std::stoi(r[0]);
                    int  parent_id  = r[1].empty() ? -1 : std::stoi(r[1]);
                    bool is_deleted = r[5] == "1";

                    CommentNode node;
                    node.id         = id;
                    node.parent_id  = parent_id;
                    node.username   = r[3];
                    node.content    = is_deleted ? "[deleted]" : r[4];
                    node.is_deleted = is_deleted;
                    node.created_at = r[6];
                    node.updated_at = r[7];
                    node.like_count = std::stoi(r[8].empty() ? "0" : r[8]);

                    node_map[id] = std::move(node);

                    if (parent_id == -1)
                        root_ids.push_back(id);
                }

                // Construit les relations enfants
                for (auto& [id, node] : node_map) {
                    if (node.parent_id != -1 &&
                        node_map.count(node.parent_id))
                        node_map[node.parent_id].children.push_back(id);
                }

                // Sérialisation récursive — std::function permet
                // la récursion dans une lambda
                std::function<crow::json::wvalue(int)> serialize =
                    [&](int id) -> crow::json::wvalue {
                        auto& n = node_map[id];
                        crow::json::wvalue out;
                        out["id"]         = n.id;
                        out["parent_id"]  = n.parent_id;
                        out["username"]   = n.username;
                        out["content"]    = n.content;
                        out["is_deleted"] = n.is_deleted;
                        out["created_at"] = n.created_at;
                        out["updated_at"] = n.updated_at;
                        out["like_count"] = n.like_count;

                        std::vector<crow::json::wvalue> replies;
                        for (int child_id : n.children)
                            replies.push_back(serialize(child_id));
                        out["replies"] = std::move(replies);
                        return out;
                    };

                std::vector<crow::json::wvalue> roots;
                for (int rid : root_ids)
                    roots.push_back(serialize(rid));

                crow::json::wvalue out;
                out["comments"] = std::move(roots);
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        CROW_ROUTE(app, "/api/documents/<int>/comments")
        .methods("POST"_method)
        ([&pool, &sessions](const crow::request& req, int doc_id) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("content"))
                return crow::response(400, R"({"error":"Missing content"})");

            std::string content = std::string(body["content"].s());
            if (content.empty() || content.size() > 2000)
                return crow::response(400,
                    R"({"error":"Content must be 1-2000 chars"})");

            try {
                auto db = pool.Acquire();

                auto doc = QueryRows(db.get(),
                    "SELECT id FROM documents "
                    "WHERE id = ? AND is_approved = 1 LIMIT 1",
                    {std::to_string(doc_id)});
                if (doc.empty())
                    return crow::response(404,
                        R"({"error":"Document not found"})");

                std::string parent_id = "";
                if (body.has("parent_id")) {
                    int pid = static_cast<int>(body["parent_id"].i());
                    auto parent = QueryRows(db.get(),
                        "SELECT id FROM comments "
                        "WHERE id = ? AND document_id = ? "
                        "AND is_deleted = 0 LIMIT 1",
                        {std::to_string(pid), std::to_string(doc_id)});
                    if (parent.empty())
                        return crow::response(404,
                            R"({"error":"Parent comment not found"})");
                    parent_id = std::to_string(pid);
                }

                uint64_t comment_id = Execute(db.get(),
                    "INSERT INTO comments "
                    "(document_id, user_id, parent_id, content) "
                    "VALUES (?, ?, NULLIF(?, ''), ?)",
                    {
                        std::to_string(doc_id),
                        std::to_string(session->user_id),
                        parent_id,
                        content
                    });

                GamificationService::AddPoints(
                    db.get(), session->user_id, 2,
                    "comment_posted", static_cast<int>(comment_id));
                GamificationService::CheckAndAwardBadges(
                    db.get(), session->user_id);

                crow::json::wvalue out;
                out["id"]      = static_cast<int>(comment_id);
                out["message"] = "Comment posted";
                return crow::response(201, out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        CROW_ROUTE(app, "/api/comments/<int>")
        .methods("PATCH"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("content"))
                return crow::response(400, R"({"error":"Missing content"})");

            std::string content = std::string(body["content"].s());
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
                    return crow::response(404,
                        R"({"error":"Comment not found"})");

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

        CROW_ROUTE(app, "/api/comments/<int>")
        .methods("DELETE"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT user_id FROM comments WHERE id = ? LIMIT 1",
                    {std::to_string(id)});

                if (rows.empty())
                    return crow::response(404,
                        R"({"error":"Comment not found"})");

                bool is_owner = std::stoi(rows[0][0]) == session->user_id;
                if (!is_owner && !session->is_admin)
                    return crow::response(403, R"({"error":"Forbidden"})");

                Execute(db.get(),
                    "UPDATE comments SET is_deleted = 1, content = '' "
                    "WHERE id = ?",
                    {std::to_string(id)});

                return crow::response(200, R"({"message":"Deleted"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        CROW_ROUTE(app, "/api/comments/<int>/like")
        .methods("POST"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT id FROM comments "
                    "WHERE id = ? AND is_deleted = 0 LIMIT 1",
                    {std::to_string(id)});
                if (rows.empty())
                    return crow::response(404,
                        R"({"error":"Comment not found"})");

                Execute(db.get(),
                    "INSERT IGNORE INTO comment_likes (user_id, comment_id) "
                    "VALUES (?, ?)",
                    {std::to_string(session->user_id), std::to_string(id)});

                return crow::response(200, R"({"message":"Liked"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        CROW_ROUTE(app, "/api/comments/<int>/like")
        .methods("DELETE"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
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