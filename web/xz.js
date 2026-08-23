// 압축하지 않는 .xz 를 만든다.
//
// ★ 왜 이런 게 필요한가
//
//   Vial 은 키보드 정의를 **압축된 채로** 장치에서 읽어간다
//   (vial-qmk 의 util/vial_generate_definition.py 가 lzma.compress 를 쓴다).
//   그런데 브라우저에는 LZMA 인코더가 없다 — CompressionStream 은 deflate 와
//   gzip 뿐이다. 그래서 지금까지 보드에 담는 일이 파이썬 몫이었다.
//
// ★ 그런데 압축할 필요가 없다.
//
//   .xz 컨테이너 안의 LZMA2 는 **"압축 안 한 청크"** 를 정식으로 허용한다
//   (제어 바이트 0x01 / 0x02). 그것만 이어 붙이면 인코더 없이도 규격에 맞는
//   .xz 가 된다. 받는 쪽은 표준 lzma 라 그대로 풀린다.
//
//   실측 (풀사이즈 153키 정의):
//     minify 원본  1,486 B
//     무압축 xz    1,548 B    <- 이것
//     파이썬 LZMA    556 B
//   한 SLOT 이 8,144 B 라 세 배 가까이 남는다. 압축률을 포기할 만하다.
//
//   파이썬 lzma.decompress() 로 왕복을 확인하고 옮겼다.
//
// 참고: xz file format 1.1.0 (tukaani.org/xz/xz-file-format.txt)

const CHUNK_MAX = 0x10000;          // 압축 안 한 청크 하나의 최대 크기

// CRC32 (xz 는 little endian 으로 적는다)
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    t[i] = c >>> 0;
  }
  return t;
})();

function crc32(bytes) {
  let c = 0xFFFFFFFF;
  for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

function u32le(v) {
  return [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];
}

// 가변 길이 정수 (7비트씩, 마지막 바이트만 MSB 가 0)
function vint(n) {
  const out = [];
  while (n >= 0x80) { out.push((n & 0x7F) | 0x80); n = Math.floor(n / 128); }
  out.push(n);
  return out;
}

function padTo4(arr) {
  while (arr.length % 4) arr.push(0);
}

/**
 * data (Uint8Array) 를 압축하지 않고 .xz 스트림으로 감싼다.
 * @returns {Uint8Array}
 */
export function xzStore(data) {
  // ── 블록 데이터 : LZMA2 의 "압축 안 한 청크" 들 ──
  //   0x01 = 압축 안 함 + 사전 리셋, 0x02 = 압축 안 함 (리셋 없음)
  //   뒤에 (길이-1) 을 big endian 2바이트로 적고 원본을 그대로 붙인다.
  const body = [];
  for (let off = 0; off < data.length; off += CHUNK_MAX) {
    const part = data.subarray(off, Math.min(off + CHUNK_MAX, data.length));
    body.push(off === 0 ? 0x01 : 0x02);
    body.push((part.length - 1) >> 8, (part.length - 1) & 0xFF);
    for (let i = 0; i < part.length; i++) body.push(part[i]);
  }
  body.push(0x00);                                  // 끝 표시

  // ── 블록 헤더 ──
  //   플래그 : 필터 1개(하위 2비트 0) | 0x40 압축크기 있음 | 0x80 비압축크기 있음
  let hdr = [0x40 | 0x80];
  hdr = hdr.concat(vint(body.length), vint(data.length));
  hdr = hdr.concat(vint(0x21), vint(1), [0x00]);    // LZMA2 필터, 사전 4KB
  //   전체 크기(크기 바이트 + 본문 + CRC32)가 4의 배수여야 한다
  while ((hdr.length + 1 + 4) % 4) hdr.push(0);
  //   ★ 크기 바이트는 "실제 크기 / 4 - 1" 이다. -1 을 빠뜨리면 통째로 깨진다.
  hdr.unshift((hdr.length + 1 + 4) / 4 - 1);
  hdr = hdr.concat(u32le(crc32(Uint8Array.from(hdr))));

  // ── 블록 = 헤더 + 데이터 + 패딩 + 검사값 ──
  const unpadded = hdr.length + body.length + 4;    // 패딩은 빼고 센다 (인덱스용)
  let block = hdr.concat(body);
  padTo4(block);
  block = block.concat(u32le(crc32(data)));

  // ── 인덱스 ──
  let idx = [0x00].concat(vint(1), vint(unpadded), vint(data.length));
  padTo4(idx);
  idx = idx.concat(u32le(crc32(Uint8Array.from(idx))));

  // ── 스트림 헤더 / 푸터 ──  검사 방식 = CRC32 (0x01)
  const flags = [0x00, 0x01];
  const head = [0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00]
    .concat(flags, u32le(crc32(Uint8Array.from(flags))));
  const back = u32le(idx.length / 4 - 1);
  const foot = u32le(crc32(Uint8Array.from(back.concat(flags))))
    .concat(back, flags, [0x59, 0x5A]);

  return Uint8Array.from(head.concat(block, idx, foot));
}
