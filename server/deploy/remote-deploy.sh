#!/bin/bash
# Runs on the VPS from /opt/allstars (scripts/deploy-server.ps1 uploads the repo's server/
# as server.new): swaps it in, rebuilds and starts the container, then installs the nginx
# site and its certificate. Touches only /opt/allstars and allstars.komplexaci.cz's site.
set -euo pipefail
domain=allstars.komplexaci.cz
public_address=46.36.38.4
root=/opt/allstars
site=/etc/nginx/sites-available/$domain
link=/etc/nginx/sites-enabled/$domain
cd "$root"

if [ ! -f allstars-server.json ]; then
    echo "missing $root/allstars-server.json: deploy once with -AccessKeyFrom <Coalesced.ini>" >&2
    exit 1
fi
# The image runs as distroless's nonroot user.
chown 65532:65532 allstars-server.json
chmod 400 allstars-server.json

# On a failed build or health check, the previous tree and container come back.
rm -rf server.previous compose.yaml.previous
[ -d server ] && mv server server.previous
[ -f compose.yaml ] && cp compose.yaml compose.yaml.previous
mv server.new server
install -m 644 server/deploy/compose.yaml compose.yaml
if ! docker compose up -d --build --wait --wait-timeout 120; then
    docker compose logs --tail 20 || true
    if [ -d server.previous ]; then
        rm -rf server && mv server.previous server && mv compose.yaml.previous compose.yaml
        docker compose up -d --build --wait --wait-timeout 120 || true
        echo "the new server did not come up healthy; restored the previous one" >&2
    fi
    exit 1
fi
rm -rf server.previous compose.yaml.previous
docker image prune -f --filter label=org.opencontainers.image.title=allstars-server >/dev/null
docker compose ps

# nginx -t checks every site: on failure, put ours back as it was so the others keep running.
reload_nginx() {
    if nginx -t 2>/dev/null; then
        systemctl reload nginx
        return 0
    fi
    nginx -t || true
    if [ -f "$site.previous" ]; then mv "$site.previous" "$site"; else rm -f "$site" "$link"; fi
    echo "nginx rejected the $domain site; restored the previous one" >&2
    return 1
}
install_site() {
    [ -f "$site" ] && cp "$site" "$site.previous"
    cat > "$site"
    ln -sfn "$site" "$link"
    reload_nginx
    rm -f "$site.previous"
}

if [ ! -f "/etc/letsencrypt/live/$domain/fullchain.pem" ]; then
    resolved=$(getent ahostsv4 "$domain" | awk 'NR == 1 { print $1 }' || true)
    if [ "$resolved" != "$public_address" ]; then
        echo "$domain resolves to '${resolved:-nothing}', not $public_address: skipped the nginx site" \
            "and certificate until the DNS-only A record exists"
        exit 0
    fi
    # HTTP only, so certbot's webroot challenge is served before the certificate exists.
    install_site <<SITE
server {
    listen 80;
    listen [::]:80;
    server_name $domain;
    location /.well-known/acme-challenge/ { root /var/www/html; }
    location / { return 404; }
}
SITE
    certbot certonly --webroot -w /var/www/html -d "$domain" --non-interactive --agree-tos \
        --keep-until-expiring
fi
install_site < server/deploy/$domain
echo "https://$domain/ is served by nginx"
