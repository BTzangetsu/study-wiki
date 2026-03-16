// js/pages/login.js

document.addEventListener('DOMContentLoaded', async () => {
    // redirectIfAuth: true → si déjà connecté, redirige vers index.html
    // Pas besoin de voir la page login si on est déjà connecté.
    await initAuth({ redirectIfAuth: true });
    bindForm();
    bindPasswordToggle('password', 'toggle-password', 'eye-icon');
});

// ============================================================
// GESTION DU FORMULAIRE
// ============================================================
function bindForm() {
    const form      = document.getElementById('login-form');
    const submitBtn = document.getElementById('submit-btn');

    // L'événement submit se déclenche au clic sur le bouton
    // ET à l'appui sur Entrée dans un champ — mieux que onclick.
    form.addEventListener('submit', async (e) => {
        // preventDefault() empêche le comportement par défaut :
        // sans ça, le formulaire rechargerait la page en envoyant
        // les données en GET dans l'URL.
        e.preventDefault();

        // Réinitialise les erreurs précédentes
        clearErrors();

        // Récupère les valeurs des champs
        const identifier = document.getElementById('identifier').value.trim();
        const password   = document.getElementById('password').value;


        // Validation côté client — évite des allers-retours réseau
        // inutiles pour des erreurs évidentes
        let hasError = false;

        if (!identifier) {
            showFieldError('identifier',
                'Renseigne ton nom d\'utilisateur ou ton email');
            hasError = true;
        }

        if (!password) {
            showFieldError('password', 'Le mot de passe est requis');
            hasError = true;
        }

        if (hasError) return;

        // Désactive le bouton pendant la requête pour éviter
        // les doubles soumissions si l'utilisateur clique vite
        setLoading(submitBtn, true, 'Connexion…');

        try {
            await Auth.login({ identifier, password });

            // Succès → redirige vers la page d'accueil
            // Le cookie de session est posé automatiquement
            // par le navigateur depuis la réponse du serveur
            window.location.href = 'index.html';

        } catch (err) {
            // err.status est le code HTTP qu'on a attaché
            // à l'erreur dans api.js
            if (err.status === 401) {
                showFormAlert('Email ou mot de passe incorrect', 'error');
            } else if (err.status === 403) {
                showFormAlert('Compte banni', 'error');
            } else {
                showFormAlert('Erreur serveur, réessaie plus tard', 'error');
            }
            setLoading(submitBtn, false, 'Se connecter');
        }
    });
}

// ============================================================
// HELPERS FORMULAIRE
// Fonctions réutilisées dans login.js ET register.js
// ============================================================

// Affiche une erreur sous un champ spécifique
function showFieldError(fieldId, message) {
    const errEl = document.getElementById(`${fieldId}-error`);
    if (errEl) errEl.textContent = message;

    const input = document.getElementById(fieldId);
    if (input) input.style.borderColor = 'var(--red-700)';
}

// Affiche une alerte globale au-dessus du bouton submit
function showFormAlert(message, type = 'error') {
    const el = document.getElementById('form-alert');
    if (!el) return;
    el.textContent = message;
    el.className   = `form-alert visible ${type}`;
}

// Remet tous les champs en état normal
function clearErrors() {
    document.querySelectorAll('.form-error')
        .forEach(el => el.textContent = '');

    document.querySelectorAll('.form-input')
        .forEach(el => el.style.borderColor = '');

    const alert = document.getElementById('form-alert');
    if (alert) alert.className = 'form-alert';
}

// Met le bouton en état chargement — texte + désactivé
function setLoading(btn, loading, text) {
    btn.disabled     = loading;
    btn.textContent  = text;
    // opacity visuelle pour indiquer que le bouton est inactif
    btn.style.opacity = loading ? '0.7' : '1';
}

// ============================================================
// TOGGLE VISIBILITÉ MOT DE PASSE
// Bascule entre type="password" (masqué) et type="text" (visible).
// L'icône change aussi pour refléter l'état.
// ============================================================
function bindPasswordToggle(inputId, btnId, iconId) {
    const input = document.getElementById(inputId);
    const btn   = document.getElementById(btnId);
    const icon  = document.getElementById(iconId);

    if (!input || !btn) return;

    btn.addEventListener('click', () => {
        const isHidden = input.type === 'password';
        input.type = isHidden ? 'text' : 'password';

        // Change l'icône selon l'état
        // Oeil barré = mot de passe visible (on peut le cacher)
        // Oeil normal = mot de passe caché (on peut le voir)
        if (icon) {
            icon.innerHTML = isHidden
                ? `<path d="M17.94 17.94A10.07 10.07 0 0112 20c-7 0-11-8-11-8
                           a18.45 18.45 0 015.06-5.94M9.9 4.24A9.12 9.12 0
                           0112 4c7 0 11 8 11 8a18.5 18.5 0 01-2.16 3.19m-6.72
                           -1.07a3 3 0 11-4.24-4.24"/>
                   <line x1="1" y1="1" x2="23" y2="23"/>`
                : `<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/>
                   <circle cx="12" cy="12" r="3"/>`;
        }
    });
}