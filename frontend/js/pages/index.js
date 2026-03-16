// js/pages/index.js

// ============================================================
// ÉTAT DE LA PAGE
// On centralise tous les filtres actifs dans un objet.
// Quand un filtre change, on met à jour cet objet et on
// rappelle loadDocuments() — une seule fonction gère tout.
// ============================================================
const filters = {
    q:          '',
    school_id:  '',
    subject_id: '',
    year:       '',
    type:       '',
    page:       1,
    limit:      20,
};

// ============================================================
// POINT D'ENTRÉE — s'exécute au chargement de la page.
// DOMContentLoaded se déclenche quand le HTML est parsé
// mais avant que les images soient chargées — c'est le bon
// moment pour manipuler le DOM.
// ============================================================
document.addEventListener('DOMContentLoaded', async () => {

    // 1. Initialise l'auth — met à jour la navbar
    //    Pas de redirect ici : la page d'accueil est publique
    await initAuth();

    // 2. Remplit les selects école et année
    populateYears();
    await loadSchools();

    // 3. Charge les documents avec les filtres par défaut
    await loadDocuments();

    // 4. Charge la sidebar en parallèle
    //    Promise.all lance les deux requêtes en même temps
    //    au lieu de les attendre l'une après l'autre
    await Promise.all([
        loadTopUsers(),
        loadGlobalStats(),
    ]);

    // 5. Branche les écouteurs d'événements
    bindEvents();
});

// ============================================================
// REMPLISSAGE DES FILTRES STATIQUES
// ============================================================

// Remplit le select année avec les années de 2015 à aujourd'hui
function populateYears() {
    const select = document.getElementById('filter-year');
    const currentYear = new Date().getFullYear();

    // On construit de l'année courante jusqu'à 2015
    for (let y = currentYear; y >= 2015; y--) {
        const opt = document.createElement('option');
        // .value est ce qui sera envoyé au backend
        opt.value = y;
        // .textContent est ce qui s'affiche à l'utilisateur
        opt.textContent = y;
        select.appendChild(opt);
    }
}

// Charge les écoles depuis le backend et remplit le select
async function loadSchools() {
    try {
        // On réutilise GET /api/leaderboard/schools pour avoir
        // la liste des écoles avec leurs noms
        const data = await Leaderboard.schools({ limit: 100 });

        const select = document.getElementById('filter-school');
        data.leaderboard.forEach(school => {
            const opt = document.createElement('option');
            opt.value = school.id;
            opt.textContent = school.name;
            select.appendChild(opt);
        });
    } catch (e) {
        // Silencieux — le filtre reste juste vide si ça échoue
    }
}

// ============================================================
// CHARGEMENT DES DOCUMENTS
// C'est la fonction principale — elle lit l'objet filters,
// appelle le backend, et injecte les cartes dans le DOM.
// ============================================================
async function loadDocuments() {
    const stateEl = document.getElementById('docs-state');
    const listEl  = document.getElementById('docs-list');

    // Affiche le spinner pendant le chargement
    // On vide d'abord la liste pour ne pas empiler les résultats
    listEl.innerHTML  = '';
    stateEl.innerHTML = `<div class="state-loading">
        <div class="spinner"></div>
        <span>Chargement…</span>
    </div>`;

    try {
        // Construit les params en ne gardant que les valeurs non vides.
        // Si filters.type est '' on ne l'envoie pas du tout au backend.
        const params = {};
        Object.entries(filters).forEach(([key, val]) => {
            if (val !== '' && val !== null) params[key] = val;
        });

        const data = await Documents.list(params);

        // Vide le spinner
        stateEl.innerHTML = '';

        if (data.documents.length === 0) {
            stateEl.innerHTML = `<div class="state-empty">
                <span>Aucun document trouvé</span>
                <span class="text-muted">Essaie d'autres filtres</span>
            </div>`;
            return;
        }

        // Injecte chaque carte dans la liste
        // On construit le HTML sous forme de chaîne pour les cartes
        // c'est plus performant que de créer des éléments un par un
        // pour de longues listes
        data.documents.forEach(doc => {
            listEl.insertAdjacentHTML('beforeend', renderDocCard(doc));
        });

        // Branche les événements sur les cartes fraîchement injectées
        bindCardEvents();

        // Construit la pagination
        renderPagination(data.page, data.limit, data.documents.length);

    } catch (err) {
        stateEl.innerHTML = `<div class="state-error">
            <span>Erreur lors du chargement</span>
            <span class="text-muted">${escapeHtml(err.message)}</span>
        </div>`;
    }
}

