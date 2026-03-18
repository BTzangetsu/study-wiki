// routes/DocumentRoutes.hpp
#pragma once

#include "crow.h"
#include "../db/ConnectionPool.hpp"
#include "../db/QueryHelpers.hpp"
#include "../session/SessionManager.hpp"
#include "../utils/Crypto.hpp"
#include "../services/GamificationService.hpp"

// ================================================================
// Signed URL maison — zéro dépendance
// Format : base_url/bucket/key?expires=TS&sig=HMAC(secret, method+key+TS)
// ================================================================

struct StorageConfig {
    std::string endpoint;   // ex: https://s3.us-west-001.backblazeb2.com
    std::string bucket;
    std::string secret_key; // clé secrète B2 / Wasabi
    int         url_ttl_seconds = 900; // 15 min
};

inline std::string MakeSignedUrl(const StorageConfig& cfg,
                                  const std::string& method, // "PUT" ou "GET"
                                  const std::string& key)
{
    long long expires = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()
        ) + cfg.url_ttl_seconds;

    std::string expires_str = std::to_string(expires);
    // Message signé : METHOD + ":" + key + ":" + expires
    std::string message = method + ":" + key + ":" + expires_str;
    auto digest = hmac_sha256(cfg.secret_key, message);

    // Encode signature en hex
    static const char hex[] = "0123456789abcdef";
    std::string sig;
    sig.reserve(64);
    for (auto b : digest.bytes) {
        sig += hex[(b >> 4) & 0xF];
        sig += hex[b & 0xF];
    }

    return cfg.endpoint + "/" + cfg.bucket + "/" + key
         + "?expires=" + expires_str
         + "&sig=" + sig;
}

// ================================================================

