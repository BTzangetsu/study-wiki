// js/pages/document.js

// ============================================================
// ÉTAT DE LA PAGE
// ============================================================
let docId       = null;   // ID du document courant
let docData     = null;   // données du document
let isFavorite  = false;  // état favori de l'utilisateur
let userVote    = null;   // note que l'user a donnée (1-5 ou null)

// ============================================================
// POINT D'ENTRÉE
// ============================================================
document.addEventListener('DOMContentLoaded', async () => {
    // Récupère l'id depuis l'URL : document.html?id=42
    // URLSearchParams parse les query params de l'URL courante
    const params = new URLSearchParams(window.location.search);
    docId = params.get('id');

    if (!docId) {
        showState('error', 'Document introuvable');
        return;
    }

    await initAuth();
    await loadDocument();

    // Branche les événements globaux de la page
    bindPageEvents();
});

// ============================================================
// CHARGEMENT DU DOCUMENT
// ============================================================
async function loadDocument() {
    showState('loading');

    try {
        // Charge le document et les commentaires en parallèle
        const [doc, commentsData] = await Promise.all([
            Documents.get(docId),
            Comments.list(docId),
        ]);

        docData = doc;

        // Cache le spinner, affiche le contenu
        document.getElementById('doc-state').style.display   = 'none';
        document.getElementById('doc-content').style.display = '';

        renderDocument(doc);
        renderComments(commentsData.comments);

        // Si connecté, vérifie l'état favori
        if (currentUser) {
            await checkFavoriteState();
        }

    } catch (err) {
        if (err.status === 404) {
            showState('error', 'Ce document n\'existe pas ou a été supprimé');
        } else {
            showState('error', 'Erreur lors du chargement');
        }
    }
}

// ============================================================
// RENDU DU DOCUMENT
// ============================================================
function renderDocument(doc) {

    // Titre de la page navigateur
    document.title = `${doc.title} — Annales.io`;

    // Fil d'ariane
    const breadcrumb = document.getElementById('doc-breadcrumb');
    let crumbs = [];
    if (doc.school)  crumbs.push(`<span>${escapeHtml(doc.school)}</span>`);
    if (doc.subject) crumbs.push(`<span>${escapeHtml(doc.subject)}</span>`);
    if (doc.year)    crumbs.push(`<span>${escapeHtml(doc.year)}</span>`);
    breadcrumb.innerHTML = crumbs.join('');

    // Infos principales
    document.getElementById('doc-title').textContent =
        doc.title;
    document.getElementById('doc-author').textContent =
        'par ' + doc.author;
    document.getElementById('doc-date').textContent =
        formatDate(doc.created_at);
    document.getElementById('doc-size').textContent =
        formatSize(doc.size_bytes);
    document.getElementById('doc-description').textContent =
        doc.description || '';

    // Tag type
    const tagClass = {
        exam: 'tag-exam', td: 'tag-td',
        cours: 'tag-cours', tp: 'tag-tp', autre: 'tag-autre'
    }[doc.type] || 'tag-autre';
    document.getElementById('doc-tag').innerHTML =
        `<span class="tag ${tagClass}">${escapeHtml(doc.type)}</span>`;

    // Avatar auteur
    document.getElementById('doc-avatar').textContent =
        getInitials(doc.author);

    // Stats
    document.getElementById('stat-downloads').textContent =
        doc.download_count;
    document.getElementById('stat-votes').textContent =
        doc.vote_count;
    document.getElementById('stat-rating').textContent =
        doc.vote_count > 0
            ? doc.avg_rating.toFixed(1) + ' / 5'
            : '—';

    // Avatar dans le formulaire de commentaire
    if (currentUser) {
        document.getElementById('comment-avatar').textContent =
            getInitials(currentUser.username);
    }

    // Boutons owner/admin
    if (currentUser &&
        (currentUser.id === doc.user_id || currentUser.is_admin)) {
        document.getElementById('owner-actions').style.display = 'flex';
    }
}

