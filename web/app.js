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
const CMD_SLOT_READ   = 0x03;
const CMD_SLOT_BEGIN  = 0x04;
const CMD_SLOT_DATA   = 0x05;
const CMD_SLOT_COMMIT = 0x06;
const CMD_SLOT_ERASE  = 0x07;
const CMD_SEL_SET     = 0x08;
const CMD_HOST_INFO   = 0x09;   // 버전 4 — 키보드가 말하는 이름
const CMD_BOARD_INFO  = 0x0A;   // 버전 5 — 펌웨어 버전

// ★ 이 페이지가 기대하는 펌웨어 명령 버전.
//
//   펌웨어를 안 굽고 웹만 새로 고치면(또는 그 반대면) 새 기능이 조용히 안 돈다.
//   실제로 "키보드 이름이 왜 안 뜨지" 로 한참 헤맸다. 화면이 답하게 한다.
//   ★ link_cmd.h 의 LINK_CMD_VERSION 을 올리면 여기도 올린다.
const CMD_VERSION_NEED = 7;
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
let hostName = '';           // 그 키보드가 스스로 말하는 이름 (USB product string)
let cmdVer = 0;              // 펌웨어의 명령 버전
let reenumPending = false;   // 보드가 재열거를 예약해 뒀다 (끊을 때 일어난다)
let fwVer = '';              // 펌웨어 버전 문자열
let activeSlot = -1;         // 보드가 지금 쓰고 있는 SLOT (-1 = 없음)
let selSlot    = -1;         // 사용자가 고정해 둔 SLOT (-1 = 자동)
let editSlot   = -1;         // 지금 열어서 고치고 있는 SLOT (-1 = 새로 만드는 중)
let dirty      = false;      // 저장하지 않은 편집이 있다
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
    ? 'Vial 펌웨어 — 보드에 저장만 하면 Vial 이 장치에서 바로 읽어간다. 파일은 보관용이다.'
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

  // ★ 읽자마자 넣는다. 아래 어디서든 이 값을 본다 —
  //   나중에 넣었더니 showDev() 가 v0 를 그려서 "오래됐다" 고 잘못 알렸다.
  cmdVer = ver;
  reenumPending = false;
  dirty = false;

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

  // 버전 · 이름을 먼저 받고 나서 한 줄을 그린다
  await readBoardInfo();
  await readHostName();

  showDev(`매트릭스 ${rows}x${cols}, 꽂힌 키보드 ${n}대`
          + (kbds.length ? ` (${kbds.join(', ')})` : ''));

  applyFirmware(locked !== 0);

  // ★ poll() 을 시작하기 **전에** 읽는다.
  //   send() 는 응답 대기가 하나뿐이라 폴링과 섞이면 서로 답을 가로챈다.
  //   버전 3 부터 INFO[28] 이 "지금 고른 SLOT" 이다.
  await loadSlots(ver >= 3 ? info[28] : 0xFF, ver >= 3 ? info[31] : 0xFF);

  if (n === 0) log('★ USB-A 쪽에 키보드가 안 꽂혀 있다. 꽂아야 키를 배울 수 있다.');
  else log('배열을 넣고 [마법사 시작] 을 누른다.');

  poll();
}

