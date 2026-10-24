#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
scripts/check-edition.py — public-edition gate: fail if a tree, its git history or its binaries
contain markers that belong only to the private edition.

What is scanned (every hit is printed as location + marker + context):
  - every file under each PATH (names and contents; .git/ is skipped, history is scanned below)
      text files ............ file:line
      binaries .............. file@0xOFFSET (raw bytes, same as `strings`); *.elf, *.bin, ...
      *.hex (Intel HEX) ..... decoded first, then scanned as a binary
      *.uf2 ................. payload decoded first
      zip / tar / tar.gz / tar.xz / .gz / .deb (ar) ... unpacked in memory and scanned member by member
  - if PATH is the top level of a git repository:
      every blob reachable from any ref (= every version of every file in `git log -p --all`,
      including binaries), every path name ever committed, commit messages and author/committer
      identities, branch / tag names, annotated tag messages and remote URLs

Markers are matched case-insensitively. Short words only match as words (a word inside a longer word is not
a hit); 0x-style ids only match in text files and not inside longer hex runs.
Only generic markers (AI attribution lines, proprietary SPDX refs) are built in. The edition-specific
list is kept outside the public repository: --markers FILE, or env FARO_EDITION_MARKERS holding the
file content (CI secret). Tokens are rot13-encoded so the list never trips itself; --list-markers.

Allowlist (documented false positives): default <PATH>/scripts/check-edition.allow, else
--allowlist FILE. One entry per line:  <path-glob> <python-regex>
  The hit is ignored when its location matches the glob (fnmatch; "commit:*", "ref:*" for
  history items) and the context line matches the regex. '#' starts a comment.
Extra markers (e.g. from a CI secret): --extra-markers FILE (one plain string per line) and/or
env FARO_EDITION_EXTRA_MARKERS="a,b,c"; matched as case-insensitive substrings.

Usage:
  scripts/check-edition.py [PATH ...]            # default PATH: .
  scripts/check-edition.py --no-history dist/    # release artefacts only
  scripts/check-edition.py --exclude 'external/MeshCore/*' .
  scripts/check-edition.py --self-test
