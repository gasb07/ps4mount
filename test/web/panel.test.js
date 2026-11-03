// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 gasb07
// Web panel tests: the main page and the page for older firmwares (/legacy) in jsdom with a
// mock API, and syntax checks of their scripts. Run with: cd test/web && npm ci && npm test
const fs = require('fs');
const { JSDOM, VirtualConsole } = require('jsdom');
const W = require('path').join(__dirname, '..', '..', 'web') + '/';
const GAMES = [
  { title_id: 'CUSA00791', name: 'N++', version: '01.10', has_patch: true, size: 4e9, state: 'ready', mount: 'rw', source: '/mnt/usb0/ps4/CUSA00791', dlc: 0, dlc_total: 0, updates: 2, update_issue: '' },
  { title_id: 'CUSA28732', name: 'Civ VII', version: '01.16', has_patch: true, size: 9e9, state: 'running', mount: 'rw', source: '/mnt/usb0/ps4/CUSA28732', dlc: 35, dlc_total: 35, updates: 2, update_issue: '' },
  { title_id: 'CUSA00190', name: 'Killzone', version: '01.00', has_patch: false, size: 9e9, state: 'internal', mount: '', source: '/mnt/usb0/ps4/CUSA00190', dlc: 0, update_issue: '' },
  { title_id: 'CUSA41712', name: 'Soulstice', version: '01.03', has_patch: false, size: 9e9, state: 'ready', mount: 'rw', source: '/mnt/usb0/ps4/CUSA41712', dlc: 1, dlc_total: 3, updates: 0, update_issue: '' }];
const CFG = { batch: true, batch_size: 5, dlc: true, dlc_copy: true, restore_batch: 5, max_restore_batch: 10, paths: ['/'] };
const DETAIL = { title_id: 'CUSA41712', name: 'Soulstice', dlc_limit: 1,
  updates: [{ file: 'patch.pkg', version: '01.03', name: 'b.pkg', active: true }, { file: 'replaced/patch-1.pkg', version: '01.02', name: 'a.pkg', active: false }],
  dlcs: [{ content_id: 'EP0001-CUSA41712_00-AAAAAAAAAAAAAAAA', name: 'Pack A', on: true, data: true, copy: 'installed', reason: '' },
         { content_id: 'EP0001-CUSA41712_00-BBBBBBBBBBBBBBBB', name: 'Pack B', on: false, data: false, copy: 'problem', reason: 'copy stopped' },
         { content_id: 'EP0001-CUSA41712_00-CCCCCCCCCCCCCCCC', name: 'Pack C', on: false, data: true, copy: 'missing', reason: '' }] };
let stopOpen = ''; /* the game /api/stop/check reports open */
function reply(url) {
  if (url === '/api/stop/check') return { ok: true, open: stopOpen };
  if (url.indexOf('/api/game?') === 0) return DETAIL;
  if (url === '/api/launch?title_id=CUSA41712') return { ok: false, confirm: true, open: 'Civ VII' };
  return url.indexOf('/api/log') === 0 ? { log: 'a\nb' } : url.indexOf('/api/config') === 0 ? CFG :
    url.indexOf('/api/games') === 0 ? { version: 'PS4Mount test', games: GAMES } : { ok: true, message: 'ok ' + url };
}
const tick = () => new Promise(r => setTimeout(r, 30));
const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) process.exitCode = 1; };
function page(file, opts) {
  const reqs = [], vc = new VirtualConsole(), nav = [];
  vc.on('jsdomError', e => { if (/navigation/i.test(e.message)) nav.push(e.message); else console.log('jsdomError', e.message); });
  const dom = new JSDOM(fs.readFileSync(W + file, 'utf8'), {
    url: 'http://ps5:8770/', runScripts: 'dangerously', pretendToBeVisual: true, virtualConsole: vc,
    beforeParse(w) {
      w.setInterval = () => 0;
      w.scrollTo = () => {};
      w.confirm = () => (w.__confirm !== undefined ? w.__confirm : true);
      if (opts.dialog) {
        w.HTMLDialogElement.prototype.showModal = function () { this.setAttribute('open', ''); };
        w.HTMLDialogElement.prototype.close = function () { this.removeAttribute('open'); };
      }
      w.fetch = (url, o) => { reqs.push({ url, method: (o && o.method) || 'GET', body: o && o.body }); return Promise.resolve({ ok: true, json: () => Promise.resolve(reply(url)) }); };
      w.XMLHttpRequest = function () {
        const x = this; x.readyState = 0;
        x.open = (m, u) => { x.m = m; x.u = u; };
        x.setRequestHeader = () => {};
        x.send = b => { reqs.push({ url: x.u, method: x.m, body: b }); setTimeout(() => { x.readyState = 4; x.status = 200; x.responseText = JSON.stringify(reply(x.u)); x.onreadystatechange(); }, 1); };
      };
    } });
  return { dom, w: dom.window, d: dom.window.document, reqs, nav };
}
const acorn = require('acorn');
function scripts(file) {
  // parsed as HTML (scripts not run), so the browser's own rules find the script elements
  const doc = new JSDOM(fs.readFileSync(W + file, 'utf8')).window.document;
  return Array.from(doc.querySelectorAll('script'), (el) => el.textContent);
}
function parses(code, version) {
  try { acorn.parse(code, { ecmaVersion: version }); return ''; } catch (e) { return e.message; }
}

