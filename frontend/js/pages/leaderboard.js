// js/pages/leaderboard.js

let activeTab = 'users';

document.addEventListener('DOMContentLoaded', async () => {
    await initAuth();
    await loadTab('users');
    bindTabs();
});

function bindTabs() {
    document.querySelectorAll('.profile-tab').forEach(tab => {
        tab.addEventListener('click', async () => {
            document.querySelectorAll('.profile-tab')
                .forEach(t => t.classList.remove('active'));
            tab.classList.add('active');
            activeTab = tab.dataset.tab;
            await loadTab(activeTab);
        });
    });
}

async function loadTab(tab) {
    const state   = document.getElementById('lb-state');
    const content = document.getElementById('lb-content');

    content.innerHTML = '';
    state.innerHTML   = `<div class="state-loading" style="padding:40px 0">
        <div class="spinner"></div>
    </div>`;

    try {
        if (tab === 'users')   await loadUsers(state, content);
        if (tab === 'schools') await loadSchools(state, content);
    } catch (err) {
        state.innerHTML = `<div class="state-error">
            <span>Erreur de chargement</span>
        </div>`;
    }
}

async function loadUsers(state, content) {
    const data = await Leaderboard.users({ limit: 50 });
    state.innerHTML = '';

    if (data.leaderboard.length === 0) {
        state.innerHTML = `<div class="state-empty">
            <span>Aucun contributeur pour l'instant</span>
        </div>`;
        return;
    }

    // Médailles pour le podium
    const medals = ['🥇', '🥈', '🥉'];

    content.innerHTML = `
    <table class="lb-table">
        <thead>
            <tr>
                <th style="width:48px">Rang</th>
                <th>Utilisateur</th>
                <th>École</th>
                <th style="text-align:right">Documents</th>
                <th style="text-align:right">Points</th>
            </tr>
        </thead>
        <tbody>
            ${data.leaderboard.map((u, i) => {
                const isMe = currentUser && u.id === currentUser.id;
                return `
                <tr class="${isMe ? 'is-me' : ''}"
                    style="cursor:pointer"
                    onclick="window.location.href='profile.html?id=${u.id}'">
                    <td>
                        ${i < 3
                            ? `<span class="medal">${medals[i]}</span>`
                            : `<span class="text-muted">${u.rank}</span>`
                        }
                    </td>
                    <td>
                        <div class="flex items-center gap-2">
                            <div class="avatar avatar-sm">
                                ${getInitials(u.username)}
                            </div>
                            <span>${escapeHtml(u.username)}</span>
                            ${isMe
                                ? '<span class="badge badge-blue">Toi</span>'
                                : ''}
                        </div>
                    </td>
                    <td class="text-muted">${escapeHtml(u.school || '—')}</td>
                    <td style="text-align:right">${u.upload_count}</td>
                    <td style="text-align:right;font-weight:500;
                               color:var(--blue-600)">
                        ${u.points} pts
                    </td>
                </tr>`;
            }).join('')}
        </tbody>
    </table>`;
}

async function loadSchools(state, content) {
    const data = await Leaderboard.schools({ limit: 50 });
    state.innerHTML = '';

    if (data.leaderboard.length === 0) {
        state.innerHTML = `<div class="state-empty">
            <span>Aucune école pour l'instant</span>
        </div>`;
        return;
    }

    const medals = ['🥇', '🥈', '🥉'];

    content.innerHTML = `
    <table class="lb-table">
        <thead>
            <tr>
                <th style="width:48px">Rang</th>
                <th>École</th>
                <th>Ville</th>
                <th style="text-align:right">Documents</th>
                <th style="text-align:right">Télécharg.</th>
                <th style="text-align:right">Contributeurs</th>
            </tr>
        </thead>
        <tbody>
            ${data.leaderboard.map((s, i) => `
            <tr>
                <td>
                    ${i < 3
                        ? `<span class="medal">${medals[i]}</span>`
                        : `<span class="text-muted">${s.rank}</span>`
                    }
                </td>
                <td style="font-weight:500">${escapeHtml(s.name)}</td>
                <td class="text-muted">${escapeHtml(s.city || '—')}</td>
                <td style="text-align:right">${s.doc_count}</td>
                <td style="text-align:right">${s.total_downloads}</td>
                <td style="text-align:right">${s.contributor_count}</td>
            </tr>`).join('')}
        </tbody>
    </table>`;
}