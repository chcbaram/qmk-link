import { USAGE, NAME_OF } from './usage-table.js';
import { PRESETS } from './presets.js';

// ── qmk-link raw HID 명령 (firmware/.../link_cmd.h 와 같아야 한다) ──
const VID = 0x0483, PID = 0x5305;
const CMD_PREFIX  = 0xA0;
const TREE_VIA    = 0;
const TREE_VIAL   = 1;
const CMD_INFO    = 0x00;
const CMD_PRESSED = 0x01;
const REPORT_LEN  = 32;

let device = null;
let pending = null;          // 응답을 기다리는 resolve
let kle = null;              // 파싱한 KLE
let keys = [];              // {x,y,w,h,usage|null,label}
let cursor = -1;             // 마법사가 가리키는 키
let prevPressed = new Set(); // 직전에 눌려 있던 usage
let paused = false;          // 탭이 가려지면 멈춘다
let firmware = null;         // 'via' | 'vial'

// ★ 펌웨어에 따라 쓸 수 없는 것이 있다.
//
//   내려받는 정의가 다르다 — VIA 는 layout-via.json 을 Design 탭에 넣고,
//   Vial 은 장치가 정의를 직접 내주므로 그 파일이 지금은 쓸모가 없다.
//   (보드에 담는 것은 09단계 2단계에서 한다)
function applyFirmware(vialLocked) {
  const isVial = firmware === 'vial';

  $('exVia').style.display  = isVial ? 'none' : '';
  $('exVial').style.display = isVial ? '' : 'none';

  $('fwNote').textContent = isVial
    ? 'Vial 펌웨어다. Vial 은 정의를 장치에서 읽어가므로 vial.json 을 앱에 넣을 일이 없다 — '
      + '지금은 저장소 워크플로용으로만 받는다.'
      + (vialLocked ? '  (Vial 잠금 상태 — 매크로 편집 등은 좌우 Shift 5초로 풀어야 한다. 키 읽기는 잠겨도 된다)' : '')
    : 'VIA 펌웨어다. layout-via.json 을 VIA 의 Design 탭에 넣는다.';
  $('fwNote').style.display = '';
}

const $ = (id) => document.getElementById(id);
const log = (msg) => { $('log').textContent = msg; };

// ── 장치 ────────────────────────────────────────────────
async function connect() {
  const list = await navigator.hid.requestDevice({
    filters: [{ vendorId: VID, productId: PID, usagePage: 0xFF60 }],
  });
  if (!list.length) { log('장치를 고르지 않았다'); return; }

  device = list[0];
  if (!device.opened) await device.open();

  device.addEventListener('inputreport', (e) => {
    if (!pending) return;
    const r = pending; pending = null;
    r(new Uint8Array(e.data.buffer));
  });

  const info = await send(CMD_INFO);
  const ver = info[2], tree = info[3], locked = info[4];
  const rows = info[5], cols = info[6], n = info[7];

  let kbds = [];
  for (let i = 0; i < n; i++) {
    const o = 8 + i * 4;
    kbds.push(hex4(info[o] | (info[o+1] << 8)) + ':' + hex4(info[o+2] | (info[o+3] << 8)));
  }

  firmware = (tree === TREE_VIAL) ? 'vial' : 'via';
  $('dev').textContent =
    `${firmware.toUpperCase()} 펌웨어 — 매트릭스 ${rows}x${cols}, 꽂힌 키보드 ${n}대`
    + (kbds.length ? ` (${kbds.join(', ')})` : '');
  $('dev').className = 'ok';

  applyFirmware(locked !== 0);

  if (n === 0) log('★ USB-A 쪽에 키보드가 안 꽂혀 있다. 꽂아야 키를 배울 수 있다.');
  else log('배열을 넣고 [마법사 시작] 을 누른다.');

  poll();
}

function hex4(v) { return v.toString(16).toUpperCase().padStart(4, '0'); }

async function disconnect() {
  const d = device;
  device = null;                 // poll 루프가 스스로 끝난다
  paused = false;
  await sleep(60);
  try { if (d && d.opened) await d.close(); } catch { /* 무시 */ }
  $('dev').textContent = '연결 끊김 — VIA / Vial 을 써도 된다';
  $('dev').className = '';
  $('pressed').textContent = '(없음)';
  log('연결을 끊었다. 다시 배우려면 [보드 연결].');
}

document.addEventListener('visibilitychange', () => {
  paused = document.hidden;
  if (device) {
    $('dev').className = paused ? '' : 'ok';
    if (paused) $('pressed').textContent = '(탭이 가려져 멈춤)';
  }
});

function send(sub, ...args) {
  const buf = new Uint8Array(REPORT_LEN);
  buf[0] = CMD_PREFIX; buf[1] = sub;
  args.forEach((v, i) => buf[2 + i] = v);
  return new Promise((resolve, reject) => {
    pending = resolve;
    device.sendReport(0, buf).catch(reject);
    setTimeout(() => { if (pending === resolve) { pending = null; reject(new Error('응답 없음')); } }, 1000);
  });
}

