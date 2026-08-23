import { USAGE, NAME_OF } from './usage-table.js';
import { PRESETS } from './presets.js';
import { xzStore, xzRead } from './xz.js';

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
const CMD_SLOT_INFO   = 0x02;
const CMD_SLOT_BEGIN  = 0x04;
const CMD_SLOT_DATA   = 0x05;
const CMD_SLOT_COMMIT = 0x06;
const CMD_SLOT_ERASE  = 0x07;
const CMD_SEL_SET     = 0x08;
const SEL_AUTO        = 0xFF;
const REPORT_LEN  = 32;

// 한 SLOT 에 들어가는 데이터 (8KB - 머리말 48B). kbd_store.c 와 같아야 한다.
const SLOT_DATA_MAX = 8192 - 48;

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
let hostVid = 0, hostPid = 0;// USB-A 에 꽂힌 키보드. SLOT 머리말에 적는다
let activeSlot = -1;         // 보드가 지금 쓰고 있는 SLOT (-1 = 없음)
let selSlot    = -1;         // 사용자가 고정해 둔 SLOT (-1 = 자동)
let editSlot   = -1;         // 지금 열어서 고치고 있는 SLOT (-1 = 새로 만드는 중)
let busy = false;            // 담기/지우기 중 — 폴링을 멈춘다

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
    ? 'Vial 펌웨어 — 보드에 담기만 하면 Vial 이 장치에서 바로 읽어간다. 파일은 저장용이다.'
      + (vialLocked ? '  (Vial 잠금 상태 — 매크로 편집 등은 좌우 Shift 5초로 푼다. 키 읽기는 잠겨도 된다)' : '')
    : 'VIA 펌웨어 — 보드에 담은 뒤 layout-via.json 을 받아 VIA 의 Design 탭에 한 번 넣는다. '
      + '그 뒤로는 꽂는 대로 VIA 가 알아서 고른다.';
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

  await attach(list[0]);
}