// ★ 키보드가 스스로 말하는 이름을 읽는다 (USB product string).
//
//   vid/pid 만으로는 사람이 어느 키보드인지 못 알아본다. 펌웨어가 mount 때
//   비동기로 받아 두므로 꽂은 직후에는 잠깐 비어 있을 수 있다 — 그때는
//   다음 폴링(checkHost)에서 채워진다.
async function readHostName() {
  hostName = '';
  if (cmdVer < 4) return;                 // 이 명령이 생긴 버전

  try {
    const r = await send(CMD_HOST_INFO, 0);
    if (r[3] !== 1) return;
    hostName = new TextDecoder().decode(r.slice(8, 32)).split('\0')[0].trim();
  } catch { /* 없으면 없는 대로 */ }

  // ★ 이름 칸이 아직 기본값이면 진짜 이름으로 채워 준다.
  //   손으로 고쳤으면 건드리지 않는다.
  if (hostName && DEFAULT_NAMES.includes($('name').value.trim())) {
    $('name').value = hostName;
  }
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

    /*
     * ★ 어느 SLOT 이 적용 중인가 — 펌웨어와 같은 규칙으로 판정한다.
     *
     *   보드가 알려 주는 activeSlot 이 정답이다. 다만 그것을 못 받았을 때
     *   (구형 펌웨어라 INFO 버전이 낮을 때) -1 로 두면 "아무것도 적용 안 됨"
     *   처럼 보인다. 실제로는 kbdStoreFind() 가 첫 일치를 쓰므로 같은 규칙을
     *   여기서도 쓴다. 안 그러면 하나뿐인 SLOT 이 꺼진 등으로 보인다.
     */
    const applied = here
      ? (activeSlot >= 0 ? activeSlot : (g.rows.length ? g.rows[0].i : -1))
      : -1;
    const div = document.createElement('div');
    div.className = 'kbd-grp' + (here ? ' here' : '');

    const head = document.createElement('div');
    head.className = 'kbd-head';
    // 이름 — 꽂혀 있으면 키보드가 말하는 진짜 이름, 아니면 담아 둔 이름
    const label = here ? hostName : (g.rows.length ? g.rows[0].name : '');

    // ★ 이름이 없으면 "왜 없는지" 를 적는다.
    //
    //   그냥 비워 두면 펌웨어가 오래된 건지, 키보드가 이름을 안 주는 건지,
    //   우리가 아직 못 받은 건지 구별이 안 된다. 이 프로젝트에서 몇 번이나
    //   시간을 버린 "조용히 아무것도 안 함" 이다.
    let why = '';
    if (here && !label) {
      why = cmdVer < CMD_VERSION_NEED
        ? '이름 없음 — 펌웨어가 오래됐다 (다시 구우면 뜬다)'
        : '이름 없음 — 이 키보드가 USB 이름을 주지 않는다';
    }

    head.innerHTML = `<code>${hex4(g.vid)}:${hex4(g.pid)}</code>`
      + (label ? `<span>${esc(label)}</span>` : '')
      + (why ? `<span class="note">${why}</span>` : '')
      + (here ? '<span class="badge">지금 꽂힌 키보드</span>' : '');
    div.appendChild(head);

    if (!g.rows.length) {
      const note = document.createElement('div');
      note.className = 'note';
      note.style.padding = '4px 0 0 18px';
      note.textContent = '담긴 것이 없다 — 지금은 기본 풀사이즈 배열로 동작한다. '
                       + '아래에서 배열을 만들고 [보드에 저장] 을 누른다.';
      div.appendChild(note);
    }

    for (const r of g.rows) {
      const on  = (r.i === applied);
      const row = document.createElement('div');

      row.className = 'slot-row' + (on ? ' on' : '');

      /*
       * ★ 적용 여부를 보여 주던 등을 라디오로 바꿨다 — 그 자리에서 바로 고른다.
       *
       *   [적용] 버튼을 따로 두면 "지금 적용 중" 을 말하는 곳과 "바꾸는 곳" 이
       *   갈린다. 라디오는 둘이 같은 자리라 설명이 필요 없다.
       *   무리(키보드)마다 name 을 달리해서 서로 간섭하지 않게 한다.
       */
      const pick = document.createElement('input');

      pick.type = 'radio';
      pick.className = 'pick';
      pick.name = `pick-${g.vid}-${g.pid}`;
      pick.checked = on;
      pick.disabled = !here;
      pick.title = here ? (on ? '이미 적용 중이다' : '이 SLOT 을 적용한다')
                        : '꽂혀 있는 키보드만 적용할 수 있다';
      pick.onchange = (e) => { e.stopPropagation(); applySlot(r.i); };
      row.appendChild(pick);

      /*
       * ★ 줄을 클릭하면 편집한다 ([편집] 버튼을 없앴다).
       *
       *   적용(라디오)은 보드 상태를 바꾸고, 편집은 웹에서만 일어난다.
       *   둘을 한 동작에 묶으면 "오타 하나 고치려다 적용까지 옮겨간다".
       *   그래서 위험한 쪽을 작은 과녁(라디오)으로, 안전한 쪽을 넓은 줄로 둔다.
       */
      row.classList.add('pickable');
      if (r.i === editSlot) row.classList.add('editing');
      row.title = '클릭하면 편집한다';
      row.onclick = (e) => {
        if (e.target === pick || e.target.closest('button')) return;
        loadSlotForEdit(r.i);
      };

      row.appendChild(cell('b', `SLOT ${r.i}`));
      row.appendChild(cell('span', esc(r.name) || '(이름 없음)', 'nm'));
      row.appendChild(cell('span', `${r.len} B · PID ${hex4(PID_BASE + r.i)}`, 'note'));
      row.appendChild(cell('span', on ? '적용 중' : '', 'note on-mark'));

      const acts = document.createElement('span');
      acts.className = 'acts';
      acts.appendChild(btn('지우기', () => eraseSlot(r.i)));

      row.appendChild(acts);
      div.appendChild(row);
    }

    /*
     * ★ 새 SLOT 은 "추가" 로 자리부터 만든다.
     *
     *   전에는 [SLOT n 에 새로 담기] 가 바로 저장을 시도했다. 그런데 그 시점엔
     *   아직 배운 것이 없어서 조용히 아무 일도 안 했고, 아래 담기 칸은 여전히
     *   옛 SLOT 을 가리켰다. 무엇을 만드는 중인지 화면 어디에도 없었다.
     *
     *   이제 자리를 먼저 만들고(아직 플래시는 안 건드린다) 거기서 배열을 넣고
     *   배운 뒤에 담는다. 목록 -> 편집 -> 담기 순서가 다른 SLOT 과 같아진다.
     */
    if (here && editSlot >= 0 && slots && !slots[editSlot].used) {
      const row = document.createElement('div');

      row.className = 'slot-row';
      row.innerHTML = '<span></span>'
        + `<b>SLOT ${editSlot}</b>`
        + '<span class="note wide">새로 만드는 중 — 아래 <b>3</b> 에서 배열을 넣고 키를 배운다</span>';

      const acts = document.createElement('span');
      acts.className = 'acts';
      acts.appendChild(btn('그만두기', () => { editSlot = -1; refreshUi(); }));
      row.appendChild(acts);
      div.appendChild(row);
    }

    if (here) {
      const free = slots ? slots.findIndex(x => !x.used) : -1;
      const foot = document.createElement('div');

      /*
       * ★ [자동으로] 는 없앴다.
       *
       *   "고정을 풀면 번호가 가장 낮은 것이 쓰인다" 는 우리 내부 규칙이지
       *   사용자가 알아야 할 것이 아니다. 같은 결과는 그 SLOT 의 [적용] 로
       *   낼 수 있고, 고른 SLOT 을 지우면 보드가 알아서 되돌아간다.
       */
      foot.className = 'slot-foot';
      if (free < 0) {
        const n = document.createElement('span');
        n.className = 'note';
        n.textContent = '빈 SLOT 이 없다 — 하나 지워야 늘릴 수 있다.';
        foot.appendChild(n);
      }
      else if (editSlot !== free || slots[editSlot].used) {
        foot.appendChild(btn('SLOT 추가', () => startNewSlot()));
      }
      if (foot.childNodes.length) div.appendChild(foot);
    }
    box.appendChild(div);
  }
}