// ============================================================
// TÉLÉCHARGEMENT
// ============================================================
async function downloadDocument() {
    const btn = document.getElementById('btn-download');
    btn.disabled    = true;
    btn.textContent = 'Préparation…';

    try {
        const { download_url } = await Documents.downloadUrl(docId);

        // Ouvre l'URL signée dans un nouvel onglet.
        // Le navigateur déclenche automatiquement le téléchargement
        // si le Content-Type est application/pdf ou octet-stream.
        window.open(download_url, '_blank');

        // Met à jour le compteur localement sans recharger
        const statEl = document.getElementById('stat-downloads');
        statEl.textContent = parseInt(statEl.textContent) + 1;

        showToast('Téléchargement démarré');

    } catch (err) {
        showToast('Erreur lors du téléchargement', 'error');
    } finally {
        btn.disabled    = false;
        btn.textContent = 'Télécharger';
    }
}

// ============================================================
// FAVORIS
// ============================================================
async function checkFavoriteState() {
    try {
        const data = await Documents.favorites();
        isFavorite = data.documents.some(d => d.id === parseInt(docId));
        updateFavoriteBtn();
    } catch (e) { /* silencieux */ }
}

function updateFavoriteBtn() {
    const btn = document.getElementById('btn-favorite');
    if (!btn) return;
    btn.textContent = isFavorite ? '♥ Favori' : '♡ Favori';
    btn.style.color = isFavorite ? 'var(--blue-600)' : '';
}

async function toggleFavorite() {
    try {
        if (isFavorite) {
            await Documents.unfavorite(docId);
            isFavorite = false;
            showToast('Retiré des favoris');
        } else {
            await Documents.favorite(docId);
            isFavorite = true;
            showToast('Ajouté aux favoris');
        }
        updateFavoriteBtn();
    } catch (err) {
        showToast('Erreur', 'error');
    }
}

// ============================================================
// VOTES — étoiles interactives
// ============================================================
function bindVoteEvents() {
    const stars = document.querySelectorAll('.star');

    stars.forEach(star => {
        const val = parseInt(star.dataset.value);

        // Survol — colorie les étoiles jusqu'à celle survolée
        star.addEventListener('mouseenter', () => {
            stars.forEach(s => {
                s.classList.toggle('hover', parseInt(s.dataset.value) <= val);
            });
        });

        // Fin de survol — remet l'état actif
        star.addEventListener('mouseleave', () => {
            stars.forEach(s => s.classList.remove('hover'));
        });

        // Clic — envoie le vote
        star.addEventListener('click', () => vote(val));
    });

    // Affiche le vote existant si l'user a déjà voté
    if (userVote) updateStarsUI(userVote);
}

function updateStarsUI(score) {
    document.querySelectorAll('.star').forEach(s => {
        s.classList.toggle('active', parseInt(s.dataset.value) <= score);
    });
    document.getElementById('btn-remove-vote').style.display =
        score ? '' : 'none';
}

async function vote(score) {
    try {
        await Documents.vote(docId, score);
        userVote = score;
        updateStarsUI(score);

        // Recharge les stats pour avoir la nouvelle moyenne
        const doc = await Documents.get(docId);
        document.getElementById('stat-rating').textContent =
            doc.avg_rating.toFixed(1) + ' / 5';
        document.getElementById('stat-votes').textContent =
            doc.vote_count;

        showToast('Vote enregistré');
    } catch (err) {
        showToast('Erreur lors du vote', 'error');
    }
}

async function removeVote() {
    try {
        await Documents.removeVote(docId);
        userVote = null;
        updateStarsUI(null);
        showToast('Vote retiré');
    } catch (err) {
        showToast('Erreur', 'error');
    }
}

// ============================================================
// COMMENTAIRES — rendu de l'arbre récursif
// ============================================================
function renderComments(comments) {
    const list = document.getElementById('comments-list');

    // Compte total récursif pour afficher "12 commentaires"
    function countAll(arr) {
        return arr.reduce((acc, c) =>
            acc + 1 + countAll(c.replies || []), 0);
    }
    const total = countAll(comments);
    document.getElementById('comment-count').textContent =
        total > 0 ? `(${total})` : '';

    if (comments.length === 0) {
        list.innerHTML = `<div class="state-empty">
            <span>Aucun commentaire pour l'instant</span>
            <span class="text-muted">Sois le premier à commenter</span>
        </div>`;
        return;
    }

    // renderComment est une fonction récursive —
    // elle s'appelle elle-même pour les réponses
    list.innerHTML = comments.map(c => renderComment(c, 0)).join('');
    bindCommentEvents();
}