// 고른 장치를 붙이고 INFO 를 읽는다.
// PID 가 바뀌어 재열거된 뒤 다시 붙일 때도 이쪽으로 온다 (afterSlotWrite).
async function attach(dev) {
  device = dev;
  if (!device.opened) await device.open();

  // 같은 장치에 두 번 붙지 않게 한다 (재열거 뒤 다시 들어온다)
  if (!device._wired) {
    device._wired = true;
    device.addEventListener('inputreport', (e) => {
      if (!pending) return;
      const r = new Uint8Array(e.data.buffer);
      if (r[0] !== pending.want[0] || r[1] !== pending.want[1]) return;   // 남의 응답
      const done = pending.resolve; pending = null;
      done(r);
    });
  }

  const info = await send(CMD_INFO);
  const ver = info[2], tree = info[3], locked = info[4];
  const rows = info[5], cols = info[6], n = info[7];

  let kbds = [];
  hostVid = 0; hostPid = 0;
  for (let i = 0; i < n; i++) {
    const o = 8 + i * 4;
    const vid = info[o] | (info[o+1] << 8);
    const pid = info[o+2] | (info[o+3] << 8);
    if (i === 0) { hostVid = vid; hostPid = pid; }
    kbds.push(hex4(vid) + ':' + hex4(pid));
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
  await loadSlots(ver >= 3 ? info[28] : 0xFF, ver >= 3 ? info[31] : 0xFF);

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
async function loadSlots(active, selected) {
  activeSlot = (active !== undefined && active < SLOT_MAX) ? active : -1;
  selSlot    = (selected !== undefined && selected < SLOT_MAX) ? selected : -1;

  slots = [];
  for (let i = 0; i < SLOT_MAX; i++) {
    let r;
    try { r = await send(CMD_SLOT_INFO, i); } catch { slots = null; break; }
    slots.push({
      used: (r[2] === 0 && r[3] !== 0),
      name: new TextDecoder().decode(r.slice(10, 32)).split('\0')[0],
      vid:  r[4] | (r[5] << 8),
      pid:  r[6] | (r[7] << 8),
      len:  r[8] | (r[9] << 8),
    });
  }
  refreshUi();
}

// ★ 같은 키보드가 여러 SLOT 에 있으면, 기록된 선택이 없을 때는 번호가 낮은
//   쪽이 이긴다 (kbd_store.h). 보드가 알려 주는 activeSlot 이 정답이라
//   여기서는 표시만 한다.
function slotsOf(vid, pid) {
  if (!slots) return [];
  return slots.map((s, i) => ({ ...s, i })).filter(s => s.used && s.vid === vid && s.pid === pid);
}

// ── ② 보드에 담긴 레이아웃 (트리) ───────────────────────
//
// ★ 여기가 시나리오의 출발점이다.
//
//   꽂은 키보드가 이미 담겨 있는지를 **먼저** 보여 준다. 있으면 그 무리를
//   맨 위에 놓고 강조하고, 적용 중인 SLOT 에 ● 를 찍는다. 없으면 "담긴 것
//   없음 — 지금은 기본 풀사이즈" 라고 말해 준다. 그래야 사용자가
//   "새로 만들어야 하나 / 있는 걸 고치면 되나" 를 바로 안다.
function renderSlots() {
  const box = $('slotTree');
  const sum = $('slotSummary');

  box.innerHTML = '';
  sum.textContent = '';

  if (!device || !slots) {
    box.textContent = '보드를 연결하면 여기 보인다.';
    return;
  }

  const used = slots.filter(s => s.used).length;
  sum.textContent = `쓴 SLOT ${used} / ${SLOT_MAX}`;

  // 키보드별로 묶는다. 꽂힌 키보드가 맨 위다.
  const groups = new Map();
  slots.forEach((s, i) => {
    if (!s.used) return;
    const key = `${s.vid}:${s.pid}`;
    if (!groups.has(key)) groups.set(key, { vid: s.vid, pid: s.pid, rows: [] });
    groups.get(key).rows.push({ ...s, i });
  });

  const hereKey = `${hostVid}:${hostPid}`;
  const order = [...groups.values()].sort((a, b) =>
    (`${b.vid}:${b.pid}` === hereKey ? 1 : 0) - (`${a.vid}:${a.pid}` === hereKey ? 1 : 0));

  // 꽂혀 있는데 담긴 것이 없으면 빈 무리를 하나 만들어 준다 — 그것도 정보다
  if ((hostVid || hostPid) && !groups.has(hereKey)) {
    order.unshift({ vid: hostVid, pid: hostPid, rows: [] });
  }

  if (!order.length) {
    box.textContent = (hostVid || hostPid)
      ? '담긴 것이 없다.'
      : '담긴 것이 없다. USB-A 에 키보드를 꽂으면 그 키보드 자리가 여기 생긴다.';
    return;
  }

  for (const g of order) {
    const here = (`${g.vid}:${g.pid}` === hereKey) && (hostVid || hostPid);
    const div = document.createElement('div');
    div.className = 'kbd-grp' + (here ? ' here' : '');

    const head = document.createElement('div');
    head.className = 'kbd-head';
    head.innerHTML = `<code>${hex4(g.vid)}:${hex4(g.pid)}</code>`
      + (here ? '<span class="badge">지금 꽂힌 키보드</span>' : '');
    div.appendChild(head);

    if (!g.rows.length) {
      const note = document.createElement('div');
      note.className = 'note';
      note.style.padding = '4px 0 0 18px';
      note.textContent = '담긴 것이 없다 — 지금은 기본 풀사이즈 배열로 동작한다. '
                       + '아래에서 배열을 만들고 [보드에 담기] 를 누른다.';
      div.appendChild(note);
    }

    for (const r of g.rows) {
      const on  = here && r.i === activeSlot;
      const row = document.createElement('div');
      row.className = 'slot-row' + (on ? ' on' : '');

      const txt = document.createElement('span');
      txt.innerHTML = `<span class="dot">${on ? '\u25CF' : '\u25CB'}</span>`
        + `<b>SLOT ${r.i}</b>  ${r.name || '(이름 없음)'}`
        + `  <span class="note">${r.len} B · PID ${hex4(PID_BASE + r.i)}`
        + `${on && selSlot === r.i ? ' · 고정' : ''}</span>`;
      row.appendChild(txt);

      if (here && !on) row.appendChild(btn('적용', () => applySlot(r.i)));
      row.appendChild(btn('열기', () => openSlot(r.i)));
      row.appendChild(btn('지우기', () => eraseSlot(r.i)));
      div.appendChild(row);
    }

    if (here && g.rows.length > 1 && selSlot >= 0) {
      div.appendChild(btn('자동으로', () => applySlot(0xFF), 'note'));
    }
    box.appendChild(div);
  }
}

function btn(label, fn, cls) {
  const b = document.createElement('button');
  b.textContent = label;
  b.className = cls || '';
  b.onclick = fn;
  return b;
}

// ★ 내보내는 JSON 의 productId 는 **보드가 이 키보드에 대해 보고할 PID** 여야
//   한다. 고쳐 담는 중이면 그 SLOT, 아니면 지금 적용 중인 SLOT 이다.
function slotPid() {
  if (editSlot >= 0) return PID_BASE + editSlot;
  if (activeSlot >= 0) return PID_BASE + activeSlot;
  return 0x5305;
}

// ── ④ 담기 안내 ────────────────────────────────────────
function updateSaveNote() {
  const note = $('saveNote');
  const has  = !!(hostVid || hostPid);
  const mine = slotsOf(hostVid, hostPid);
  const free = slots ? slots.findIndex(s => !s.used) : -1;

  $('save').textContent    = editSlot >= 0 ? `SLOT ${editSlot} 에 덮어쓰기` : '보드에 담기';
  $('saveNew').style.display = (editSlot >= 0 && free >= 0) ? '' : 'none';
  $('saveNew').textContent = free >= 0 ? `새 SLOT ${free} 에 담기` : '';

  if (!device) { note.textContent = '보드를 연결하면 담을 수 있다.'; return; }
  if (!has) {
    note.innerHTML = '<b>★ USB-A 에 키보드가 없다.</b> 어느 키보드의 배열인지 알아야 담을 수 있다.';
    return;
  }

  const target = editSlot >= 0 ? editSlot : (mine.length ? mine[0].i : free);
  if (target < 0) { note.innerHTML = '<b>★ 빈 SLOT 이 없다.</b> 위에서 하나 지운다.'; return; }

  note.innerHTML =
    `<code>${hex4(hostVid)}:${hex4(hostPid)}</code> 의 배열로 <b>SLOT ${target}</b> 에 담는다. `
    + `담고 나면 보드가 PID 를 <code>0x${hex4(PID_BASE + target)}</code> 로 보고하고 `
    + '스스로 다시 열거한다 — 그래서 연결이 한 번 끊긴다.'
    + (editSlot >= 0
        ? '<br>같은 키보드의 변형본을 따로 두려면 [새 SLOT 에 담기] 를 쓴다.'
        : '');
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
  hostVid = 0; hostPid = 0;
  activeSlot = -1; selSlot = -1;
  refreshUi();
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

function send(sub, ...args) { return sendReport(sub, args, 1000); }

// 플래시를 굽는 명령(COMMIT · ERASE)은 오래 걸린다. 그때만 넉넉히 준다.
function sendReport(sub, args, timeout) {
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
    }, timeout);
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
    if (paused || busy) { await sleep(200); continue; }

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

    // ★ 키보드를 바꿔 꽂으면 스스로 따라간다.
    //
    //   USB-A 쪽은 사용자가 아무 때나 바꾼다. 연결할 때 한 번만 읽으면
    //   트리가 이전 키보드를 계속 가리킨다. 2초에 한 번 INFO 만 다시 본다
    //   (PRESSED 를 20ms 마다 보내고 있으니 이 정도는 부담이 아니다).
    if (++tick % 100 === 0) await checkHost();

    await sleep(20);
  }
}