Exit status: 0 clean, 1 markers found, 2 usage or scan error.
"""
import argparse
import bz2
import codecs
import fnmatch
import gzip
import io
import lzma
import os
import pathlib
import re
import subprocess
import sys
import tarfile
import tempfile
import threading
import zipfile

MAX_BLOB = 1 << 30           # refuse (and report) anything larger than 1 GiB
MAX_UNPACKED = 4 << 30       # total unpacked bytes per top-level file
MAX_DEPTH = 4                # archive nesting
BINARY_EXT = {".bin", ".elf", ".hex", ".uf2", ".zip", ".gz", ".tgz", ".xz", ".bz2", ".tar", ".deb",
              ".png", ".jpg", ".jpeg", ".gif", ".webp", ".pdf", ".ttf", ".otf", ".woff", ".woff2", ".a", ".o"}

# (kind, rot13 token). Kinds:
#   sub    case-insensitive substring
#   word   word-ish: not preceded by a letter (or a camelCase boundary), not followed by a lowercase letter
#   prefix word start, not followed by the listed English continuations
#   hexid  text files only; not inside a longer hex/identifier run; optional 0x prefix
#   num    not followed by another digit
# Built-in markers are generic only. The edition-specific list is NOT part of this repository:
# it comes from --markers FILE or env FARO_EDITION_MARKERS (the file's content, e.g. a CI secret).
# File format, one marker per line, tokens rot13-encoded:  <kind> <token> [!<excluded continuation> ...]
GENERIC_MARKERS = [
    ("coauthor", "Pb-Nhguberq-Ol: Pynhqr"), ("sub", "Pynhqr-Frffvba"), ("generated", "Trarengrq jvgu Pynhqr Pbqr"),
    ("sub", "YvprafrErs-"),
]
MARKERS = list(GENERIC_MARKERS)
PREFIX_EXCLUDE = {}  # rot13(lower token) -> [rot13 continuations that are not the marker]


def parse_marker_lines(text: str) -> list:
    out = []
    for i, ln in enumerate(text.splitlines(), 1):
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        parts = ln.split()
        kind = parts[0]
        excl = [x[1:] for x in parts[1:] if x.startswith("!")]
        tok = " ".join(x for x in parts[1:] if not x.startswith("!"))
        if kind not in ("sub", "word", "prefix", "hexid", "num", "coauthor", "generated") or not tok:
            raise SystemExit(f"markers line {i}: expected '<kind> <rot13 token>'")
        out.append((kind, tok))
        if excl:
            PREFIX_EXCLUDE[tok.lower()] = excl
    return out


def r13(s: str) -> str:
    return codecs.decode(s, "rot13")


class Marker:
    def __init__(self, kind: str, token: str):
        self.name = token
        low = token.lower()
        self.anchor = low.encode()
        self.text_only = kind == "hexid"
        self.on_lower = True
        e = re.escape(low)
        if kind in ("sub", "extra"):
            rx = e
        elif kind == "word":
            up, lo = re.escape(token[0].upper()), re.escape(token[0].lower())
            rest_lo, rest_up = re.escape(token[1:].lower()), re.escape(token[1:].upper())
            # matches Word, WORD, word_x, readWord, WordFrame; not inside a longer lowercase word
            rx = rf"(?:(?<![A-Za-z])[{up}{lo}]|(?<=[a-z]){up})(?:{rest_lo}|{rest_up})(?![a-z])"
            self.on_lower = False
        elif kind == "prefix":
            excl = "|".join(re.escape(r13(x)) for x in PREFIX_EXCLUDE.get(codecs.encode(low, "rot13").lower(), []))
            rx = rf"(?<![a-z0-9_]){e}" + (rf"(?!{excl})" if excl else "")
        elif kind == "hexid":
            rx = rf"(?<![0-9a-z_])(?:0x)?{e}(?![0-9a-z_])"
        elif kind == "num":
            rx = rf"{e}(?![0-9])"
        elif kind == "coauthor":
            head, who = low.split(":", 1)
            self.anchor = head.encode()
            rx = rf"{re.escape(head)}:[ \t]*{re.escape(who.strip())}"
        elif kind == "generated":
            words = low.split()
            self.anchor = " ".join(words[-2:]).encode()
            rx = rf"{re.escape(' '.join(words[:-2]))}[ \t]*\[?{re.escape(' '.join(words[-2:]))}"
        else:
            raise ValueError(kind)
        self.rx = re.compile(rx.encode())
        self.short = len(self.anchor) <= 4
        self.literal = kind in ("sub", "extra")  # the anchor alone decides, no regex needed


def build_markers(extra: list[str]) -> list[Marker]:
    ms = [Marker(k, r13(t)) for k, t in MARKERS]
    ms += [Marker("extra", x) for x in extra if x.strip()]
    return ms


def printable(b: int) -> bool:
    return 0x20 <= b <= 0x7E or b == 0x09


def run_bounds(data: bytes, start: int, end: int) -> tuple[int, int]:
    s = start
    while s > 0 and printable(data[s - 1]) and start - s < 200:
        s -= 1
    e = end
    while e < len(data) and printable(data[e]) and e - end < 200:
        e += 1
    return s, e


class Scanner:
    def __init__(self, markers, allow, max_per_loc):
        self.markers = markers
        self.allow = allow
        self.max_per_loc = max_per_loc
        self.hits = 0
        self.errors = 0
        self.locations = 0
        self.stats = {"files": 0, "blobs": 0, "commits": 0, "refs": 0}
        self.per_marker = {}

    # ---- matching ----
    def match(self, data: bytes, is_text: bool):
        low = data.lower()
        found = []
        for m in self.markers:
            if m.text_only and not is_text:
                continue
            pos = low.find(m.anchor)
            if pos < 0:
                continue
            # bytes.find is fast; the regex only runs in a small window around each anchor
            target = low if m.on_lower else data
            alen, seen = len(m.anchor), set()
            while pos >= 0:
                if m.literal:
                    spans = [(pos, pos + alen)]
                else:
                    spans = [mt.span() for mt in m.rx.finditer(target, max(0, pos - 40), pos + alen + 200)]
                for s, e in spans:
                    if (s, e) in seen:
                        continue
                    seen.add((s, e))
                    if not is_text and m.short:
                        rs, re_ = run_bounds(data, s, e)
                        if re_ - rs < 8:
                            continue  # random bytes in compressed data, not a string
                    found.append((s, e, m.name))
                pos = low.find(m.anchor, pos + 1)
        found.sort()
        return found

    def allowed(self, loc: str, context: str) -> bool:
        for glob, rx in self.allow:
            if fnmatch.fnmatch(loc, glob) and rx.search(context):
                return True
        return False

    def report(self, loc: str, items):
        """items: list of (where, [names], context)"""
        shown = 0
        kept = []
        for where, names, ctx in items:
            if self.allowed(loc, ctx):
                continue
            kept.append((where, names, ctx))
        if not kept:
            return
        self.locations += 1
        for where, names, ctx in kept:
            self.hits += 1
            for n in names:
                self.per_marker[n] = self.per_marker.get(n, 0) + 1
            if shown < self.max_per_loc:
                print(f"{where}: [{', '.join(sorted(set(names)))}] {ctx}")
                shown += 1
        if len(kept) > shown:
            print(f"{loc}: ... {len(kept) - shown} more hit(s) in this location")

    def scan_text_line(self, loc: str, text: str):
        """Short single-line items: path names, ref names, identities."""
        data = text.encode("utf-8", "replace")
        found = self.match(data, True)
        if found:
            self.report(loc, [(loc, [n for _, _, n in found], text[:160])])

    def scan_bytes(self, loc: str, data: bytes, name: str = "", depth: int = 0, budget=None):
        if budget is None:
            budget = [MAX_UNPACKED]
        name = name or loc
        if len(data) > MAX_BLOB:
            self.error(f"{loc}: {len(data)} bytes, too large to scan")
            return
        if depth < MAX_DEPTH and self.scan_container(loc, data, name, depth, budget):
            return
        head = data[:8192]
        is_text = b"\0" not in head and pathlib.PurePath(name).suffix.lower() not in BINARY_EXT
        found = self.match(data, is_text)
        if not found:
            return
        items = []
        if is_text:
            line, pos = 1, 0
            by_line = {}
            for s, e, n in found:
                line += data.count(b"\n", pos, s)
                pos = s
                by_line.setdefault(line, [s, []])[1].append(n)
            for ln, (s, names) in by_line.items():
                ls = data.rfind(b"\n", 0, s) + 1
                le = data.find(b"\n", s)
                ctx = data[ls:le if le >= 0 else len(data)].decode("utf-8", "replace").strip()
                items.append((f"{loc}:{ln}", names, ctx[:160]))
        else:
            by_off = {}
            for s, e, n in found:
                rs, re_ = run_bounds(data, s, e)
                by_off.setdefault(rs, [re_, []])[1].append(n)
            for rs, (re_, names) in by_off.items():
                ctx = data[rs:re_].decode("latin-1")
                items.append((f"{loc}@0x{rs:x}", names, '"' + ctx[:160] + '"'))
        self.report(loc, items)

    def error(self, msg: str):
        self.errors += 1
        print(f"ERROR: {msg}", file=sys.stderr)

    # ---- containers ----
    def scan_container(self, loc, data, name, depth, budget) -> bool:
        lname = name.lower()
        try:
            if data[:4] == b"PK\x03\x04":
                with zipfile.ZipFile(io.BytesIO(data)) as zf:
                    for info in zf.infolist():
                        self.scan_text_line(f"{loc}!{info.filename}", info.filename)
                        if info.is_dir():
                            continue
                        if not self.take(budget, info.file_size, loc):
                            return True
                        self.scan_bytes(f"{loc}!{info.filename}", zf.read(info), info.filename, depth + 1, budget)
                return True
            if data[:8] == b"!<arch>\n":
                self.scan_ar(loc, data, depth, budget)
                return True
            is_tar = len(data) > 262 and data[257:262] == b"ustar"
            if data[:2] == b"\x1f\x8b" or data[:6] == b"\xfd7zXZ\x00" or data[:3] == b"BZh" or is_tar:
                try:
                    with tarfile.open(fileobj=io.BytesIO(data), mode="r:*") as tf:
                        for ti in tf:
                            self.scan_text_line(f"{loc}!{ti.name}", ti.name)
                            if not ti.isfile():
                                continue
                            if not self.take(budget, ti.size, loc):
                                return True
                            f = tf.extractfile(ti)
                            self.scan_bytes(f"{loc}!{ti.name}", f.read() if f else b"", ti.name, depth + 1, budget)
                    return True
                except tarfile.TarError:
                    if is_tar:
                        raise
                if data[:2] == b"\x1f\x8b":
                    raw = gzip.decompress(data)
                elif data[:3] == b"BZh":
                    raw = bz2.decompress(data)
                else:
                    raw = lzma.decompress(data)
                if not self.take(budget, len(raw), loc):
                    return True
                inner = re.sub(r"\.(gz|xz|bz2)$", "", name, flags=re.I)
                self.scan_bytes(f"{loc}!{pathlib.PurePath(inner).name}", raw, inner, depth + 1, budget)
                return True
            if data[:4] == b"UF2\n":
                self.scan_bytes(f"{loc}!payload", decode_uf2(data), "payload.bin", depth + 1, budget)
                return True
            if lname.endswith(".hex") and data[:1] == b":":
                self.scan_bytes(f"{loc}!decoded", decode_ihex(data), "decoded.bin", depth + 1, budget)
                return True
        except (zipfile.BadZipFile, tarfile.TarError, OSError, EOFError, lzma.LZMAError, ValueError) as exc:
            if re.search(r"\.(zip|tar|tgz|gz|xz|bz2|deb|uf2|hex)$", lname):
                self.error(f"{loc}: cannot unpack ({exc}); scanned raw")
        return False

    def take(self, budget, n, loc) -> bool:
        budget[0] -= n
        if budget[0] < 0:
            self.error(f"{loc}: unpacked size limit reached, rest not scanned")
            return False
        return True

    def scan_ar(self, loc, data, depth, budget):
        pos = 8
        while pos + 60 <= len(data):
            hdr = data[pos:pos + 60]
            member = hdr[:16].decode("latin-1").strip().rstrip("/")
            size = int(hdr[48:58].decode("latin-1").strip() or 0)
            body = data[pos + 60:pos + 60 + size]
            self.scan_text_line(f"{loc}!{member}", member)
            self.scan_bytes(f"{loc}!{member}", body, member, depth + 1, budget)
            pos += 60 + size + (size & 1)

    # ---- tree ----
    def scan_tree(self, root: pathlib.Path, excludes):
        if root.is_file():
            self.scan_file(root, root.name)
            return
        for dirpath, dirnames, filenames in os.walk(root):
            rel_dir = os.path.relpath(dirpath, root)
            keep = []
            for d in sorted(dirnames):
                rel = os.path.normpath(os.path.join(rel_dir, d))
                if d == ".git" or any(fnmatch.fnmatch(rel, x) or fnmatch.fnmatch(rel + "/", x) for x in excludes):
                    continue
                keep.append(d)
            dirnames[:] = keep
            for f in sorted(filenames):
                rel = os.path.normpath(os.path.join(rel_dir, f))
                if f == ".git" or any(fnmatch.fnmatch(rel, x) for x in excludes):
                    continue
                self.scan_file(pathlib.Path(dirpath) / f, rel)

    def scan_file(self, path: pathlib.Path, rel: str):
        self.stats["files"] += 1
        self.scan_text_line(rel, rel)
        if path.is_symlink():
            self.scan_text_line(rel, os.readlink(path))
            return
        try:
            size = path.stat().st_size
            if size > MAX_BLOB:
                self.error(f"{rel}: {size} bytes, too large to scan")
                return
            data = path.read_bytes()
        except OSError as exc:
            self.error(f"{rel}: {exc}")
            return
        self.scan_bytes(rel, data, rel)

    # ---- git history ----
    def scan_git(self, root: pathlib.Path, excludes):
        def git(*args, **kw):
            return subprocess.run(["git", "-C", str(root), *args], capture_output=True, check=True, **kw).stdout

        try:
            top = git("rev-parse", "--show-toplevel").decode().strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            print(f"check-edition: {root} is not a git repository, history not scanned")
            return
        if pathlib.Path(top).resolve() != root.resolve():
            print(f"check-edition: {root} is inside {top}, not its top level; history not scanned")
            return

        def excluded(p):
            return any(fnmatch.fnmatch(p, x) for x in excludes)

        # refs, annotated tag messages, remotes
        for ref in git("for-each-ref", "--format=%(refname)").decode("utf-8", "replace").splitlines():
            self.stats["refs"] += 1
            self.scan_text_line(f"ref:{ref}", ref)
        tags = git("for-each-ref", "refs/tags", "--format=%(refname)%00%(objecttype)%00%(contents)%1e")
        for rec in tags.split(b"\x1e"):
            parts = rec.strip(b"\n").split(b"\0", 2)
            if len(parts) == 3 and parts[1] == b"tag":
                self.scan_bytes(f"tag:{parts[0].decode('utf-8', 'replace')}", parts[2], "tag.txt")
        self.scan_bytes("remotes", git("remote", "-v"), "remotes.txt")

        # commit messages and identities
        log = git("log", "--all", "--format=%H%x00%an <%ae>%x00%cn <%ce>%x00%B%x1e")
        for rec in log.split(b"\x1e"):
            parts = rec.strip(b"\n").split(b"\0", 3)
            if len(parts) != 4:
                continue
            self.stats["commits"] += 1
            sha = parts[0].decode()[:10]
            self.scan_bytes(f"commit:{sha}", b"author " + parts[1] + b"\ncommitter " + parts[2] + b"\n" + parts[3],
                            "message.txt")

        # every path name ever committed
        names = set(git("log", "--all", "--format=", "--name-only", "--no-renames", "-z").split(b"\0"))
        for n in sorted(x for x in names if x):
            p = n.decode("utf-8", "replace")
            if not excluded(p):
                self.scan_text_line(f"git-path:{p}", p)

        # every reachable blob (all versions of all files)
        objs = git("rev-list", "--objects", "--all", "--filter=object:type=blob")
        blobs = []
        for line in objs.decode("utf-8", "replace").splitlines():
            sha, _, path = line.partition(" ")
            if path and not excluded(path):
                blobs.append((sha, path))
        if not blobs:
            return
        proc = subprocess.Popen(["git", "-C", str(root), "cat-file", "--batch"],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE)

        def feed():
            try:
                for sha, _ in blobs:
                    proc.stdin.write(sha.encode() + b"\n")
                proc.stdin.close()
            except BrokenPipeError:
                pass

        t = threading.Thread(target=feed, daemon=True)
        t.start()
        out = proc.stdout
        for sha, path in blobs:
            header = out.readline().split()
            if len(header) < 3:
                self.error(f"git cat-file: bad header for {sha}")
                break
            size = int(header[2])
            data = out.read(size)
            out.read(1)
            self.stats["blobs"] += 1
            self.scan_bytes(f"git:{sha[:10]}:{path}", data, path)
        t.join()
        proc.wait()


def decode_ihex(data: bytes) -> bytes:
    out = bytearray()
    for line in data.splitlines():
        line = line.strip()
        if not line.startswith(b":") or len(line) < 11:
            continue
        rec = bytes.fromhex(line[1:].decode("ascii"))
        n, rtype = rec[0], rec[3]
        if rtype == 0:
            out += rec[4:4 + n]
    return bytes(out)


def decode_uf2(data: bytes) -> bytes:
    out = bytearray()
    for i in range(0, len(data) - 511, 512):
        blk = data[i:i + 512]
        n = int.from_bytes(blk[16:20], "little")
        out += blk[32:32 + min(n, 476)]
    return bytes(out)


def load_allowlist(path):
    allow = []
    if not path:
        return allow
    for i, raw in enumerate(pathlib.Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            raise SystemExit(f"{path}:{i}: expected '<path-glob> <regex>'")
        allow.append((parts[0], re.compile(parts[1])))
    return allow


def load_extra(path):
    extra = [x.strip() for x in os.environ.get("FARO_EDITION_EXTRA_MARKERS", "").split(",")]
    if path:
        extra += [ln.strip() for ln in pathlib.Path(path).read_text(encoding="utf-8").splitlines()
                  if ln.strip() and not ln.startswith("#")]
    return [x for x in extra if x]


def run(paths, args, extra) -> int:
    markers = build_markers(extra)
    rc = 0
    for p in paths:
        root = pathlib.Path(p)
        if not root.exists():
            print(f"ERROR: {p} does not exist", file=sys.stderr)
            return 2
        allow_file = args.allowlist
        if not allow_file and root.is_dir() and (root / "scripts/check-edition.allow").is_file():
            allow_file = root / "scripts/check-edition.allow"
        sc = Scanner(markers, load_allowlist(allow_file), args.max_per_file)
        if not args.no_tree:
            sc.scan_tree(root, args.exclude)
        if not args.no_history and root.is_dir():
            sc.scan_git(root, args.exclude)
        st = sc.stats
        summary = (f"{st['files']} files, {st['blobs']} history blobs, {st['commits']} commits, {st['refs']} refs")
        if sc.hits:
            per = ", ".join(f"{k}={v}" for k, v in sorted(sc.per_marker.items()))
            print(f"check-edition: FAIL {p}: {sc.hits} hit(s) in {sc.locations} location(s) ({summary}) [{per}]")
            rc = max(rc, 1)
        else:
            print(f"check-edition: OK {p}: no private markers ({summary})")
        if sc.errors:
            print(f"check-edition: {sc.errors} scan error(s) in {p}", file=sys.stderr)
            rc = 2 if rc == 0 else rc
    return rc


# ---------------------------------------------------------------- self-test
def self_test() -> int:
    global MARKERS
    # Made-up fixture markers covering every kind, so the self-test reveals nothing real
    fixture = [("sub", "hiddenproject"), ("sub", "secretlib"), ("word", "zorx"), ("sub", "covertmode"),
               ("word", "qubo"), ("sub", "acme.example"), ("sub", "acme_example"), ("sub", "acme-example"),
               ("hexid", "cd42"), ("sub", "HiddenWidget"), ("num", "XDR-099"), ("prefix", "Quux")]
    saved = MARKERS
    MARKERS = [(k, codecs.encode(t, "rot13")) for k, t in fixture] + list(GENERIC_MARKERS[:3])
    try:
        return _self_test()
    finally:
        MARKERS = saved


def _self_test() -> int:
    import contextlib
    import random
    ms = build_markers([])
    samples = []
    for (kind, tok), m in zip(MARKERS, ms):
        t = r13(tok)
        if kind == "generated":
            w = t.split()
            s = " ".join(w[:-2] + ["[" + w[-2], w[-1] + "](x)"])
        else:
            s = {"sub": t.upper() if len(t) % 2 else t.title(), "word": t.capitalize() + "Frame",
                 "prefix": t.capitalize() + "zzy", "hexid": "0x" + t.upper(), "num": t.upper(),
                 "coauthor": t.lower()}[kind]
        samples.append((m.name, s))
    negatives = ["azorx and zorxing", "AZORX", "quboid", "deadcd42beef", "xcd42y",
                 "XDR-098 and XDR-100", "XDR-0991"]
    failures = []

    def expect(label, cond):
        print(f"  {'PASS' if cond else 'FAIL'} {label}")
        if not cond:
            failures.append(label)

    def scan(path, history=False, allow=None):
        ns = argparse.Namespace(allowlist=allow, max_per_file=1000, no_tree=False, no_history=not history,
                                exclude=[])
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = run([str(path)], ns, [])
        return rc, buf.getvalue()

    with tempfile.TemporaryDirectory(prefix="check-edition-") as tmp:
        tmp = pathlib.Path(tmp)
        # 1. every marker hits in a text file
        d = tmp / "t1"
        d.mkdir()
        (d / "pos.txt").write_text("\n".join(f"x {s} y" for _, s in samples) + "\n")
        rc, out = scan(d)
        missing = [n for n, s in samples if f"[{n}]" not in out and n not in out]
        expect("text: every marker found", rc == 1 and not missing)
        if missing:
            print("    missing:", missing)
        # 2. negatives are clean
        d = tmp / "t2"
        d.mkdir()
        (d / "neg.txt").write_text("\n".join(negatives) + "\n")
        rc, out = scan(d)
        expect("text: words inside words, hex runs and other numbers are not hits", rc == 0)
        if rc:
            print(out)
        # 3. binary: strings found, 0x id is text-only, noise ignored
        d = tmp / "t3"
        d.mkdir()
        rnd = random.Random(7)
        noise = bytes(rnd.randrange(256) for _ in range(4096))
        sb = samples[9][1].encode()
        (d / "fw.bin").write_bytes(noise + b"\0" + sb + b"_tx\0" + b"0xCD42" + b"\0" + noise)
        rc, out = scan(d)
        expect("binary: string found by offset", rc == 1 and "fw.bin@0x" in out and out.count("fw.bin@") == 1)
        # 4. archives: zip, tar.gz, deb, Intel HEX
        d = tmp / "t4"
        d.mkdir()
        payload = ("inside " + samples[0][1] + "\n").encode()
        with zipfile.ZipFile(d / "pkg.zip", "w", zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("docs/inner.txt", payload)
        tgz = io.BytesIO()
        with tarfile.open(fileobj=tgz, mode="w:gz") as tf:
            ti = tarfile.TarInfo("bundle/readme.txt")
            ti.size = len(payload)
            tf.addfile(ti, io.BytesIO(payload))
        (d / "bundle.tar.gz").write_bytes(tgz.getvalue())
        ar = b"!<arch>\n"
        for mname, body in ((b"debian-binary", b"2.0\n"), (b"data.tar.gz", tgz.getvalue())):
            ar += mname.ljust(16) + b"0".ljust(12) + b"0".ljust(6) + b"0".ljust(6) + b"100644".ljust(8) \
                + str(len(body)).encode().ljust(10) + b"`\n" + body + (b"\n" if len(body) & 1 else b"")
        (d / "pkg.deb").write_bytes(ar)
        hexdata = b"\0\0" + samples[1][1].encode() + b"\0"
        recs = []
        for off in range(0, len(hexdata), 16):
            chunk = hexdata[off:off + 16]
            rec = bytes([len(chunk), off >> 8, off & 255, 0]) + chunk
            recs.append(":" + (rec + bytes([(-sum(rec)) & 255])).hex().upper())
        (d / "fw.hex").write_text("\n".join(recs + [":00000001FF"]) + "\n")
        rc, out = scan(d)
        for label, key in (("zip", "pkg.zip!docs/inner.txt:1"), ("tar.gz", "bundle.tar.gz!bundle/readme.txt:1"),
                           ("deb", "pkg.deb!data.tar.gz!bundle/readme.txt:1"), ("hex", "fw.hex!decoded@0x")):
            expect(f"archive: {label}", key in out)
        # 5. allowlist suppresses a documented false positive
        d = tmp / "t5"
        d.mkdir()
        (d / "strings.txt").write_text(f"ok {samples[3][1]} documented\n")
        allow = tmp / "allow.txt"
        allow.write_text("# test\nstrings.txt  documented$\n")
        rc, _ = scan(d, allow=str(allow))
        expect("allowlist: hit suppressed", rc == 0)
        # 6. git history: deleted file, file name, commit message, branch name
        d = tmp / "t6"
        d.mkdir()
        hooks = tmp / "no-hooks"  # throwaway fixture repo: keep the user's global hooks out of it
        hooks.mkdir()
        g = ["git", "-C", str(d), "-c", "user.name=t", "-c", "user.email=t@example.invalid",
             "-c", "commit.gpgsign=false", "-c", "init.defaultBranch=main", "-c", f"core.hooksPath={hooks}"]
        try:
            subprocess.run(g + ["init", "-q"], check=True)
            old = d / ("old_" + samples[6][1].replace(".", "_") + ".txt")
            old.write_text("v1 " + samples[2][1] + "\n")
            subprocess.run(g + ["add", "-A"], check=True)
            subprocess.run(g + ["commit", "-q", "-m", "add\n\n" + samples[-3][1]], check=True)
            old.unlink()
            (d / "clean.txt").write_text("nothing here\n")
            subprocess.run(g + ["add", "-A"], check=True)
            subprocess.run(g + ["commit", "-q", "-m", "clean"], check=True)
            subprocess.run(g + ["branch", "feat/" + samples[4][1]], check=True)
        except (OSError, subprocess.CalledProcessError) as exc:
            expect(f"git fixture ({exc})", False)
        else:
            rc_tree, _ = scan(d)
            rc, out = scan(d, history=True)
            expect("git: working tree alone is clean", rc_tree == 0)
            expect("git: deleted blob found", "git:" in out and ":old_" in out)
            expect("git: path name found", "git-path:old_" in out)
            expect("git: commit message found", "commit:" in out)
            expect("git: branch name found", "ref:refs/heads/feat/" in out)
    print("self-test:", "FAIL" if failures else "OK", f"({len(failures)} failure(s))")
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Fail if private-edition markers are present.")
    ap.add_argument("paths", nargs="*", default=["."], help="directories or files to scan (default: .)")
    ap.add_argument("--no-history", action="store_true", help="do not scan git history")
    ap.add_argument("--no-tree", action="store_true", help="only scan git history")
    ap.add_argument("--exclude", action="append", default=[], help="fnmatch glob of paths to skip (repeatable)")
    ap.add_argument("--allowlist", help="allowlist file (default: <PATH>/scripts/check-edition.allow)")
    ap.add_argument("--markers", help="edition marker file (rot13, see header); default env FARO_EDITION_MARKERS")
    ap.add_argument("--extra-markers", help="file with extra plain-text markers, one per line")
    ap.add_argument("--max-per-file", type=int, default=20, help="hits printed per location (default 20)")
    ap.add_argument("--list-markers", action="store_true", help="print the decoded marker list and exit")
    ap.add_argument("--self-test", action="store_true", help="run the built-in fixture test and exit")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    global MARKERS
    marker_text = ""
    if args.markers:
        marker_text = pathlib.Path(args.markers).read_text(encoding="utf-8")
    elif os.environ.get("FARO_EDITION_MARKERS"):
        marker_text = os.environ["FARO_EDITION_MARKERS"]
    MARKERS = list(GENERIC_MARKERS) + parse_marker_lines(marker_text)
    if not marker_text:
        print("check-edition: WARNING: no edition marker list given (--markers / FARO_EDITION_MARKERS); "
              "only generic markers are checked", file=sys.stderr)
    extra = load_extra(args.extra_markers)
    if args.list_markers:
        for (kind, tok) in MARKERS:
            print(f"{kind:9} {r13(tok)}")
        for x in extra:
            print(f"{'extra':9} {x}")
        return 0
    return run(args.paths, args, extra)


if __name__ == "__main__":
    sys.exit(main())
