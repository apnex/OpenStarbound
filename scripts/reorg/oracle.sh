#!/bin/bash
# scripts/reorg/oracle.sh <candidate-ref>
# THE ACCEPTANCE ORACLE for the render-work branch reorg: PASS iff the candidate's tracked tree is byte-identical
# to the frozen legacy trunk dev/upstream-merge (excluding the reorg meta-work itself). An empty diff proves the
# reconstruction lost nothing and changed nothing. See docs/superpowers/specs/2026-07-19-render-branch-reorg-design.md.
set -u
cd "$(git rev-parse --show-toplevel)" || exit 2
CAND="${1:?usage: oracle.sh <candidate-ref>}"
DIFF=$(git diff --stat "$CAND" dev/upstream-merge -- . \
        ':(exclude)scripts/reorg/' \
        ':(exclude)docs/superpowers/specs/2026-07-19-render-branch-reorg-design.md' \
        ':(exclude)docs/superpowers/plans/2026-07-19-render-branch-reorg-plan.md')
if [ -z "$DIFF" ]; then
  echo "ORACLE PASS: $CAND is byte-identical to dev/upstream-merge"; exit 0
else
  echo "ORACLE FAIL: residual diff (mis-attributed or dropped hunks) --"; echo "$DIFF"; exit 1
fi