let tick = 0;

async function checkHost() {
  let info;
  try { info = await send(CMD_INFO); } catch { return; }

  const n   = info[7];
  const vid = n ? (info[8] | (info[9] << 8)) : 0;
  const pid = n ? (info[10] | (info[11] << 8)) : 0;
  const act = info[2] >= 3 ? info[28] : 0xFF;
  const sel = info[2] >= 3 ? info[31] : 0xFF;

  if (vid === hostVid && pid === hostPid &&
      act === (activeSlot < 0 ? 0xFF : activeSlot) &&
      sel === (selSlot < 0 ? 0xFF : selSlot)) return;

  const swapped = (vid !== hostVid || pid !== hostPid);
  hostVid = vid; hostPid = pid;

  if (swapped) {
    editSlot = -1;                        // 다른 키보드다. 고치던 맥락을 버린다
    const fw = (firmware || '').toUpperCase();
    $('dev').textContent = vid
      ? `${fw} 펌웨어 — 꽂힌 키보드 ${hex4(vid)}:${hex4(pid)}`
      : `${fw} 펌웨어 — USB-A 에 키보드 없음`;
    log(vid ? `키보드가 바뀌었다 — ${hex4(vid)}:${hex4(pid)}` : 'USB-A 쪽 키보드가 빠졌다.');
  }

  await loadSlots(act, sel);
}
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

