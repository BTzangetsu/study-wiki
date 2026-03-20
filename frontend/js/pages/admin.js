// js/pages/admin.js

document.addEventListener('DOMContentLoaded', async () => {
    // redirectIfNoAuth + vérification admin côté client
    await initAuth({ redirectIfNoAuth: true });

    if (!currentUser?.is_admin) {
        window.location.href = 'index.html';
        return;
    }

    await loadStats();
    if (currentUser?.is_super_admin) {
        document.getElementById('tab-admin-requests').style.display = '';
    }
    await loadTab('reports');
    bindTabs();
});

// ============================================================
// STATS DU DASHBOARD
// ============================================================
async function loadStats() {
    try {
        const s = await Admin.stats();

        // Valeurs
        document.querySelector('#stat-pending-reports .admin-stat-val')
            .textContent = s.pending_reports;
        document.querySelector('#stat-pending-docs .admin-stat-val')
            .textContent = s.pending_documents;
        document.querySelector('#stat-pending-sugs .admin-stat-val')
            .textContent = s.pending_suggestions;
        document.getElementById('stat-total-users').textContent =
            s.total_users;
        document.getElementById('stat-total-docs').textContent =
            s.total_documents;
        document.getElementById('stat-new-today').textContent =
            s.new_users_today + ' users / ' + s.new_docs_today + ' docs';

        // Colore en rouge les cartes avec des éléments en attente
        if (s.pending_reports > 0)
            document.getElementById('stat-pending-reports')
                .classList.add('has-pending');
        if (s.pending_documents > 0)
            document.getElementById('stat-pending-docs')
                .classList.add('has-pending');
        if (s.pending_suggestions > 0)
            document.getElementById('stat-pending-sugs')
                .classList.add('has-pending');

    } catch (e) { /* silencieux */ }
}

// ============================================================
// ONGLETS
// ============================================================
function bindTabs() {
    document.querySelectorAll('.profile-tab').forEach(tab => {
        tab.addEventListener('click', async () => {
            document.querySelectorAll('.profile-tab')
                .forEach(t => t.classList.remove('active'));
            tab.classList.add('active');
            await loadTab(tab.dataset.tab);
        });
    });
}

async function loadTab(tab) {
    const state   = document.getElementById('admin-state');
    const content = document.getElementById('admin-content');

    content.innerHTML = '';
    state.innerHTML   = `<div class="state-loading"
                               style="padding:40px 0">
        <div class="spinner"></div>
    </div>`;

    try {
        if (tab === 'reports')     await loadReports(state, content);
        if (tab === 'documents')   await loadDocuments(state, content);
        if (tab === 'suggestions') await loadSuggestions(state, content);
        if (tab === 'users')       await loadUsers(state, content);
        if (tab === 'admin-requests') await loadAdminRequests(state, content);
    } catch (err) {
        state.innerHTML = `<div class="state-error">
            <span>Erreur : ${escapeHtml(err.message)}</span>
        </div>`;
    }
}

