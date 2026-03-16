// js/pages/upload.js

// ============================================================
// ÉTAT DE LA PAGE
// ============================================================
let selectedFile = null;  // fichier sélectionné par l'utilisateur

// ============================================================
// POINT D'ENTRÉE
// ============================================================
document.addEventListener('DOMContentLoaded', async () => {
    // redirectIfNoAuth: true → redirige vers login si non connecté.
    // La page upload est réservée aux utilisateurs connectés.
    await initAuth({ redirectIfNoAuth: true });

    populateYears();
    await loadSchools();
    bindDropZone();
    bindSchoolChange();
    bindForm();
});

// ============================================================
// REMPLISSAGE DES SELECTS
// ============================================================
function populateYears() {
    const select      = document.getElementById('doc-year');
    const currentYear = new Date().getFullYear();
    for (let y = currentYear; y >= 2015; y--) {
        const opt = document.createElement('option');
        opt.value       = y;
        opt.textContent = y;
        select.appendChild(opt);
    }
}

async function loadSchools() {
    try {
        const data   = await Leaderboard.schools({ limit: 100 });
        const select = document.getElementById('doc-school');
        data.leaderboard.forEach(s => {
            const opt       = document.createElement('option');
            opt.value       = s.id;
            opt.textContent = s.name;
            select.appendChild(opt);
        });
    } catch (e) { /* silencieux */ }
}

// Quand l'école change, charge les matières de cette école.
// La matière est désactivée tant qu'aucune école n'est choisie.
function bindSchoolChange() {
    const schoolSelect  = document.getElementById('doc-school');
    const subjectSelect = document.getElementById('doc-subject');

    schoolSelect.addEventListener('change', async () => {
        const schoolId = schoolSelect.value;

        // Remet le select matière à son état initial
        subjectSelect.innerHTML =
            '<option value="">Choisir une matière…</option>';

        if (!schoolId) {
            subjectSelect.disabled = true;
            return;
        }

        try {
            // On réutilise GET /api/search avec school_id
            // pour récupérer les documents et en déduire les matières.
            // Idéalement on aurait GET /api/schools/:id/subjects —
            // à ajouter dans le back si la liste devient importante.
            const data = await Search.query({
                school_id: schoolId,
                limit: 100
            });

            // Déduplique les matières à partir des résultats
            const subjects = [...new Map(
                data.results
                    .filter(d => d.subject)
                    .map(d => [d.subject, d.subject])
            ).values()];

            if (subjects.length === 0) {
                subjectSelect.innerHTML =
                    '<option value="">Aucune matière disponible</option>';
                subjectSelect.disabled = true;
                return;
            }

            subjects.forEach(name => {
                const opt       = document.createElement('option');
                opt.value       = name;
                opt.textContent = name;
                subjectSelect.appendChild(opt);
            });

            subjectSelect.disabled = false;

        } catch (e) {
            subjectSelect.disabled = true;
        }
    });
}

// ============================================================
// ZONE DE DÉPÔT — drag & drop + clic
// ============================================================
function bindDropZone() {
    const zone      = document.getElementById('drop-zone');
    const fileInput = document.getElementById('file-input');
    const changeBtn = document.getElementById('btn-change-file');

    // Clic sur la zone → ouvre le sélecteur de fichier natif
    zone.addEventListener('click', () => fileInput.click());

    // Clic sur "Changer" → remet la zone visible
    changeBtn.addEventListener('click', () => {
        fileInput.click();
    });

    // Événements drag & drop
    // dragover doit appeler preventDefault() pour autoriser
    // le drop — sans ça le navigateur ouvre le fichier
    zone.addEventListener('dragover', (e) => {
        e.preventDefault();
        zone.classList.add('drag-over');
    });

    zone.addEventListener('dragleave', () => {
        zone.classList.remove('drag-over');
    });

    zone.addEventListener('drop', (e) => {
        e.preventDefault();
        zone.classList.remove('drag-over');
        const file = e.dataTransfer.files[0];
        if (file) handleFileSelected(file);
    });

    // Sélection via le sélecteur natif
    fileInput.addEventListener('change', () => {
        const file = fileInput.files[0];
        if (file) handleFileSelected(file);
    });
}