// ── 눌린 키 폴링 ────────────────────────────────────────
async function poll() {
  while (device) {
    // ★ 탭이 가려지면 폴링을 멈춘다.
    //
    //   VIA / Vial 도 같은 raw HID 를 쓴다. 우리가 계속 물어보고 있으면
    //   저쪽이 보낸 질문의 답을 우리가 가로채고, VIA 는 "VIA 키보드처럼
    //   응답하지 않는다" 고 판단한다. 탭을 옮기는 순간 조용해져야 한다.
    if (paused) { await sleep(200); continue; }

    let r;
    try { r = await send(CMD_PRESSED); }
    catch { await sleep(200); continue; }

    const cur = new Set(r.slice(3, 3 + r[2]));
    $('pressed').textContent = cur.size
      ? [...cur].map(u => '0x' + u.toString(16).toUpperCase().padStart(2, '0')
                        + (NAME_OF[u] ? ` (${NAME_OF[u]})` : '')).join('   ')
      : '(없음)';

    // 새로 눌린 것만 마법사에 먹인다
    for (const u of cur) if (!prevPressed.has(u)) onKeyDown(u);
    prevPressed = cur;

    await sleep(20);
  }
}
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

// ── KLE ────────────────────────────────────────────────
function parseKle(text) {
  let t = text.trim();
  if (!t.startsWith('[')) throw new Error('KLE raw 데이터가 아니다');
  // KLE raw 는 바깥 대괄호가 없는 형태로 복사되기도 한다
  let arr;
  try { arr = JSON.parse(t); }
  catch { arr = JSON.parse('[' + t + ']'); }
  if (!Array.isArray(arr[0])) arr = [arr];

  const out = [];
  let y = 0;
  for (const row of arr) {
    let x = 0, w = 1, h = 1;
    for (const it of row) {
      if (typeof it === 'object') {
        if ('x' in it) x += it.x;
        if ('y' in it) y += it.y;
        if ('w' in it) w = it.w;
        if ('h' in it) h = it.h;
        continue;
      }
      out.push({ x, y, w, h, label: String(it).split('\n')[0], usage: null });
      x += w; w = 1; h = 1;
    }
    y += 1;
  }
  return out;
}

// ── 프리셋 · 파일 ──────────────────────────────────────
function fillPresets() {
  const sel = $('preset');
  PRESETS.forEach((p, i) => {
    const o = document.createElement('option');
    o.value = String(i); o.textContent = p.name;
    sel.appendChild(o);
  });
  sel.onchange = () => {
    const p = PRESETS[Number(sel.value)];
    if (!p) return;
    $('kle').value = JSON.stringify(p.layout, null, 0).slice(1, -1).replace(/\],\[/g, '],\n[');
    loadKle();
    // 이름 칸도 채운다 — 내려받는 파일명이 키보드마다 구별된다
    $('name').value = p.name.replace(/\s*\(\d+\)$/, '');
    log(`${p.name} 을 넣었다. [마법사 시작] 을 누른다.`);
  };
}

function openFile() { $('file').click(); }

$('file') && ($('file').onchange = async (e) => {
  const f = e.target.files[0];
  if (!f) return;
  const text = await f.text();
  // KLE raw · 우리 layout-kle.json · VIA/Vial 정의 셋 다 받는다
  let body = text;
  try {
    const j = JSON.parse(text);
    if (j.layout) body = JSON.stringify(j.layout);                       // layout-kle.json
    else if (j.layouts && j.layouts.keymap) body = JSON.stringify(j.layouts.keymap);  // via / vial
  } catch { /* KLE raw 는 그대로 둔다 */ }
  $('kle').value = body;
  loadKle();
  log(`${f.name} 을 읽었다.`);
});

function loadKle() {
  try {
    keys = parseKle($('kle').value);
    cursor = -1;
    render();
    log(`${keys.length} 키를 읽었다. [마법사 시작] 을 누르고 강조된 자리의 키를 누른다.`);
  } catch (e) { log('KLE 읽기 실패 — ' + e.message); }
}

// ── 그리기 ──────────────────────────────────────────────
const U = 46;
function render() {
  const box = $('board');
  box.innerHTML = '';
  let maxx = 0, maxy = 0;
  keys.forEach((k, i) => {
    const d = document.createElement('div');
    d.className = 'key' + (i === cursor ? ' cur' : '') + (k.usage !== null ? ' done' : '');
    d.style.left = k.x * U + 'px';
    d.style.top = k.y * U + 'px';
    d.style.width = k.w * U - 4 + 'px';
    d.style.height = k.h * U - 4 + 'px';
    d.textContent = k.usage !== null ? (NAME_OF[k.usage] || '0x' + k.usage.toString(16)) : k.label;
    d.title = k.usage !== null ? '0x' + k.usage.toString(16).toUpperCase() : '아직 안 배움';
    d.onclick = () => { cursor = i; render(); log('이 자리의 키를 누른다. 못 누르는 키면 아래에서 직접 고른다.'); };
    box.appendChild(d);
    maxx = Math.max(maxx, k.x + k.w); maxy = Math.max(maxy, k.y + k.h);
  });
  box.style.width = maxx * U + 'px';
  box.style.height = maxy * U + 'px';
  $('progress').textContent = `${keys.filter(k => k.usage !== null).length} / ${keys.length}`;
}