// ── KLE ────────────────────────────────────────────────
// ★ 범례가 "행,열" 이면 이미 배운 자리다 — usage 를 되살린다.
//
//   우리가 내보낸 layout-via.json / layout-vial.json 이나 보드에 담긴 정의를
//   다시 열 때가 그렇다. 이걸 안 하면 배열을 조금 고치려 해도 전부 다시
//   눌러야 했다. 좌표가 곧 usage 라 되살리는 데 표가 필요 없다.
function decodeLegend(label) {
  const m = /^(\d{1,2}),(\d{1,2})$/.exec(label.trim());
  if (!m) return { label, usage: null };

  const r = +m[1], c = +m[2];
  if (r > 15 || c > 15) return { label, usage: null };

  const u = (r << 4) | c;
  return { label: NAME_OF[u] || label, usage: u };
}

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
      out.push({ x, y, w, h, ...decodeLegend(String(it).split('\n')[0]) });
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
    // ★ 새 배열을 넣었으면 더는 그 SLOT 을 고치는 중이 아니다.
    //   안 그러면 엉뚱한 SLOT 에 덮어쓴다.
    editSlot = -1;
    render();
    refreshUi();
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

// ── 보드에 담기 · 지우기 ────────────────────────────────
//
// ★ 파이썬 없이 여기서 끝난다.
//
//   Vial 은 정의를 압축된 채로 읽어가는데 브라우저에 LZMA 인코더가 없다.
//   그래서 예전에는 tools/kbd_upload.py 가 필요했다. 지금은 **압축을 안 한
//   .xz** 를 만들어 보낸다 (xz.js 주석 참고) — 규격에 맞으므로 받는 쪽은
//   그대로 푼다. 1.5KB 쯤 되는데 한 SLOT 이 8KB 라 넉넉하다.

// 이름을 23바이트 UTF-8 로 자른다. 잘린 자리에 반쪽짜리 글자를 남기지 않는다.
function nameBytes23() {
  let b = new TextEncoder().encode(($('name').value || '').trim());
  if (b.length > 22) {
    b = b.subarray(0, 22);
    while (b.length && (b[b.length - 1] & 0xC0) === 0x80) b = b.subarray(0, b.length - 1);
    if (b.length && (b[b.length - 1] & 0x80)) b = b.subarray(0, b.length - 1);
  }
  const out = new Uint8Array(23);
  out.set(b);
  return [...out];
}