// ============================================================
// ONGLET SIGNALEMENTS
// ============================================================
async function loadReports(state, content) {
    const data = await Admin.reports({ status: 'pending', limit: 50 });
    state.innerHTML = '';

    if (data.reports.length === 0) {
        state.innerHTML = `<div class="state-empty">
            <span>Aucun signalement en attente</span>
        </div>`;
        return;
    }

    content.innerHTML = `
    <table class="admin-table">
        <thead>
            <tr>
                <th>Cible</th>
                <th>Raison</th>
                <th>Signalé par</th>
                <th>Date</th>
                <th>Actions</th>
            </tr>
        </thead>
        <tbody>
            ${data.reports.map(r => `
            <tr>
                <td>
                    ${r.document_id > 0
                        ? `<a href="document.html?id=${r.document_id}"
                              class="link">
                               Doc #${r.document_id}
                               ${r.doc_title
                                   ? '— ' + escapeHtml(r.doc_title)
                                   : ''}
                           </a>`
                        : `Commentaire #${r.comment_id}`
                    }
                </td>
                <td>
                    <span class="badge badge-blue">
                        ${escapeHtml(r.reason)}
                    </span>
                    ${r.details
                        ? `<p class="text-muted text-sm"
                               style="margin-top:4px">
                               ${escapeHtml(r.details)}
                           </p>`
                        : ''}
                </td>
                <td class="text-muted">${escapeHtml(r.reporter)}</td>
                <td class="text-muted">${formatDate(r.created_at)}</td>
                <td>
                    <div class="admin-actions">
                        <button class="btn btn-secondary btn-sm
                                       btn-resolve-report"
                                data-id="${r.id}"
                                data-status="reviewed">
                            Traité
                        </button>
                        <button class="btn btn-secondary btn-sm
                                       btn-resolve-report"
                                data-id="${r.id}"
                                data-status="dismissed">
                            Ignorer
                        </button>
                        ${r.document_id > 0
                            ? `<button class="btn btn-danger btn-sm
                                              btn-delete-doc"
                                       data-id="${r.document_id}">
                                   Supprimer doc
                               </button>`
                            : `<button class="btn btn-danger btn-sm
                                              btn-delete-comment"
                                       data-id="${r.comment_id}">
                                   Supprimer comment
                               </button>`
                        }
                    </div>
                </td>
            </tr>`).join('')}
        </tbody>
    </table>`;

    // Délégation d'événements
    content.addEventListener('click', async (e) => {
        const resolveBtn = e.target.closest('.btn-resolve-report');
        const deleteDoc  = e.target.closest('.btn-delete-doc');
        const deleteCom  = e.target.closest('.btn-delete-comment');

        if (resolveBtn) {
            try {
                await Admin.updateReport(resolveBtn.dataset.id,
                    { status: resolveBtn.dataset.status });
                showToast('Signalement mis à jour');
                await loadTab('reports');
                await loadStats();
            } catch (err) { showToast('Erreur', 'error'); }
        }

        if (deleteDoc) {
            if (!confirm('Supprimer ce document ?')) return;
            try {
                await Admin.deleteDocument(deleteDoc.dataset.id);
                showToast('Document supprimé');
                await loadTab('reports');
                await loadStats();
            } catch (err) { showToast('Erreur', 'error'); }
        }

        if (deleteCom) {
            if (!confirm('Supprimer ce commentaire ?')) return;
            try {
                await Admin.deleteComment(deleteCom.dataset.id);
                showToast('Commentaire supprimé');
                await loadTab('reports');
            } catch (err) { showToast('Erreur', 'error'); }
        }
    });
}

// ============================================================
// ONGLET DOCUMENTS EN ATTENTE
// ============================================================
async function loadDocuments(state, content) {
    // On récupère les docs non approuvés via la route admin
    const data = await Admin.reports({ status: 'pending', limit: 1 });
    state.innerHTML = '';

    // On utilise GET /api/documents avec is_approved=0
    // Pour ça il faut une route admin dédiée —
    // on passe par search avec un flag spécial
    // Pour l'instant on affiche un message informatif
    content.innerHTML = `
    <div class="state-empty" style="padding:40px 0">
        <span>Aucun document en attente d'approbation</span>
        <span class="text-muted">
            Les documents sont approuvés automatiquement à l'upload.
            Tu peux les désapprouver depuis la page de chaque document.
        </span>
    </div>`;
}

// ============================================================
// ONGLET SUGGESTIONS
// ============================================================
async function loadSuggestions(state, content) {
    const data = await Suggestions.list({ status: 'pending', limit: 50 });
    state.innerHTML = '';

    if (data.suggestions.length === 0) {
        state.innerHTML = `<div class="state-empty">
            <span>Aucune suggestion en attente</span>
        </div>`;
        return;
    }

    content.innerHTML = `
    <table class="admin-table">
        <thead>
            <tr>
                <th>Titre</th>
                <th>Par</th>
                <th>Votes</th>
                <th>Date</th>
                <th>Actions</th>
            </tr>
        </thead>
        <tbody>
            ${data.suggestions.map(s => `
            <tr>
                <td>
                    <div style="font-weight:500">
                        ${escapeHtml(s.title)}
                    </div>
                    <div class="text-muted text-sm"
                         style="margin-top:2px">
                        ${escapeHtml(s.description.substring(0, 80))}
                        ${s.description.length > 80 ? '…' : ''}
                    </div>
                </td>
                <td class="text-muted">${escapeHtml(s.username)}</td>
                <td style="font-weight:500;color:var(--blue-600)">
                    ${s.upvotes}
                </td>
                <td class="text-muted">${formatDate(s.created_at)}</td>
                <td>
                    <div class="admin-actions">
                        <button class="btn btn-secondary btn-sm
                                       btn-sug-status"
                                data-id="${s.id}"
                                data-status="planned">
                            Planifier
                        </button>
                        <button class="btn btn-secondary btn-sm
                                       btn-sug-status"
                                data-id="${s.id}"
                                data-status="done">
                            Réalisée
                        </button>
                        <button class="btn btn-danger btn-sm
                                       btn-sug-status"
                                data-id="${s.id}"
                                data-status="rejected">
                            Refuser
                        </button>
                    </div>
                </td>
            </tr>`).join('')}
        </tbody>
    </table>`;

    content.addEventListener('click', async (e) => {
        const btn = e.target.closest('.btn-sug-status');
        if (!btn) return;
        try {
            await Admin.updateSuggestion(btn.dataset.id,
                { status: btn.dataset.status });
            showToast('Suggestion mise à jour');
            await loadTab('suggestions');
            await loadStats();
        } catch (err) { showToast('Erreur', 'error'); }
    });
}

