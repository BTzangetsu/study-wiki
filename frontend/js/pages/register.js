// js/pages/register.js

document.addEventListener('DOMContentLoaded', async () => {
    await initAuth({ redirectIfAuth: true });
    bindForm();
    bindPasswordToggle('password', 'toggle-password', 'eye-icon');
    bindPasswordStrength();
});

function bindForm() {
    const form      = document.getElementById('register-form');
    const submitBtn = document.getElementById('submit-btn');

    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        clearErrors();

        const username        = document.getElementById('username').value.trim();
        const email           = document.getElementById('email').value.trim();
        const password        = document.getElementById('password').value;
        const confirmPassword = document.getElementById('confirm-password').value;

        // Validation complète avant d'envoyer au serveur
        let hasError = false;

        if (!username || username.length < 3) {
            showFieldError('username',
                'Le pseudo doit faire au moins 3 caractères');
            hasError = true;
        }

        if (email && !email.includes('@')) {
            showFieldError('email', 'Email invalide');
            hasError = true;
        }

        if (!password || password.length < 8) {
            showFieldError('password',
                'Le mot de passe doit faire au moins 8 caractères');
            hasError = true;
        }

        if (password !== confirmPassword) {
            showFieldError('confirm-password',
                'Les mots de passe ne correspondent pas');
            hasError = true;
        }

        if (hasError) return;

        setLoading(submitBtn, true, 'Création du compte…');

        try {
            await Auth.register({ username, email, password });
            window.location.href = 'index.html';

        } catch (err) {
            if (err.status === 409) {
                showFormAlert(
                    'Cet email ou ce pseudo est déjà utilisé',
                    'error'
                );
            } else if (err.status === 400) {
                showFormAlert(err.message, 'error');
            } else {
                showFormAlert('Erreur serveur, réessaie plus tard', 'error');
            }
            setLoading(submitBtn, false, 'Créer mon compte');
        }
    });
}

// ============================================================
// INDICATEUR DE FORCE DU MOT DE PASSE
// Se met à jour en temps réel à chaque frappe.
// Calcule un score selon des critères simples.
// ============================================================
function bindPasswordStrength() {
    const input = document.getElementById('password');
    if (!input) return;

    input.addEventListener('input', () => {
        const val   = input.value;
        const score = getPasswordScore(val);
        updateStrengthUI(score);
    });
}

// Retourne un score de 0 à 4 selon la complexité
function getPasswordScore(password) {
    if (!password) return 0;
    let score = 0;

    // +1 par critère rempli
    if (password.length >= 8)                score++;  // longueur min
    if (password.length >= 12)               score++;  // longueur bonne
    if (/[A-Z]/.test(password))             score++;  // majuscule
    if (/[0-9]/.test(password))             score++;  // chiffre
    if (/[^A-Za-z0-9]/.test(password))     score++;  // caractère spécial

    // Plafonné à 4
    return Math.min(score, 4);
}

// Met à jour la barre et le label selon le score
function updateStrengthUI(score) {
    const fill  = document.getElementById('strength-fill');
    const label = document.getElementById('strength-label');
    if (!fill || !label) return;

    // Chaque score correspond à une largeur et une couleur
    const levels = [
        { width: '0%',   color: '',                    text: ''          },
        { width: '25%',  color: 'var(--red-700)',      text: 'Faible'    },
        { width: '50%',  color: 'var(--amber-700)',    text: 'Moyen'     },
        { width: '75%',  color: 'var(--blue-600)',     text: 'Bien'      },
        { width: '100%', color: 'var(--green-700)',    text: 'Fort'      },
    ];

    const level = levels[score];
    fill.style.width      = level.width;
    fill.style.background = level.color;
    label.textContent     = level.text;
    label.style.color     = level.color;
}

// Les helpers showFieldError, showFormAlert, clearErrors,
// setLoading sont définis dans login.js.
// Pour éviter la duplication, on pourrait les déplacer dans
// auth.js — c'est le prochain refactor naturel quand les
// pages auth seront toutes finies.