// Appelée quand un fichier est choisi — par clic ou drag
function handleFileSelected(file) {
    // Vérifie la taille — 50 Mo max
    if (file.size > 50 * 1024 * 1024) {
        showFormAlert('Le fichier dépasse 50 Mo', 'error');
        return;
    }

    selectedFile = file;

    // Cache la zone de dépôt, affiche l'aperçu
    document.getElementById('drop-zone').style.display    = 'none';
    document.getElementById('file-preview').style.display = 'flex';

    // Remplit l'aperçu
    document.getElementById('preview-name').textContent =
        file.name;
    document.getElementById('preview-size').textContent =
        formatSize(file.size);

    // Pré-remplit le titre avec le nom du fichier nettoyé.
    // "exam_maths_2023.pdf" → "exam maths 2023"
    const titleInput = document.getElementById('title');
    if (!titleInput.value) {
        titleInput.value = file.name
            .replace(/\.[^.]+$/, '')  // retire l'extension
            .replace(/[_\-]/g, ' ')   // remplace _ et - par des espaces
            .trim();
    }

    // Active le bouton submit maintenant qu'un fichier est prêt
    updateSubmitState();
}

// Active le bouton submit seulement si fichier + titre + type sont remplis
function updateSubmitState() {
    const hasFile  = selectedFile !== null;
    const hasTitle = document.getElementById('title').value.trim().length > 0;
    const hasType  = document.getElementById('doc-type').value !== '';

    document.getElementById('submit-btn').disabled =
        !(hasFile && hasTitle && hasType);
}

// Branche les listeners pour mettre à jour l'état du bouton
document.addEventListener('DOMContentLoaded', () => {
    ['title', 'doc-type'].forEach(id => {
        document.getElementById(id)
            ?.addEventListener('input', updateSubmitState);
        document.getElementById(id)
            ?.addEventListener('change', updateSubmitState);
    });
});

// ============================================================
// SOUMISSION DU FORMULAIRE — flux en deux étapes
// ============================================================
function bindForm() {
    const form = document.getElementById('upload-form');

    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        if (!selectedFile) return;

        const title       = document.getElementById('title').value.trim();
        const type        = document.getElementById('doc-type').value;
        const year        = document.getElementById('doc-year').value;
        const schoolId    = document.getElementById('doc-school').value;
        const description = document.getElementById('description').value.trim();

        // Validation
        let hasError = false;
        if (!title) {
            showFieldError('title', 'Le titre est requis');
            hasError = true;
        }
        if (!type) {
            showFieldError('type', 'Le type est requis');
            hasError = true;
        }
        if (hasError) return;

        // Désactive le formulaire pendant l'upload
        const submitBtn = document.getElementById('submit-btn');
        submitBtn.disabled    = true;
        submitBtn.textContent = 'Upload en cours…';

        // Affiche la barre de progression
        document.getElementById('progress-wrap').style.display = '';

        try {
            // ÉTAPE 1 + 2 : upload vers B2 via la fonction de api.js
            // onProgress met à jour la barre en temps réel
            const storageKey = await uploadFile(
                selectedFile,
                (pct) => updateProgress(pct)
            );

            // ÉTAPE 3 : enregistre les métadonnées en base
            updateProgress(100, 'Enregistrement…');

            const result = await Documents.create({
                storage_key: storageKey,
                title,
                type,
                year:        year        || undefined,
                school_id:   schoolId    ? parseInt(schoolId) : undefined,
                description: description || undefined,
                size_bytes:  selectedFile.size,
            });

            showToast('Document uploadé ! +10 points');

            // Redirige vers la page du document après 1 seconde
            setTimeout(() => {
                window.location.href = `document.html?id=${result.id}`;
            }, 1000);

        } catch (err) {
            if (err.status === 413) {
                showFormAlert('Fichier trop volumineux (max 50 Mo)', 'error');
            } else {
                showFormAlert(
                    err.message || 'Erreur lors de l\'upload',
                    'error'
                );
            }

            // Remet le formulaire en état initial
            submitBtn.disabled    = false;
            submitBtn.textContent = 'Uploader le document';
            document.getElementById('progress-wrap').style.display = 'none';
        }
    });
}

// Met à jour visuellement la barre de progression
function updateProgress(pct, label = 'Upload en cours…') {
    document.getElementById('progress-fill').style.width = pct + '%';
    document.getElementById('progress-pct').textContent  = pct + '%';
    document.getElementById('progress-label').textContent = label;
}

// ============================================================
// HELPERS — réutilisés depuis login.js
// ============================================================
function showFieldError(fieldId, message) {
    const el = document.getElementById(`${fieldId}-error`);
    if (el) el.textContent = message;
    const input = document.getElementById(fieldId);
    if (input) input.style.borderColor = 'var(--red-700)';
}

function showFormAlert(message, type = 'error') {
    const el = document.getElementById('form-alert');
    if (!el) return;
    el.textContent = message;
    el.className   = `form-alert visible ${type}`;
}