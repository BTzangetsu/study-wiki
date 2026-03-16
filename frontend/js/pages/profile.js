// js/pages/profile.js

// ============================================================
// ÉTAT DE LA PAGE
// ============================================================
let profileUserId = null;  // user dont on affiche le profil
let isOwnProfile  = false; // true si c'est son propre profil
let activeTab     = 'documents';

// ============================================================
// POINT D'ENTRÉE
// ============================================================
document.addEventListener('DOMContentLoaded', async () => {
    await initAuth({ redirectIfNoAuth: true });

    // Récupère l'id depuis l'URL : profile.html?id=3
    // Si pas d'id → on affiche son propre profil
    const params = new URLSearchParams(window.location.search);
    const urlId  = params.get('id');

    profileUserId = urlId ? parseInt(urlId) : currentUser.id;
    isOwnProfile  = profileUserId === currentUser.id;

    await loadProfile();
    bindTabs();
    if (isOwnProfile) bindEditModal();
});

// ============================================================
// CHARGEMENT DU PROFIL
// ============================================================
async function loadProfile() {
    showState('loading');

    try {
        const user = await Users.get(profileUserId);

        document.getElementById('profile-state').style.display   = 'none';
        document.getElementById('profile-content').style.display = '';

        renderHeader(user);
        renderBadges(user.badges || []);

        // Affiche l'onglet favoris seulement sur son propre profil
        if (isOwnProfile) {
            document.getElementById('tab-favorites').style.display = '';
            document.getElementById('own-profile-actions').style.display = '';

            // Pré-remplit le formulaire d'édition
            document.getElementById('edit-username').value =
                currentUser.username;
        }

        // Charge les documents du premier onglet
        await loadTab('documents');

    } catch (err) {
        if (err.status === 404) {
            showState('error', 'Utilisateur introuvable');
        } else {
            showState('error', 'Erreur lors du chargement');
        }
    }
}

// ============================================================
// RENDU DE L'EN-TÊTE
// ============================================================
function renderHeader(user) {
    document.title = `${user.username} — Annales.io`;

    document.getElementById('profile-avatar').textContent =
        getInitials(user.username);
    document.getElementById('profile-username').textContent =
        user.username;
    document.getElementById('profile-school').textContent =
        user.school || '';
    document.getElementById('profile-since').textContent =
        'Membre depuis ' + formatDate(user.created_at);

    document.getElementById('stat-points').textContent =
        user.points;

    // Calcule le total de téléchargements reçus sur tous ses docs
    const totalDl = (user.documents || [])
        .reduce((acc, d) => acc + (d.download_count || 0), 0);

    document.getElementById('stat-doc-count').textContent =
        user.documents?.length || 0;
    document.getElementById('stat-dl-count').textContent =
        totalDl;
}

// ============================================================
// RENDU DES BADGES
// ============================================================

// Map des emojis par nom de badge — extensible
const BADGE_ICONS = {
    'Premier pas':      '🎯',
    'Contributeur':     '📚',
    'Prolifique':       '🚀',
    'Populaire':        '⭐',
    'Incontournable':   '🏆',
    'Bien noté':        '👍',
    'Actif':            '💬',
    'Expert':           '🎓',
    'Légende':          '👑',
};

function renderBadges(badges) {
    const section = document.getElementById('badges-section');
    const grid    = document.getElementById('badges-list');

    if (badges.length === 0) {
        section.style.display = 'none';
        return;
    }

    grid.innerHTML = badges.map(b => `
        <div class="badge-card">
            <div class="badge-icon">
                ${BADGE_ICONS[b.name] || '🏅'}
            </div>
            <span class="badge-name">${escapeHtml(b.name)}</span>
            <span class="badge-date">${formatDate(b.earned_at)}</span>
        </div>
    `).join('');
}

// ============================================================
// ONGLETS
// ============================================================
function bindTabs() {
    document.querySelectorAll('.profile-tab').forEach(tab => {
        tab.addEventListener('click', async () => {
            // Met à jour l'onglet actif visuellement
            document.querySelectorAll('.profile-tab')
                .forEach(t => t.classList.remove('active'));
            tab.classList.add('active');

            activeTab = tab.dataset.tab;
            await loadTab(activeTab);
        });
    });
}

async function loadTab(tab) {
    const content = document.getElementById('tab-content');
    content.innerHTML = `<div class="state-loading" style="padding:40px 0">
        <div class="spinner"></div>
    </div>`;

    try {
        if (tab === 'documents') {
            await loadDocumentsTab(content);
        } else if (tab === 'favorites') {
            await loadFavoritesTab(content);
        }
    } catch (err) {
        content.innerHTML = `<div class="state-error">
            <span>Erreur de chargement</span>
        </div>`;
    }
}

async function loadDocumentsTab(container) {
    const data = await Users.documents(profileUserId, { limit: 50 });

    if (data.documents.length === 0) {
        container.innerHTML = `<div class="state-empty">
            <span>Aucun document partagé pour l'instant</span>
            ${isOwnProfile ? `<a href="upload.html">
                <button class="btn btn-primary btn-sm">
                    Uploader mon premier document
                </button>
            </a>` : ''}
        </div>`;
        return;
    }

    container.innerHTML = `
        <div class="profile-doc-list">
            ${data.documents.map(doc => renderProfileDocCard(doc)).join('')}
        </div>`;

    bindDocCardEvents(container);
}