function cell(tag, html, cls) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  e.innerHTML = html;
  return e;
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

  // ★ 버튼 이름이 곧 하는 일이다. 대상 SLOT 을 늘 달고 있어야
  //   "어디에 담기는지" 를 화면 어딘가에서 다시 찾지 않는다.
  const isNew = (editSlot >= 0 && slots && !slots[editSlot].used);

  /*
   * ★ 무엇에 저장할지 정해지기 전에는 못 누르게 한다.
   *
   *   전에는 보드만 연결해도 눌렸고, 그러면 "이 키보드가 이미 쓰는 SLOT" 에
   *   그대로 덮어썼다. 배열을 넣은 적도 없는데 실수로 지워 버릴 수 있었다.
   *   위 2 에서 [편집] 하거나 [SLOT 추가] 를 눌러야 대상이 정해진다.
   */
  $('save').disabled = (editSlot < 0);
  $('save').textContent = editSlot < 0 ? '보드에 저장'
                        : (isNew ? `SLOT ${editSlot} 에 저장`
                                 : `SLOT ${editSlot} 에 덮어쓰기`);

  if (!device) { note.textContent = '보드를 연결하면 저장할 수 있다.'; return; }
  if (!has) {
    note.innerHTML = '<b>★ USB-A 에 키보드가 없다.</b> 어느 키보드의 배열인지 알아야 저장할 수 있다.';
    return;
  }
  if (editSlot < 0) {
    note.innerHTML = '어디에 저장할지 먼저 정한다 — 위 <b>2</b> 에서 <b>[편집]</b> 을 누르거나 '
      + '<b>[SLOT 추가]</b> 로 새 자리를 만든다.';
    return;
  }

  const target = editSlot;

  note.innerHTML =
    `<code>${hex4(hostVid)}:${hex4(hostPid)}</code> 의 배열로 <b>SLOT ${target}</b> 에 저장한다.`
    + '<br>저장하는 동안 연결은 끊기지 않는다. 적용되는 SLOT 이 바뀌면 <b>[연결 끊기]</b> 를 '
    + `누르는 순간 보드가 <code>0x${hex4(PID_BASE + target)}</code> 로 다시 뜬다.`
    + '<br>같은 키보드의 변형본을 따로 두려면 위 <b>2</b> 의 [SLOT 추가] 를 쓴다.';
}

// 연결 상태 한 줄. 펌웨어가 무엇인지 늘 같이 적는다.
function showDev(tail) {
  const old = cmdVer < CMD_VERSION_NEED;

  $('dev').textContent =
    `${(firmware || '').toUpperCase()} 펌웨어`
    + (fwVer ? ` ${fwVer}` : '')
    + ` (명령 v${cmdVer})`
    + (old ? ` ★ 오래됐다 — v${CMD_VERSION_NEED} 가 필요하다. 다시 구우면 된다` : '')
    + (tail ? ` — ${tail}` : '');
  $('dev').className = old ? 'bad' : 'ok';
}

async function readBoardInfo() {
  fwVer = '';
  if (cmdVer < 5) return;                 // 그 전 펌웨어에는 이 명령이 없다

  try {
    const r = await send(CMD_BOARD_INFO);
    if (r[2] !== 0) return;
    fwVer = new TextDecoder().decode(r.slice(4, 32)).split('\0')[0].trim();
  } catch { /* 없으면 없는 대로 */ }
}

