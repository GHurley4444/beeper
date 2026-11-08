#!/usr/bin/env python3
"""
Regenerate the phone app's data files straight from the Beeper firmware.

  python3 phone-app/tools/extract_firmware.py            # data.js + logo.js + icons
  python3 phone-app/tools/extract_firmware.py --font path/to/font5x8.bin   # also font.js

Reads (never writes) the firmware sources in the repo root:
  beeper.ino  - prescript pools, generator word pools, rewards, idle texts,
                game list, first song + song catalogue
  songs.h     - the other song lyric arrays
  tools.h     - tarot, oracle, word-generator lists, cipher keypad
  index.h     - 320x96 RGB565 boot logo

Writes:
  phone-app/js/data.js   - every string table, keyed by its firmware name
  phone-app/js/logo.js   - logo pixels as base64 RGB565 (little-endian)
  phone-app/icons/*.png  - home-screen icons made from the logo
  phone-app/js/font.js   - (only with --font) the 5x7 GLCD font TFT_eSPI uses

Arrays are cut to their firmware COUNT constant when one exists, so the app
draws from exactly the entries the device can reach.
"""
import argparse
import base64
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
ROOT = os.path.dirname(APP)


# ---------------------------------------------------------------- tokenizer
def tokenize(src):
    """Minimal C tokenizer: strings, identifiers, numbers, punctuation.
    Comments and preprocessor lines (except #define) are dropped."""
    toks = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c in ' \t\r\n':
            i += 1
        elif src.startswith('//', i):
            i = src.find('\n', i)
            i = n if i < 0 else i
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif c == '#':
            j = src.find('\n', i)
            j = n if j < 0 else j
            line = src[i:j]
            m = re.match(r'#define\s+(\w+)\s+(\d+)\b', line)
            if m:
                toks.append(('DEFINE', (m.group(1), int(m.group(2)))))
            i = j
        elif c == '"':
            j = i + 1
            buf = []
            while j < n and src[j] != '"':
                if src[j] == '\\':
                    nxt = src[j + 1]
                    buf.append({'n': '\n', 't': '\t', '"': '"', '\\': '\\', "'": "'"}.get(nxt, nxt))
                    j += 2
                else:
                    buf.append(src[j])
                    j += 1
            toks.append(('STR', ''.join(buf)))
            i = j + 1
        elif c == "'":
            j = src.find("'", i + 1 + (src[i + 1] == '\\'))
            toks.append(('CHAR', src[i + 1:j]))
            i = j + 1
        elif c.isalpha() or c == '_':
            j = i
            while j < n and (src[j].isalnum() or src[j] == '_'):
                j += 1
            toks.append(('ID', src[i:j]))
            i = j
        elif c.isdigit():
            j = i
            while j < n and (src[j].isalnum() or src[j] in '.x'):
                j += 1
            toks.append(('NUM', src[i:j]))
            i = j
        else:
            toks.append(('P', c))
            i += 1
    return toks


def parse(src):
    toks = tokenize(src)
    arrays, structs, ints, strings = {}, {}, {}, {}
    i = 0
    while i < len(toks):
        t = toks[i]
        if t[0] == 'DEFINE':
            ints[t[1][0]] = t[1][1]
        # const int NAME = 123;
        elif t == ('ID', 'int') and i + 3 < len(toks) and toks[i + 1][0] == 'ID' \
                and toks[i + 2] == ('P', '=') and toks[i + 3][0] == 'NUM' and toks[i + 4] == ('P', ';'):
            ints[toks[i + 1][1]] = int(toks[i + 3][1], 0)
        # char* NAME[...] = { "a", "b" "c", ... };
        elif t == ('ID', 'char') and toks[i + 1] == ('P', '*') and toks[i + 2][0] == 'ID' \
                and toks[i + 3] == ('P', '['):
            name = toks[i + 2][1]
            j = i + 4
            while toks[j] != ('P', '='):
                j += 1
            if toks[j + 1] == ('P', '{'):
                j += 2
                items, cur = [], None
                while toks[j] != ('P', '}'):
                    if toks[j][0] == 'STR':
                        cur = (cur or '') + toks[j][1]
                    elif toks[j] == ('P', ','):
                        if cur is not None:
                            items.append(cur)
                        cur = None
                    j += 1
                if cur is not None:
                    items.append(cur)
                arrays[name] = items
                i = j
        # char NAME[] = "....";
        elif t == ('ID', 'char') and toks[i + 1][0] == 'ID' and toks[i + 2] == ('P', '[') \
                and toks[i + 3] == ('P', ']') and toks[i + 4] == ('P', '=') and toks[i + 5][0] == 'STR':
            strings[toks[i + 1][1]] = toks[i + 5][1]
        # TarotCard TAROT[22] = { { "a", "b" }, ... };
        elif t == ('ID', 'TarotCard') and toks[i + 1][0] == 'ID' and toks[i + 2] == ('P', '['):
            name = toks[i + 1][1]
            j = i
            while toks[j] != ('P', '{'):
                j += 1
            j += 1
            rows, cur, depth = [], [], 1
            while depth:
                tk = toks[j]
                if tk == ('P', '{'):
                    depth += 1
                    cur = []
                elif tk == ('P', '}'):
                    depth -= 1
                    if depth == 1:
                        rows.append(cur)
                elif tk[0] == 'STR' and depth == 2:
                    cur.append(tk[1])
                j += 1
            structs[name] = rows
            i = j
        i += 1
    return arrays, structs, ints, strings


