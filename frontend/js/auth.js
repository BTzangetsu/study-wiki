// js/auth.js
// ============================================================
// État courant de l'utilisateur.
// null = personne de connecté.
// Objet = { id, username, email, points, is_admin, created_at }
// ============================================================
let currentUser = null;

// ============================================================
// Initialise l'état auth au chargement de la page.
// À appeler en premier dans chaque page JS.
//
// redirectIfAuth   : true → redirige vers index.html si connecté
//                    (utile sur login.html et register.html)
// redirectIfNoAuth : true → redirige vers login.html si non connecté
//                    (utile sur upload.html, profile.html...)
// ============================================================
async function initAuth({ redirectIfAuth = false, redirectIfNoAuth = false } = {}) {
    try {
        currentUser = await Auth.me();

        // L'utilisateur est connecté.
        if (redirectIfAuth) {
            window.location.href = 'index.html';
            return;
        }

        // Met à jour l'UI de la navbar selon l'état connecté.
        updateNavbar(true);

    } catch (err) {
        // err.status === 401 → pas de session valide = non connecté.
        currentUser = null;

        if (redirectIfNoAuth) {
            window.location.href = 'login.html';
            return;
        }

        updateNavbar(false);
    }
}

// ============================================================
// Met à jour la navbar selon l'état de connexion.
// On cherche des éléments par leurs attributs data-auth
// et data-noauth pour afficher/cacher selon l'état.
//
// data-auth    : visible seulement si connecté
// data-noauth  : visible seulement si non connecté
// data-admin   : visible seulement si admin
// ============================================================
function updateNavbar(isLoggedIn) {
    // querySelectorAll retourne tous les éléments qui matchent
    // le sélecteur CSS, sous forme de NodeList.
    document.querySelectorAll('[data-auth]').forEach(el => {
        // style.display = '' → remet le display par défaut de l'élément
        // (au lieu d'imposer 'block' qui casserait les flex items)
        el.style.display = isLoggedIn ? '' : 'none';
    });

    document.querySelectorAll('[data-noauth]').forEach(el => {
        el.style.display = isLoggedIn ? 'none' : '';
    });

    document.querySelectorAll('[data-admin]').forEach(el => {
        el.style.display = (isLoggedIn && currentUser?.is_admin) ? '' : 'none';
    });

    // Met à jour le username affiché dans la navbar si l'élément existe
    const usernameEl = document.getElementById('nav-username');
    if (usernameEl && currentUser) {
        usernameEl.textContent = currentUser.username;
    }
}

// ============================================================
// Déconnexion — appelée sur le bouton logout de toutes les pages.
// ============================================================
async function logout() {
    try {
        await Auth.logout();
    } finally {
        // On redirige dans tous les cas, même si la requête échoue.
        // Le cookie sera de toute façon invalidé côté serveur.
        currentUser = null;
        window.location.href = 'login.html';
    }
}

// ============================================================
// TOAST — notification temporaire
// Affiche un message en bas de page pendant 3 secondes.
//
// message : texte à afficher
// type    : 'success' (défaut) ou 'error'
// ============================================================
function showToast(message, type = 'success') {
    // Récupère ou crée l'élément toast
    let toast = document.getElementById('toast');
    if (!toast) {
        toast = document.createElement('div');
        toast.id = 'toast';
        toast.className = 'toast';
        document.body.appendChild(toast);
    }

    toast.textContent = message;
    toast.classList.toggle('error', type === 'error');

    // Ajoute .show → déclenche la transition CSS translateY(0)
    toast.classList.add('show');

    // Retire .show après 3 secondes → animation de sortie
    setTimeout(() => toast.classList.remove('show'), 3000);
}

// ============================================================
// HELPERS — utilitaires réutilisables dans toutes les pages
// ============================================================

// Formate une date ISO en français lisible
// "2024-01-15T10:30:00" → "15 jan. 2024"
function formatDate(isoString) {
    if (!isoString) return '';
    return new Date(isoString).toLocaleDateString('fr-FR', {
        day:   'numeric',
        month: 'short',
        year:  'numeric',
    });
}

// Formate une taille en octets en unité lisible
// 204800 → "200 Ko"
function formatSize(bytes) {
    if (!bytes) return '';
    if (bytes < 1024)        return bytes + ' o';
    if (bytes < 1024 * 1024) return Math.round(bytes / 1024) + ' Ko';
    return (bytes / (1024 * 1024)).toFixed(1) + ' Mo';
}

// Génère les initiales d'un username pour les avatars
// "alice_martin" → "AM"
function getInitials(username) {
    if (!username) return '?';
    return username
        .split(/[_\s-]/)        // découpe sur _, espace, tiret
        .filter(Boolean)
        .slice(0, 2)
        .map(w => w[0].toUpperCase())
        .join('');
}

// Génère les étoiles HTML selon une note
// 4.2 → "★★★★☆"
function renderStars(rating) {
    const full  = Math.round(rating);
    const empty = 5 - full;
    return '★'.repeat(full) + '☆'.repeat(empty);
}

// Échappe le HTML pour éviter les injections XSS.
// Si un utilisateur enregistre "<script>..." comme username,
// cette fonction le transforme en texte inoffensif.
function escapeHtml(str) {
    const div = document.createElement('div');
    div.appendChild(document.createTextNode(str || ''));
    return div.innerHTML;
}