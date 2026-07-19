#!/usr/bin/env python3
# apply-shared.py -- apply the partitioned shared-file hunks to their cluster branches.
# Render stack rebuilt nested (layer1->layer2->layer3); perf/tooling branches patched onto skeletons.
# Every (branch,file) subset comes from scripts/reorg/shared-hunks.tsv (a proven 1..N partition per file).
import subprocess, os, sys
from collections import defaultdict

REPO="/root/frackin/OpenStarbound"
WT="/root/frackin/reorg-wt"
BASE="2ea33530"; TRUNK="dev/upstream-merge"
SEL=f"{REPO}/scripts/reorg/select-hunks.py"
TMP="/tmp/claude-0/-home-apnex/c29c1332-648a-42c6-87f0-1a6f14884fb0/scratchpad"

def g(*a, check=True):
    return subprocess.run(["git","-C",WT,*a], capture_output=True, text=True, check=check)
def owned(key):
    return [l.split("\t")[0] for l in open(f"{REPO}/scripts/reorg/manifest.tsv") if l.rstrip("\n").split("\t")[1]==key]

# load partition: (branch,file)->[idx]; file->set(branch)
bybf=defaultdict(list); files=defaultdict(set)
for line in open(f"{REPO}/scripts/reorg/shared-hunks.tsv"):
    p,i,c=line.rstrip("\n").split("\t"); bybf[(c,p)].append(int(i)); files[p].add(c)

def apply_subset(branch, f, idxs):
    full=g("diff",BASE,TRUNK,"--",f).stdout
    fp=f"{TMP}/_full.patch"; sp=f"{TMP}/_sel.patch"
    open(fp,"w").write(full)
    r=subprocess.run(["python3",SEL,fp,",".join(map(str,sorted(idxs)))],capture_output=True,text=True)
    if r.returncode!=0: return False,"select:"+r.stderr
    open(sp,"w").write(r.stdout)
    ap=g("apply","--3way","--index","--recount",sp,check=False)
    if ap.returncode!=0:
        # leftover conflict/partial -> report; caller decides fallback
        return False,"apply:"+ap.stderr.strip()
    return True,""

fails=[]
def do_branch_shared(branch, only_files=None, skip_files=None):
    for f in sorted(fp for (b,fp) in bybf if b==branch):
        if only_files is not None and f not in only_files: continue
        if skip_files is not None and f in skip_files: continue
        ok,err=apply_subset(branch,f,bybf[(branch,f)])
        if ok: print(f"    apply {branch:<30} {f}  hunks={sorted(bybf[(branch,f)])}")
        else:  print(f"    FAIL  {branch:<30} {f}  -> {err}"); fails.append((branch,f,err))

RENDER={"render/layer1","render/layer2-retained-surface","render/layer3-passes"}
render_only=[p for p in files if files[p]<=RENDER]

print("=== rebuild render stack nested ===")
# layer1 = BASE + L1 owned + StarRenderer_opengl.{cpp,hpp} verbatim + L1 shared hunks
g("switch","-C","render/layer1",BASE)
l1_owned=owned("render/layer1")+["source/application/StarRenderer_opengl.cpp","source/application/StarRenderer_opengl.hpp"]
g("checkout",TRUNK,"--",*l1_owned)
do_branch_shared("render/layer1")
g("commit","-q","-m","reorg(render/layer1): surface substrate — GlRenderSurface/TexturePrimitives + whole OpenGlRenderer backend + interface/build hunks")
print("  layer1 committed", g("rev-parse","--short","render/layer1").stdout.strip())

# layer2 from layer1 + L2 shared hunks
g("switch","-C","render/layer2-retained-surface","render/layer1")
do_branch_shared("render/layer2-retained-surface")
g("commit","-q","-m","reorg(render/layer2-retained-surface): env/parallax retained caches + composite() + L2 Renderer-interface hunks")
print("  layer2 committed", g("rev-parse","--short","render/layer2-retained-surface").stdout.strip())

# layer3 from layer2 + L3 owned + L3 shared hunks
g("switch","-C","render/layer3-passes","render/layer2-retained-surface")
g("checkout",TRUNK,"--",*owned("render/layer3"))
do_branch_shared("render/layer3-passes")
g("commit","-q","-m","reorg(render/layer3-passes): GpuLightmapPass + CDL + lighting passes + L3 Renderer-interface/asset hunks")
print("  layer3 committed", g("rev-parse","--short","render/layer3-passes").stdout.strip())

print("\n=== PARTIAL ORACLE: render-only shared files == trunk at layer3 ===")
ro_resid=[]
for f in sorted(render_only):
    d=g("diff","render/layer3-passes",TRUNK,"--",f).stdout
    if d.strip(): ro_resid.append(f)
if ro_resid:
    print("  RESIDUAL (need verbatim fallback):"); [print("   ",f) for f in ro_resid]
    for f in ro_resid:
        g("checkout",TRUNK,"--",f)
    g("commit","-q","-m","reorg(render/layer3-passes): verbatim-trunk fallback for render-only files that resisted nested apply")
    print("  applied verbatim fallback + committed")
else:
    print("  ALL render-only shared files == trunk at layer3 (clean nested apply)")

print("\n=== apply shared hunks to perf/tooling branches (onto skeletons) ===")
for branch in ["perf/entity-dispatch","perf/core","perf/server-tick","perf/animation-drawable",
               "perf/world-client-lighting","tooling/telemetry","tooling/render-harness-oracle"]:
    fs=sorted(fp for (b,fp) in bybf if b==branch)
    if not fs: continue
    g("switch",branch)
    do_branch_shared(branch)
    st=g("status","--porcelain").stdout.strip()
    if st:
        g("add","-A")
        g("commit","-q","-m",f"reorg({branch}): shared-file hunks (cross-cluster; union completes at integration)")
        print(f"  {branch} committed", g("rev-parse","--short",branch).stdout.strip())

print("\n=== FAILURES ===")
print("  ", fails if fails else "NONE — every subset applied cleanly")
sys.exit(1 if fails else 0)
