# Génère les certificats pour les deux sous-domaines
certbot certonly --standalone \
    -d study-wiki.katnr.com \
    -d api.study-wiki.katnr.com

# Les certificats sont dans :
# /etc/letsencrypt/live/study-wiki.katnr.com/
# /etc/letsencrypt/live/api.study-wiki.katnr.com/