#!/usr/bin/env python3
"""
QMK / Vial 원본 받아오기 (Windows / macOS / Linux 공통)

  python3 fetch_upstream.py            upstream.json 대로 받는다. 이미 맞으면 아무것도 안 한다
  python3 fetch_upstream.py --check    받아져 있는지만 확인한다 (CMake 가 쓴다)
  python3 fetch_upstream.py --update   업스트림의 최신 리비전을 조회해서 upstream.json 을 갱신한다
  python3 fetch_upstream.py --force    지우고 다시 받는다

왜 서브모듈이 아닌가

  qmk_firmware 는 전체 히스토리가 524MB, vial-qmk 는 265MB 다. 서브모듈로 두면
  클론할 때마다 그만큼 받는다. 여기서는 히스토리 · 블롭 · 트리를 전부 잘라서 받는다.

    --depth 1              히스토리 제거
    --filter=blob:none     체크아웃하지 않는 경로의 파일 내용은 안 받는다
    --sparse               keyboards/ 등 나머지를 워킹트리에서 제외

  결과는 리포당 5MB 안팎이다. 리비전은 upstream.json 이 고정하므로 재현성은 같다.
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


SDK_DIR = Path(__file__).resolve().parent.parent
JSON_PATH = SDK_DIR / "upstream.json"
DEST_ROOT = SDK_DIR / "upstream"


def run(args, cwd=None, check=True, quiet=False):
    return subprocess.run(
        args, cwd=cwd, check=check,
        stdout=subprocess.PIPE if quiet else None,
        stderr=subprocess.STDOUT if quiet else None,
        text=True)


def git_out(args, cwd=None):
    r = subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def load_spec():
    if not JSON_PATH.is_file():
        sys.exit(f"없다: {JSON_PATH}")
    data = json.loads(JSON_PATH.read_text())
    return {k: v for k, v in data.items() if not k.startswith("_")}


def is_sha(rev):
    return bool(re.fullmatch(r"[0-9a-f]{7,40}", rev))


def current_rev(path):
    """받아져 있는 리비전을 돌려준다. 태그면 태그 이름, 아니면 SHA."""
    if not (path / ".git").exists():
        return None
    tag = git_out(["git", "describe", "--tags", "--exact-match"], cwd=path)
    if tag:
        return tag
    return git_out(["git", "rev-parse", "HEAD"], cwd=path)


def matches(path, rev):
    cur = current_rev(path)
    if cur is None:
        return False
    if cur == rev:
        return True
    # SHA 를 적어 둔 경우 앞부분만 비교한다
    if is_sha(rev) and cur.startswith(rev):
        return True
    return False


def fetch_one(name, spec, force=False):
    dest = DEST_ROOT / name
    url = spec["url"]
    rev = spec["rev"]
    sparse = spec.get("sparse", [])

    if dest.exists() and not force:
        if matches(dest, rev):
            print(f"  {name:16s} {rev}  (이미 받아져 있다)")
            return True
        print(f"  {name:16s} 리비전이 다르다 -> 다시 받는다")

    if dest.exists():
        shutil.rmtree(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)

    print(f"  {name:16s} {rev} 받는 중 ...")

    common = ["--depth", "1", "--filter=blob:none", "--sparse"]

    if is_sha(rev):
        # SHA 는 --branch 로 못 받는다. init 후 그 커밋만 fetch 한다.
        run(["git", "init", "-q", str(dest)])
        run(["git", "remote", "add", "origin", url], cwd=dest)
        run(["git", "fetch", "-q", "--depth", "1", "--filter=blob:none",
             "origin", rev], cwd=dest)
        if sparse:
            run(["git", "sparse-checkout", "set"] + sparse, cwd=dest)
        run(["git", "checkout", "-q", "FETCH_HEAD"], cwd=dest)
    else:
        run(["git", "clone", "-q"] + common + ["--branch", rev, url, str(dest)])
        if sparse:
            run(["git", "sparse-checkout", "set"] + sparse, cwd=dest)

    size = sum(f.stat().st_size for f in dest.rglob("*") if f.is_file())
    print(f"  {name:16s} 완료 — {size/1024/1024:.1f} MB")
    return True


def cmd_check(spec):
    ok = True
    for name, s in spec.items():
        dest = DEST_ROOT / name
        if not dest.exists():
            print(f"  {name:16s} 없다")
            ok = False
        elif not matches(dest, s["rev"]):
            print(f"  {name:16s} 리비전이 다르다 (원하는 값 {s['rev']})")
            ok = False
        else:
            print(f"  {name:16s} {s['rev']}  OK")
    if not ok:
        print("\n  python3 firm-sdk/tools/fetch_upstream.py 를 실행한다")
    return 0 if ok else 1


def cmd_update(spec):
    """업스트림의 최신 릴리스 태그 / 브랜치 커밋을 조회해 upstream.json 을 갱신한다."""
    data = json.loads(JSON_PATH.read_text())
    changed = False

    for name, s in spec.items():
        url = s["url"]
        cur = s["rev"]

        if is_sha(cur):
            branch = s.get("branch", "HEAD")
            out = git_out(["git", "ls-remote", url, branch])
            new = out.split()[0] if out else ""
        else:
            out = git_out(["git", "ls-remote", "--tags", "--refs", url])
            tags = [l.split("/")[-1] for l in out.splitlines()]
            tags = [t for t in tags if re.fullmatch(r"\d+\.\d+\.\d+", t)]
            tags.sort(key=lambda v: [int(x) for x in v.split(".")])
            new = tags[-1] if tags else ""

        if not new:
            print(f"  {name:16s} 조회 실패")
            continue
        if new == cur:
            print(f"  {name:16s} {cur}  (최신)")
        else:
            print(f"  {name:16s} {cur} -> {new}")
            data[name]["rev"] = new
            changed = True

    if changed:
        JSON_PATH.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")
        print("\n  upstream.json 갱신됨. fetch_upstream.py 를 다시 실행한다")
    return 0


def main():
    ap = argparse.ArgumentParser(description="QMK / Vial 원본 받아오기")
    ap.add_argument("--check", action="store_true", help="받아져 있는지만 확인")
    ap.add_argument("--update", action="store_true", help="최신 리비전으로 upstream.json 갱신")
    ap.add_argument("--force", action="store_true", help="지우고 다시 받는다")
    ap.add_argument("name", nargs="?", help="특정 항목만")
    args = ap.parse_args()

    spec = load_spec()
    if args.name:
        if args.name not in spec:
            sys.exit(f"upstream.json 에 없다: {args.name}")
        spec = {args.name: spec[args.name]}

    if args.check:
        return cmd_check(spec)
    if args.update:
        return cmd_update(spec)

    for name, s in spec.items():
        fetch_one(name, s, force=args.force)
    return 0


if __name__ == "__main__":
    sys.exit(main())