// ── 시나리오 1 : 새로 만들어 담는다 ─────────────────────
//   slot 을 안 주면 "이 키보드가 이미 쓰는 SLOT" -> "첫 빈 SLOT" 순으로 고른다.
async function saveSlot(slot) {
  if (!device) { log('보드를 먼저 연결한다.'); return; }
  if (!hostVid && !hostPid) {
    log('★ USB-A 쪽에 키보드가 없다. 어느 키보드의 배열인지 알아야 담을 수 있다.');
    return;
  }
  if (!keys.some(k => k.usage !== null)) { log('먼저 배열을 읽고 키를 배운다.'); return; }

  if (slot === undefined) {
    const mine = slotsOf(hostVid, hostPid);
    slot = editSlot >= 0 ? editSlot
         : (mine.length ? mine[0].i : (slots ? slots.findIndex(s => !s.used) : -1));
  }
  if (slot < 0) { log('★ 빈 SLOT 이 없다. 위에서 하나 지운다.'); return; }

  const raw  = new TextEncoder().encode(JSON.stringify(buildDef(true)));
  const blob = xzStore(raw);

  if (blob.length > SLOT_DATA_MAX) {
    log(`★ 너무 크다 — ${blob.length} B / ${SLOT_DATA_MAX} B`);
    return;
  }

  busy = true;
  await sleep(80);                       // 폴링이 멎기를 기다린다
  try {
    log(`SLOT ${slot} 에 담는 중… ${raw.length} B -> ${blob.length} B`);

    let r = await send(CMD_SLOT_BEGIN, slot,
                       hostVid & 0xFF, hostVid >> 8, hostPid & 0xFF, hostPid >> 8,
                       blob.length & 0xFF, blob.length >> 8, ...nameBytes23());
    if (r[2] !== 0) throw new Error('BEGIN 거부 (' + r[2] + ')');

    const step = REPORT_LEN - 4;         // [0][1] 명령 + [2..3] 오프셋
    for (let off = 0; off < blob.length; off += step) {
      const chunk = blob.subarray(off, Math.min(off + step, blob.length));
      r = await send(CMD_SLOT_DATA, off & 0xFF, off >> 8, ...chunk);
      if (r[2] !== 0) throw new Error(`DATA 거부 (오프셋 ${off})`);
    }

    // 여기서만 플래시를 굽는다. 8KB 소거 + 기록이라 오래 걸린다.
    r = await sendReport(CMD_SLOT_COMMIT, [slot], 5000);
    if (r[2] !== 0) throw new Error('굽기 실패 (' + r[2] + ')');

    // ★ 담은 SLOT 을 곧바로 고정한다.
    //   안 그러면 같은 키보드의 낮은 번호 SLOT 이 이겨서, 새로 담은 것이
    //   적용되지 않는다 — "담았는데 왜 안 바뀌지" 가 되는 자리다.
    try { await sendReport(CMD_SEL_SET, [slot], 5000); } catch { /* 구형 펌웨어 */ }

    editSlot = slot;
    slots = null;
    afterSlotWrite(`SLOT ${slot} 에 담았다 (${blob.length} B).`);
  } catch (e) {
    busy = false;
    log('담기 실패 — ' + e.message);
  }
}

// ── 시나리오 2 : 담아 둔 것을 열어 고친다 ────────────────
//
// ★ 이게 없어서 배열을 조금 고치려 해도 처음부터 다시 배워야 했다.
//   범례가 "행,열" 이라 parseKle 가 usage 까지 되살린다 (decodeLegend).
async function openSlot(slot) {
  if (!device || !slots || !slots[slot] || !slots[slot].used) return;

  busy = true;
  await sleep(80);
  try {
    const n = slots[slot].len;
    const buf = new Uint8Array(n);
    let got = 0;

    while (got < n) {
      const r = await send(CMD_SLOT_READ, slot, got & 0xFF, got >> 8);
      if (r[2] !== 0) throw new Error('읽기 실패');
      const take = Math.min(r[3], n - got);
      buf.set(r.subarray(4, 4 + take), got);
      got += take;
    }

    const def = JSON.parse(new TextDecoder().decode(xzRead(buf)));
    if (!def.layouts || !def.layouts.keymap) throw new Error('배열이 없는 정의다');

    $('name').value = def.name || '';
    $('kle').value  = JSON.stringify(def.layouts.keymap);
    keys = parseKle($('kle').value);
    cursor = -1;
    editSlot = slot;
    render();
    refreshUi();
    log(`SLOT ${slot} 을 열었다 — ${keys.length} 키. 고친 뒤 [SLOT ${slot} 에 덮어쓰기].`);
  } catch (e) {
    log('열기 실패 — ' + e.message);
  }
  busy = false;
}

// ── 시나리오 3 : 변형본 사이를 오간다 ───────────────────
async function applySlot(slot) {
  if (!device) return;

  busy = true;
  await sleep(80);
  try {
    const r = await sendReport(CMD_SEL_SET, [slot], 5000);
    if (r[2] !== 0) throw new Error('결과 ' + r[2]);

    slots = null;
    afterSlotWrite(slot === SEL_AUTO ? '자동으로 되돌렸다.' : `SLOT ${slot} 을 적용했다.`);
  } catch (e) {
    busy = false;
    log('적용 실패 — ' + e.message);
  }
}

async function eraseSlot(slot) {
  if (!device) return;
  if (!confirm(`SLOT ${slot} 을 지운다. 되돌릴 수 없다.`)) return;

  busy = true;
  await sleep(80);
  try {
    const r = await sendReport(CMD_SLOT_ERASE, [slot], 5000);
    if (r[2] !== 0) throw new Error('결과 ' + r[2]);

    if (editSlot === slot) editSlot = -1;
    slots = null;
    afterSlotWrite(`SLOT ${slot} 을 지웠다.`);
  } catch (e) {
    busy = false;
    log('지우기 실패 — ' + e.message);
  }
}

