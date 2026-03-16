// db/QueryHelpers.hpp
#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <mysql/mysql.h>

// ================================================================
// Row = vector de strings, null inclus comme chaîne vide
// ================================================================

using Row  = std::vector<std::string>;
using Rows = std::vector<Row>;

// ================================================================
// SELECT — retourne toutes les lignes
// ================================================================

inline Rows QueryRows(MYSQL* conn,
                      const std::string& sql,
                      const std::vector<std::string>& params = {})
{
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) throw std::runtime_error("stmt_init failed");

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size())) {
        std::string err = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        throw std::runtime_error("prepare: " + err);
    }

    // Bind paramètres IN
    std::vector<MYSQL_BIND>    in_bind(params.size());
    std::vector<unsigned long> in_len(params.size());
    memset(in_bind.data(), 0, sizeof(MYSQL_BIND) * params.size());

    for (size_t i = 0; i < params.size(); ++i) {
        in_len[i]               = params[i].size();
        in_bind[i].buffer_type   = MYSQL_TYPE_STRING;
        in_bind[i].buffer        = const_cast<char*>(params[i].c_str());
        in_bind[i].buffer_length = in_len[i];
        in_bind[i].length        = &in_len[i];
    }
    if (!params.empty() && mysql_stmt_bind_param(stmt, in_bind.data())) {
        std::string err = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        throw std::runtime_error("bind_param: " + err);
    }

    if (mysql_stmt_execute(stmt)) {
        std::string err = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        throw std::runtime_error("execute: " + err);
    }

    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
    Rows rows;

    if (meta) {
        unsigned int cols = mysql_num_fields(meta);

        // Buffers de sortie — 2 Ko par colonne, suffisant pour les champs
        const size_t BUF = 2048;
        std::vector<std::vector<char>> bufs(cols, std::vector<char>(BUF));
        std::vector<unsigned long>     out_len(cols, 0);
        std::vector<bool>           is_null(cols, false);
        std::vector<MYSQL_BIND>        out_bind(cols);
        memset(out_bind.data(), 0, sizeof(MYSQL_BIND) * cols);

        for (unsigned int i = 0; i < cols; ++i) {
            out_bind[i].buffer_type   = MYSQL_TYPE_STRING;
            out_bind[i].buffer        = bufs[i].data();
            out_bind[i].buffer_length = BUF;
            out_bind[i].length        = &out_len[i];
            out_bind[i].is_null       = &is_null[i];
        }
        mysql_stmt_bind_result(stmt, out_bind.data());
        mysql_stmt_store_result(stmt);

        while (mysql_stmt_fetch(stmt) == 0) {
            Row row;
            row.reserve(cols);
            for (unsigned int i = 0; i < cols; ++i) {
                if (is_null[i])
                    row.emplace_back("");
                else
                    row.emplace_back(bufs[i].data(), out_len[i]);
            }
            rows.push_back(std::move(row));
        }
        mysql_free_result(meta);
    }

    mysql_stmt_close(stmt);
    return rows;
}

// ================================================================
// INSERT / UPDATE / DELETE — retourne le last_insert_id
// ================================================================

inline uint64_t Execute(MYSQL* conn,
                        const std::string& sql,
                        const std::vector<std::string>& params = {})
{
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) throw std::runtime_error("stmt_init failed");

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size())) {
        std::string err = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        throw std::runtime_error("prepare: " + err);
    }

    std::vector<MYSQL_BIND>    bind(params.size());
    std::vector<unsigned long> lengths(params.size());
    memset(bind.data(), 0, sizeof(MYSQL_BIND) * params.size());

    for (size_t i = 0; i < params.size(); ++i) {
        lengths[i]               = params[i].size();
        bind[i].buffer_type      = MYSQL_TYPE_STRING;
        bind[i].buffer           = const_cast<char*>(params[i].c_str());
        bind[i].buffer_length    = lengths[i];
        bind[i].length           = &lengths[i];
    }
    if (!params.empty() && mysql_stmt_bind_param(stmt, bind.data())) {
        std::string err = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        throw std::runtime_error("bind_param: " + err);
    }

    if (mysql_stmt_execute(stmt)) {
        std::string err = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        throw std::runtime_error("execute: " + err);
    }

    uint64_t last_id = mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);
    return last_id;
}

// ================================================================
// Helper : extraction du token depuis le header Cookie
// ================================================================

inline std::string ExtractSessionToken(const std::string& cookie_header) {
    auto pos = cookie_header.find("session=");
    if (pos == std::string::npos) return "";
    std::string token = cookie_header.substr(pos + 8, 64);
    // Trim sur ; éventuel
    auto end = token.find(';');
    if (end != std::string::npos) token = token.substr(0, end);
    return token;
}