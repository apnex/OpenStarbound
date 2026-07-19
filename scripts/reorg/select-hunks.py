#!/usr/bin/env python3
# select-hunks.py <single-file-unified-diff> <hunk-indices>
# Emit the diff header (everything before the first @@) followed by ONLY the 1-based
# hunk indices named in <hunk-indices> (comma list, e.g. "1,3,4" or "all").
# Used by the reorg diff-partition: a shared file's full BASE->trunk diff is split into
# per-cluster hunk buckets; this emits one bucket as an applyable patch.
import sys, re

def main():
    if len(sys.argv) != 3:
        sys.exit("usage: select-hunks.py <patchfile> <comma-hunk-indices|all>")
    patchfile, spec = sys.argv[1], sys.argv[2]
    with open(patchfile) as f:
        lines = f.readlines()
    # header = up to first @@; hunks = @@..next-@@ blocks
    first = next((i for i, l in enumerate(lines) if l.startswith("@@")), len(lines))
    header, body = lines[:first], lines[first:]
    hunks, cur = [], None
    for l in body:
        if l.startswith("@@"):
            if cur is not None:
                hunks.append(cur)
            cur = [l]
        else:
            if cur is not None:
                cur.append(l)
    if cur is not None:
        hunks.append(cur)
    if spec == "all":
        want = set(range(1, len(hunks) + 1))
    else:
        want = set(int(x) for x in spec.split(",") if x.strip())
    bad = [i for i in want if i < 1 or i > len(hunks)]
    if bad:
        sys.exit(f"select-hunks: hunk index out of range {bad} (file has {len(hunks)} hunks)")
    out = list(header)
    for i, h in enumerate(hunks, 1):
        if i in want:
            out.extend(h)
    sys.stdout.write("".join(out))

if __name__ == "__main__":
    main()
