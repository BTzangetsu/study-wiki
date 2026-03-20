// js/pages/index.js

// ============================================================
// ÉTAT DE LA PAGE
// ============================================================
const filters = {
    q: '', school_id: '', subject_id: '',
    year: '', type: '', page: 1, limit: 20,
};

// État du panneau
let panelDocId    = null;  // id du doc actuellement dans le panneau
let panelIsFav    = false; // état favori
let viewerUrl     = null;  // URL signée pour le viewer
let viewerDocId   = null;  // id du doc dans le viewer

// ============================================================
// POINT D'ENTRÉE
// ============================================================
document.addEventListener('DOMContentLoaded', async () => {
    await initAuth();
    populateYears();
    await loadSchools();
    await loadDocuments();
    await Promise.all([loadTopUsers(), loadGlobalStats()]);
    bindEvents();
    bindPanelEvents();
    bindViewerEvents();
});

// ============================================================
// FILTRES STATIQUES
// ============================================================
function populateYears() {
    const select = document.getElementById('filter-year');
    const y = new Date().getFullYear();
    for (let i = y; i >= 2015; i--) {
        const opt = document.createElement('option');
        opt.value = i; opt.textContent = i;
        select.appendChild(opt);
    }
}

async function loadSchools() {
    try {
        const data   = await Leaderboard.schools({ limit: 100 });
        const select = document.getElementById('filter-school');
        data.leaderboard.forEach(s => {
            const opt = document.createElement('option');
            opt.value = s.id; opt.textContent = s.name;
            select.appendChild(opt);
        });
    } catch (e) {}
}

// ============================================================
// CHARGEMENT DES DOCUMENTS
// ============================================================
async function loadDocuments() {
    const stateEl = document.getElementById('docs-state');
    const listEl  = document.getElementById('docs-list');
    listEl.innerHTML  = '';
    stateEl.innerHTML = `<div class="state-loading">
        <div class="spinner"></div><span>Chargement…</span>
    </div>`;

    try {
        const params = {};
        Object.entries(filters).forEach(([k, v]) => {
            if (v !== '' && v !== null) params[k] = v;
        });

        const data = await Documents.list(params);
        stateEl.innerHTML = '';

        if (data.documents.length === 0) {
            stateEl.innerHTML = `<div class="state-empty">
                <span>Aucun document trouvé</span>
                <span class="text-muted">Essaie d'autres filtres</span>
            </div>`;
            return;
        }

        data.documents.forEach(doc => {
            listEl.insertAdjacentHTML('beforeend', renderDocCard(doc));
        });

        bindCardEvents();
        renderPagination(data.page, data.limit, data.documents.length);

    } catch (err) {
        stateEl.innerHTML = `<div class="state-error">
            <span>Erreur de chargement</span>
        </div>`;
    }
}

