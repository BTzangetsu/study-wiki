// js/api.js
// ============================================================
// BASE URL — pointe vers le backend.
// En développement : http://localhost:8080
// En production : https://tondomaine.fr
// On lit depuis un attribut data- sur le body pour pouvoir
// changer sans toucher au JS.
// ============================================================
const API_BASE = document.body.dataset.api || 'http://localhost:8080';

// ============================================================
// FONCTION CENTRALE — api()
// Wrappе tous les fetch vers le backend.
//
// Paramètres :
//   path    : chemin relatif, ex: '/api/documents'
//   options : objet optionnel avec method, body, params
//
// Retourne : la réponse parsée en JSON, ou lance une erreur.
// ============================================================
async function api(path, options = {}) {
    const { method = 'GET', body, params } = options;

    // Construction de l'URL avec query params si fournis.
    // URLSearchParams transforme un objet en query string :
    // { page: 1, limit: 20 } → "?page=1&limit=20"
    let url = API_BASE + path;
    if (params) {
        const qs = new URLSearchParams(params).toString();
        if (qs) url += '?' + qs;
    }

    const fetchOptions = {
        method,
        // credentials: 'include' → envoie les cookies à chaque requête.
        // OBLIGATOIRE pour que le cookie de session soit transmis.
        credentials: 'include',
        headers: {}
    };

    // Si on a un body, on le sérialise en JSON et on indique
    // au serveur que c'est du JSON via le header Content-Type.
    // JSON.stringify convertit l'objet JS en chaîne JSON :
    // { title: "..." } → '{"title":"..."}'
    if (body) {
        fetchOptions.headers['Content-Type'] = 'application/json';
        fetchOptions.body = JSON.stringify(body);
    }

    const res = await fetch(url, fetchOptions);

    // Le serveur a répondu, mais ça ne veut pas dire que c'est OK.
    // res.ok est true si le code HTTP est entre 200 et 299.
    // On parse toujours le JSON pour récupérer le message d'erreur.
    const data = await res.json().catch(() => ({}));

    if (!res.ok) {
        // On crée une erreur avec le message du serveur,
        // plus le code HTTP pour pouvoir le tester dans les pages.
        const err = new Error(data.error || 'Erreur inconnue');
        err.status = res.status;
        throw err;
    }

    return data;
}

// ============================================================
// RACCOURCIS — évitent de répéter { method: 'POST' } partout
// ============================================================
const get    = (path, params)  => api(path, { params });
const post   = (path, body)    => api(path, { method: 'POST',   body });
const patch  = (path, body)    => api(path, { method: 'PATCH',  body });
const del    = (path)          => api(path, { method: 'DELETE' });

// ============================================================
// ROUTES AUTH
// ============================================================
const Auth = {
    register: (data)  => post('/api/auth/register', data),
    login:    (data)  => post('/api/auth/login', data),
    logout:   ()      => post('/api/auth/logout'),
    me:       ()      => get('/api/auth/me'),
    update:   (data)  => patch('/api/auth/me', data),
    delete:   ()      => del('/api/auth/me'),
};

// ============================================================
// ROUTES DOCUMENTS
// ============================================================
const Documents = {
    list:       (params)      => get('/api/documents', params),
    get:        (id)          => get(`/api/documents/${id}`),
    favorites:  ()            => get('/api/documents/favorites'),
    uploadUrl:  (data)        => post('/api/documents/upload-url', data),
    create:     (data)        => post('/api/documents', data),
    update:     (id, data)    => patch(`/api/documents/${id}`, data),
    delete:     (id)          => del(`/api/documents/${id}`),
    downloadUrl:(id)          => get(`/api/documents/${id}/download-url`),
    vote:       (id, score)   => post(`/api/documents/${id}/vote`, { score }),
    removeVote: (id)          => del(`/api/documents/${id}/vote`),
    favorite:   (id)          => post(`/api/documents/${id}/favorite`),
    unfavorite: (id)          => del(`/api/documents/${id}/favorite`),
};

