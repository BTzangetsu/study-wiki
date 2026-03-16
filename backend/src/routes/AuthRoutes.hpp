// routes/AuthRoutes.hpp
#pragma once

#include "crow.h"
#include "../db/ConnectionPool.hpp"
#include "../db/QueryHelpers.hpp"
#include "../session/SessionManager.hpp"
#include "../utils/Crypto.hpp"

class AuthRoutes {
public:
    static void Register(crow::App<crow::CORSHandler>& app,
                         ConnectionPool& pool,
                         SessionManager& sessions,
                         RateLimiter& limiter)
    {
        // --------------------------------------------------------
        // POST /api/auth/register
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/auth/register")
        .methods("POST"_method)
        ([&pool, &sessions, &limiter](const crow::request& req) {
            if (limiter.Check(req).code == 429)
                return crow::response(429, R"({"error":"Too many requests"})");

            auto body = crow::json::load(req.body);
            if (!body ||
                !body.has("username") ||
                !body.has("password"))
                return crow::response(400, R"({"error":"Missing fields"})");

            std::string username = body["username"].s();
            std::string email = body.has("email") ? std::string(body["email"].s()) : std::string("");
            std::string password = body["password"].s();

            if (username.size() < 3)
                return crow::response(400, R"({"error":"Username too short"})");
            if (password.size() < 8)
                return crow::response(400, R"({"error":"Password too short"})");
            if (!email.empty() && email.find('@') == std::string::npos)
                return crow::response(400, R"({"error":"Invalid email"})");

            try {
                auto db = pool.Acquire();

                // Vérification unicité
                if (!email.empty()) {
                    auto existing = QueryRows(db.get(),
                        "SELECT id FROM users "
                        "WHERE username = ? OR (email IS NOT NULL AND email = ?) LIMIT 1",
                        {username, email});
                    if (!existing.empty())
                        return crow::response(409,
                            R"({"error":"Email or username already taken"})");
                } else {
                    auto existing = QueryRows(db.get(),
                        "SELECT id FROM users WHERE username = ? LIMIT 1",
                        {username});
                    if (!existing.empty())
                        return crow::response(409,
                            R"({"error":"Username already taken"})");
                }

                // Hash PBKDF2
                std::string salt   = GenerateRandomToken();
                std::string hash   = pbkdf2_sha256(password, salt);
                std::string stored = salt + ":" + hash;

                uint64_t user_id = Execute(db.get(),
                    "INSERT INTO users (username, email, password_hash) "
                    "VALUES (?, NULLIF(?, ''), ?)",
                    {username, email, stored});

                std::string token = sessions.CreateSession(
                    static_cast<int>(user_id), username, false);

                crow::response res(201);
                res.add_header("Content-Type", "application/json");
                res.add_header("Set-Cookie",
                    "session=" + token +
                    "; HttpOnly; SameSite=Strict; Path=/; Max-Age=86400");
                res.write(R"({"message":"Account created"})");
                return res;

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // POST /api/auth/login
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/auth/login")
        .methods("POST"_method)
        ([&pool, &sessions, &limiter](const crow::request& req) {
            if (limiter.Check(req).code == 429)
                return crow::response(429, R"({"error":"Too many requests"})");
            auto body = crow::json::load(req.body);
            //         identifier peut être un email ou un username

            if (!body || !body.has("identifier") || !body.has("password"))
                return crow::response(400, R"({"error":"Missing fields"})");

            std::string identifier = body["identifier"].s();
            std::string password   = body["password"].s();

            // Détecte si c'est un email (contient @) ou un username
            std::string query;
            if (identifier.find('@') != std::string::npos) {
                query = "SELECT id, username, password_hash, is_active, is_admin "
                        "FROM users WHERE email = ? LIMIT 1";
            } else {
                query = "SELECT id, username, password_hash, is_active, is_admin "
                        "FROM users WHERE username = ? LIMIT 1";
            }

            try {
                auto db = pool.Acquire();
                auto rows = QueryRows(db.get(), query, {identifier});

                // Même message d'erreur volontairement — évite l'énumération
                if (rows.empty())
                    return crow::response(401, R"({"error":"Invalid credentials"})");

                auto& row       = rows[0];
                int  user_id    = std::stoi(row[0]);
                auto username   = row[1];
                auto stored     = row[2]; // format "salt:hash"
                bool is_active  = row[3] == "1";
                bool is_admin   = row[4] == "1";

                if (!is_active)
                    return crow::response(403, R"({"error":"Account banned"})");

                // Séparation salt:hash
                auto sep = stored.find(':');
                if (sep == std::string::npos)
                    return crow::response(500, R"({"error":"Corrupted password record"})");

                std::string salt      = stored.substr(0, sep);
                std::string expected  = stored.substr(sep + 1);
                std::string candidate = pbkdf2_sha256(password, salt);

                // Comparaison en temps constant — évite timing attack
                if (candidate.size() != expected.size())
                    return crow::response(401, R"({"error":"Invalid credentials"})");
                uint8_t diff = 0;
                for (size_t i = 0; i < candidate.size(); ++i)
                    diff |= candidate[i] ^ expected[i];
                if (diff != 0)
                    return crow::response(401, R"({"error":"Invalid credentials"})");

                Execute(db.get(),
                    "UPDATE users SET last_login = NOW() WHERE id = ?",
                    {std::to_string(user_id)});

                std::string token = sessions.CreateSession(
                    user_id, username, is_admin);

                crow::response res(200);
                res.add_header("Content-Type", "application/json");
                res.add_header("Set-Cookie",
                    "session=" + token +
                    "; HttpOnly; SameSite=Strict; Path=/; Max-Age=86400");
                res.write(R"({"message":"Logged in"})");
                return res;

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // POST /api/auth/logout
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/auth/logout")
        .methods("POST"_method)
        ([&sessions](const crow::request& req) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            if (!token.empty())
                sessions.DeleteSession(token);

            crow::response res(200);
            res.add_header("Set-Cookie",
                "session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
            res.write(R"({"message":"Logged out"})");
            return res;
        });

        // --------------------------------------------------------
        // GET /api/auth/me  — nécessite une session valide
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/auth/me")
        .methods("GET"_method)
        ([&pool, &sessions,&limiter](const crow::request& req) {
                if (limiter.Check(req).code == 429)
                    return crow::response(429, R"({"error":"Too many requests"})");
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT id, username, email, points, is_admin, created_at "
                    "FROM users WHERE id = ? LIMIT 1",
                    {std::to_string(session->user_id)});

                if (rows.empty())
                    return crow::response(404, R"({"error":"User not found"})");

                auto& r = rows[0];
                crow::json::wvalue out;
                out["id"]         = std::stoi(r[0]);
                out["username"]   = r[1];
                out["email"]      = r[2];
                out["points"]     = std::stoi(r[3]);
                out["is_admin"]   = r[4] == "1";
                out["created_at"] = r[5];

                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // PATCH /api/auth/me — modifier username ou mot de passe
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/auth/me")
        .methods("PATCH"_method)
        ([&pool, &sessions,&limiter](const crow::request& req) {
                if (limiter.Check(req).code == 429)
                    return crow::response(429, R"({"error":"Too many requests"})");
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            auto body = crow::json::load(req.body);
            if (!body)
                return crow::response(400, R"({"error":"Invalid JSON"})");

            try {
                auto db = pool.Acquire();

                if (body.has("username")) {
                    std::string nu = body["username"].s();
                    if (nu.size() < 3)
                        return crow::response(400,
                            R"({"error":"Username too short"})");
                    auto check = QueryRows(db.get(),
                        "SELECT id FROM users WHERE username = ? AND id != ?",
                        {nu, std::to_string(session->user_id)});
                    if (!check.empty())
                        return crow::response(409,
                            R"({"error":"Username taken"})");
                    Execute(db.get(),
                        "UPDATE users SET username = ? WHERE id = ?",
                        {nu, std::to_string(session->user_id)});
                }

                if (body.has("password")) {
                    std::string np = body["password"].s();
                    if (np.size() < 8)
                        return crow::response(400,
                            R"({"error":"Password too short"})");
                    std::string salt   = GenerateRandomToken();
                    std::string hash   = pbkdf2_sha256(np, salt);
                    std::string stored = salt + ":" + hash;
                    Execute(db.get(),
                        "UPDATE users SET password_hash = ? WHERE id = ?",
                        {stored, std::to_string(session->user_id)});
                }

                return crow::response(200, R"({"message":"Updated"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // DELETE /api/auth/me — supprimer son compte
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/auth/me")
        .methods("DELETE"_method)
        ([&pool, &sessions](const crow::request& req) {
            std::string token = ExtractSessionToken(
                req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db = pool.Acquire();
                Execute(db.get(),
                    "DELETE FROM users WHERE id = ?",
                    {std::to_string(session->user_id)});
                sessions.DeleteSession(token);

                crow::response res(200);
                res.add_header("Set-Cookie",
                    "session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
                res.write(R"({"message":"Account deleted"})");
                return res;

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });
    }
};