class DocumentRoutes {
public:
    static void Register(crow::App<crow::CORSHandler>& app,
                         ConnectionPool& pool,
                         SessionManager& sessions,
                         const StorageConfig& storage)
    {
        // --------------------------------------------------------
        // GET /api/documents
        // Filtres optionnels : ?school_id=&subject_id=&year=&type=&page=&limit=
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents")
        .methods("GET"_method)
        ([&pool](const crow::request& req) {
            try {
                // Construction dynamique de la requête selon les filtres
                std::string sql =
                    "SELECT d.id, d.title, d.description, d.year, d.type, "
                    "       d.avg_rating, d.vote_count, d.download_count, "
                    "       d.created_at, d.size_bytes, "
                    "       u.username, s.name AS subject, sc.name AS school "
                    "FROM documents d "
                    "JOIN users u    ON u.id = d.user_id "
                    "LEFT JOIN subjects s  ON s.id = d.subject_id "
                    "LEFT JOIN schools sc  ON sc.id = d.school_id "
                    "WHERE d.is_approved = 1 ";

                std::vector<std::string> params;

                auto school_id  = req.url_params.get("school_id");
                auto subject_id = req.url_params.get("subject_id");
                auto year       = req.url_params.get("year");
                auto type       = req.url_params.get("type");

                if (school_id)  { sql += "AND d.school_id = ? ";  params.push_back(school_id);  }
                if (subject_id) { sql += "AND d.subject_id = ? "; params.push_back(subject_id); }
                if (year)       { sql += "AND d.year = ? ";       params.push_back(year);        }
                if (type)       { sql += "AND d.type = ? ";       params.push_back(type);        }

                sql += "ORDER BY d.created_at DESC ";

                // Pagination
                int page  = 1;
                int limit = 20;
                if (req.url_params.get("page"))
                    page  = std::max(1, std::stoi(req.url_params.get("page")));
                if (req.url_params.get("limit"))
                    limit = std::min(100, std::max(1,
                        std::stoi(req.url_params.get("limit"))));

                int offset = (page - 1) * limit;
                sql += "LIMIT ? OFFSET ?";
                params.push_back(std::to_string(limit));
                params.push_back(std::to_string(offset));

                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(), sql, params);

                crow::json::wvalue out;
                out["page"]  = page;
                out["limit"] = limit;

                std::vector<crow::json::wvalue> docs;
                for (auto& r : rows) {
                    crow::json::wvalue d;
                    d["id"]             = std::stoi(r[0]);
                    d["title"]          = r[1];
                    d["description"]    = r[2];
                    d["year"]           = r[3];
                    d["type"]           = r[4];
                    d["avg_rating"]     = std::stod(r[5].empty() ? "0" : r[5]);
                    d["vote_count"]     = std::stoi(r[6].empty() ? "0" : r[6]);
                    d["download_count"] = std::stoi(r[7].empty() ? "0" : r[7]);
                    d["created_at"]     = r[8];
                    d["size_bytes"]     = r[9];
                    d["author"]         = r[10];
                    d["subject"]        = r[11];
                    d["school"]         = r[12];
                    docs.push_back(std::move(d));
                }
                out["documents"] = std::move(docs);
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // GET /api/documents/favorite  [auth requise]
        // --------------------------------------------------------
       CROW_ROUTE(app, "/api/documents/favorites")
        .methods("GET"_method)
        ([&pool, &sessions](const crow::request& req) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT d.id, d.title, d.description, d.year, d.type, "
                    "       d.avg_rating, d.vote_count, d.download_count, "
                    "       d.created_at, d.size_bytes, "
                    "       u.username, s.name, sc.name "
                    "FROM favorites f "
                    "JOIN documents d ON d.id = f.document_id "
                    "JOIN users u     ON u.id = d.user_id "
                    "LEFT JOIN subjects s  ON s.id = d.subject_id "
                    "LEFT JOIN schools sc  ON sc.id = d.school_id "
                    "WHERE f.user_id = ? AND d.is_approved = 1 "
                    "ORDER BY f.created_at DESC",
                    {std::to_string(session->user_id)});

                std::vector<crow::json::wvalue> docs;
                for (auto& r : rows) {
                    crow::json::wvalue d;
                    d["id"]             = std::stoi(r[0]);
                    d["title"]          = r[1];
                    d["description"]    = r[2];
                    d["year"]           = r[3];
                    d["type"]           = r[4];
                    d["avg_rating"]     = std::stod(r[5].empty() ? "0" : r[5]);
                    d["vote_count"]     = std::stoi(r[6].empty() ? "0" : r[6]);
                    d["download_count"] = std::stoi(r[7].empty() ? "0" : r[7]);
                    d["created_at"]     = r[8];
                    d["size_bytes"]     = r[9];
                    d["author"]         = r[10];
                    d["subject"]        = r[11];
                    d["school"]         = r[12];
                    docs.push_back(std::move(d));
                }

                crow::json::wvalue out;
                out["documents"] = std::move(docs);
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // GET /api/documents/:id
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents/<int>")
        .methods("GET"_method)
        ([&pool](const crow::request&, int id) {
            try {
                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT d.id, d.title, d.description, d.year, d.type, "
                    "       d.avg_rating, d.vote_count, d.download_count, "
                    "       d.created_at, d.size_bytes, d.user_id, "
                    "       u.username, s.name, sc.name "
                    "FROM documents d "
                    "JOIN users u   ON u.id = d.user_id "
                    "LEFT JOIN subjects s  ON s.id = d.subject_id "
                    "LEFT JOIN schools sc  ON sc.id = d.school_id "
                    "WHERE d.id = ? AND d.is_approved = 1 LIMIT 1",
                    {std::to_string(id)});

                if (rows.empty())
                    return crow::response(404, R"({"error":"Document not found"})");

                auto& r = rows[0];
                crow::json::wvalue d;
                d["id"]             = std::stoi(r[0]);
                d["title"]          = r[1];
                d["description"]    = r[2];
                d["year"]           = r[3];
                d["type"]           = r[4];
                d["avg_rating"]     = std::stod(r[5].empty() ? "0" : r[5]);
                d["vote_count"]     = std::stoi(r[6].empty() ? "0" : r[6]);
                d["download_count"] = std::stoi(r[7].empty() ? "0" : r[7]);
                d["created_at"]     = r[8];
                d["size_bytes"]     = r[9];
                d["user_id"]        = std::stoi(r[10]);
                d["author"]         = r[11];
                d["subject"]        = r[12];
                d["school"]         = r[13];
                return crow::response(d);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // POST /api/documents/upload-url  [auth requise]
        // Body : { "filename": "exam.pdf", "size_bytes": 204800 }
        // Retourne une presigned PUT URL + le storage_key à renvoyer
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents/upload-url")
        .methods("POST"_method)
        ([&sessions, &storage](const crow::request& req) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("filename") || !body.has("size_bytes"))
                return crow::response(400, R"({"error":"Missing filename or size_bytes"})");

            std::string filename   = json_str(body, "filename");
            long long   size_bytes = body["size_bytes"].i();

            // 50 Mo max
            if (size_bytes > 50 * 1024 * 1024)
                return crow::response(400, R"({"error":"File too large (max 50MB) "})");

            // Clé unique : userId/timestamp_randomtoken_filename
            std::string key = std::to_string(session->user_id) + "/"
                            + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count())
                            + "_" + GenerateRandomToken().substr(0, 16)
                            + "_" + filename;

            std::string upload_url = MakeSignedUrl(storage, "PUT", key);

            crow::json::wvalue out;
            out["upload_url"]  = upload_url;
            out["storage_key"] = key;
            out["expires_in"]  = storage.url_ttl_seconds;
            return crow::response(out);
        });

        // --------------------------------------------------------
        // POST /api/documents  [auth requise]
        // Appelé APRÈS que le client a uploadé vers B2
        // Body : { storage_key, title, description, year,
        //          type, subject_id, school_id, size_bytes }
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents")
        .methods("POST"_method)
        ([&pool, &sessions, &storage](const crow::request& req) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            auto body = crow::json::load(req.body);
            if (!body ||
                !body.has("storage_key") ||
                !body.has("title")       ||
                !body.has("type"))
                return crow::response(400, R"({"error":"Missing required fields"})");

            std::string storage_key = json_str(body, "storage_key");
            std::string title       = json_str(body, "title");
            std::string description = json_str(body, "description");
            std::string year        = body.has("year") ? std::to_string(body["year"].i()) : "";
            std::string type        = json_str(body, "type");
            std::string subject_id  = body.has("subject_id") ? std::to_string(body["subject_id"].i()) : "";
            std::string school_id   = body.has("school_id")  ? std::to_string(body["school_id"].i())  : "";
            std::string size_bytes  = body.has("size_bytes") ? std::to_string(body["size_bytes"].i())  : "0";

            // Vérification que le storage_key appartient bien à cet utilisateur
            std::string prefix = std::to_string(session->user_id) + "/";
            if (storage_key.substr(0, prefix.size()) != prefix)
                return crow::response(403, R"({"error":"Invalid storage key"})");

            try {
                auto db = pool.Acquire();
                uint64_t doc_id = Execute(db.get(),
                    "INSERT INTO documents "
                    "(user_id, subject_id, school_id, title, description, "
                    " year, type, storage_key, storage_bucket, size_bytes) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    {
                        std::to_string(session->user_id),
                        subject_id, school_id,
                        title, description,
                        year, type,
                        storage_key, storage.bucket,
                        size_bytes
                    });

                // Points : +10 pour un upload
                GamificationService::AddPoints(db.get(), session->user_id, 10, "upload",
                    static_cast<int>(doc_id));
                GamificationService::CheckAndAwardBadges(db.get(), session->user_id);

                crow::json::wvalue out;
                out["id"]      = static_cast<int>(doc_id);
                out["message"] = "Document created";
                return crow::response(201, out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // GET /api/documents/:id/download-url  [auth requise]
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents/<int>/download-url")
        .methods("GET"_method)
        ([&pool, &sessions, &storage](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT storage_key FROM documents "
                    "WHERE id = ? AND is_approved = 1 LIMIT 1",
                    {std::to_string(id)});

                if (rows.empty())
                    return crow::response(404, R"({"error":"Document not found"})");

                std::string key = rows[0][0];

                // Incrément download_count
                Execute(db.get(),
                    "UPDATE documents SET download_count = download_count + 1 "
                    "WHERE id = ?",
                    {std::to_string(id)});

                // Points pour l'auteur : +1 par téléchargement
                // Récupère d'abord l'auteur du document
                auto author = QueryRows(db.get(),
                    "SELECT user_id FROM documents WHERE id = ? LIMIT 1",
                    {std::to_string(id)});
                if (!author.empty()) {
                    int author_id = std::stoi(author[0][0]);
                    GamificationService::AddPoints(db.get(), author_id, 1,
                        "download_received", id);
                    GamificationService::CheckAndAwardBadges(db.get(), author_id);
                }

                std::string dl_url = MakeSignedUrl(storage, "GET", key);

                crow::json::wvalue out;
                out["download_url"] = dl_url;
                out["expires_in"]   = storage.url_ttl_seconds;
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // PATCH /api/documents/:id  [auth requise, owner only]
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents/<int>")
        .methods("PATCH"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            auto body = crow::json::load(req.body);
            if (!body)
                return crow::response(400, R"({"error":"Invalid JSON"})");

            try {
                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT user_id FROM documents WHERE id = ? LIMIT 1",
                    {std::to_string(id)});

                if (rows.empty())
                    return crow::response(404, R"({"error":"Document not found"})");

                bool is_owner = std::stoi(rows[0][0]) == session->user_id;
                if (!is_owner && !session->is_admin)
                    return crow::response(403, R"({"error":"Forbidden"})");

                if (body.has("title"))
                    Execute(db.get(),
                        "UPDATE documents SET title = ? WHERE id = ?",
                        {json_str(body, "title"), std::to_string(id)});

                if (body.has("description"))
                    Execute(db.get(),
                        "UPDATE documents SET description = ? WHERE id = ?",
                        {json_str(body, "description"), std::to_string(id)});

                return crow::response(200, R"({"message":"Updated"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // DELETE /api/documents/:id  [auth requise, owner ou admin]
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents/<int>")
        .methods("DELETE"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(),
                    "SELECT user_id FROM documents WHERE id = ? LIMIT 1",
                    {std::to_string(id)});

                if (rows.empty())
                    return crow::response(404, R"({"error":"Document not found"})");

                bool is_owner = std::stoi(rows[0][0]) == session->user_id;
                if (!is_owner && !session->is_admin)
                    return crow::response(403, R"({"error":"Forbidden"})");

                // Note : le fichier dans B2 n'est PAS supprimé ici
                // à gérer via un job de nettoyage séparé sur le storage_key
                Execute(db.get(),
                    "DELETE FROM documents WHERE id = ?",
                    {std::to_string(id)});

                return crow::response(200, R"({"message":"Deleted"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // POST /api/documents/:id/vote  [auth requise]
        // Body : { "score": 1-5 }
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents/<int>/vote")
        .methods("POST"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            auto body = crow::json::load(req.body);
            if (!body || !body.has("score"))
                return crow::response(400, R"({"error":"Missing score"})");

            int score = static_cast<int>(body["score"].i());
            if (score < 1 || score > 5)
                return crow::response(400, R"({"error":"Score must be 1-5"})");

            try {
                auto db = pool.Acquire();

                // Vérifie que le doc existe
                auto rows = QueryRows(db.get(),
                    "SELECT id FROM documents WHERE id = ? LIMIT 1",
                    {std::to_string(id)});
                if (rows.empty())
                    return crow::response(404, R"({"error":"Document not found"})");

                // INSERT OR UPDATE du vote (UPSERT MariaDB)
                Execute(db.get(),
                    "INSERT INTO document_votes (user_id, document_id, score) "
                    "VALUES (?, ?, ?) "
                    "ON DUPLICATE KEY UPDATE score = VALUES(score)",
                    {std::to_string(session->user_id),
                     std::to_string(id),
                     std::to_string(score)});

                // Recalcul avg_rating et vote_count
                Execute(db.get(),
                    "UPDATE documents d SET "
                    "d.avg_rating = (SELECT AVG(score) FROM document_votes WHERE document_id = ?), "
                    "d.vote_count = (SELECT COUNT(*) FROM document_votes WHERE document_id = ?) "
                    "WHERE d.id = ?",
                    {std::to_string(id), std::to_string(id), std::to_string(id)});

                return crow::response(200, R"({"message":"Vote recorded"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // DELETE /api/documents/:id/vote  [auth requise]
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents/<int>/vote")
        .methods("DELETE"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db = pool.Acquire();
                Execute(db.get(),
                    "DELETE FROM document_votes "
                    "WHERE user_id = ? AND document_id = ?",
                    {std::to_string(session->user_id), std::to_string(id)});

                Execute(db.get(),
                    "UPDATE documents d SET "
                    "d.avg_rating = COALESCE((SELECT AVG(score) FROM document_votes WHERE document_id = ?), 0), "
                    "d.vote_count = (SELECT COUNT(*) FROM document_votes WHERE document_id = ?) "
                    "WHERE d.id = ?",
                    {std::to_string(id), std::to_string(id), std::to_string(id)});

                return crow::response(200, R"({"message":"Vote removed"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // POST /api/documents/:id/favorite  [auth requise]
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents/<int>/favorite")
        .methods("POST"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db = pool.Acquire();
                Execute(db.get(),
                    "INSERT IGNORE INTO favorites (user_id, document_id) "
                    "VALUES (?, ?)",
                    {std::to_string(session->user_id), std::to_string(id)});
                return crow::response(200, R"({"message":"Added to favorites"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

        // --------------------------------------------------------
        // DELETE /api/documents/:id/favorite  [auth requise]
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/documents/<int>/favorite")
        .methods("DELETE"_method)
        ([&pool, &sessions](const crow::request& req, int id) {
            std::string token = ExtractSessionToken(req.get_header_value("Cookie"));
            auto session = sessions.GetSession(token);
            if (!session)
                return crow::response(401, R"({"error":"Not authenticated"})");

            try {
                auto db = pool.Acquire();
                Execute(db.get(),
                    "DELETE FROM favorites WHERE user_id = ? AND document_id = ?",
                    {std::to_string(session->user_id), std::to_string(id)});
                return crow::response(200, R"({"message":"Removed from favorites"})");

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });

    }
};