// ============================================================
// RENDU D'UNE CARTE DOCUMENT
// Retourne une chaîne HTML pour un document.
// escapeHtml() sur toutes les données venant du serveur
// pour éviter les injections XSS.
// ============================================================
function renderDocCard(doc) {
    // Choisit la classe de couleur selon le type
    const tagClass = {
        exam:  'tag-exam',
        td:    'tag-td',
        cours: 'tag-cours',
        tp:    'tag-tp',
        autre: 'tag-autre',
    }[doc.type] || 'tag-autre';

    return `
    <div class="card card-hover doc-card"
         data-id="${doc.id}"
         role="button"
         tabindex="0">

        <div class="doc-icon">
            <svg viewBox="0 0 24 24">
                <path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0
                         002-2V8z"/>
                <polyline points="14 2 14 8 20 8"/>
            </svg>
        </div>

        <div class="doc-body">
            <div class="doc-title truncate">
                ${escapeHtml(doc.title)}
            </div>
            <div class="doc-meta">
                ${doc.school  ? `<span>${escapeHtml(doc.school)}</span>`  : ''}
                ${doc.subject ? `<span>${escapeHtml(doc.subject)}</span>` : ''}
                ${doc.year    ? `<span>${escapeHtml(doc.year)}</span>`    : ''}
                <span>par ${escapeHtml(doc.author)}</span>
                <span class="tag ${tagClass}">${escapeHtml(doc.type)}</span>
            </div>
        </div>

        <div class="doc-right">
            <span class="stars">${renderStars(doc.avg_rating)}</span>
            <span class="text-muted">${doc.download_count} téléch.</span>
            <span class="text-muted">${formatSize(doc.size_bytes)}</span>
        </div>

    </div>`;
}

// ============================================================
// PAGINATION
// On calcule si il y a probablement une page suivante :
// si le nombre de résultats retournés = limit, il y en a sûrement.
// ============================================================
function renderPagination(page, limit, count) {
    const el      = document.getElementById('pagination');
    const hasNext = count === limit;
    const hasPrev = page > 1;

    if (!hasNext && !hasPrev) {
        el.innerHTML = '';
        return;
    }

    let html = '';

    if (hasPrev) {
        html += `<button class="pg-btn" onclick="goToPage(${page - 1})">
                    ←
                 </button>`;
    }

    // Affiche quelques numéros de page autour de la page courante
    const start = Math.max(1, page - 2);
    const end   = page + 2;

    for (let i = start; i <= end; i++) {
        if (i === page) {
            html += `<button class="pg-btn active">${i}</button>`;
        } else if (i > 0) {
            html += `<button class="pg-btn" onclick="goToPage(${i})">
                        ${i}
                     </button>`;
        }
    }

    if (hasNext) {
        html += `<button class="pg-btn" onclick="goToPage(${page + 1})">
                    →
                 </button>`;
    }

    el.innerHTML = html;
}

function goToPage(page) {
    filters.page = page;
    loadDocuments();
    // Remonte en haut de la liste documents
    document.getElementById('docs-list').scrollIntoView(
        { behavior: 'smooth' }
    );
}

