// js/pages/suggestions.js

let activeStatus = 'pending';

document.addEventListener('DOMContentLoaded', async () => {
    await initAuth();
    await loadSuggestions();
    bindStatusPills();
    if (currentUser) bindForm();
});

// ============================================================
// CHARGEMENT
// ============================================================
async function loadSuggestions() {
    const state = document.getElementById('sug-state');
    const list  = document.getElementById('sug-list');

    list.innerHTML  = '';
    state.innerHTML = `<div class="state-loading" style="padding:40px 0">
        <div class="spinner"></div>
    </div>`;

    try {
        const data = await Suggestions.list({
            status: activeStatus,
            limit:  50
        });
        state.innerHTML = '';

        if (data.suggestions.length === 0) {
            state.innerHTML = `<div class="state-empty">
                <span>Aucune suggestion dans cette catégorie</span>
            </div>`;
            return;
        }

        list.innerHTML = data.suggestions
            .map(s => renderSuggestion(s))
            .join('');

        bindVoteEvents();

    } catch (err) {
        state.innerHTML = `<div class="state-error">
            <span>Erreur de chargement</span>
        </div>`;
    }
}

// ============================================================
// RENDU D'UNE SUGGESTION
// ============================================================
const STATUS_LABELS = {
    pending:  { label: 'En attente', class: 'badge-blue'     },
    planned:  { label: 'Planifiée',  class: 'badge-planned'  },
    done:     { label: 'Réalisée',   class: 'badge-done'     },
    rejected: { label: 'Refusée',    class: 'badge-rejected' },
};

function renderSuggestion(s) {
    const status  = STATUS_LABELS[s.status] || STATUS_LABELS.pending;
    const canVote = currentUser !== null;

    return `
    <div class="card sug-card" data-id="${s.id}">

        <!-- Bouton upvote à gauche -->
        <button class="upvote-btn ${s.has_voted ? 'voted' : ''}"
                data-id="${s.id}"
                data-voted="${s.has_voted}"
                ${!canVote ? 'disabled title="Connecte-toi pour voter"' : ''}>
            <span class="upvote-arrow">▲</span>
            <span class="upvote-count">${s.upvotes}</span>
        </button>

        <!-- Corps -->
        <div class="sug-body">
            <div class="sug-title">${escapeHtml(s.title)}</div>
            <div class="sug-desc">${escapeHtml(s.description)}</div>
            <div class="sug-meta">
                <span class="badge ${status.class}">
                    ${status.label}
                </span>
                <span>par ${escapeHtml(s.username)}</span>
                <span>${formatDate(s.created_at)}</span>
                ${currentUser && currentUser.username === s.username
                    ? `<button class="comment-action-btn btn-delete-sug"
                               data-id="${s.id}"
                               style="color:var(--red-700)">
                           Supprimer
                       </button>`
                    : ''}
            </div>
        </div>

    </div>`;
}

// ============================================================
// VOTE
// Délégation sur la liste — un seul listener pour tous les boutons
// ============================================================
function bindVoteEvents() {
    const list = document.getElementById('sug-list');

    list.addEventListener('click', async (e) => {
        // Vote
        const voteBtn = e.target.closest('.upvote-btn');
        if (voteBtn && currentUser) {
            const id    = voteBtn.dataset.id;
            const voted = voteBtn.dataset.voted === 'true';
            const count = voteBtn.querySelector('.upvote-count');

            try {
                if (voted) {
                    await Suggestions.removeVote(id);
                    voteBtn.classList.remove('voted');
                    voteBtn.dataset.voted = 'false';
                    count.textContent = parseInt(count.textContent) - 1;
                } else {
                    await Suggestions.vote(id);
                    voteBtn.classList.add('voted');
                    voteBtn.dataset.voted = 'true';
                    count.textContent = parseInt(count.textContent) + 1;
                }
            } catch (err) {
                showToast('Erreur', 'error');
            }
        }

        // Supprimer
        const delBtn = e.target.closest('.btn-delete-sug');
        if (delBtn) {
            const id = delBtn.dataset.id;
            if (!confirm('Supprimer cette suggestion ?')) return;
            try {
                await Suggestions.delete(id);
                showToast('Suggestion supprimée');
                await loadSuggestions();
            } catch (err) {
                showToast('Erreur', 'error');
            }
        }
    });
}

// ============================================================
// PILLS DE STATUT
// ============================================================
function bindStatusPills() {
    document.getElementById('status-pills')
        .addEventListener('click', async (e) => {
            const pill = e.target.closest('.pill');
            if (!pill) return;
            document.querySelectorAll('#status-pills .pill')
                .forEach(p => p.classList.remove('active'));
            pill.classList.add('active');
            activeStatus = pill.dataset.status;
            await loadSuggestions();
        });
}

// ============================================================
// FORMULAIRE NOUVELLE SUGGESTION
// ============================================================
function bindForm() {
    const form = document.getElementById('sug-form');

    form.addEventListener('submit', async (e) => {
        e.preventDefault();

        // Reset erreurs
        ['sug-title-error', 'sug-desc-error'].forEach(id => {
            document.getElementById(id).textContent = '';
        });

        const title       = document.getElementById('sug-title')
            .value.trim();
        const description = document.getElementById('sug-description')
            .value.trim();

        let hasError = false;

        if (!title) {
            document.getElementById('sug-title-error').textContent =
                'Le titre est requis';
            hasError = true;
        }
        if (!description) {
            document.getElementById('sug-desc-error').textContent =
                'La description est requise';
            hasError = true;
        }
        if (hasError) return;

        const btn = document.getElementById('sug-submit');
        btn.disabled    = true;
        btn.textContent = 'Envoi…';

        try {
            await Suggestions.create({ title, description });

            // Vide le formulaire
            document.getElementById('sug-title').value       = '';
            document.getElementById('sug-description').value = '';

            showToast('Suggestion envoyée, merci !');
            await loadSuggestions();

        } catch (err) {
            const alert = document.getElementById('sug-alert');
            if (err.status === 429) {
                alert.textContent = 'Tu as déjà 3 suggestions en attente';
            } else {
                alert.textContent = err.message || 'Erreur';
            }
            alert.className = 'form-alert visible error';
        } finally {
            btn.disabled    = false;
            btn.textContent = 'Proposer';
        }
    });
}