function hex4(v) { return v.toString(16).toUpperCase().padStart(4, '0'); }

// 키보드가 보낸 문자열을 그대로 innerHTML 에 넣지 않는다
function esc(t) {
  return String(t).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}

async function disconnect() {
  const d = device;
  device = null;                 // poll 루프가 스스로 끝난다
  paused = false;
  await sleep(60);
  try { if (d && d.opened) await d.close(); } catch { /* 무시 */ }
  $('dev').textContent = '연결 끊김 — VIA / Vial 을 써도 된다';
  $('dev').className = '';
  firmware = null;
  cmdVer = 0; fwVer = '';
  slots = null;
  hostVid = 0; hostPid = 0; hostName = '';
  activeSlot = -1; selSlot = -1;
  refreshUi();
  $('exVia').style.display = 'none';
  $('exVial').style.display = 'none';
  $('fwNote').textContent = '보드를 연결하면 펌웨어에 맞는 내보내기가 나온다.';
  $('pressed').textContent = '(없음)';
  log('연결을 끊었다.'
      + (reenumPending ? ' 보드가 새 PID 로 다시 뜬다 — VIA / Vial 이 새 배열을 읽는다.' : '')
      + ' 다시 배우려면 [보드 연결].');
  reenumPending = false;
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

  if (!swapped && !hostName && vid) await readHostName();   // 늦게 도착한 이름

  if (swapped) {
    editSlot = -1;                        // 다른 키보드다. 고치던 맥락을 버린다
    await readHostName();
    showDev(vid ? `꽂힌 키보드 ${hex4(vid)}:${hex4(pid)}` : 'USB-A 에 키보드 없음');
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
//
// ★ 단, **우리 정의일 때만**이다.
//
//   남의 VIA 정의도 범례가 "행,열" 인데 그건 **그 키보드의 매트릭스 좌표**다.
//   BARAM 45K 의 VIA json 은 "3,6" 이 스페이스인데, 그대로 믿으면 usage 0x36
//   (마침표) 으로 읽는다. 배열을 넣자마자 엉뚱한 키를 다 배운 상태가 된다.
//
//   우리 것은 16x16 이다 — 좌표가 곧 usage 라서 그 크기일 수밖에 없다.
//   그래서 파일을 열 때 matrix 를 보고 이 스위치를 켠다.
let trustCoords = true;

function decodeLegend(raw) {
  const lines = String(raw).split('\n');
  const head  = lines[0].trim();

  if (!trustCoords) return { label: head, usage: null };

  const m = /^(\d{1,2}),(\d{1,2})$/.exec(head);
  if (!m) return { label: head, usage: null };

  const r = +m[1], c = +m[2];
  if (r > 15 || c > 15) return { label: head, usage: null };

  const u = (r << 4) | c;

  // ★ 0,0 = usage 0x00 "눌린 키 없음" — 아직 안 배운 자리 표시다 (buildLayout 주석)
  if (u === 0) return { label: (lines[1] || '').trim(), usage: null };

  return { label: NAME_OF[u] || head, usage: u };
}

function parseKle(text) {
  let t = text.trim();
  if (!t.startsWith('[')) throw new Error('KLE raw 데이터가 아니다');
  // KLE raw 는 바깥 대괄호가 없는 형태로 복사되기도 한다
  let arr;
  try { arr = JSON.parse(t); }
  catch { arr = JSON.parse('[' + t + ']'); }

  /*
   * ★ KLE 파일은 첫 원소가 {"name": ...} 같은 메타데이터일 수 있다.
   *   배열인 것만 행이다. 예전에는 첫 원소가 배열이 아니면 전체를 한 행으로
   *   봤는데, 그러면 메타데이터가 붙은 파일이 통째로 어긋났다.
   */
  const rows = arr.filter(Array.isArray);

  arr = rows.length ? rows : [arr];

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
      out.push({ x, y, w, h, ...decodeLegend(it) });
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
    /*
     * ★ 첫 칸은 편집 중일 때 "원래 배열로" 가 된다.
     *
     *   SLOT 을 열어 놓고 프리셋을 이것저것 골라 보다가 첫 칸을 다시 고르면
     *   되돌아갈 것처럼 보이는데, 예전에는 아무 일도 안 했다. 원래 것을
     *   보려면 위로 올라가 [편집] 을 다시 눌러야 했다.
     */
    if (sel.value === '') {
      if (editSlot >= 0 && slots && slots[editSlot] && slots[editSlot].used) {
        loadSlotForEdit(editSlot);
      }
      return;
    }

    const p = PRESETS[Number(sel.value)];
    if (!p) return;
    trustCoords = true;                 // 프리셋 범례는 키 이름이다 (좌표가 아니다)
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

  // ★ 셀렉트는 "지금 내용이 어디서 왔나" 를 가리킨다.
  //   파일에서 왔으면 직전에 고른 프리셋 이름이 남아 있으면 안 된다.
  $('preset').value = '';
  trustCoords = true;
  try {
    const j = JSON.parse(text);

    // KLE 원본 — 최상위 배열, 첫 원소가 메타데이터일 수 있다
    if (Array.isArray(j)) {
      const meta = j.find(x => x && !Array.isArray(x) && typeof x === 'object');

      if (meta && meta.name) $('name').value = meta.name;
    }
    else if (j.layout) body = JSON.stringify(j.layout);                  // 예전 layout-kle.json
    else if (j.layouts && j.layouts.keymap) {                            // via / vial
      body = JSON.stringify(j.layouts.keymap);
      // ★ 우리 정의만 좌표를 usage 로 믿는다 (decodeLegend 주석 참고)
      trustCoords = !!(j.matrix && j.matrix.rows === 16 && j.matrix.cols === 16);
      if (!trustCoords && j.name) $('name').value = j.name;
    }
  } catch { /* KLE raw 는 그대로 둔다 */ }
  $('kle').value = body;
  loadKle();
  log(`${f.name} 을 읽었다.`);
});

function loadKle() {
  try {
    keys = parseKle($('kle').value);
    cursor = -1;
    dirty = true;
    // ★ 배열을 바꿔도 "어느 SLOT 을 고치는 중" 은 유지한다.
    //
    //   SLOT 0 을 열어 놓고 프리셋을 고르는 것은 "SLOT 0 의 배열을 이걸로
    //   바꾸겠다" 는 뜻이다. 여기서 맥락을 버리면 덮어쓸 길이 사라진다.
    //   위험한 쪽 — 키보드를 바꿔 꽂는 것 — 은 checkHost() 가 따로 막는다.
    //   그만두고 싶으면 위 뱃지를 누른다.
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
    d.onclick = () => {
      cursor = i; render(); refreshUi();
      log('이 자리의 키를 누른다. 못 누르는 키면 아래에서 이름으로 직접 넣는다.');
    };
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
  dirty = true;
  next();
}

function next() {
  let i = cursor + 1;
  while (i < keys.length && keys[i].usage !== null) i++;
  cursor = i < keys.length ? i : -1;
  render();
  refreshUi();
  if (cursor < 0) log('전부 배웠다. 아래에서 내려받는다.');
}

function startWizard() {
  if (!keys.length) { log('먼저 KLE 를 읽는다.'); return; }
  cursor = keys.findIndex(k => k.usage === null);
  render();
  refreshUi();
  log(cursor < 0 ? '이미 다 배웠다.' : '강조된 자리의 키를 누른다. 건너뛰려면 [건너뛰기].');
}

function skip() { if (cursor >= 0) { cursor++; next(); } }

/*
 * 배운 것을 전부 되돌린다. **배열(KLE)은 그대로 둔다** — 자리는 남기고
 * 채워 넣은 usage 만 비운다. 다른 키보드로 같은 배열을 다시 배울 때 쓴다.
 */
function clearAll() {
  if (!keys.length) { log('먼저 배열을 읽는다.'); return; }

  const n = keys.filter(k => k.usage !== null).length;

  if (n === 0) { log('이미 다 비어 있다.'); return; }
  if (!confirm(`배운 키 ${n}개를 모두 지운다. 배열은 그대로 둔다.`)) return;

  keys.forEach(k => { k.usage = null; });
  dirty = true;
  cursor = -1;
  render();
  refreshUi();
  log(`${n}개를 지웠다. [마법사 시작] 으로 다시 배운다.`);
}

// 잘못 배운 자리를 되돌린다. 커서는 그대로 둬서 바로 다시 누를 수 있게 한다.
function clearCur() {
  if (cursor < 0 || cursor >= keys.length) { log('먼저 자리를 고른다 (키를 클릭).'); return; }

  const had = keys[cursor].usage;
  keys[cursor].usage = null;
  dirty = true;
  render();
  log(had === null ? '이미 비어 있다.'
                   : `0x${had.toString(16).toUpperCase()} 를 비웠다. 이 자리의 키를 다시 누른다.`);
}

/*
 * 자리를 클릭해 고른 뒤 키 이름으로 직접 채운다.
 *
 * ★ 왜 필요한가 — 못 누르는 키가 있다.
 *
 *   Fn 조합으로만 나오는 키(밝기 · 미디어)는 눌러도 그 usage 가 안 올라오고,
 *   꽂은 키보드에 아예 없는 자리를 배열에 그려 둘 수도 있다.
 *
 * ★ 넣고 나서 말을 해 준다. 예전에는 아무 표시가 없어서 "동작을 안 한다" 로
 *   보였다 — 실제로는 배열의 그 칸이 초록으로 바뀌고 커서가 다음으로 갔는데,
 *   눈은 입력칸에 있었다.
 */
function manualAssign() {
  const name = $('manual').value.trim().toUpperCase();

  if (cursor < 0 || cursor >= keys.length) { log('먼저 위 배열에서 자리를 클릭한다.'); return; }
  if (!name) { log('키 이름을 넣는다 — 칸에 몇 자만 쳐도 목록이 뜬다.'); return; }
  if (!Object.prototype.hasOwnProperty.call(USAGE, name)) {
    log(`모르는 이름: ${name}`);
    return;
  }

  const u = USAGE[name];

  onKeyDown(u);
  $('manual').value = '';
  log(`${name} (0x${u.toString(16).toUpperCase().padStart(2, '0')}) 을 넣었다.`
      + (cursor >= 0 ? ' 다음 자리로 넘어갔다.' : ''));
}

// 이름 목록을 자동완성에 넣는다. 어떤 이름이 되는지 외우지 않아도 된다.
function fillKeyNames() {
  const dl = $('keynames');

  for (const name of Object.keys(USAGE).sort()) {
    const o = document.createElement('option');

    o.value = name;
    o.label = '0x' + USAGE[name].toString(16).toUpperCase().padStart(2, '0');
    dl.appendChild(o);
  }
}

// ── 내보내기 ────────────────────────────────────────────
/*
 * KLE 를 다시 조립한다. legendOf() 가 각 자리의 범례를 정한다.
 *
 * ★ 안 배운 자리도 남긴다.
 *
 *   예전에는 usage 가 없는 자리를 통째로 버렸다. 그래서 두 키만 배우고
 *   저장했다가 다시 열면 **그 두 키만 남은 배열**이 됐다 — 나머지 자리가
 *   사라져서 이어서 배울 수가 없었다.
 *
 *   범례를 "0,0" 으로 적는다. usage 0x00 은 HID 에서 "눌린 키 없음" 이라
 *   진짜 키와 겹치지 않는다. 뒤에 줄바꿈으로 원래 이름을 붙여 두면 다시
 *   열 때 무슨 자리였는지도 살아난다 (VIA/Vial 은 첫 줄만 좌표로 읽는다).
 */
function buildLayout(legendOf) {
  const rows = [];
  let cur = [], prevY = null, prevX = 0;
  for (const k of keys) {
    if (prevY === null || k.y !== prevY) {
      if (cur.length) rows.push(cur);
      cur = []; prevX = 0;
      if (prevY !== null && k.y - prevY !== 1) cur.push({ y: k.y - prevY - 1 });
      prevY = k.y;
    }
    if (k.x !== prevX) { cur.push({ x: k.x - prevX }); prevX = k.x; }
    if (k.w !== 1) cur.push({ w: k.w });
    if (k.h !== 1) cur.push({ h: k.h });
    cur.push(legendOf(k));
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
    log('★ USB-A 쪽에 키보드가 없다. 어느 키보드의 배열인지 알아야 저장할 수 있다.');
    return;
  }
  if (!keys.some(k => k.usage !== null)) { log('먼저 배열을 읽고 키를 배운다.'); return; }

  const todo = keys.filter(k => k.usage === null).length;

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
    log(`SLOT ${slot} 에 저장 중… ${raw.length} B -> ${blob.length} B`);

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

    let reenum = (r[3] === 1);

    /*
     * ★ 담자마자 적용하는 것은 **처음 한 벌일 때만** 이다.
     *
     *   전에는 담을 때마다 그 SLOT 을 고정했다. 그러면 변형본을 하나 더
     *   만들 때마다 PID 가 바뀌어 연결이 끊긴다 — 여러 번 고쳐 담는 동안
     *   계속 끊기는 게 이상했다.
     *
     *   쓰던 것이 이미 있으면 그대로 두고, 바꾸고 싶을 때 [적용] 을 누른다.
     *   무엇이 적용 중인지는 트리의 녹색 등이 말해 준다.
     */
    if (activeSlot < 0) {
      try {
        const rs = await sendReport(CMD_SEL_SET, [slot], 5000);
        if (rs[2] === 0) reenum = reenum || (rs[3] === 1);
      } catch { /* 구형 펌웨어 */ }
    }

    editSlot = slot;
    dirty = false;

    /*
     * ★ 담긴 사실을 화면에도 바로 반영한다.
     *
     *   담고 나면 보드가 재열거하는데, 처음 보는 PID 면 브라우저가 장치를
     *   안 돌려준다. 그때 slots 를 비워 두면 화면이 담기 전 상태로 남아
     *   "SLOT 1 을 만드는 중" 이 그대로 붙어 있었다 — 이미 담았는데도.
     */
    if (slots && slots[slot]) {
      slots[slot] = {
        used: true,
        name: new TextDecoder().decode(Uint8Array.from(nameBytes23())).split('\0')[0],
        vid: hostVid, pid: hostPid, len: blob.length,
      };
    }
    finishSlotOp(`SLOT ${slot} 에 저장했다 (${blob.length} B).`
                 + (todo ? ` 아직 안 배운 자리 ${todo}개는 빈 자리로 들어갔다 — 다시 열어 이어서 배우면 된다.` : '')
                 + (activeSlot >= 0 && activeSlot !== slot ? ' 쓰려면 [적용] 을 누른다.' : ''),
                 reenum);
  } catch (e) {
    recoverAfterOp('저장', e);
  }
}

/*
 * 명령이 실패했을 때 — 정말 실패인지, 보드가 다시 열거된 것인지 가른다.
 *
 * ★ 보드가 재열거하면 응답이 사라진다. 그때 "실패" 라고만 적으면 사용자는
 *   이미 끝난 일을 다시 하려 든다 (실제로 지우기가 그랬다 — 보드에서는
 *   지워졌는데 화면은 '실패 — 응답 없음' 이었다).
 *   말을 걸어 보고, 죽었으면 다시 붙여서 진짜 상태를 읽어 온다.
 */
async function recoverAfterOp(what, e) {
  busy = false;

  try {
    const info = await send(CMD_INFO);
    await loadSlots(info[2] >= 3 ? info[28] : 0xFF, info[2] >= 3 ? info[31] : 0xFF);
    log(`${what} 실패 — ${e.message}`);
    return;
  } catch { /* 장치가 사라졌다 */ }

  afterSlotWrite(`${what} 중 보드가 다시 열거됐다.`);
}

/*
 * 슬롯을 건드린 뒤 뒷정리.
 *
 * ★ 여기서 연결을 놓지 않는다.
 *
 *   보드는 PID 가 바뀔 때만 재열거하고, 그것도 **우리가 말을 거는 동안은
 *   계속 미룬다** (link_cmd.c 의 usbPostponeReenum). 그래서 담고 · 고치고 ·
 *   적용하는 내내 연결이 유지된다. 실제로 끊기는 것은 [연결 끊기] 를 누르거나
 *   탭을 옮겨 조용해진 뒤다.
 *
 *   pending 은 "언젠가 재열거된다" 는 표시일 뿐이라 안내에만 쓴다.
 *   장치가 정말 사라진 경우는 catch 에서 받는다.
 */
async function finishSlotOp(msg, pending) {
  try {
    const info = await send(CMD_INFO);
    await loadSlots(info[2] >= 3 ? info[28] : 0xFF, info[2] >= 3 ? info[31] : 0xFF);
  } catch {
    afterSlotWrite(msg);              // 정말 끊긴 경우 — 다시 붙여 본다
    return;
  }

  busy = false;
  reenumPending = pending || reenumPending;
  log(msg + (reenumPending
    ? ' PID 가 바뀌므로 [연결 끊기] 를 누르면 보드가 새 이름으로 다시 뜬다.' : ''));
  refreshUi();
}

/*
 * 저장하지 않은 편집을 버려도 되는지 묻는다.
 *
 * ★ 다른 SLOT 으로 옮길 때만 묻는다.
 *
 *   프리셋을 바꾸거나 파일을 여는 것은 "이 SLOT 의 배열을 이걸로 바꾸겠다" 는
 *   의도적인 행동이고, 되돌릴 길(첫 칸의 "원래 배열로")이 이미 있다.
 *   거기까지 물으면 금방 성가셔진다.
 */
function askDiscard(to) {
  if (!dirty || editSlot < 0) return true;

  const n = keys.filter(k => k.usage !== null).length;

  return confirm(`SLOT ${editSlot} 에 저장하지 않은 편집이 있다 (배운 키 ${n}개).\n`
                 + `버리고 ${to} 로 옮길까?`);
}

// 빈 SLOT 자리를 잡는다. 아직 플래시는 안 건드린다 — 담을 때 쓴다.
function startNewSlot() {
  if (!device || !slots) { log('보드를 먼저 연결한다.'); return; }

  const free = slots.findIndex(x => !x.used);
  if (free < 0) { log('★ 빈 SLOT 이 없다. 하나 지워야 늘릴 수 있다.'); return; }
  if (free !== editSlot && !askDiscard(`새 SLOT ${free}`)) return;

  editSlot = free;
  refreshUi();
  log(`SLOT ${free} 을 만든다 — 아래에서 배열을 넣고 키를 배운 뒤 [SLOT ${free} 에 저장].`);
}

// ── 시나리오 2 : 담아 둔 것을 편집한다 ──────────────────
//
// ★ 이게 없어서 배열을 조금 고치려 해도 처음부터 다시 배워야 했다.
//   범례가 "행,열" 이라 parseKle 가 usage 까지 되살린다 (decodeLegend).
//   그래서 불러온 순간 이미 다 배운 상태다 — 곧바로 고칠 수 있다.
async function loadSlotForEdit(slot) {
  if (!device || !slots || !slots[slot] || !slots[slot].used) return;

  if (slot === editSlot) {
    // 같은 줄을 다시 클릭 — 원래 배열로 되돌리는 뜻이다
    if (dirty && !confirm(`SLOT ${slot} 을 보드에 담긴 원래 배열로 되돌릴까?`)) return;
  }
  else if (!askDiscard(`SLOT ${slot}`)) return;

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

    trustCoords = true;                   // 보드에 담긴 것은 우리 정의다
    dirty = false;                        // 방금 보드에서 읽어 왔다
    $('preset').value = '';               // 프리셋이 아니라 SLOT 을 보고 있다
    $('name').value = def.name || '';
    $('kle').value  = JSON.stringify(def.layouts.keymap);
    keys = parseKle($('kle').value);
    cursor = -1;
    editSlot = slot;
    render();
    refreshUi();
    log(`SLOT ${slot} 을 편집한다 — ${keys.length} 키. 고친 뒤 [SLOT ${slot} 에 덮어쓰기].`);
  } catch (e) {
    log('편집 실패 — ' + e.message);
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

    if (slot !== SEL_AUTO) { activeSlot = slot; selSlot = slot; }
    finishSlotOp(slot === SEL_AUTO ? '자동으로 되돌렸다.' : `SLOT ${slot} 을 적용했다.`,
                 r[3] === 1);
  } catch (e) {
    recoverAfterOp('적용', e);
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
    if (slots && slots[slot]) slots[slot] = { used: false, name: '', vid: 0, pid: 0, len: 0 };
    finishSlotOp(`SLOT ${slot} 을 지웠다.`, r[3] === 1);
  } catch (e) {
    recoverAfterOp('지우기', e);
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
    refreshUi();        // ★ 트리만 말고 뱃지·담기 칸까지. 예전에 여기서 어긋났다
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

  // 첫 칸의 이름 — 되돌릴 것이 있을 때만 그렇게 말한다
  {
    const stored = (editSlot >= 0 && slots && slots[editSlot] && slots[editSlot].used);

    $('preset').options[0].textContent =
      stored ? `— SLOT ${editSlot} 의 원래 배열로 —` : '— 프리셋에서 고르기 —';
  }

  $('assign').disabled = (cursor < 0 || cursor >= keys.length);

  const badge = $('editBadge');
  if (editSlot >= 0) {
    const isNew = (slots && !slots[editSlot].used);

    badge.textContent = `SLOT ${editSlot} 을 ${isNew ? '만드는' : '고치는'} 중`
                      + (dirty ? '  ●' : '') + '  ✕';
    badge.title = dirty ? '● = 저장하지 않은 편집이 있다. 누르면 그만둔다'
                        : '누르면 그만둔다';
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
    layouts: { keymap: buildLayout(legendAddr) },
  };
  if (isVial) def.lighting = 'none';
  return def;
}

// 정의(VIA/Vial)용 — 좌표. 안 배운 자리는 "0,0" + 원래 이름
function legendAddr(k) {
  if (k.usage === null) return '0,0\n' + (k.label || '');
  return `${k.usage >> 4},${k.usage & 0x0F}`;
}

// KLE 원본용 — 사람이 읽는 이름
function legendName(k) {
  if (k.usage === null) return k.label || '';
  return NAME_OF[k.usage] || `${k.usage >> 4},${k.usage & 0x0F}`;
}

function exportVia()  { download(slug() + 'layout-via.json',  buildDef(false)); }
function exportVial() { download(slug() + 'layout-vial.json', buildDef(true)); }
/*
 * KLE 원본. 범례가 '키 이름' 이라 사람이 읽고 고칠 수 있다.
 *
 * ★ keyboard-layout-editor.com 이 그대로 여는 형식으로 낸다.
 *
 *   최상위가 **배열**이고 첫 원소가 메타데이터다. 예전에는 우리 편하자고
 *   {_comment, layout} 로 감쌌는데, 그러면 KLE 의 Upload 가 아무것도 안
 *   그린다 — 내보내는 뜻이 없어진다.
 *   gen_keymap.py 는 두 모양을 다 받는다 (kle_rows).
 */
function exportKle() {
  const rows = buildLayout(legendName);

  const meta = {
    name: $('name').value || 'QMK-LINK',
    notes: '웹 마법사가 만들었다. 범례는 키 이름이고 gen_keymap.py 의 입력이다.',
  };

  download(slug() + 'layout-kle.json', [meta, ...rows]);
}

fillPresets();
fillKeyNames();
refreshUi();
$('loadFile').onclick = openFile;
$('connect').onclick = connect;
$('disconnect').onclick = disconnect;
$('load').onclick = loadKle;
$('start').onclick = startWizard;
$('skip').onclick = skip;
$('clear').onclick = clearCur;
$('clearAll').onclick = clearAll;
$('assign').onclick = manualAssign;
$('manual').onkeydown = (e) => { if (e.key === 'Enter') manualAssign(); };
$('editBadge').onclick = () => { if (!askDiscard('새로 만들기')) return; editSlot = -1; dirty = false; refreshUi(); log('고치던 SLOT 을 놓았다 — 이제 새로 만든다.'); };
$('save').onclick = () => saveSlot();
$('exVia').onclick = exportVia;
$('exVial').onclick = exportVial;
$('exKle').onclick = exportKle;

if (!('hid' in navigator)) {
  $('dev').textContent = '이 브라우저는 WebHID 를 지원하지 않는다 — Chrome / Edge 를 쓴다';
  $('dev').className = 'bad';
}