// ============================================================
// ROUTES COMMENTAIRES
// ============================================================
const Comments = {
    list:   (docId)           => get(`/api/documents/${docId}/comments`),
    post:   (docId, data)     => post(`/api/documents/${docId}/comments`, data),
    update: (id, content)     => patch(`/api/comments/${id}`, { content }),
    delete: (id)              => del(`/api/comments/${id}`),
    like:   (id)              => post(`/api/comments/${id}/like`),
    unlike: (id)              => del(`/api/comments/${id}/like`),
};

// ============================================================
// ROUTES RECHERCHE
// ============================================================
const Search = {
    query: (params) => get('/api/search', params),
};

// ============================================================
// ROUTES UTILISATEURS & CLASSEMENTS
// ============================================================
const Users = {
    get:       (id)     => get(`/api/users/${id}`),
    documents: (id, p)  => get(`/api/users/${id}/documents`, p),
};

const Leaderboard = {
    users:   (params) => get('/api/leaderboard/users',   params),
    schools: (params) => get('/api/leaderboard/schools', params),
};

// ============================================================
// ROUTES SIGNALEMENTS
// ============================================================
const Reports = {
    create: (data) => post('/api/reports', data),
};

// ============================================================
// ROUTES SUGGESTIONS
// ============================================================
const Suggestions = {
    list:       (params) => get('/api/suggestions', params),
    get:        (id)     => get(`/api/suggestions/${id}`),
    create:     (data)   => post('/api/suggestions', data),
    vote:       (id)     => post(`/api/suggestions/${id}/vote`),
    removeVote: (id)     => del(`/api/suggestions/${id}/vote`),
    delete:     (id)     => del(`/api/suggestions/${id}`),
};

// ============================================================
// ROUTES ADMIN
// ============================================================
const Admin = {
    stats:              ()          => get('/api/admin/stats'),
    reports:            (params)    => get('/api/admin/reports', params),
    updateReport:       (id, data)  => patch(`/api/admin/reports/${id}`, data),
    updateDocument:     (id, data)  => patch(`/api/admin/documents/${id}`, data),
    deleteDocument:     (id)        => del(`/api/admin/documents/${id}`),
    deleteComment:      (id)        => del(`/api/admin/comments/${id}`),
    updateUser:         (id, data)  => patch(`/api/admin/users/${id}`, data),
    updateSuggestion:   (id, data)  => patch(`/api/admin/suggestions/${id}`, data),
};

// ============================================================
// UPLOAD FICHIER vers B2 — flux en deux étapes
//
// Étape 1 : demander une signed URL au backend
// Étape 2 : envoyer le fichier directement vers B2
//           avec un PUT (pas de JSON, juste le binaire)
//
// onProgress : callback appelé avec le % d'avancement (0→100)
//              pour afficher une barre de progression
// ============================================================
async function uploadFile(file, onProgress) {
    // Étape 1 — récupère la signed URL
    const { upload_url, storage_key } = await Documents.uploadUrl({
        filename:   file.name,
        size_bytes: file.size,
    });

    // Étape 2 — envoie le fichier vers B2.
    // On utilise XMLHttpRequest plutôt que fetch ici
    // car fetch ne supporte pas le suivi de progression.
    // XMLHttpRequest est plus ancien mais expose upload.onprogress.
    await new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();

        // upload.onprogress se déclenche régulièrement pendant l'envoi.
        // loaded = octets envoyés, total = taille totale.
        xhr.upload.onprogress = (e) => {
            if (e.lengthComputable && onProgress) {
                onProgress(Math.round((e.loaded / e.total) * 100));
            }
        };

        xhr.onload  = () => xhr.status < 400 ? resolve() : reject(new Error('Upload failed'));
        xhr.onerror = () => reject(new Error('Network error'));

        xhr.open('PUT', upload_url);
        // Pas de Content-Type JSON ici — on envoie le fichier brut
        xhr.send(file);
    });

    return storage_key;
}