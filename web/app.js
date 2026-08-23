import { USAGE, NAME_OF } from './usage-table.js';
import { PRESETS } from './presets.js';

// ── qmk-link raw HID 명령 (firmware/.../link_cmd.h 와 같아야 한다) ──
const VID = 0x0483;

// ★ VID 만 보면 안 된다.
//
//   0x0483 은 baram 키보드들이 같이 쓴다. 다른 보드도 usage page 0xFF60 짜리
//   raw HID 를 갖고 있어서, VID 만 걸러면 장치 선택 창에 그것들이 같이 뜨고
//   잘못 고르면 "모르는 명령(0xFF)" 만 돌아온다. 실제로 파이썬 도구가 그렇게
//   wish-he 를 열었다.
//
//   0x5305 는 지금 PID, 0x5400~0x540F 는 저장된 레이아웃마다 바뀔 PID 다.
const PID_LIST = [0x5305];
for (let i = 0; i < 16; i++) PID_LIST.push(0x5400 + i);
const CMD_PREFIX  = 0xA0;
const TREE_VIA    = 0;
const TREE_VIAL   = 1;
const CMD_INFO    = 0x00;
const CMD_PRESSED = 0x01;
const CMD_SLOT_INFO = 0x02;
const REPORT_LEN  = 32;

// 저장된 레이아웃 칸마다 보드가 다르게 보고할 PID. 0x5400 + 칸.
// firmware/qmk-link/src/ap/modules/link/link_cmd.h 의 LINK_PID_BASE 와 같아야 한다.
const PID_BASE   = 0x5400;
const SLOT_MAX   = 16;

let device = null;
let pending = null;          // 응답을 기다리는 resolve
let kle = null;              // 파싱한 KLE
let keys = [];              // {x,y,w,h,usage|null,label}
let cursor = -1;             // 마법사가 가리키는 키
let prevPressed = new Set(); // 직전에 눌려 있던 usage
let paused = false;          // 탭이 가려지면 멈춘다
let firmware = null;         // 'via' | 'vial'
let slots = null;            // [{used, name}] — 연결했을 때만 채워진다

// ★ 펌웨어에 따라 쓸 수 없는 것이 있다.
//
//   내려받는 정의가 다르다 — VIA 는 layout-via.json 을 Design 탭에 넣고,
//   Vial 은 장치가 정의를 직접 내주므로 그 파일이 지금은 쓸모가 없다.
//   (보드에 담는 것은 09단계 2단계에서 한다)
const DEFAULT_NAMES = ['QMK-LINK VIA', 'QMK-LINK VIAL'];

function applyFirmware(vialLocked) {
  const isVial = firmware === 'vial';

  // 이름 칸이 아직 기본값이면 펌웨어에 맞춰 준다.
  // 프리셋을 골랐거나 손으로 고쳤으면 건드리지 않는다.
  if (DEFAULT_NAMES.includes($('name').value.trim())) {
    $('name').value = 'QMK-LINK ' + (isVial ? 'VIAL' : 'VIA');
  }

  $('exVia').style.display  = isVial ? 'none' : '';
  $('exVial').style.display = isVial ? '' : 'none';

  $('fwNote').textContent = isVial
    ? 'Vial 펌웨어 — Vial 은 정의를 장치에서 읽어가므로 앱에 파일을 넣을 일이 없다. '
      + 'layout-vial.json 은 저장소에 두거나 나중에 보드에 담을 때 쓴다.'
      + (vialLocked ? '  (Vial 잠금 상태 — 매크로 편집 등은 좌우 Shift 5초로 푼다. 키 읽기는 잠겨도 된다)' : '')
    : 'VIA 펌웨어 — layout-via.json 을 VIA 의 Design 탭에 넣는다.';
  $('fwNote').style.display = '';
}

const $ = (id) => document.getElementById(id);
const log = (msg) => { $('log').textContent = msg; };