// depth = profondeur dans l'arbre (0 = racine)
// On limite l'indentation visuelle à 3 niveaux max
function renderComment(comment, depth) {
    const isDeleted = comment.is_deleted;
    const canDelete = currentUser &&
        (currentUser.id === comment.user_id || currentUser.is_admin);

    const replies = (comment.replies || [])
        .map(r => renderComment(r, depth + 1))
        .join('');

    return `
    <div class="comment" data-comment-id="${comment.id}">

        <div class="avatar avatar-sm">
            ${getInitials(comment.username)}
        </div>

        <div class="comment-body">
            <div class="comment-header">
                <span class="comment-username">
                    ${escapeHtml(comment.username)}
                </span>
                <span class="comment-date">
                    ${formatDate(comment.created_at)}
                </span>
            </div>

            <div class="comment-content">
                ${isDeleted
                    ? '<em style="color:var(--gray-400)">[supprimé]</em>'
                    : escapeHtml(comment.content)
                }
            </div>

            ${!isDeleted ? `
            <div class="comment-actions">
                <button class="comment-action-btn btn-like
                               ${comment.has_liked ? 'liked' : ''}"
                        data-id="${comment.id}"
                        ${currentUser ? '' : 'disabled'}>
                    ♥ ${comment.like_count}
                </button>
                ${currentUser ? `
                <button class="comment-action-btn btn-reply"
                        data-id="${comment.id}">
                    Répondre
                </button>` : ''}
                ${canDelete ? `
                <button class="comment-action-btn btn-delete-comment"
                        data-id="${comment.id}"
                        style="color:var(--red-700)">
                    Supprimer
                </button>` : ''}
            </div>

            <!-- Formulaire de réponse inline — caché par défaut -->
            <div class="reply-form" id="reply-form-${comment.id}">
                <div class="avatar avatar-sm">
                    ${currentUser ? getInitials(currentUser.username) : '?'}
                </div>
                <div style="flex:1">
                    <textarea
                        class="form-textarea reply-input"
                        placeholder="Ta réponse…"
                        rows="2"
                        data-parent="${comment.id}"
                    ></textarea>
                    <div class="flex gap-2"
                         style="justify-content:flex-end;margin-top:6px">
                        <button class="btn btn-secondary btn-sm btn-cancel-reply"
                                data-id="${comment.id}">
                            Annuler
                        </button>
                        <button class="btn btn-primary btn-sm btn-submit-reply"
                                data-parent="${comment.id}">
                            Répondre
                        </button>
                    </div>
                </div>
            </div>
            ` : ''}

            ${replies.length ? `
            <div class="comment-replies">${replies}</div>
            ` : ''}
        </div>

    </div>`;
}

// ============================================================
// ÉVÉNEMENTS COMMENTAIRES
// Délégation sur le conteneur parent — un seul listener
// pour tous les boutons like / répondre / supprimer
// ============================================================
function bindCommentEvents() {
    const list = document.getElementById('comments-list');

    list.addEventListener('click', async (e) => {
        const likeBtn   = e.target.closest('.btn-like');
        const replyBtn  = e.target.closest('.btn-reply');
        const cancelBtn = e.target.closest('.btn-cancel-reply');
        const submitBtn = e.target.closest('.btn-submit-reply');
        const deleteBtn = e.target.closest('.btn-delete-comment');

        // LIKE / UNLIKE
        if (likeBtn && currentUser) {
            const id     = likeBtn.dataset.id;
            const liked  = likeBtn.classList.contains('liked');
            try {
                if (liked) {
                    await Comments.unlike(id);
                } else {
                    await Comments.like(id);
                }
                // Mise à jour locale du compteur sans recharger
                likeBtn.classList.toggle('liked');
                const count = parseInt(likeBtn.textContent.match(/\d+/)?.[0] || 0);
                likeBtn.textContent = `♥ ${liked ? count - 1 : count + 1}`;
            } catch (err) {
                showToast('Erreur', 'error');
            }
        }

        // AFFICHER le formulaire de réponse
        if (replyBtn) {
            const id   = replyBtn.dataset.id;
            const form = document.getElementById(`reply-form-${id}`);
            if (form) {
                form.classList.add('open');
                form.querySelector('textarea')?.focus();
            }
        }

        // ANNULER la réponse
        if (cancelBtn) {
            const id   = cancelBtn.dataset.id;
            const form = document.getElementById(`reply-form-${id}`);
            if (form) {
                form.classList.remove('open');
                form.querySelector('textarea').value = '';
            }
        }

        // SOUMETTRE une réponse
        if (submitBtn) {
            const parentId = submitBtn.dataset.parent;
            const textarea = document.querySelector(
                `.reply-input[data-parent="${parentId}"]`
            );
            const content = textarea?.value.trim();
            if (!content) return;

            submitBtn.disabled = true;
            try {
                await Comments.post(docId, {
                    content,
                    parent_id: parseInt(parentId)
                });
                showToast('Réponse publiée');
                // Recharge les commentaires pour afficher la réponse
                const data = await Comments.list(docId);
                renderComments(data.comments);
            } catch (err) {
                showToast('Erreur', 'error');
                submitBtn.disabled = false;
            }
        }

        // SUPPRIMER un commentaire
        if (deleteBtn) {
            const id = deleteBtn.dataset.id;
            if (!confirm('Supprimer ce commentaire ?')) return;
            try {
                await Comments.delete(id);
                showToast('Commentaire supprimé');
                const data = await Comments.list(docId);
                renderComments(data.comments);
            } catch (err) {
                showToast('Erreur', 'error');
            }
        }
    });
}