def read(name):
    with open(os.path.join(ROOT, name), encoding='utf-8') as f:
        return f.read()


def clean(s):
    # The device prints raw bytes through a 5x7 font; the only non-ASCII in
    # the data is an em dash, which renders as junk there. Use a plain dash.
    return s.replace('—', '-').replace('–', '-').replace('’', "'")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--font', help='path to Adafruit font5x8.bin (writes js/font.js)')
    args = ap.parse_args()

    ino_a, ino_s, ino_i, _ = parse(read('beeper.ino'))
    son_a, _, son_i, _ = parse(read('songs.h'))
    too_a, too_s, too_i, too_str = parse(read('tools.h'))

    ints = {**ino_i, **son_i, **too_i}
    out = {}

    # Prescript pools + rare + rewards: cut to their COUNT constants.
    count_for = {
        'easyGame': 'EASY_GAME_COUNT', 'gamePrescripts': 'GAME_POOL_COUNT',
        'hardGame': 'HARD_GAME_COUNT', 'rareGame': 'RARE_GAME_COUNT',
        'easyCity': 'EASY_CITY_COUNT', 'cityPrescripts': 'CITY_COUNT',
        'hardCity': 'HARD_CITY_COUNT', 'rareCity': 'RARE_CITY_COUNT',
        'easyCon': 'EASY_CON_COUNT', 'conPrescripts': 'CON_COUNT',
        'hardCon': 'HARD_CON_COUNT', 'rareCon': 'RARE_CON_COUNT',
        'easyTask': 'EASY_TASK_COUNT', 'taskPrescripts': 'TASK_COUNT',
        'hardTask': 'HARD_TASK_COUNT', 'rareTask': 'RARE_TASK_COUNT',
        'coreRewards': 'GAME_REWARD_COUNT', 'corePunishments': 'GAME_PUNISH_COUNT',
        'cityRewards': 'CITY_REWARD_COUNT', 'cityPunishments': 'CITY_PUNISH_COUNT',
        'conRewards': 'CON_REWARD_COUNT', 'conPunishments': 'CON_PUNISH_COUNT',
        'taskRewards': 'TASK_REWARD_COUNT', 'taskPunishments': 'TASK_PUNISH_COUNT',
        'idleTexts': 'IDLE_TEXT_COUNT',
    }
    warnings = []
    for name, items in ino_a.items():
        items = [clean(s) for s in items]
        if name in count_for:
            cnt = ints[count_for[name]]
            if cnt != len(items):
                warnings.append(f'{name}: {len(items)} entries, firmware count {cnt}')
            items = items[:cnt]
        out[name] = items
    # Small label arrays local to draw functions are ported as code instead.
    for local in ('poolNames', 'items', 'labels', 'mnames', 'dnames', 'catNames', 'menuNames'):
        out.pop(local, None)

    # Songs: catalogue order and titles come from songCatalogue in beeper.ino.
    ino_src = read('beeper.ino')
    cat = re.search(r'songCatalogue\[\]\s*=\s*\{(.*?)\};', ino_src, re.S).group(1)
    songs = []
    for title, arr, cnt in re.findall(r'\{\s*"([^"]+)"\s*,\s*(\w+)\s*,\s*(\w+)\s*\}', cat):
        lines = ino_a.get(arr) or son_a.get(arr)
        n = len(lines) if cnt == 'LYRICS_COUNT' else ints.get(cnt, len(lines))
        if n != len(lines):
            warnings.append(f'{arr}: {len(lines)} lines, firmware count {n}')
        songs.append({'title': title, 'lines': [clean(s) for s in lines[:n]]})
    out['songs'] = songs
    out.pop('lyricsLines', None)

    out['TAROT'] = [{'name': clean(a), 'desc': clean(b)} for a, b in too_s['TAROT']]
    for name in ('ORACLE_RESPONSES', 'GEN_NOUNS', 'GEN_ADJECTIVES', 'GEN_VERBS', 'GEN_NAMES', 'COIN_FACES'):
        out[name] = [clean(s) for s in too_a[name]]
    out['CIPHER_CHARS'] = too_str['CIPHER_CHARS']

    # Remember the random(N) bound each generator uses, so the app can draw
    # from the same slice the firmware does (see generate*Presc in beeper.ino).
    bounds = {}
    for pool, n in re.findall(r'=\s*(\w+)\[random\((\d+)\)\]', ino_src):
        bounds[pool] = int(n)
    out['randomBounds'] = bounds
    for pool, n in bounds.items():
        if pool in out and n != len(out[pool]):
            warnings.append(f'{pool}: firmware draws random({n}) from {len(out[pool])} entries')

    js = ['// GENERATED by tools/extract_firmware.py from the Beeper firmware.',
          '// Do not edit by hand: change the firmware and re-run the script.',
          'const FW = ' + json.dumps(out, indent=1, ensure_ascii=True) + ';', '']
    with open(os.path.join(APP, 'js', 'data.js'), 'w') as f:
        f.write('\n'.join(js))

    # ---- logo
    logo_src = read('index.h')
    vals = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{4})', logo_src)]
    assert len(vals) == 320 * 96, len(vals)
    raw = b''.join(v.to_bytes(2, 'little') for v in vals)
    with open(os.path.join(APP, 'js', 'logo.js'), 'w') as f:
        f.write('// GENERATED by tools/extract_firmware.py from index.h (320x96 RGB565, little-endian).\n')
        f.write('const LOGO_W = 320, LOGO_H = 96;\n')
        f.write('const LOGO_B64 = "' + base64.b64encode(raw).decode() + '";\n')

    make_icons(vals)

    if args.font:
        with open(args.font, 'rb') as f:
            d = f.read()
        assert d[:2] == b'\x05\x08' and len(d) == 2 + 256 * 5, 'expected font5x8.bin'
        glyphs = list(d[2:])
        with open(os.path.join(APP, 'js', 'font.js'), 'w') as f:
            f.write('// Adafruit "classic" 5x7 GLCD font (the font TFT_eSPI uses as font 1).\n')
            f.write('// SPDX-FileCopyrightText: 2021 ladyada for Adafruit Industries\n')
            f.write('// SPDX-License-Identifier: MIT\n')
            f.write('// 256 glyphs x 5 column bytes, bit 0 = top row.\n')
            f.write('const GLCD_FONT = new Uint8Array([\n')
            for k in range(0, len(glyphs), 20):
                f.write(','.join(str(b) for b in glyphs[k:k + 20]) + ',\n')
            f.write(']);\n')

    for w in warnings:
        print('note:', w, file=sys.stderr)
    print('wrote data.js (%d tables, %d songs), logo.js, icons%s'
          % (len(out), len(songs), ', font.js' if args.font else ''))


def make_icons(vals):
    try:
        from PIL import Image
    except ImportError:
        print('PIL not installed: skipping icons', file=sys.stderr)
        return
    def rgb(v):
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)
    logo = Image.new('RGB', (320, 96))
    logo.putdata([rgb(v) for v in vals])
    bbox = logo.getbbox() or (0, 0, 320, 96)
    logo = logo.crop(bbox)
    bg = (15, 15, 15)
    for size in (180, 192, 512):
        icon = Image.new('RGB', (size, size), bg)
        pad = int(size * 0.12)
        w = size - 2 * pad
        h = max(1, round(logo.height * w / logo.width))
        scaled = logo.resize((w, h), Image.NEAREST)
        icon.paste(scaled, (pad, (size - h) // 2))
        icon.save(os.path.join(APP, 'icons', f'icon-{size}.png'))


if __name__ == '__main__':
    main()