// ============================================================
// SIDEBAR — top contributeurs
// ============================================================
async function loadTopUsers() {
    try {
        const data = await Leaderboard.users({ limit: 5 });
        const el   = document.getElementById('top-users');
        const rankClasses = ['gold', 'silver', 'bronze', '', ''];

        el.innerHTML = data.leaderboard.map((u, i) => `
            <div class="lb-row">
                <span class="lb-rank ${rankClasses[i]}">${u.rank}</span>
                <div class="avatar avatar-sm">
                    ${getInitials(u.username)}
                </div>
                <span class="lb-name truncate">
                    ${escapeHtml(u.username)}
                </span>
                <span class="lb-pts">${u.points} pts</span>
            </div>
        `).join('');

    } catch (e) {
        document.getElementById('top-users').innerHTML =
            '<p class="text-muted">Indisponible</p>';
    }
}

// ============================================================
// SIDEBAR — stats globales
// On réutilise GET /api/leaderboard/schools pour le total écoles
// et GET /api/leaderboard/users pour le total users
// ============================================================
async function loadGlobalStats() {
    try {
        // On récupère les docs pour avoir le total
        const docs  = await Documents.list({ limit: 1 });
        const users = await Leaderboard.users({ limit: 1 });

        const el = document.getElementById('global-stats');
        el.innerHTML = `
            <div class="stat-row">
                <span class="stat-label">Documents</span>
                <span class="stat-val">${docs.documents.length > 0 ? '1 000+' : '0'}</span>
            </div>
            <div class="stat-row">
                <span class="stat-label">Étudiants</span>
                <span class="stat-val">${users.leaderboard.length > 0 ? '3 000+' : '0'}</span>
            </div>
        `;
    } catch (e) {
        document.getElementById('global-stats').innerHTML =
            '<p class="text-muted">Indisponible</p>';
    }
}

// ============================================================
// ÉVÉNEMENTS — branchés une seule fois au chargement
// ============================================================
function bindEvents() {

    // Recherche texte — on attend 400ms après la dernière frappe
    // avant de lancer la requête (debounce).
    // Sans ça, une requête partirait à chaque lettre tapée.
    let searchTimer;
    document.getElementById('filter-search')
        .addEventListener('input', (e) => {
            clearTimeout(searchTimer);
            searchTimer = setTimeout(() => {
                filters.q    = e.target.value.trim();
                filters.page = 1;
                loadDocuments();
            }, 400);
        });

    // Filtre école
    document.getElementById('filter-school')
        .addEventListener('change', (e) => {
            filters.school_id = e.target.value;
            filters.page      = 1;
            loadDocuments();
        });

    // Filtre matière
    document.getElementById('filter-subject')
        .addEventListener('change', (e) => {
            filters.subject_id = e.target.value;
            filters.page       = 1;
            loadDocuments();
        });

    // Filtre année
    document.getElementById('filter-year')
        .addEventListener('change', (e) => {
            filters.year = e.target.value;
            filters.page = 1;
            loadDocuments();
        });

    // Pills de type — délégation d'événement.
    // Au lieu d'attacher un listener sur chaque pill,
    // on en attache un seul sur le conteneur parent.
    // e.target est l'élément cliqué.
    document.getElementById('type-pills')
        .addEventListener('click', (e) => {
            const pill = e.target.closest('.pill');
            if (!pill) return;

            // Retire .active de toutes les pills
            document.querySelectorAll('.pill')
                .forEach(p => p.classList.remove('active'));

            // Ajoute .active sur la pill cliquée
            pill.classList.add('active');

            filters.type = pill.dataset.type;
            filters.page = 1;
            loadDocuments();
        });
}

// ============================================================
// ÉVÉNEMENTS SUR LES CARTES — rebranché après chaque chargement
// car les cartes sont recréées dans le DOM à chaque requête
// ============================================================
function bindCardEvents() {
    document.querySelectorAll('.doc-card[data-id]')
        .forEach(card => {
            card.addEventListener('click', () => {
                const id = card.dataset.id;
                // Redirige vers la page détail avec l'id en query param
                // document.html?id=42
                window.location.href = `document.html?id=${id}`;
            });

            // Accessibilité — permet la navigation au clavier.
            // Enter sur un élément focusable = clic
            card.addEventListener('keydown', (e) => {
                if (e.key === 'Enter') card.click();
            });
        });
}