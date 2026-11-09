#!/usr/bin/env bash
# Copies the web app to the GPD (headcrab) over Tailscale SSH, swapping the
# folder in one step so the phone never sees a half-copied version.
# Tailscale on the GPD serves ~/beeper-web at https://headcrab.tail8aa062.ts.net/beeper/
# (one-time setup, run on the GPD: sudo tailscale serve --bg --https=443 --set-path=/beeper /home/g5/beeper-web)
#
#   bash tools/deploy.sh
set -euo pipefail
cd "$(dirname "$0")/.."
TS="${TAILSCALE:-$(command -v tailscale || echo '/c/Program Files/Tailscale/tailscale.exe')}"
HOST="${BEEPER_HOST:-g5@headcrab}"

node --test test/*.test.js >/dev/null || { echo "tests failed; not deploying" >&2; exit 1; }

tar -czf - index.html styles.css manifest.webmanifest sw.js data.js js img |
  "$TS" ssh "$HOST" 'set -e
    rm -rf ~/beeper-web.new && mkdir ~/beeper-web.new
    tar -xzf - -C ~/beeper-web.new
    chmod -R a+rX ~/beeper-web.new
    if [ -d ~/beeper-web ]; then mv ~/beeper-web ~/beeper-web.old; fi
    mv ~/beeper-web.new ~/beeper-web
    rm -rf ~/beeper-web.old
    echo "deployed $(find ~/beeper-web -type f | wc -l) files, $(grep -o "beeper-[0-9]*" ~/beeper-web/sw.js | head -1)"'
