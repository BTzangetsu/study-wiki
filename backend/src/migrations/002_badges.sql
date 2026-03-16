-- migrations/002_badges.sql
INSERT INTO badges (name, description, icon_key, condition_type, condition_value) VALUES
('Premier pas',      'Premier document uploadé',          'badge_first',      'first_upload',       1),
('Contributeur',     '5 documents uploadés',              'badge_contrib',    'upload_count',       5),
('Prolifique',       '20 documents uploadés',             'badge_prolific',   'upload_count',       20),
('Populaire',        '100 téléchargements reçus',         'badge_popular',    'download_received',  100),
('Incontournable',   '1000 téléchargements reçus',        'badge_unmissable', 'download_received',  1000),
('Bien noté',        '10 votes reçus',                    'badge_rated',      'vote_received',      10),
('Actif',            '10 commentaires postés',            'badge_active',     'comment_count',      10),
('Expert',           '500 points',                        'badge_expert',     'points_threshold',   500),
('Légende',          '2000 points',                       'badge_legend',     'points_threshold',   2000);