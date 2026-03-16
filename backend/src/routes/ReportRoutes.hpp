// routes/ReportRoutes.hpp
#pragma once

#include "crow.h"
#include "../db/ConnectionPool.hpp"
#include "../db/QueryHelpers.hpp"
#include "../session/SessionManager.hpp"

class ReportRoutes {
public:
    static void Register(crow::App<crow::CORSHandler>& app,
                         ConnectionPool& pool,
                         SessionManager& sessions)
    {
        // --------------------------------------------------------
        // POST /api/reports  [auth requise]
        // Body : { "document_id": 42, "reason": "spam", "details": "..." }
        // OU    { "comment_id":  12, "reason": "inappropriate" }
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/reports")
        .methods("POST"_method)
        ([&pool, &sessions](const crow::request& req) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("reason"))
                return crow::response(400, R"({"error":"Missing reason"})");

            bool has_doc     = body.has("document_id");
            bool has_comment = body.has("comment_id");

            if (!has_doc && !has_comment)
                return crow::response(400,
                    R"({"error":"Provide document_id or comment_id"})");
            if (has_doc && has_comment)
                return crow::response(400,
                    R"({"error":"Provide only one of document_id or comment_id"})");

            std::string reason  = body["reason"].s();
            std::string details = body.has("details") ? body["details"].s() : "";

            // Validation de la raison
            static const std::vector<std::string> valid_reasons = {
                "spam", "inappropriate", "wrong_content", "copyright", "other"
            };
            if (std::find(valid_reasons.begin(), valid_reasons.end(), reason)
                    == valid_reasons.end())
                return crow::response(400, R"({"error":"Invalid reason"})");

            try {
                auto db = pool.Acquire();

                if (has_doc) {
                    int doc_id = static_cast<int>(body["document_id"].i());
                    auto check = QueryRows(db.get(),
                        "SELECT id FROM documents WHERE id = ? LIMIT 1",
                        {std::to_string(doc_id)});
                    if (check.empty())
                        return crow::response(404,
                            R"({"error":"Document not found"})");

                    // Un seul signalement par user/document
                    auto already = QueryRows(db.get(),
                        "SELECT id FROM reports "
                        "WHERE user_id = ? AND document_id = ? LIMIT 1",
                        {std::to_string(session->user_id),
                         std::to_string(doc_id)});
                    if (!already.empty())
                        return crow::response(409,
                            R"({"error":"Already reported"})");

                    Execute(db.get(),
                        "INSERT INTO reports "
                        "(user_id, document_id, reason, details) "
                        "VALUES (?, ?, ?, ?)",
                        {std::to_string(session->user_id),
                         std::to_string(doc_id),
                         reason, details});

                } else {
                    int comment_id = static_cast<int>(body["comment_id"].i());
                    auto check = QueryRows(db.get(),
                        "SELECT id FROM comments WHERE id = ? LIMIT 1",
                        {std::to_string(comment_id)});
                    if (check.empty())
                        return crow::response(404,
                            R"({"error":"Comment not found"})");

                    auto already = QueryRows(db.get(),
                        "SELECT id FROM reports "
                        "WHERE user_id = ? AND comment_id = ? LIMIT 1",
                        {std::to_string(session->user_id),
                         std::to_string(comment_id)});
                    if (!already.empty())
                        return crow::response(409,
                            R"({"error":"Already reported"})");

                    Execute(db.get(),
                        "INSERT INTO reports "
                        "(user_id, comment_id, reason, details) "
                        "VALUES (?, ?, ?, ?)",
                        {std::to_string(session->user_id),
                         std::to_string(comment_id),
                         reason, details});
                }

                return crow::response(201, R"({"message":"Report submitted"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });
    }
};