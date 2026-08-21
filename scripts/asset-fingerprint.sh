# A hash of the asset chain a measurement ran against. SOURCE this; it defines one function.
#
#   . scripts/asset-fingerprint.sh
#   fp=$(asset_fingerprint "$BOOT") || fp="MISSING"
#
# WHY IT IS SHARED RATHER THAN COPIED (#277). It lived inline in lever-matrix.sh, so the matrix
# recorded which content it measured and render-profile.sh -- the entrypoint every A/B actually uses
# -- recorded nothing. Copy-pasting it into the second script would have been the same defect one
# level up: two implementations drift, and then a matrix leg and a profile leg carry two hashes that
# cannot be compared even when the content is identical.
#
# WHAT IT HASHES, AND THE LIMIT THAT FOLLOWS. Path, size and mtime -- NOT content. That is enough for
# the job it has, which is detecting that the chain moved under one machine between two runs, and it
# is cheap: the perf chain is 2.4 GB and content-hashing it per leg would cost more than some legs.
# The limits are real and worth knowing before anyone trusts it further:
#
#   * It is PATH-SENSITIVE. Copying the chain to a different directory changes the hash even though
#     every byte is identical, so it cannot compare two machines or two layouts. This was found the
#     useful way: the plan to freeze the chain into harness/fixture/ proposed "the copy is
#     byte-identical so the hash must be unchanged" as its safety check, and that check would have
#     FAILED. The freeze was dropped for other reasons first, but the assumption was wrong.
#   * A bare `touch` with no content change moves it. False positives are possible; false negatives
#     essentially are not, which is the right way round for a tripwire.
#
# DO NOT CHANGE THE ALGORITHM without deciding what happens to every fingerprint already recorded.
# matrix-20260816-160346 carries 276059e356eb74ad and the same chain reads 82514b4583a94c9b today
# (Steam updated three Workshop mods at 08:34 on 2026-08-22, mid-experiment -- which is what this
# whole facility exists to make visible). Altering the hash function silently re-bases every one of
# those into incomparability.

asset_fingerprint() {
  python3 - "${1:?usage: asset_fingerprint <sbinit.config>}" <<'ASSETPY'
import json, os, sys, hashlib
cfg = json.load(open(sys.argv[1]))
h = hashlib.sha256()
missing = []
for src in cfg.get("assetSources", []):
    if not os.path.exists(src):
        missing.append(src)
        continue
    if os.path.isdir(src):
        # A directory source is a TREE. A mod being edited changes files inside it without touching
        # the directory's own mtime, so stat'ing the directory would report "unchanged" while the
        # content under measurement moved.
        entries = []
        for root, _, files in os.walk(src):
            for f in files:
                fp = os.path.join(root, f)
                try:
                    st = os.stat(fp)
                    entries.append("%s:%d:%d" % (fp, st.st_size, int(st.st_mtime)))
                except OSError:
                    entries.append("%s:UNREADABLE" % fp)
        for e in sorted(entries):
            h.update(e.encode())
    else:
        st = os.stat(src)
        h.update(("%s:%d:%d" % (src, st.st_size, int(st.st_mtime))).encode())
if missing:
    print("MISSING:" + ",".join(missing))
    sys.exit(1)
print(h.hexdigest()[:16])
ASSETPY
}