// ============================================================
// ONGLET UTILISATEURS
// ============================================================
async function loadUsers(state, content) {
    const data = await Leaderboard.users({ limit: 100 });
    state.innerHTML = '';

    content.innerHTML = `
    <table class="admin-table">
        <thead>
            <tr>
                <th>Utilisateur</th>
                <th>École</th>
                <th style="text-align:right">Points</th>
                <th style="text-align:right">Docs</th>
                <th>Actions</th>
            </tr>
        </thead>
        <tbody>
            ${data.leaderboard.map(u => `
            <tr>
                <td>
                    <div class="flex items-center gap-2">
                        <div class="avatar avatar-sm">
                            ${getInitials(u.username)}
                        </div>
                        <a href="profile.html?id=${u.id}"
                           class="link">
                            ${escapeHtml(u.username)}
                        </a>
                    </div>
                </td>
                <td class="text-muted">
                    ${escapeHtml(u.school || '—')}
                </td>
                <td style="text-align:right">${u.points}</td>
                <td style="text-align:right">${u.upload_count}</td>
                <td>
                    <div class="admin-actions">
                        <button class="btn btn-danger btn-sm btn-ban"
                                data-id="${u.id}"
                                data-active="1">
                            Bannir
                        </button>
                    </div>
                </td>
            </tr>`).join('')}
        </tbody>
    </table>`;

    content.addEventListener('click', async (e) => {
        const banBtn = e.target.closest('.btn-ban');
        if (!banBtn) return;

        // Empêche de se bannir soi-même
        if (parseInt(banBtn.dataset.id) === currentUser.id) {
            showToast('Tu ne peux pas te bannir toi-même', 'error');
            return;
        }

        if (!confirm('Bannir cet utilisateur ?')) return;

        try {
            await Admin.updateUser(banBtn.dataset.id,
                { is_active: 0 });
            showToast('Utilisateur banni');
            await loadTab('users');
        } catch (err) {
            showToast(err.message || 'Erreur', 'error');
        }
    });
}

async function loadAdminRequests(state, content) {
    const data = await get('/api/admin-requests');
    state.innerHTML = '';

    if (data.requests.length === 0) {
        state.innerHTML = `<div class="state-empty">
            <span>Aucune demande en attente</span>
        </div>`;
        return;
    }

    content.innerHTML = `
    <table class="admin-table">
        <thead>
            <tr>
                <th>Utilisateur</th>
                <th>Points</th>
                <th>Docs</th>
                <th>Raison</th>
                <th>Date</th>
                <th>Actions</th>
            </tr>
        </thead>
        <tbody>
            ${data.requests.map(r => `
            <tr>
                <td>
                    <a href="profile.html?id=${r.user_id}"
                       class="link">
                        ${escapeHtml(r.username)}
                    </a>
                </td>
                <td>${r.points} pts</td>
                <td>${r.doc_count} docs</td>
                <td style="max-width:300px;font-size:13px;
                           color:var(--gray-600)">
                    ${escapeHtml(r.reason.substring(0, 120))}
                    ${r.reason.length > 120 ? '…' : ''}
                </td>
                <td class="text-muted">
                    ${formatDate(r.created_at)}
                </td>
                <td>
                    <div class="admin-actions">
                        <button class="btn btn-primary btn-sm
                                       btn-approve-admin"
                                data-id="${r.id}">
                            Approuver
                        </button>
                        <button class="btn btn-danger btn-sm
                                       btn-reject-admin"
                                data-id="${r.id}">
                            Refuser
                        </button>
                    </div>
                </td>
            </tr>`).join('')}
        </tbody>
    </table>`;

    content.addEventListener('click', async (e) => {
        const approveBtn = e.target.closest('.btn-approve-admin');
        const rejectBtn  = e.target.closest('.btn-reject-admin');

        if (approveBtn) {
            if (!confirm('Approuver cette demande et rendre cet utilisateur admin ?'))
                return;
            try {
                await patch(`/api/admin-requests/${approveBtn.dataset.id}`,
                    { status: 'approved' });
                showToast('Utilisateur promu admin');
                await loadTab('admin-requests');
            } catch (err) {
                showToast(err.message || 'Erreur', 'error');
            }
        }

        if (rejectBtn) {
            if (!confirm('Refuser cette demande ?')) return;
            try {
                await patch(`/api/admin-requests/${rejectBtn.dataset.id}`,
                    { status: 'rejected' });
                showToast('Demande refusée');
                await loadTab('admin-requests');
            } catch (err) {
                showToast(err.message || 'Erreur', 'error');
            }
        }
    });
}