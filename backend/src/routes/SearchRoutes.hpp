// routes/SearchRoutes.hpp
#pragma once

#include "crow.h"
#include "../db/ConnectionPool.hpp"
#include "../db/QueryHelpers.hpp"

class SearchRoutes {
public:
    static void Register(crow::App<crow::CORSHandler>& app,
                         ConnectionPool& pool)
    {
        // --------------------------------------------------------
        // GET /api/search
        // ?q=&school_id=&subject_id=&year=&type=&page=&limit=
        // --------------------------------------------------------
        CROW_ROUTE(app, "/api/search")
        .methods("GET"_method)
        ([&pool](const crow::request& req) {
            try {
                auto q          = req.url_params.get("q");
                auto school_id  = req.url_params.get("school_id");
                auto subject_id = req.url_params.get("subject_id");
                auto year       = req.url_params.get("year");
                auto type       = req.url_params.get("type");

                // Au moins un critère requis
                if (!q && !school_id && !subject_id && !year && !type)
                    return crow::response(400,
                        R"({"error":"At least one search parameter required"})");

                std::string sql =
                    "SELECT d.id, d.title, d.description, d.year, d.type, "
                    "       d.avg_rating, d.vote_count, d.download_count, "
                    "       d.created_at, d.size_bytes, "
                    "       u.username, s.name, sc.name "
                    "FROM documents d "
                    "JOIN users u         ON u.id  = d.user_id "
                    "LEFT JOIN subjects s ON s.id  = d.subject_id "
                    "LEFT JOIN schools sc ON sc.id = d.school_id "
                    "WHERE d.is_approved = 1 ";

                std::vector<std::string> params;

                // Recherche full-text sur titre + description
                if (q) {
                    std::string like = std::string("%") + q + "%";
                    sql += "AND (d.title LIKE ? OR d.description LIKE ? "
                           "OR s.name LIKE ? OR sc.name LIKE ?) ";
                    params.push_back(like);
                    params.push_back(like);
                    params.push_back(like);
                    params.push_back(like);
                }
                if (school_id)  { sql += "AND d.school_id = ? ";  params.push_back(school_id);  }
                if (subject_id) { sql += "AND d.subject_id = ? "; params.push_back(subject_id); }
                if (year)       { sql += "AND d.year = ? ";       params.push_back(year);        }
                if (type)       { sql += "AND d.type = ? ";       params.push_back(type);        }

                // Tri par pertinence : si query texte, on remonte les titres
                // qui matchent exactement en premier
                if (q) {
                    std::string exact = std::string("%") + q + "%";
                    sql += "ORDER BY (d.title LIKE ?) DESC, d.avg_rating DESC ";
                    params.push_back(exact);
                } else {
                    sql += "ORDER BY d.created_at DESC ";
                }

                int page  = 1;
                int limit = 20;
                if (req.url_params.get("page"))
                    page  = std::max(1, std::stoi(req.url_params.get("page")));
                if (req.url_params.get("limit"))
                    limit = std::min(50, std::max(1,
                        std::stoi(req.url_params.get("limit"))));

                sql += "LIMIT ? OFFSET ?";
                params.push_back(std::to_string(limit));
                params.push_back(std::to_string((page - 1) * limit));

                auto db   = pool.Acquire();
                auto rows = QueryRows(db.get(), sql, params);

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
                out["page"]      = page;
                out["limit"]     = limit;
                out["results"]   = std::move(docs);
                return crow::response(out);

            } catch (const std::exception& e) {
                return crow::response(500, e.what());
            }
        });
    }
};