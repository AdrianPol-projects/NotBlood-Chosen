import re, json, sys
src = open(sys.argv[1], encoding='latin1').read()
sec = None; out = {'sprite': {}, 'wall': {}, 'sector': {}}; auto = {}
for line in src.splitlines():
    line = line.strip()
    if line.startswith('['): sec = line.strip('[]'); continue
    if not line or line[0] in ';#': continue
    m = re.match(r'Type(\d+)\s*=\s*\((.*)\)\s*$', line)
    if not m: continue
    t = int(m.group(1)); body = m.group(2)
    # split on commas outside parentheses
    parts, depth, cur = [], 0, ''
    for ch in body:
        if ch == '(': depth += 1
        if ch == ')': depth -= 1
        if ch == ',' and depth == 0: parts.append(cur); cur = ''
        else: cur += ch
    parts.append(cur)
    kv = {}
    for p in parts:
        if '=' in p:
            k, v = p.split('=', 1); kv[k.strip()] = v.strip()
    key = {'Sprites': 'sprite', 'Walls': 'wall', 'Sectors': 'sector'}.get(sec)
    if key:
        e = {'name': kv.get('Name', '')}
        if kv.get('Caption'): e['caption'] = kv['Caption']
        data = [kv.get('Data%d' % i, '') for i in range(1, 5)]
        if any(data): e['data'] = data
        out[key][t] = e
    elif sec == 'AutoAdjustSprites' and t not in auto:
        def num(k):
            try: return int(kv.get(k, '-1'))
            except ValueError: return -1
        auto[t] = {'tile': num('Tile'), 'xr': num('SizeX'), 'yr': num('SizeY'), 'pal': num('Pal'), 'group': kv.get('Group', '')}
for t, a in auto.items():
    if t in out['sprite']: out['sprite'][t].update({k: v for k, v in a.items() if v != -1 and v != ''})
print('// Blood object types: names, captions and what their data fields mean, with default tiles and sizes.')
print('// Made from OBJTYPES.INI of XMAPEDIT (https://github.com/NoOneBlood/xmapedit, by nuke.YKT and NoOne)')
print('// by tools/mapeditor/mkblooddata.py; the command and channel names are from source/blood/src/eventq.h.')
print('(function (root) {')
print('  root.BloodData = ' + json.dumps(out, separators=(',', ':')) + ';')
print('''  root.BloodData.commands = { 0: 'Off', 1: 'On', 2: 'State', 3: 'Toggle', 4: 'Not state', 5: 'Link', 6: 'Lock', 7: 'Unlock', 8: 'Toggle lock',
    9: 'Stop off', 10: 'Stop on', 11: 'Stop next', 12: 'Counter sector', 20: 'Callback', 21: 'Repeat', 30: 'Sprite push', 31: 'Sprite impact',
    32: 'Sprite pickup', 33: 'Sprite touch', 34: 'Sprite sight', 35: 'Sprite proximity', 36: 'Sprite explode', 40: 'Sector push', 41: 'Sector impact',
    42: 'Sector enter', 43: 'Sector exit', 50: 'Wall push', 51: 'Wall impact', 52: 'Wall touch' };
  for (let i = 64; i < 256; i++) root.BloodData.commands[i] = 'Number ' + (i - 64);
  root.BloodData.channels = { 1: 'set total secrets', 2: 'secret found', 3: 'text over', 4: 'level exit', 5: 'secret level exit', 6: 'custom level exit',
    7: 'level start', 8: 'level start (bloodbath, teams)', 9: 'level start (co-op)', 10: 'level start (teams)', 15: 'player death (team A)',
    16: 'player death (team B)', 90: 'remote bomb 0', 91: 'remote bomb 1', 92: 'remote bomb 2', 93: 'remote bomb 3', 94: 'remote bomb 4',
    95: 'remote bomb 5', 96: 'remote bomb 6', 97: 'remote bomb 7' };''')
print("  if (typeof module !== 'undefined' && module.exports) module.exports = root.BloodData;")
print("})(typeof window !== 'undefined' ? window : this);")