(async () => {
  // the browser of older firmwares: plain ES5 only
  scripts('legacy.html').forEach((code, i) => { const e = parses(code, 5); ok(!e, 'legacy script ' + i + ' is ES5' + (e ? ': ' + e : '')); });
  // the head script of the main page runs everywhere before the redirect: ES5 too
  const main = scripts('index.html');
  ok(main.length === 2, 'index has the redirect script and the panel script');
  { const e = parses(main[0], 5); ok(!e, 'index redirect script is ES5' + (e ? ': ' + e : '')); }
  { const e = parses(main[1], 2017); ok(!e, 'index panel script is ES2017' + (e ? ': ' + e : '')); }

  // --- modern page, old browser: sent to /legacy
  let p = page('index.html', { dialog: false });
  await tick();
  ok(p.nav.length > 0, 'index without <dialog> navigates away (to /legacy)');
  ok(fs.readFileSync(W + 'index.html', 'utf8').includes("location.replace('/legacy')"), 'redirect target is /legacy');

  // --- modern page
  p = page('index.html', { dialog: true });
  let { w, d, reqs } = p;
  await tick();
  ok(p.nav.length === 0, 'index with <dialog> stays');
  ok(d.querySelectorAll('.card').length === 4, 'four game cards');
  const names = () => [...d.querySelectorAll('.card .name')].map(e => e.textContent).join('|');
  ok(names() === 'Civ VII|Killzone|N++|Soulstice', 'games in alphabetical order');
  const chip = t => [...d.querySelectorAll('#summary span')].find(e => e.textContent.indexOf(t) === 0);
  ok(chip('With DLC: 2') !== undefined, 'With DLC chip counts games with add-ons');
  w.games.push({ title_id: 'CUSA41712', name: 'Soulstice', version: '01.03', has_patch: false, size: 9e9, state: 'duplicate', mount: '', source: '/mnt/usb1/ps4/CUSA41712', dlc: 0, update_issue: '' }); w.render();
  ok(chip('Duplicates: 1') !== undefined && d.querySelector('.b-duplicate').textContent === 'Duplicate', 'duplicate chip and badge');
  w.games.pop(); w.render();
  w.games.push({ title_id: 'CUSA41713', name: 'Zed', version: '01.00', has_patch: false, size: 1e9, state: 'addons', mount: 'ro', source: '/mnt/usb0/ps4/CUSA41713', dlc: 2, update_issue: '' }); w.render();
  ok(d.querySelector('.b-addons').textContent === 'Installing add-ons' && !d.querySelector('[data-a="launch"][data-t="CUSA41713"]'), 'game with add-ons to install is not ready and has no Play');
  w.games.pop(); w.render();
  chip('With DLC').click(); ok(names() === 'Civ VII|Soulstice', 'With DLC filter');
  chip('Ready').click(); ok(names() === 'N++|Soulstice', 'Ready filter');
  chip('4 game(s)').click(); ok(d.querySelectorAll('.card').length === 4, 'all games again');
  ok(d.querySelector('[data-a="restore"][data-t="CUSA28732"]') !== null, 'running game has Move pkgs back');
  ok(d.querySelector('[data-a="restore"][data-t="CUSA00190"]') === null, 'internal game has no Move pkgs back');
  d.getElementById('settingsbtn').click(); await tick();
  ok(d.getElementById('setdlg').hasAttribute('open') && d.getElementById('s-rsize').value === '5', 'settings open with restore group 5');
  const rs = d.getElementById('s-rsize');
  rs.value = '12'; rs.dispatchEvent(new w.Event('input')); ok(rs.value === '10', 'restore group 12 capped to 10');
  const gs = d.getElementById('s-size');
  gs.value = '99'; gs.dispatchEvent(new w.Event('input')); ok(gs.value === '50', 'group size capped to 50');
  const kp = (el, ch) => { const e = new w.KeyboardEvent('keypress', { key: ch, charCode: ch.charCodeAt(0), cancelable: true }); el.dispatchEvent(e); return e.defaultPrevented; };
  ok(kp(gs, 'a') && kp(gs, '-') && kp(gs, 'e') && !kp(gs, '7'), 'group size: keys other than digits are blocked');
  ok(gs.getAttribute('inputmode') === 'numeric' && d.getElementById('s-rsize').getAttribute('inputmode') === 'numeric', 'numeric keyboard on both fields');
  ok(gs.type === 'number' && d.getElementById('s-rsize').type === 'number', 'both fields are type=number (numeric keyboard on the PS5)');
  gs.value = ''; reqs.length = 0; d.getElementById('setform').dispatchEvent(new w.Event('submit', { cancelable: true })); await tick();
  ok(reqs.length === 0 && d.getElementById('toast').textContent.includes('1 to 50'), 'empty group size refused');
  gs.value = '5';
  rs.value = '0'; reqs.length = 0; d.getElementById('s-restore').click(); await tick();
  ok(reqs.length === 0 && d.getElementById('toast').textContent.includes('1 to 10'), 'group 0 refused');
  rs.value = '3'; w.__confirm = false; d.getElementById('s-restore').click(); await tick();
  ok(reqs.length === 0, 'move all cancelled at the warning sends nothing');
  w.__confirm = true; d.getElementById('s-restore').click(); await tick();
  ok(reqs.length === 2 && reqs[0].url === '/api/config' && reqs[0].body === 'restore_batch=3', 'move all saves group 3 first');
  ok(reqs[1].url === '/api/restore' && reqs[1].method === 'POST' && reqs[1].body === undefined, 'then restores all (no body)');
  ok(!d.getElementById('setdlg').hasAttribute('open'), 'settings closed after starting');
  d.getElementById('settingsbtn').click(); await tick();
  rs.value = '1';
  d.getElementById('s-pick').click(); await tick();
  const rows = d.querySelectorAll('#p-rows tr');
  ok(d.getElementById('pickdlg').hasAttribute('open') && rows.length === 3, 'picker lists 3 games (internal left out)');
  ok(rows[0].textContent.includes('Game, update and 35 add-ons'), 'row says game, update and add-ons are moved back');
  ok(d.querySelector('#pickdlg .note').textContent.includes('all its add-ons'), 'picker note about add-ons');
  ok(d.getElementById('p-go').disabled, 'nothing selected: button disabled');
  const bx = d.querySelectorAll('#p-rows input');
  bx[0].checked = true; bx[2].checked = true; d.getElementById('p-rows').dispatchEvent(new w.Event('change', { bubbles: true }));
  ok(!d.getElementById('p-go').disabled && d.getElementById('p-info').textContent.includes('2 game(s) selected, removed from the library 1 at a time'), 'two selected, groups of 1 shown');
  reqs.length = 0; d.getElementById('p-go').click(); await tick();
  ok(reqs[0].body === 'restore_batch=1' && reqs[1].url === '/api/restore' && reqs[1].body === 'title_ids=CUSA28732,CUSA41712', 'selected restore sends the two title ids');
  ok(!d.getElementById('pickdlg').hasAttribute('open'), 'picker closed after starting');
  d.getElementById('settingsbtn').click(); await tick();
  d.getElementById('s-pick').click(); await tick();
  d.getElementById('p-all').checked = true; d.getElementById('p-all').dispatchEvent(new w.Event('change'));
  ok([...d.querySelectorAll('#p-rows input')].every(b => b.checked), 'select all checks every row');
  reqs.length = 0; rs.value = '7';
  d.getElementById('setform').dispatchEvent(new w.Event('submit', { cancelable: true })); await tick();
  ok(reqs[0] && reqs[0].url === '/api/config' && reqs[0].body.includes('restore_batch=7'), 'Save sends restore_batch');
  d.getElementById('settingsbtn').click(); await tick();
  ok(d.getElementById('s-dlccopy').checked, 'copy add-ons checked from the config');
  d.getElementById('s-dlccopy').checked = false; reqs.length = 0;
  d.getElementById('setform').dispatchEvent(new w.Event('submit', { cancelable: true })); await tick();
  ok(reqs[0] && reqs[0].body.includes('dlc_copy=0'), 'Save sends dlc_copy=0');

  // updates, add-ons and the play button
  ok(d.querySelector('.play[data-t="CUSA00791"]') !== null && d.querySelector('.play[data-t="CUSA28732"]') === null, 'play button on ready games only');
  ok(d.querySelector('[data-a="updates"][data-t="CUSA00791"]') !== null && d.querySelector('[data-a="updates"][data-t="CUSA41712"]') === null, 'Updates button only with two or more updates');
  ok(d.querySelector('[data-a="dlcs"][data-t="CUSA41712"]') !== null && d.querySelector('[data-a="dlcs"][data-t="CUSA00791"]') === null, 'Add-ons button only with add-ons');
  ok(d.querySelector('[data-a="dlcs"][data-t="CUSA28732"]').disabled, 'running game: add-ons cannot be changed');
  ok(d.querySelector('.card:nth-child(4) .meta').textContent.includes('1 of 3 add-ons'), 'card shows add-ons on of total');
  reqs.length = 0; d.querySelector('.play[data-t="CUSA00791"]').click(); await tick();
  ok(reqs[0].url === '/api/launch?title_id=CUSA00791' && reqs[0].method === 'POST', 'play starts the game');
  let asked = '';
  w.confirm = m => { asked = m; return true; };
  reqs.length = 0; d.querySelector('.play[data-t="CUSA41712"]').click(); await tick(); await tick();
  ok(asked.includes('Civ VII is open') && reqs[1] && reqs[1].url === '/api/launch?title_id=CUSA41712&force=1', 'another game open: asks, then closes it and starts');
  w.confirm = () => false;
  reqs.length = 0; d.querySelector('.play[data-t="CUSA41712"]').click(); await tick(); await tick();
  ok(reqs.filter(r => r.url.indexOf('/api/launch') === 0).length === 1, 'refused: the open game is left alone');
  w.confirm = () => (w.__confirm !== undefined ? w.__confirm : true);
  let alerted = '', stopAsked = '';
  w.alert = m => { alerted = m; };
  w.confirm = m => { stopAsked = m; return true; };
  stopOpen = 'Titanfall 2';
  reqs.length = 0; d.getElementById('stop').click(); await tick(); await tick();
  ok(alerted.includes('Close Titanfall 2') && !stopAsked && !reqs.some(r => r.url === '/api/stop'), 'stop with a game open: says to close it, without asking or stopping');
  stopOpen = ''; alerted = '';
  reqs.length = 0; d.getElementById('stop').click(); await tick(); await tick();
  ok(stopAsked.includes('rest mode') && !alerted && reqs.some(r => r.url === '/api/stop' && r.method === 'POST'), 'stop with no game open: asks, then stops');
  w.confirm = () => (w.__confirm !== undefined ? w.__confirm : true);
  d.querySelector('[data-a="updates"][data-t="CUSA00791"]').click(); await tick();
  ok(d.getElementById('upddlg').hasAttribute('open') && d.querySelectorAll('#u-rows tr').length === 2, 'updates dialog lists both');
  ok(d.getElementById('u-go').disabled, 'the update in use cannot be chosen again');
  d.querySelectorAll('#u-rows input')[1].checked = true; d.getElementById('u-rows').dispatchEvent(new w.Event('change', { bubbles: true }));
  reqs.length = 0; d.getElementById('u-go').click(); await tick();
  ok(reqs[0].url === '/api/update?title_id=CUSA41712' && reqs[0].body === 'file=replaced%2Fpatch-1.pkg', 'update choice sent');
  d.querySelector('[data-a="dlcs"][data-t="CUSA41712"]').click(); await tick();
  const db = d.querySelectorAll('#d-rows input');
  ok(db.length === 3 && db[0].checked && !db[1].checked, 'add-ons dialog shows which are on');
  const dr = d.querySelectorAll('#d-rows tr');
  ok(dr[0].textContent.includes('Installed') && dr[1].textContent.includes('Problem: copy stopped'), 'add-ons dialog shows the copy status');
  ok(dr[2].textContent.includes('Not installed'), 'add-ons dialog shows an add-on not installed');
  ok(db[2].disabled && !db[1].disabled && d.getElementById('d-info').textContent.includes('limit reached'), 'past the limit no other add-on with content can be turned on, unlock-only ones can');
  db[1].checked = true; d.getElementById('d-rows').dispatchEvent(new w.Event('change', { bubbles: true }));
  ok(db[2].disabled && d.getElementById('d-info').textContent.includes('2 of 3 on, 1 of 1 with content'), 'unlock-only add-ons do not count for the limit');
  reqs.length = 0; d.getElementById('d-go').click(); await tick();
  ok(reqs[0].url === '/api/dlc?title_id=CUSA41712' &&
     reqs[0].body === 'on=' + encodeURIComponent('EP0001-CUSA41712_00-AAAAAAAAAAAAAAAA,EP0001-CUSA41712_00-BBBBBBBBBBBBBBBB'), 'add-on choice sent');
  db[0].checked = false; db[1].checked = false; d.getElementById('d-all').checked = true;
  d.getElementById('d-all').dispatchEvent(new w.Event('change'));
  ok(db[0].checked && db[1].checked && !db[2].checked, 'select all: every unlock-only add-on, add-ons with content up to the limit');

  // --- legacy page
  p = page('legacy.html', {}); ({ w, d, reqs } = p);
  await tick();
  ok(d.querySelectorAll('.card').length === 4 && d.getElementById('ver').textContent === 'PS4Mount test', 'legacy lists the games');
  const lnames = () => [].map.call(d.querySelectorAll('.card .name'), x => x.textContent).join('|');
  ok(lnames() === 'Civ VII|Killzone|N++|Soulstice', 'legacy games in alphabetical order');
  const lchip = t => [].filter.call(d.querySelectorAll('#summary span'), x => x.textContent.indexOf(t) === 0)[0];
  w.games.push({ title_id: 'CUSA41712', name: 'Soulstice', version: '01.03', has_patch: false, size: 9e9, state: 'duplicate', mount: '', source: '/mnt/usb1/ps4/CUSA41712', dlc: 0, update_issue: '' }); w.render();
  ok(lchip('Duplicates: 1') !== undefined && d.querySelector('.b-duplicate').textContent === 'Duplicate', 'legacy duplicate chip and badge');
  w.games.pop(); w.render();
  w.games.push({ title_id: 'CUSA41713', name: 'Zed', version: '01.00', has_patch: false, size: 1e9, state: 'addons', mount: 'ro', source: '/mnt/usb0/ps4/CUSA41713', dlc: 2, update_issue: '' }); w.render();
  ok(d.querySelector('.b-addons').textContent === 'Installing add-ons' && !d.querySelector('[data-a="launch"][data-t="CUSA41713"]'), 'legacy game with add-ons to install is not ready and has no Play');
  w.games.pop(); w.render();
  lchip('With DLC').click(); ok(lnames() === 'Civ VII|Soulstice', 'legacy With DLC filter');
  lchip('Ready').click(); ok(lnames() === 'N++|Soulstice', 'legacy Ready filter');
  lchip('4 game(s)').click(); ok(d.querySelectorAll('.card').length === 4, 'legacy all games again');
  ok(!/=>|\blet\b|\bconst\b|`|fetch\(|showModal|<dialog/.test(fs.readFileSync(W + 'legacy.html', 'utf8').split('<script>')[1]), 'legacy script is ES5 without fetch or dialog');
  d.getElementById('nav-settings').click(); await tick();
  ok(d.getElementById('v-settings').className.includes('on') && d.getElementById('s-rsize').value === '5', 'legacy settings page with group 5');
  const lr = d.getElementById('s-rsize');
  lr.value = '15'; lr.onkeyup(); ok(lr.value === '10', 'legacy caps the restore group to 10');
  const lg = d.getElementById('s-size');
  ok(lg.onkeypress({ charCode: 98, preventDefault() {} }) === false && lg.onkeypress({ charCode: 52 }) !== false, 'legacy blocks a letter key, lets a digit through');
  ok(lg.getAttribute('inputmode') === 'numeric' && d.getElementById('s-rsize').getAttribute('inputmode') === 'numeric', 'legacy numeric keyboard on both fields');
  ok(lg.type === 'number' && d.getElementById('s-rsize').type === 'number', 'legacy fields are type=number');
  lg.value = '0'; reqs.length = 0; d.getElementById('s-save').click(); await tick();
  ok(reqs.length === 0 && d.body.textContent.includes('1 to 50'), 'legacy group size 0 refused');
  lg.value = '5';
  lr.value = '2'; reqs.length = 0; d.getElementById('s-restore').click(); await tick();
  ok(reqs[0].body === 'restore_batch=2' && reqs[1].url === '/api/restore' && reqs[1].body === '', 'legacy move all: group 2 then restore all');
  d.getElementById('nav-settings').click(); await tick();
  ok(d.getElementById('s-dlccopy').checked, 'legacy copy add-ons checked from the config');
  d.getElementById('s-dlccopy').checked = false; reqs.length = 0; d.getElementById('s-save').click(); await tick();
  ok(reqs[0] && reqs[0].body.includes('dlc_copy=0'), 'legacy Save sends dlc_copy=0');
  d.getElementById('nav-settings').click(); await tick();
  d.getElementById('s-rsize').value = '1';
  d.getElementById('s-pick').click(); await tick();
  ok(d.getElementById('v-pick').className.includes('on') && d.querySelectorAll('#p-rows tr').length === 3, 'legacy picker page with 3 games');
  const lb = d.querySelectorAll('#p-rows input'); lb[1].click();
  lb[2].click();
  ok(d.getElementById('p-info').textContent.includes('2 game(s) selected, removed from the library 1 at a time'), 'legacy picker counts and groups');
  reqs.length = 0; d.getElementById('p-go').click(); await tick();
  ok(reqs[1].body === 'title_ids=CUSA00791,CUSA41712', 'legacy selected restore sends title ids');
  d.getElementById('nav-log').click(); await tick();
  ok(d.getElementById('v-log').className.includes('on') && d.getElementById('log').textContent === 'a\nb', 'legacy log page');
  d.getElementById('l-back').click();
  reqs.length = 0; d.querySelector('[data-a="unmount"][data-t="CUSA00791"]').click(); await tick();
  ok(reqs[0].url === '/api/unmount?title_id=CUSA00791' && reqs[0].method === 'POST', 'legacy card action posts');
  ok(d.querySelector('.play[data-t="CUSA00791"]') !== null && d.querySelector('.play[data-t="CUSA28732"]') === null, 'legacy play button on ready games only');
  ok(d.querySelector('[data-a="updates"][data-t="CUSA00791"]') !== null && d.querySelector('[data-a="updates"][data-t="CUSA41712"]') === null, 'legacy Updates button only with two or more updates');
  ok(d.querySelector('[data-a="dlcs"][data-t="CUSA41712"]') !== null && d.querySelector('[data-a="dlcs"][data-t="CUSA00791"]') === null, 'legacy Add-ons button only with add-ons');
  ok(d.querySelector('[data-a="dlcs"][data-t="CUSA28732"]').disabled, 'legacy running game: add-ons cannot be changed');
  ok(d.querySelectorAll('.card')[3].textContent.includes('1 of 3 add-ons'), 'legacy card shows add-ons on of total');
  reqs.length = 0; d.querySelector('.play[data-t="CUSA00791"] .tri').click(); await tick();
  ok(reqs[0] && reqs[0].url === '/api/launch?title_id=CUSA00791' && reqs[0].method === 'POST', 'legacy play starts the game');
  let lasked = '';
  w.confirm = m => { lasked = m; return true; };
  reqs.length = 0; d.querySelector('.play[data-t="CUSA41712"] .tri').click(); await tick(); await tick();
  ok(lasked.includes('Civ VII is open') && reqs[1] && reqs[1].url === '/api/launch?title_id=CUSA41712&force=1', 'legacy: another game open: asks, then closes it and starts');
  w.confirm = () => false;
  reqs.length = 0; d.querySelector('.play[data-t="CUSA41712"] .tri').click(); await tick(); await tick();
  ok(reqs.filter(r => r.url.indexOf('/api/launch') === 0).length === 1, 'legacy refused: the open game is left alone');
  w.confirm = () => (w.__confirm !== undefined ? w.__confirm : true);
  let lalerted = '', lstopAsked = '';
  w.alert = m => { lalerted = m; };
  w.confirm = m => { lstopAsked = m; return true; };
  stopOpen = 'Titanfall 2';
  reqs.length = 0; d.getElementById('stop').click(); await tick(); await tick();
  ok(lalerted.includes('Close Titanfall 2') && !lstopAsked && !reqs.some(r => r.url === '/api/stop'), 'legacy: stop with a game open: says to close it, without asking or stopping');
  stopOpen = ''; lalerted = '';
  reqs.length = 0; d.getElementById('stop').click(); await tick(); await tick();
  ok(lstopAsked.includes('rest mode') && !lalerted && reqs.some(r => r.url === '/api/stop' && r.method === 'POST'), 'legacy: stop with no game open: asks, then stops');
  w.confirm = () => (w.__confirm !== undefined ? w.__confirm : true);
  d.querySelector('[data-a="updates"][data-t="CUSA00791"]').click(); await tick();
  ok(d.getElementById('v-upd').className.includes('on') && d.querySelectorAll('#u-rows tr').length === 2, 'legacy updates page lists both');
  ok(d.getElementById('u-go').disabled, 'legacy: the update in use cannot be chosen again');
  d.querySelectorAll('#u-rows input')[1].click();
  ok(!d.getElementById('u-go').disabled, 'legacy: another update can be chosen');
  reqs.length = 0; d.getElementById('u-go').click(); await tick();
  ok(reqs[0].url === '/api/update?title_id=CUSA41712' && reqs[0].body === 'file=replaced%2Fpatch-1.pkg', 'legacy update choice sent');
  d.querySelector('[data-a="dlcs"][data-t="CUSA41712"]').click(); await tick();
  const ldb = d.querySelectorAll('#d-rows input');
  ok(d.getElementById('v-dlc').className.includes('on') && ldb.length === 3 && ldb[0].checked && !ldb[1].checked, 'legacy add-ons page shows which are on');
  const ldr = d.querySelectorAll('#d-rows tr');
  ok(ldr[0].textContent.includes('Installed') && ldr[1].textContent.includes('Problem: copy stopped'), 'legacy add-ons page shows the copy status');
  ok(ldr[2].textContent.includes('Not installed'), 'legacy add-ons page shows an add-on not installed');
  ok(ldb[2].disabled && !ldb[1].disabled && d.getElementById('d-info').textContent.includes('limit reached'), 'legacy: past the limit no other add-on with content can be turned on, unlock-only ones can');
  ldb[1].click();
  ok(ldb[2].disabled && d.getElementById('d-info').textContent.includes('2 of 3 on, 1 of 1 with content'), 'legacy: unlock-only add-ons do not count for the limit');
  reqs.length = 0; d.getElementById('d-go').click(); await tick();
  ok(reqs[0].url === '/api/dlc?title_id=CUSA41712' &&
     reqs[0].body === 'on=' + encodeURIComponent('EP0001-CUSA41712_00-AAAAAAAAAAAAAAAA,EP0001-CUSA41712_00-BBBBBBBBBBBBBBBB'), 'legacy add-on choice sent');
  ldb[0].checked = false; ldb[1].checked = false; d.getElementById('d-all').checked = true;
  d.getElementById('d-all').onclick.call(d.getElementById('d-all'));
  ok(ldb[0].checked && ldb[1].checked && !ldb[2].checked, 'legacy select all: every unlock-only add-on, add-ons with content up to the limit');
})();