function renderDocCard(doc) {
    const tagClass = {
        exam: 'tag-exam', td: 'tag-td',
        cours: 'tag-cours', tp: 'tag-tp', autre: 'tag-autre'
    }[doc.type] || 'tag-autre';

    return `
    <div class="card card-hover doc-card"
         data-id="${doc.id}"
         role="button" tabindex="0">
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

function bindCardEvents() {
    document.querySelectorAll('.doc-card[data-id]').forEach(card => {
        card.addEventListener('click', () => openPanel(card.dataset.id));
        card.addEventListener('keydown', e => {
            if (e.key === 'Enter') openPanel(card.dataset.id);
        });
    });
}

// ============================================================
// PANNEAU LATÉRAL
// ============================================================
async function openPanel(docId) {
    // Si on clique sur le même doc → ferme le panneau
    if (panelDocId === String(docId)) {
        closePanel();
        return;
    }

    panelDocId = String(docId);

    // Surligne la carte active
    document.querySelectorAll('.doc-card').forEach(c =>
        c.classList.toggle('active', c.dataset.id === panelDocId));

    // Ouvre le panneau
    document.getElementById('main-layout').classList.add('with-panel');

    // Vide et affiche un spinner dans le panneau
    const inner = document.getElementById('detail-panel-inner');
    document.getElementById('panel-title').textContent    = 'Chargement…';
    document.getElementById('panel-meta').innerHTML       = '';
    document.getElementById('panel-stars').textContent    = '';
    document.getElementById('panel-desc').textContent     = '';
    document.getElementById('panel-comments').innerHTML   = '';

    try {
        // Charge doc + commentaires en parallèle
        const [doc, commentsData] = await Promise.all([
            Documents.get(docId),
            Comments.list(docId),
        ]);

        // Remplit le panneau
        document.getElementById('panel-title').textContent =
            doc.title;

        const metaParts = [doc.school, doc.subject, doc.year]
            .filter(Boolean)
            .map(s => `<span>${escapeHtml(s)}</span>`)
            .join('');
        document.getElementById('panel-meta').innerHTML = metaParts;

        document.getElementById('panel-stars').textContent =
            doc.vote_count > 0
                ? `${renderStars(doc.avg_rating)} ${doc.avg_rating.toFixed(1)} · ${doc.download_count} téléch.`
                : `${doc.download_count} téléchargements`;

        document.getElementById('panel-desc').textContent =
            doc.description || '';

        // Lien "voir tous les commentaires"
        document.getElementById('btn-see-all-comments').href =
            `document.html?id=${docId}`;

        // 3 premiers commentaires racine
        const rootComments = (commentsData.comments || [])
            .filter(c => !c.is_deleted)
            .slice(0, 3);

        document.getElementById('panel-comments').innerHTML =
            rootComments.length === 0
                ? `<p class="text-muted text-sm">
                       Aucun commentaire pour l'instant
                   </p>`
                : rootComments.map(c => `
                    <div class="panel-comment">
                        <div class="avatar avatar-sm">
                            ${getInitials(c.username)}
                        </div>
                        <div class="panel-comment-body">
                            <span class="panel-comment-user">
                                ${escapeHtml(c.username)}
                            </span>
                             — ${escapeHtml(c.content.substring(0, 80))}
                            ${c.content.length > 80 ? '…' : ''}
                        </div>
                    </div>`).join('');

        // Vérifie si favori
        if (currentUser) {
            try {
                const favs = await Documents.favorites();
                panelIsFav = favs.documents.some(
                    d => d.id === parseInt(docId));
                updateFavBtn();
            } catch (e) {}
        }

        // Étoiles de vote — remet à zéro
        resetPanelVote();

        // Stocke l'URL du doc pour le viewer
        viewerDocId = docId;

    } catch (err) {
        document.getElementById('panel-title').textContent =
            'Erreur de chargement';
    }
}

function closePanel() {
    document.getElementById('main-layout').classList.remove('with-panel');
    document.querySelectorAll('.doc-card').forEach(c =>
        c.classList.remove('active'));
    panelDocId  = null;
    viewerDocId = null;
    viewerUrl   = null;
}

// ============================================================
// ÉVÉNEMENTS DU PANNEAU
// ============================================================
function bindPanelEvents() {

    // Fermer le panneau
    document.getElementById('btn-close-panel')
        .addEventListener('click', closePanel);

    // Télécharger
    document.getElementById('btn-panel-download')
        ?.addEventListener('click', async () => {
            if (!panelDocId) return;
            try {
                const { download_url } = await Documents.downloadUrl(panelDocId);
                window.open(download_url, '_blank');
                showToast('Téléchargement démarré');
            } catch (err) {
                showToast('Erreur', 'error');
            }
        });

    // Aperçu
    document.getElementById('btn-panel-preview')
        .addEventListener('click', async () => {
            if (!panelDocId) return;
            await openViewer(panelDocId);
        });

    // Favori
    document.getElementById('btn-panel-favorite')
        ?.addEventListener('click', async () => {
            if (!panelDocId || !currentUser) return;
            try {
                if (panelIsFav) {
                    await Documents.unfavorite(panelDocId);
                    panelIsFav = false;
                    showToast('Retiré des favoris');
                } else {
                    await Documents.favorite(panelDocId);
                    panelIsFav = true;
                    showToast('Ajouté aux favoris');
                }
                updateFavBtn();
            } catch (err) {
                showToast('Erreur', 'error');
            }
        });

    // Vote étoiles
    const starsContainer = document.getElementById('panel-stars-input');
    if (starsContainer) {
        const stars = starsContainer.querySelectorAll('.star');

        stars.forEach(star => {
            const val = parseInt(star.dataset.value);

            star.addEventListener('mouseenter', () => {
                stars.forEach(s =>
                    s.classList.toggle('hover',
                        parseInt(s.dataset.value) <= val));
            });

            star.addEventListener('mouseleave', () => {
                stars.forEach(s => s.classList.remove('hover'));
            });

            star.addEventListener('click', async () => {
                if (!panelDocId || !currentUser) return;
                try {
                    await Documents.vote(panelDocId, val);
                    stars.forEach(s =>
                        s.classList.toggle('active',
                            parseInt(s.dataset.value) <= val));
                    showToast('Vote enregistré');
                } catch (err) {
                    showToast('Erreur', 'error');
                }
            });
        });
    }
}

function updateFavBtn() {
    const btn = document.getElementById('btn-panel-favorite');
    if (!btn) return;
    btn.textContent = panelIsFav ? '♥ Favori' : '♡ Favori';
    btn.style.color = panelIsFav ? 'var(--blue-600)' : '';
}

function resetPanelVote() {
    document.querySelectorAll('#panel-stars-input .star')
        .forEach(s => {
            s.classList.remove('active');
            s.classList.remove('hover');
        });
}

// ============================================================
// VIEWER — aperçu du fichier
// ============================================================
async function openViewer(docId) {
    const overlay   = document.getElementById('viewer-overlay');
    const body      = document.getElementById('viewer-body');
    const titleEl   = document.getElementById('viewer-modal-title');
    const dlBtn     = document.getElementById('btn-viewer-download');

    // Affiche un spinner pendant la récupération de l'URL
    body.innerHTML = `<div class="state-loading" style="padding:60px 0">
        <div class="spinner"></div>
        <span>Préparation de l'aperçu…</span>
    </div>`;

    overlay.classList.add('open');

    try {
        const { download_url } = await Documents.downloadUrl(docId);
        viewerUrl = download_url;

        // Titre
        titleEl.textContent =
            document.getElementById('panel-title').textContent;

        // Détecte le type depuis la clé de stockage
        const key = download_url.split('?')[0]; // retire les params
        const ext = key.split('.').pop().toLowerCase();

        if (ext === 'pdf') {
            // PDF — iframe native du navigateur
            // Le navigateur affiche son propre viewer PDF
            body.innerHTML =
                `<iframe src="${viewerUrl}" title="Aperçu PDF"></iframe>`;

        } else if (['png','jpg','jpeg','gif','webp'].includes(ext)) {
            // Image — balise img simple
            body.innerHTML =
                `<img src="${viewerUrl}" alt="Aperçu">`;

        } else {
            // Format non prévisualisable
            body.innerHTML = `<div class="viewer-unsupported">
                <span>Aperçu non disponible pour ce format</span>
                <span class="text-muted">
                    (.${ext}) — télécharge le fichier pour l'ouvrir
                </span>
                <button class="btn btn-primary"
                        onclick="document.getElementById(
                            'btn-viewer-download').click()">
                    Télécharger
                </button>
            </div>`;
        }

        // Bouton télécharger dans le footer du viewer
        dlBtn.onclick = () => {
            window.open(viewerUrl, '_blank');
            showToast('Téléchargement démarré');
        };

    } catch (err) {
        body.innerHTML = `<div class="viewer-unsupported">
            <span>Impossible de charger l'aperçu</span>
            <span class="text-muted">${escapeHtml(err.message)}</span>
        </div>`;
    }
}

function closeViewer() {
    const overlay = document.getElementById('viewer-overlay');
    overlay.classList.remove('open');

    // Vide l'iframe pour stopper le chargement PDF
    setTimeout(() => {
        document.getElementById('viewer-body').innerHTML = '';
    }, 200);
}

function bindViewerEvents() {
    // Bouton fermer
    document.getElementById('btn-close-viewer')
        .addEventListener('click', closeViewer);

    // Clic en dehors du modal → ferme le viewer
    document.getElementById('viewer-overlay')
        .addEventListener('click', (e) => {
            if (e.target === document.getElementById('viewer-overlay'))
                closeViewer();
        });

    // Touche Escape → ferme le viewer
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') {
            if (document.getElementById('viewer-overlay')
                    .classList.contains('open')) {
                closeViewer();
            }
        }
    });
}

// ============================================================
// SIDEBAR
// ============================================================
async function loadTopUsers() {
    try {
        const data = await Leaderboard.users({ limit: 5 });
        const el   = document.getElementById('top-users');
        const cls  = ['gold', 'silver', 'bronze', '', ''];
        el.innerHTML = data.leaderboard.map((u, i) => `
            <div class="lb-row">
                <span class="lb-rank ${cls[i]}">${u.rank}</span>
                <div class="avatar avatar-sm">
                    ${getInitials(u.username)}
                </div>
                <span class="lb-name truncate">
                    ${escapeHtml(u.username)}
                </span>
                <span class="lb-pts">${u.points} pts</span>
            </div>`).join('');
    } catch (e) {
        document.getElementById('top-users').innerHTML =
            '<p class="text-muted">Indisponible</p>';
    }
}

async function loadGlobalStats() {
    try {
        const docs  = await Documents.list({ limit: 1 });
        const users = await Leaderboard.users({ limit: 1 });
        document.getElementById('global-stats').innerHTML = `
            <div class="stat-row">
                <span class="stat-label">Documents</span>
                <span class="stat-val">
                    ${docs.documents.length > 0 ? '1 000+' : '0'}
                </span>
            </div>
            <div class="stat-row">
                <span class="stat-label">Étudiants</span>
                <span class="stat-val">
                    ${users.leaderboard.length > 0 ? '3 000+' : '0'}
                </span>
            </div>`;
    } catch (e) {}
}

// ============================================================
// PAGINATION
// ============================================================
function renderPagination(page, limit, count) {
    const el      = document.getElementById('pagination');
    const hasNext = count === limit;
    const hasPrev = page > 1;
    if (!hasNext && !hasPrev) { el.innerHTML = ''; return; }

    let html = '';
    if (hasPrev)
        html += `<button class="pg-btn"
                         onclick="goToPage(${page-1})">←</button>`;

    const start = Math.max(1, page - 2);
    for (let i = start; i <= page + 2; i++) {
        if (i === page)
            html += `<button class="pg-btn active">${i}</button>`;
        else if (i > 0)
            html += `<button class="pg-btn"
                             onclick="goToPage(${i})">${i}</button>`;
    }

    if (hasNext)
        html += `<button class="pg-btn"
                         onclick="goToPage(${page+1})">→</button>`;

    el.innerHTML = html;
}

function goToPage(page) {
    filters.page = page;
    // Ferme le panneau au changement de page
    closePanel();
    loadDocuments();
    document.getElementById('docs-list')
        .scrollIntoView({ behavior: 'smooth' });
}

// ============================================================
// ÉVÉNEMENTS FILTRES
// ============================================================
function bindEvents() {
    let searchTimer;
    document.getElementById('filter-search')
        .addEventListener('input', (e) => {
            clearTimeout(searchTimer);
            searchTimer = setTimeout(() => {
                filters.q    = e.target.value.trim();
                filters.page = 1;
                closePanel();
                loadDocuments();
            }, 400);
        });

    document.getElementById('filter-school')
        .addEventListener('change', (e) => {
            filters.school_id = e.target.value;
            filters.page = 1;
            closePanel();
            loadDocuments();
        });

    document.getElementById('filter-subject')
        .addEventListener('change', (e) => {
            filters.subject_id = e.target.value;
            filters.page = 1;
            closePanel();
            loadDocuments();
        });

    document.getElementById('filter-year')
        .addEventListener('change', (e) => {
            filters.year = e.target.value;
            filters.page = 1;
            closePanel();
            loadDocuments();
        });

    document.getElementById('type-pills')
        .addEventListener('click', (e) => {
            const pill = e.target.closest('.pill');
            if (!pill) return;
            document.querySelectorAll('.pill')
                .forEach(p => p.classList.remove('active'));
            pill.classList.add('active');
            filters.type = pill.dataset.type;
            filters.page = 1;
            closePanel();
            loadDocuments();
        });
}