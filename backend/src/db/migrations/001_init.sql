-- ============================================================
-- SOCLE : écoles, matières, utilisateurs
-- ============================================================

CREATE TABLE schools (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    name          VARCHAR(200) NOT NULL UNIQUE,
    city          VARCHAR(100),
    country       VARCHAR(100) DEFAULT 'France',
    doc_count     INT DEFAULT 0,
    created_at    DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE subjects (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    name          VARCHAR(150) NOT NULL,
    school_id     INT NOT NULL REFERENCES schools(id),
    UNIQUE(name, school_id)
);

CREATE TABLE users (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    username      VARCHAR(50)  NOT NULL UNIQUE,
    -- NULL si non fourni, UNIQUE seulement quand non NULL
    email         VARCHAR(200) DEFAULT NULL,
    password_hash VARCHAR(255) NOT NULL,
    school_id     INT REFERENCES schools(id),
    points        INT DEFAULT 0,
    is_active     TINYINT DEFAULT 1,
    is_admin      TINYINT DEFAULT 0,
    created_at    DATETIME DEFAULT CURRENT_TIMESTAMP,
    last_login    DATETIME
);
CREATE UNIQUE INDEX idx_users_email
    ON users(email);

-- ============================================================
-- DOCUMENTS
-- ============================================================

CREATE TABLE documents (
    id              INT AUTO_INCREMENT PRIMARY KEY,
    user_id         INT NOT NULL REFERENCES users(id),
    subject_id      INT REFERENCES subjects(id),
    school_id       INT REFERENCES schools(id),
    title           VARCHAR(300) NOT NULL,
    description     TEXT,
    year            YEAR,
    type            ENUM('exam','td','cours','tp','autre') NOT NULL,
    storage_key     VARCHAR(500) NOT NULL UNIQUE,   -- chemin dans B2/Wasabi
    storage_bucket  VARCHAR(200) NOT NULL,
    size_bytes      BIGINT,
    avg_rating      FLOAT DEFAULT 0,
    vote_count      INT DEFAULT 0,
    download_count  INT DEFAULT 0,
    is_approved     TINYINT DEFAULT 1,
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- ============================================================
-- COMMENTAIRES + RÉPONSES (arbre récursif)
-- ============================================================

-- Un commentaire peut avoir un parent_id qui pointe vers
-- un autre commentaire du même document → profondeur infinie.
-- NULL = commentaire racine, sinon = réponse à un commentaire.

CREATE TABLE comments (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    document_id   INT NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
    user_id       INT NOT NULL REFERENCES users(id),
    parent_id     INT DEFAULT NULL REFERENCES comments(id) ON DELETE CASCADE,
    content       TEXT NOT NULL,
    is_deleted    TINYINT DEFAULT 0,   -- soft delete (contenu masqué, thread gardé)
    created_at    DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at    DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- Index pour charger rapidement tous les enfants d'un commentaire
CREATE INDEX idx_comments_parent    ON comments(parent_id);
CREATE INDEX idx_comments_document  ON comments(document_id);

-- ============================================================
-- VOTES SUR DOCUMENTS  (1 à 5 étoiles, 1 vote par user/doc)
-- ============================================================

CREATE TABLE document_votes (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    user_id       INT NOT NULL REFERENCES users(id),
    document_id   INT NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
    score         TINYINT NOT NULL CHECK (score BETWEEN 1 AND 5),
    created_at    DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, document_id)
);

-- ============================================================
-- LIKES SUR COMMENTAIRES  (upvote simple, 1 par user/comment)
-- ============================================================

CREATE TABLE comment_likes (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    user_id       INT NOT NULL REFERENCES users(id),
    comment_id    INT NOT NULL REFERENCES comments(id) ON DELETE CASCADE,
    created_at    DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, comment_id)
);

-- ============================================================
-- FAVORIS
-- ============================================================

CREATE TABLE favorites (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    user_id       INT NOT NULL REFERENCES users(id),
    document_id   INT NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
    created_at    DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, document_id)
);

-- ============================================================
-- SIGNALEMENTS
-- ============================================================

CREATE TABLE reports (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    user_id       INT NOT NULL REFERENCES users(id),
    document_id   INT REFERENCES documents(id) ON DELETE CASCADE,
    comment_id    INT REFERENCES comments(id)  ON DELETE CASCADE,  -- ou un commentaire
    reason        ENUM('spam','inappropriate','wrong_content','copyright','other') NOT NULL,
    details       TEXT,
    status        ENUM('pending','reviewed','dismissed') DEFAULT 'pending',
    created_at    DATETIME DEFAULT CURRENT_TIMESTAMP,
    CHECK (
        (document_id IS NOT NULL AND comment_id IS NULL) OR
        (document_id IS NULL AND comment_id IS NOT NULL)
    )
);

-- ============================================================
-- GAMIFICATION
-- ============================================================

CREATE TABLE badges (
    id               INT AUTO_INCREMENT PRIMARY KEY,
    name             VARCHAR(100) NOT NULL UNIQUE,
    description      TEXT,
    icon_key         VARCHAR(200),
    condition_type   ENUM(
        'upload_count',
        'download_received',
        'points_threshold',
        'vote_received',
        'comment_count',
        'first_upload'
    ) NOT NULL,
    condition_value  INT NOT NULL
);

CREATE TABLE user_badges (
    id          INT AUTO_INCREMENT PRIMARY KEY,
    user_id     INT NOT NULL REFERENCES users(id),
    badge_id    INT NOT NULL REFERENCES badges(id),
    earned_at   DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, badge_id)
);

-- Historique des mouvements de points (audit + affichage)
CREATE TABLE points_log (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    user_id       INT NOT NULL REFERENCES users(id),
    delta         INT NOT NULL,          -- positif ou négatif
    reason        ENUM(
        'upload',
        'download_received',
        'vote_received',
        'comment_posted',
        'report_validated',
        'badge_earned'
    ) NOT NULL,
    ref_id        INT,                   -- id du document/comment concerné
    created_at    DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- ============================================================
-- INDEX DE PERFORMANCE SUPPLÉMENTAIRES
-- ============================================================

CREATE INDEX idx_documents_school    ON documents(school_id);
CREATE INDEX idx_documents_subject   ON documents(subject_id);
CREATE INDEX idx_documents_year      ON documents(year);
CREATE INDEX idx_documents_type      ON documents(type);
CREATE INDEX idx_points_log_user     ON points_log(user_id);