async function loadFavoritesTab(container) {
    const data = await Documents.favorites();

    if (data.documents.length === 0) {
        container.innerHTML = `<div class="state-empty">
            <span>Aucun favori pour l'instant</span>
            <a href="index.html" class="link">
                Parcourir les documents
            </a>
        </div>`;
        return;
    }

    container.innerHTML = `
        <div class="profile-doc-list">
            ${data.documents.map(doc => renderProfileDocCard(doc)).join('')}
        </div>`;

    bindDocCardEvents(container);
}

// Carte doc légère pour le profil — moins d'infos que index
function renderProfileDocCard(doc) {
    const tagClass = {
        exam: 'tag-exam', td: 'tag-td',
        cours: 'tag-cours', tp: 'tag-tp', autre: 'tag-autre'
    }[doc.type] || 'tag-autre';

    return `
    <div class="card card-hover doc-card" data-id="${doc.id}">
        <div class="doc-icon">
            <svg viewBox="0 0 24 24" fill="none"
                 stroke="currentColor" stroke-width="1.5">
                <path d="M14 2H6a2 2 0 00-2 2v16a2 2 0
                         002 2h12a2 2 0 002-2V8z"/>
                <polyline points="14 2 14 8 20 8"/>
            </svg>
        </div>
        <div class="doc-body">
            <div class="doc-title truncate">
                ${escapeHtml(doc.title)}
            </div>
            <div class="doc-meta">
                ${doc.subject
                    ? `<span>${escapeHtml(doc.subject)}</span>` : ''}
                ${doc.year
                    ? `<span>${escapeHtml(doc.year)}</span>` : ''}
                <span class="tag ${tagClass}">
                    ${escapeHtml(doc.type)}
                </span>
            </div>
        </div>
        <div class="doc-right">
            <span class="stars">
                ${renderStars(doc.avg_rating)}
            </span>
            <span class="text-muted">
                ${doc.download_count} téléch.
            </span>
        </div>
    </div>`;
}

function bindDocCardEvents(container) {
    container.querySelectorAll('.doc-card[data-id]').forEach(card => {
        card.addEventListener('click', () => {
            window.location.href = `document.html?id=${card.dataset.id}`;
        });
    });
}

// ============================================================
// MODAL ÉDITION PROFIL
// ============================================================
function bindEditModal() {
    const modal   = document.getElementById('edit-modal');
    const openBtn = document.getElementById('btn-edit-profile');
    const closeBtn= document.getElementById('btn-close-edit');
    const saveBtn = document.getElementById('btn-save-profile');
    const delBtn  = document.getElementById('btn-delete-account');

    openBtn.addEventListener('click', () => {
        modal.style.display = '';
    });

    closeBtn.addEventListener('click', () => {
        modal.style.display = 'none';
        clearEditErrors();
    });

    // Ferme la modal au clic sur le backdrop
    modal.addEventListener('click', (e) => {
        if (e.target === modal.firstElementChild) {
            modal.style.display = 'none';
        }
    });

    // Sauvegarde
    saveBtn.addEventListener('click', async () => {
        clearEditErrors();

        const username = document.getElementById('edit-username')
            .value.trim();
        const password = document.getElementById('edit-password')
            .value;

        let hasError = false;

        if (!username || username.length < 3) {
            document.getElementById('edit-username-error').textContent =
                'Au moins 3 caractères';
            hasError = true;
        }

        if (password && password.length < 8) {
            document.getElementById('edit-password-error').textContent =
                'Au moins 8 caractères';
            hasError = true;
        }

        if (hasError) return;

        // Construit l'objet avec seulement les champs modifiés
        const payload = { username };
        if (password) payload.password = password;

        saveBtn.disabled    = true;
        saveBtn.textContent = 'Enregistrement…';

        try {
            await Auth.update(payload);
            showToast('Profil mis à jour');
            modal.style.display = 'none';

            // Met à jour l'affichage sans recharger
            document.getElementById('profile-username').textContent =
                username;
            document.getElementById('profile-avatar').textContent =
                getInitials(username);

        } catch (err) {
            const alertEl = document.getElementById('edit-alert');
            alertEl.textContent = err.message || 'Erreur';
            alertEl.className   = 'form-alert visible error';
        } finally {
            saveBtn.disabled    = false;
            saveBtn.textContent = 'Enregistrer';
        }
    });

    // Suppression de compte
    delBtn.addEventListener('click', async () => {
        // Double confirmation pour une action irréversible
        const confirmed = confirm(
            'Supprimer définitivement ton compte ?\n' +
            'Tous tes documents seront supprimés. Cette action est irréversible.'
        );
        if (!confirmed) return;

        try {
            await Auth.delete();
            window.location.href = 'index.html';
        } catch (err) {
            showToast('Erreur lors de la suppression', 'error');
        }
    });
}

function clearEditErrors() {
    ['edit-username-error', 'edit-password-error'].forEach(id => {
        const el = document.getElementById(id);
        if (el) el.textContent = '';
    });
    const alert = document.getElementById('edit-alert');
    if (alert) alert.className = 'form-alert';
}

// ============================================================
// HELPERS LOCAUX
// ============================================================
function showState(type, message = '') {
    const el = document.getElementById('profile-state');
    if (type === 'loading') {
        el.innerHTML = `<div class="state-loading" style="padding:80px 0">
            <div class="spinner"></div>
        </div>`;
    } else {
        el.innerHTML = `<div class="state-error" style="padding:80px 0">
            <span>${escapeHtml(message)}</span>
            <a href="index.html" class="link">← Retour</a>
        </div>`;
    }
}