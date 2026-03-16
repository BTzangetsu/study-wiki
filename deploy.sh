#!/bin/bash
# deploy.sh
# Usage :
#   ./deploy.sh dev   → localhost
#   ./deploy.sh prod  → domaines de production

set -e  # arrête le script à la première erreur

# ============================================================
# CONFIGURATION
# Modifie ces deux valeurs selon ton projet
# ============================================================
PROD_FRONTEND_URL="https://study-wiki.katnr.com"
PROD_API_URL="https://api.study-wiki.katnr.com"

DEV_FRONTEND_URL="http://localhost:3000"
DEV_API_URL="http://localhost:8080"

FRONTEND_DIR="./frontend"
ENV_FILE=".env"

NGINX_CONTAINER="nginx"  # nom du conteneur nginx dans docker-compose

# ============================================================
# LECTURE DE L'ENVIRONNEMENT
# ============================================================
ENV=${1:-dev}  # "dev" par défaut si pas d'argument

if [ "$ENV" = "prod" ]; then
    API_URL="$PROD_API_URL"
    FRONTEND_URL="$PROD_FRONTEND_URL"
    echo "Déploiement en mode PRODUCTION"
elif [ "$ENV" = "dev" ]; then
    API_URL="$DEV_API_URL"
    FRONTEND_URL="$DEV_FRONTEND_URL"
    echo "Déploiement en mode DÉVELOPPEMENT"
else
    echo "Usage : ./deploy.sh [dev|prod]"
    exit 1
fi

# ============================================================
# ÉTAPE 1 — mise à jour du data-api dans tous les HTML
#
# sed -i → modifie le fichier en place (in-place)
# 's|ancien|nouveau|g' → remplace toutes les occurrences
# On utilise | comme séparateur au lieu de / pour éviter
# les conflits avec les / dans les URLs
# ============================================================
echo "Mise à jour du data-api dans les fichiers HTML..."

# Détecte l'OS pour la syntaxe sed
# macOS (BSD sed) exige un argument après -i
# Linux (GNU sed) accepte -i sans argument
if [[ "$OSTYPE" == "darwin"* ]]; then
    SED="sed -i ''"
else
    SED="sed -i"
fi

# Remplace toutes les valeurs data-api existantes
# Le pattern matche n'importe quelle URL entre les guillemets
find "$FRONTEND_DIR" -name "*.html" | while read -r file; do
    $SED "s|data-api=\"[^\"]*\"|data-api=\"$API_URL\"|g" "$file"
    echo "  ✓ $file"
done

# ============================================================
# ÉTAPE 2 — mise à jour de ALLOWED_ORIGIN dans le .env
# ============================================================
echo "Mise à jour de ALLOWED_ORIGIN dans .env..."

if [ -f "$ENV_FILE" ]; then
    $SED "s|ALLOWED_ORIGIN=.*|ALLOWED_ORIGIN=$FRONTEND_URL|" "$ENV_FILE"
    echo "  ✓ .env → ALLOWED_ORIGIN=$FRONTEND_URL"
else
    echo "  ✗ Fichier .env introuvable"
    exit 1
fi

# ============================================================
# ÉTAPE 3 — actions selon l'environnement
# ============================================================
if [ "$ENV" = "prod" ]; then
    echo "Copie du frontend vers /var/www/study-wiki/frontend..."
    cp -r "$FRONTEND_DIR"/* /var/www/study-wiki/frontend/

    echo "Rebuild et redémarrage du backend..."
    docker compose build study-wiki-backend
    docker compose up -d study-wiki-backend

    echo "Rechargement de nginx..."
    docker exec nginx "$NGINX_CONTAINER" -s reload

    echo ""
    echo "Déploiement terminé."
    echo "Frontend : $FRONTEND_URL"
    echo "API      : $API_URL"

elif [ "$ENV" = "dev" ]; then
    echo ""
    echo "Environnement dev configuré."
    echo "Lance le backend avec : docker compose up -d"
    echo "Sers le frontend avec : npx serve frontend/ -p 3000"
fi