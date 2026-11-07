#!/usr/bin/env bash
# Makes the certificate the game client checks when it asks www.growtopia1.com which server to join,
# and the PSNode-Setup.cmd players run to trust it. Run as root on the VPS:
#   bash web/deploy/join/make-game-cert.sh <public IP>
#
# The CA is name-constrained to growtopia1.com and growtopia2.com, so even someone holding its key
# couldn't use it for other websites. Its key stays in /root/psnode-ca (root only) and is never
# copied anywhere. Re-running keeps the CA (players don't need to run setup again) and renews the
# server certificate.
set -euo pipefail
IP="${1:?usage: make-game-cert.sh <public IP>}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CA_DIR=/root/psnode-ca
CTX=/opt/gurotopia/resources/ctx
OUT=/var/www/psnode-join

umask 077
mkdir -p "$CA_DIR"
if [ ! -f "$CA_DIR/ca.key" ]; then
  openssl req -x509 -newkey rsa:3072 -sha256 -days 3650 -nodes \
    -keyout "$CA_DIR/ca.key" -out "$CA_DIR/ca.crt" -subj "/CN=PSNode Game CA" \
    -addext "basicConstraints=critical,CA:TRUE,pathlen:0" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -addext "nameConstraints=critical,permitted;DNS:growtopia1.com,permitted;DNS:growtopia2.com"
fi

openssl req -newkey rsa:2048 -sha256 -nodes -keyout "$CA_DIR/server.key" -out "$CA_DIR/server.csr" -subj "/CN=www.growtopia1.com"
cat > "$CA_DIR/server.ext" <<EXT
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:growtopia1.com,DNS:www.growtopia1.com,DNS:growtopia2.com,DNS:www.growtopia2.com
EXT
openssl x509 -req -in "$CA_DIR/server.csr" -CA "$CA_DIR/ca.crt" -CAkey "$CA_DIR/ca.key" -CAcreateserial \
  -days 825 -sha256 -extfile "$CA_DIR/server.ext" -out "$CA_DIR/server.crt"
openssl verify -CAfile "$CA_DIR/ca.crt" "$CA_DIR/server.crt"

# @note the gateway (8443) and nginx (443) both read these
for f in server.crt server.key; do
  [ -f "$CTX/$f" ] && [ ! -f "$CTX/$f.before-psnode-ca" ] && cp -p "$CTX/$f" "$CTX/$f.before-psnode-ca"
done
install -m 644 -o root -g gurotopia "$CA_DIR/server.crt" "$CTX/server.crt"
install -m 640 -o root -g gurotopia "$CA_DIR/server.key" "$CTX/server.key"

umask 022
mkdir -p "$OUT"
python3 - "$HERE/PSNode-Setup.cmd.template" "$OUT/PSNode-Setup.cmd" "$IP" "$CA_DIR/ca.crt" <<'PY'
import sys
tpl, out, ip, ca = sys.argv[1:]
s = open(tpl).read().replace('{{IP}}', ip).replace('{{CA_PEM}}', open(ca).read().strip())
open(out, 'w', newline='\r\n').write(s)  # @note cmd.exe wants CRLF
PY
install -m 644 "$HERE/index.html" "$OUT/index.html"
chmod 644 "$OUT/PSNode-Setup.cmd"

systemctl restart gurotopia-gateway
nginx -t && systemctl reload nginx
echo "done: https://psnode.cloud/join/"
