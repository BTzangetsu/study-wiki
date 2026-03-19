-- 003_suggestions.sql
CREATE TABLE suggestions (
    id          INT AUTO_INCREMENT PRIMARY KEY,
    user_id     INT NOT NULL REFERENCES users(id),
    title       VARCHAR(200) NOT NULL,
    description TEXT NOT NULL,
    status      ENUM('pending','planned','done','rejected') DEFAULT 'pending',
    upvotes     INT DEFAULT 0,
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE suggestion_votes (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    user_id       INT NOT NULL REFERENCES users(id),
    suggestion_id INT NOT NULL REFERENCES suggestions(id) ON DELETE CASCADE,
    created_at    DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, suggestion_id)
);
