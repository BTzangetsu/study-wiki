// middleware/AuthGuard.hpp
#pragma once

#include "crow.h"
#include "../session/SessionManager.hpp"
#include "../db/QueryHelpers.hpp"

struct AuthGuard : crow::ILocalMiddleware {
    struct context {
        int         user_id = -1;
        std::string username;
        bool        is_admin = false;
        bool        authenticated = false;
    };

    void before_handle(crow::request& req, crow::response& res, context& ctx) {
        std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
        if (token.empty()) {
            res.code = 401;
            res.write(R"({"error":"Not authenticated"})");
            res.end();
            return;
        }
        auto session = sessions_->GetSession(token);
        if (!session) {
            res.code = 401;
            res.write(R"({"error":"Session expired"})");
            res.end();
            return;
        }
        ctx.user_id       = session->user_id;
        ctx.username      = session->username;
        ctx.is_admin      = session->is_admin;
        ctx.authenticated = true;
    }

    void after_handle(crow::request&, crow::response&, context&) {}

    SessionManager* sessions_ = nullptr;
};

struct AdminGuard : crow::ILocalMiddleware {
    struct context {};

    void before_handle(crow::request& req, crow::response& res, context&) {
        std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
        if (token.empty()) { res.code = 401; res.end(); return; }
        auto session = sessions_->GetSession(token);
        if (!session || !session->is_admin) {
            res.code = 403;
            res.write(R"({"error":"Forbidden"})");
            res.end();
            return;
        }
    }

    void after_handle(crow::request&, crow::response&, context&) {}

    SessionManager* sessions_ = nullptr;
};