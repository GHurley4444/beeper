# Beeper for phones

The Beeper handheld as an installable web app (plain HTML, CSS and ES modules, no
build tools). Live on the tailnet at **https://headcrab.tail8aa062.ts.net/beeper/**.
On the iPhone: Tailscale on, open the link in Safari, Share → Add to Home Screen.

## Content comes from the firmware

`data.js`, `img/logo.png` and the icons are generated from `../beeper.ino`,
`../tools.h`, `../songs.h` and `../index.h`, so the device and the app share one copy.
After editing those files:

```
node tools/build-data.mjs
```

## Run, test, deploy

```
npm start                 # http://localhost:4173 (python http.server)
npm test                  # game-logic tests (node --test)
bash tools/deploy.sh      # tests, then copies the app to the GPD
```

Bump `VERSION` in `sw.js` when shipping so phones fetch every file fresh.

## Hosting on the GPD (headcrab)

Tailscale on the GPD serves `/home/g5/beeper-web` at `/beeper/` with its own HTTPS
certificate, tailnet only. One-time setup, already done (needs sudo):

```
sudo tailscale serve --bg --https=443 --set-path=/beeper /home/g5/beeper-web
```

Don't use `tailscale set --operator=g5` to avoid sudo: the operator is `voicegrid`,
which Voice Grid needs to renew its own certificate on port 3901.
To stop serving: `sudo tailscale serve --https=443 off`.

## Not ported

WiFi/BLE/Bluetooth scanners (browsers can't scan), screen brightness (not
controllable from a page), and the QR screen (it never fit its URL on the device).
