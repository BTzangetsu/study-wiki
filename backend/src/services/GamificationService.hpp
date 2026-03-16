// services/GamificationService.hpp
#pragma once

#include "../db/ConnectionPool.hpp"
#include "../db/QueryHelpers.hpp"

// ================================================================
// Appelé après chaque action qui peut déclencher un badge.
// Toujours appelé avec une connexion déjà acquise pour éviter
// de consommer deux slots du pool.
// ================================================================

class GamificationService {
public:

    // --------------------------------------------------------
    // Point d'entrée principal — à appeler après chaque action
    // qui modifie les points ou les compteurs d'un utilisateur
    // --------------------------------------------------------
    static void CheckAndAwardBadges(MYSQL* conn, int user_id) {
        // Charge tous les badges existants
        auto badges = QueryRows(conn,
            "SELECT id, condition_type, condition_value "
            "FROM badges",
            {});

        // Charge les badges déjà obtenus par l'user
        auto already = QueryRows(conn,
            "SELECT badge_id FROM user_badges WHERE user_id = ?",
            {std::to_string(user_id)});

        std::unordered_set<int> owned;
        for (auto& r : already)
            owned.insert(std::stoi(r[0]));

        // Charge les stats actuelles de l'user en une seule requête
        auto stats = QueryRows(conn,
            "SELECT "
            "  u.points, "
            "  (SELECT COUNT(*) FROM documents "
            "   WHERE user_id = ? AND is_approved = 1) AS upload_count, "
            "  (SELECT COALESCE(SUM(download_count), 0) FROM documents "
            "   WHERE user_id = ?) AS downloads_received, "
            "  (SELECT COUNT(*) FROM document_votes dv "
            "   JOIN documents d ON d.id = dv.document_id "
            "   WHERE d.user_id = ?) AS votes_received, "
            "  (SELECT COUNT(*) FROM comments "
            "   WHERE user_id = ? AND is_deleted = 0) AS comment_count "
            "FROM users u WHERE u.id = ? LIMIT 1",
            {
                std::to_string(user_id),
                std::to_string(user_id),
                std::to_string(user_id),
                std::to_string(user_id),
                std::to_string(user_id)
            });

        if (stats.empty()) return;

        int points            = std::stoi(stats[0][0]);
        int upload_count      = std::stoi(stats[0][1]);
        int downloads_received= std::stoi(stats[0][2]);
        int votes_received    = std::stoi(stats[0][3]);
        int comment_count     = std::stoi(stats[0][4]);

        for (auto& badge : badges) {
            int         badge_id   = std::stoi(badge[0]);
            std::string cond_type  = badge[1];
            int         cond_value = std::stoi(badge[2]);

            // Déjà obtenu → skip
            if (owned.count(badge_id)) continue;

            bool earned = false;

            if      (cond_type == "upload_count")      earned = upload_count       >= cond_value;
            else if (cond_type == "download_received")  earned = downloads_received >= cond_value;
            else if (cond_type == "points_threshold")   earned = points             >= cond_value;
            else if (cond_type == "vote_received")      earned = votes_received     >= cond_value;
            else if (cond_type == "comment_count")      earned = comment_count      >= cond_value;
            else if (cond_type == "first_upload")       earned = upload_count       >= 1;

            if (earned) AwardBadge(conn, user_id, badge_id);
        }
    }

    // --------------------------------------------------------
    // Ajoute des points avec log — utiliser cette fonction
    // plutôt que de faire l'UPDATE directement dans les routes
    // --------------------------------------------------------
    static void AddPoints(MYSQL* conn,
                          int user_id,
                          int delta,
                          const std::string& reason,
                          int ref_id = -1)
    {
        Execute(conn,
            "UPDATE users SET points = points + ? WHERE id = ?",
            {std::to_string(delta), std::to_string(user_id)});

        Execute(conn,
            "INSERT INTO points_log (user_id, delta, reason, ref_id) "
            "VALUES (?, ?, ?, ?)",
            {
                std::to_string(user_id),
                std::to_string(delta),
                reason,
                ref_id >= 0 ? std::to_string(ref_id) : ""
            });
    }

private:
    static void AwardBadge(MYSQL* conn, int user_id, int badge_id) {
        // INSERT IGNORE — sécurité si double appel concurrent
        Execute(conn,
            "INSERT IGNORE INTO user_badges (user_id, badge_id) "
            "VALUES (?, ?)",
            {std::to_string(user_id), std::to_string(badge_id)});

        // +5 points pour l'obtention d'un badge
        AddPoints(conn, user_id, 5, "badge_earned", badge_id);
    }
};