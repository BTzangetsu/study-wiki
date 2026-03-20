-- migrations/004_admin_system.sql

-- Colonne super admin sur users
ALTER TABLE users ADD COLUMN is_super_admin TINYINT DEFAULT 0 AFTER is_admin;

-- Table des demandes d'admin
CREATE TABLE admin_requests (
    id          INT AUTO_INCREMENT PRIMARY KEY,
    user_id     INT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    reason      TEXT NOT NULL,
    status      ENUM('pending','approved','rejected') DEFAULT 'pending',
    reviewed_by INT REFERENCES users(id),
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
    reviewed_at DATETIME,
    UNIQUE(user_id)  -- une seule demande en cours par user
);