// ============================================================
// ÉVÉNEMENTS GLOBAUX DE LA PAGE
// ============================================================
function bindPageEvents() {

    // Télécharger
    document.getElementById('btn-download')
        ?.addEventListener('click', downloadDocument);

    // Favori
    document.getElementById('btn-favorite')
        ?.addEventListener('click', toggleFavorite);

    // Supprimer le document (owner/admin)
    document.getElementById('btn-delete')
        ?.addEventListener('click', async () => {
            if (!confirm('Supprimer définitivement ce document ?')) return;
            try {
                await Documents.delete(docId);
                showToast('Document supprimé');
                setTimeout(() => window.location.href = 'index.html', 1000);
            } catch (err) {
                showToast('Erreur lors de la suppression', 'error');
            }
        });

    // Retirer son vote
    document.getElementById('btn-remove-vote')
        ?.addEventListener('click', removeVote);

    // Branche les étoiles si connecté
    if (currentUser) bindVoteEvents();

    // Nouveau commentaire racine
    document.getElementById('btn-comment')
        ?.addEventListener('click', async () => {
            const input   = document.getElementById('comment-input');
            const content = input.value.trim();
            if (!content) return;

            const btn = document.getElementById('btn-comment');
            btn.disabled = true;

            try {
                await Comments.post(docId, { content });
                input.value = '';
                showToast('Commentaire publié');
                const data = await Comments.list(docId);
                renderComments(data.comments);
            } catch (err) {
                showToast(err.message || 'Erreur', 'error');
            } finally {
                btn.disabled = false;
            }
        });

    // Modal signalement — ouvrir
    document.getElementById('btn-report')
        ?.addEventListener('click', () => {
            document.getElementById('report-modal').style.display = '';
        });

    // Modal signalement — fermer
    document.getElementById('btn-close-report')
        ?.addEventListener('click', () => {
            document.getElementById('report-modal').style.display = 'none';
        });

    // Modal signalement — soumettre
    document.getElementById('btn-submit-report')
        ?.addEventListener('click', async () => {
            const reason  = document.getElementById('report-reason').value;
            const details = document.getElementById('report-details').value;
            try {
                await Reports.create({
                    document_id: parseInt(docId),
                    reason,
                    details
                });
                document.getElementById('report-modal').style.display = 'none';
                showToast('Signalement envoyé, merci');
            } catch (err) {
                if (err.status === 409) {
                    showToast('Tu as déjà signalé ce document', 'error');
                } else {
                    showToast('Erreur', 'error');
                }
            }
        });
}

// ============================================================
// HELPERS LOCAUX
// ============================================================
function showState(type, message = '') {
    const el = document.getElementById('doc-state');
    if (type === 'loading') {
        el.innerHTML = `<div class="state-loading" style="padding:80px 0">
            <div class="spinner"></div>
            <span>Chargement…</span>
        </div>`;
    } else {
        el.innerHTML = `<div class="state-error" style="padding:80px 0">
            <span>${escapeHtml(message)}</span>
            <a href="index.html" class="link">← Retour aux documents</a>
        </div>`;
    }
}