// ── 장치 ────────────────────────────────────────────────
async function connect() {
  const list = await navigator.hid.requestDevice({
    filters: PID_LIST.map(pid => ({ vendorId: VID, productId: pid, usagePage: 0xFF60 })),
  });
  if (!list.length) { log('장치를 고르지 않았다'); return; }

  device = list[0];
  if (!device.opened) await device.open();

  device.addEventListener('inputreport', (e) => {
    if (!pending) return;
    const r = new Uint8Array(e.data.buffer);
    if (r[0] !== pending.want[0] || r[1] !== pending.want[1]) return;   // 남의 응답
    const done = pending.resolve; pending = null;
    done(r);
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

  // ★ poll() 을 시작하기 **전에** 읽는다.
  //   send() 는 응답 대기가 하나뿐이라 폴링과 섞이면 서로 답을 가로챈다.
  //   버전 3 부터 INFO[28] 이 "지금 고른 SLOT" 이다.
  await loadSlots(ver >= 3 ? info[28] : 0xFF);

  if (n === 0) log('★ USB-A 쪽에 키보드가 안 꽂혀 있다. 꽂아야 키를 배울 수 있다.');
  else log('배열을 넣고 [마법사 시작] 을 누른다.');

  poll();
}

// ── 보드의 레이아웃 SLOT ────────────────────────────────
//
// ★ 왜 내보내기 옆에서 SLOT 을 고르게 하나
//
//   VIA 는 정의를 VID/PID 로 찾는다. 보드는 꽂힌 키보드가 몇 번 SLOT 에 담겨
//   있는지에 따라 PID 를 0x5400 + SLOT 으로 보고한다. 그래서 내보내는 JSON 의
//   productId 가 **담을 SLOT** 과 맞아야 VIA 가 그 정의를 고른다.
//   어긋나면 VIA 가 "모르는 키보드" 로 본다 — 조용히 틀리는 종류다.
async function loadSlots(activeSlot) {
  slots = [];
  for (let i = 0; i < SLOT_MAX; i++) {
    let r;
    try { r = await send(CMD_SLOT_INFO, i); } catch { slots = null; break; }
    const used = (r[2] === 0 && r[3] !== 0);
    const name = used
      ? new TextDecoder().decode(r.slice(10, 32)).split('\0')[0]
      : '';
    slots.push({ used, name });
  }
  fillSlots(activeSlot);
}

function fillSlots(activeSlot) {
  const sel = $('slot');
  const keep = sel.value;

  sel.innerHTML = '';
  sel.appendChild(new Option('담지 않음 (PID 5305)', '-1'));
  for (let i = 0; i < SLOT_MAX; i++) {
    const s = slots && slots[i];
    const tag = !slots ? '' : (s.used ? ` — ${s.name || '이름 없음'}` : ' — 비어 있음');
    sel.appendChild(new Option(`SLOT ${i} (PID ${hex4(PID_BASE + i)})${tag}`, String(i)));
  }

  // 고르는 순서
  //   1. 지금 꽂힌 키보드가 이미 쓰고 있는 SLOT (보드가 알려 준다)
  //   2. 첫 빈 SLOT
  //   3. 사용자가 직전에 고른 것 — 연결 전 목록을 다시 그릴 때다
  let want;

  if (activeSlot !== undefined && activeSlot < SLOT_MAX) want = String(activeSlot);
  else if (slots) {
    const empty = slots.findIndex((s) => !s.used);
    want = String(empty < 0 ? 0 : empty);
  }
  else want = (keep !== '' ? keep : '-1');

  sel.value = want;
  slotChanged();
}

function slotPid() {
  const v = parseInt($('slot').value, 10);
  return (v >= 0 && v < SLOT_MAX) ? (PID_BASE + v) : 0x5305;
}

function slotChanged() {
  const v = parseInt($('slot').value, 10);
  const note = $('slotNote');

  if (v < 0) {
    note.innerHTML = '보드에 담지 않는다. <code>productId</code> 는 <code>0x5305</code> 그대로다 — '
      + '맞는 레이아웃이 없을 때 보드가 보고하는 값이다.';
    return;
  }

  const file = slug() + 'layout-vial.json';
  const name = ($('name').value || '').trim();
  note.innerHTML = `<code>productId</code> 를 <code>0x${hex4(PID_BASE + v)}</code> 로 적는다. `
    + '보드에 담아야 그 PID 로 보고한다:'
    + `<br><code>python3 tools/kbd_upload.py put ${v} ${file} --name "${name}"</code>`
    + '<br>담는 파일은 두 펌웨어 모두 <code>layout-vial.json</code> 이다 — '
    + 'VIA 펌웨어는 내용을 안 쓰고 이 SLOT 의 vid/pid 만 본다 (그게 PID 전환의 열쇠다).';
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
  firmware = null;
  slots = null;
  fillSlots();
  $('exVia').style.display = 'none';
  $('exVial').style.display = 'none';
  $('fwNote').textContent = '보드를 연결하면 펌웨어에 맞는 내보내기가 나온다.';
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
    // ★ 응답을 대조한다.
    //
    //   같은 raw HID 를 VIA · Vial · 이 페이지가 나눠 쓴다. 다른 쪽이 물어본
    //   답이 우리에게 배달될 수 있다. 앞 두 바이트가 맞는 것만 받는다.
    pending = { want: [CMD_PREFIX, sub], resolve };
    device.sendReport(0, buf).catch(reject);
    setTimeout(() => {
      if (pending && pending.resolve === resolve) { pending = null; reject(new Error('응답 없음')); }
    }, 1000);
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

// 잘못 배운 자리를 되돌린다. 커서는 그대로 둬서 바로 다시 누를 수 있게 한다.
function clearCur() {
  if (cursor < 0 || cursor >= keys.length) { log('먼저 자리를 고른다 (키를 클릭).'); return; }

  const had = keys[cursor].usage;
  keys[cursor].usage = null;
  render();
  log(had === null ? '이미 비어 있다.'
                   : `0x${had.toString(16).toUpperCase()} 를 비웠다. 이 자리의 키를 다시 누른다.`);
}

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
    vendorId: '0x0483', productId: '0x' + hex4(slotPid()),
    matrix: { rows: 16, cols: 16 },
    layouts: { keymap: buildLayout() },
  });
}
function exportVial() {
  download(slug() + 'layout-vial.json', {
    name: $('name').value || 'QMK-LINK VIAL',
    vendorId: '0x0483', productId: '0x' + hex4(slotPid()), lighting: 'none',
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
fillSlots();
$('slot').onchange = slotChanged;
$('name').addEventListener('input', slotChanged);
$('loadFile').onclick = openFile;
$('connect').onclick = connect;
$('disconnect').onclick = disconnect;
$('load').onclick = loadKle;
$('start').onclick = startWizard;
$('skip').onclick = skip;
$('clear').onclick = clearCur;
$('assign').onclick = manualAssign;
$('exVia').onclick = exportVia;
$('exVial').onclick = exportVial;
$('exKle').onclick = exportKle;

if (!('hid' in navigator)) {
  $('dev').textContent = '이 브라우저는 WebHID 를 지원하지 않는다 — Chrome / Edge 를 쓴다';
  $('dev').className = 'bad';
}