// ★ 담거나 지우거나 적용하면 보드가 PID 를 바꾸며 스스로 재열거한다.
//
//   그러면 지금 쥐고 있는 WebHID 장치가 죽는다. 브라우저에는 **PID 가 다르면
//   다른 장치**다. 전에 허락받은 적이 있으면 getDevices() 로 조용히 다시
//   잡히고, 처음 보는 PID 면 사용자가 [보드 연결] 을 한 번 눌러야 한다.
//   (브라우저 규칙이라 우회할 방법이 없다)
async function afterSlotWrite(msg) {
  log(msg + ' 보드가 다시 열거되는 중…');

  const old = device;
  device = null;                          // poll 루프를 끝낸다
  busy = false;
  await sleep(1200);                      // 끊고(250ms) 붙는(100ms) 시간 + 열거
  try { if (old && old.opened) await old.close(); } catch { /* 이미 사라졌다 */ }

  const found = (await navigator.hid.getDevices()).find(d =>
    d.vendorId === VID && PID_LIST.includes(d.productId) &&
    d.collections.some(c => c.usagePage === 0xFF60));

  if (!found) {
    $('dev').textContent = '보드가 다시 열거됐다 — [보드 연결] 을 한 번 더 누른다';
    $('dev').className = '';
    renderSlots();
    log(msg + ' PID 가 바뀌어 연결이 끊겼다. [보드 연결] 을 누르면 이어서 쓸 수 있다.');
    return;
  }

  await attach(found);
  log(msg + ' VIA / Vial 이 새 정의를 읽어간다.');
}

// 화면에서 상태를 보여 주는 곳을 한 번에 갱신한다.
// ★ 한 군데서만 그린다 — 여러 곳에서 따로 고치면 반드시 어긋난다.
function refreshUi() {
  renderSlots();
  updateSaveNote();

  const badge = $('editBadge');
  if (editSlot >= 0) {
    badge.textContent = `SLOT ${editSlot} 을 고치는 중`;
    badge.style.display = '';
  } else {
    badge.style.display = 'none';
  }
}

function download(name, obj) {
  const blob = new Blob([JSON.stringify(obj, null, 2) + '\n'], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob); a.download = name; a.click();
}

// ★ 내보내기와 보드에 담기가 같은 것을 써야 한다.
//   두 군데서 따로 만들면 반드시 어긋난다.
function buildDef(isVial) {
  const def = {
    name: $('name').value || (isVial ? 'QMK-LINK VIAL' : 'QMK-LINK VIA'),
    vendorId: '0x0483', productId: '0x' + hex4(slotPid()),
    matrix: { rows: 16, cols: 16 },
    layouts: { keymap: buildLayout() },
  };
  if (isVial) def.lighting = 'none';
  return def;
}

function exportVia()  { download(slug() + 'layout-via.json',  buildDef(false)); }
function exportVial() { download(slug() + 'layout-vial.json', buildDef(true)); }
function exportKle() {
  // 저장소 워크플로용 — 범례가 '키 이름' 이라 사람이 읽고 고칠 수 있다
  const rows = buildLayout().map(row => row.map(it =>
    typeof it === 'string'
      ? (NAME_OF[(parseInt(it.split(',')[0]) << 4) | parseInt(it.split(',')[1])] || it)
      : it));
  download(slug() + 'layout-kle.json', { _comment: ['웹 마법사가 만들었다. gen_keymap.py 의 입력이다.'], layout: rows });
}

fillPresets();
refreshUi();
$('loadFile').onclick = openFile;
$('connect').onclick = connect;
$('disconnect').onclick = disconnect;
$('load').onclick = loadKle;
$('start').onclick = startWizard;
$('skip').onclick = skip;
$('clear').onclick = clearCur;
$('assign').onclick = manualAssign;
$('save').onclick = () => saveSlot();
$('saveNew').onclick = () => saveSlot(slots ? slots.findIndex(x => !x.used) : -1);
$('exVia').onclick = exportVia;
$('exVial').onclick = exportVial;
$('exKle').onclick = exportKle;

if (!('hid' in navigator)) {
  $('dev').textContent = '이 브라우저는 WebHID 를 지원하지 않는다 — Chrome / Edge 를 쓴다';
  $('dev').className = 'bad';
}