// ── 마법사 ──────────────────────────────────────────────
function onKeyDown(u) {
  if (cursor < 0 || cursor >= keys.length) return;

  const dup = keys.findIndex((k, i) => k.usage === u && i !== cursor);
  if (dup >= 0) {
    log(`0x${u.toString(16).toUpperCase()} 는 이미 다른 자리에 있다. 그쪽을 비우고 다시 누른다.`);
    keys[dup].usage = null;
  }
  keys[cursor].usage = u;
  next();
}

function next() {
  let i = cursor + 1;
  while (i < keys.length && keys[i].usage !== null) i++;
  cursor = i < keys.length ? i : -1;
  render();
  if (cursor < 0) log('전부 배웠다. 아래에서 내려받는다.');
}

function startWizard() {
  if (!keys.length) { log('먼저 KLE 를 읽는다.'); return; }
  cursor = keys.findIndex(k => k.usage === null);
  render();
  log(cursor < 0 ? '이미 다 배웠다.' : '강조된 자리의 키를 누른다. 건너뛰려면 [건너뛰기].');
}

function skip() { if (cursor >= 0) { cursor++; next(); } }

function manualAssign() {
  const name = $('manual').value.trim().toUpperCase();
  if (cursor < 0) { log('먼저 자리를 고른다 (키를 클릭).'); return; }
  if (!(name in USAGE)) { log(`모르는 이름: ${name}`); return; }
  onKeyDown(USAGE[name]);
}

// ── 내보내기 ────────────────────────────────────────────
function buildLayout() {
  // KLE 를 다시 조립하되 범례를 "row,col" 로 바꾼다
  const rows = [];
  let cur = [], prevY = null, prevX = 0;
  for (const k of keys) {
    if (k.usage === null) continue;
    if (prevY === null || k.y !== prevY) {
      if (cur.length) rows.push(cur);
      cur = []; prevX = 0;
      if (prevY !== null && k.y - prevY !== 1) cur.push({ y: k.y - prevY - 1 });
      prevY = k.y;
    }
    if (k.x !== prevX) { cur.push({ x: k.x - prevX }); prevX = k.x; }
    if (k.w !== 1) cur.push({ w: k.w });
    if (k.h !== 1) cur.push({ h: k.h });
    cur.push(`${k.usage >> 4},${k.usage & 0x0F}`);
    prevX += k.w;
  }
  if (cur.length) rows.push(cur);
  return rows;
}

// "HHKB Lite 2" -> "hhkb-lite-2-". 내려받는 파일명 앞에 붙는다.
function slug() {
  const v = ($('name').value || '').trim().toLowerCase()
    .replace(/[^a-z0-9가-힣]+/g, '-').replace(/^-+|-+$/g, '');
  return v ? v + '-' : '';
}

function download(name, obj) {
  const blob = new Blob([JSON.stringify(obj, null, 2) + '\n'], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob); a.download = name; a.click();
}

function exportVia() {
  download(slug() + 'layout-via.json', {
    name: $('name').value || 'QMK-LINK VIA',
    vendorId: '0x0483', productId: '0x5305',
    matrix: { rows: 16, cols: 16 },
    layouts: { keymap: buildLayout() },
  });
}
function exportVial() {
  download(slug() + 'vial.json', {
    name: $('name').value ? $('name').value.replace(' VIA', ' VIAL') : 'QMK-LINK VIAL',
    vendorId: '0x0483', productId: '0x5305', lighting: 'none',
    matrix: { rows: 16, cols: 16 },
    layouts: { keymap: buildLayout() },
  });
}
function exportKle() {
  // 저장소 워크플로용 — 범례가 '키 이름' 이라 사람이 읽고 고칠 수 있다
  const rows = buildLayout().map(row => row.map(it =>
    typeof it === 'string'
      ? (NAME_OF[(parseInt(it.split(',')[0]) << 4) | parseInt(it.split(',')[1])] || it)
      : it));
  download(slug() + 'layout-kle.json', { _comment: ['웹 마법사가 만들었다. gen_keymap.py 의 입력이다.'], layout: rows });
}

fillPresets();
$('loadFile').onclick = openFile;
$('connect').onclick = connect;
$('disconnect').onclick = disconnect;
$('load').onclick = loadKle;
$('start').onclick = startWizard;
$('skip').onclick = skip;
$('assign').onclick = manualAssign;
$('exVia').onclick = exportVia;
$('exVial').onclick = exportVial;
$('exKle').onclick = exportKle;

if (!('hid' in navigator)) {
  $('dev').textContent = '이 브라우저는 WebHID 를 지원하지 않는다 — Chrome / Edge 를 쓴다';
  $('dev').className = 'bad';
}
