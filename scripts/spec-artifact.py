#!/usr/bin/env python3
"""Render the target-state system architecture to a shareable HTML page.

WHY THIS LIVES IN THE REPO. The first version of this renderer was written ad-hoc in a session
scratchpad, published, and then lost when the scratchpad was cleared -- so the next update had to
reverse-engineer the design system out of the published HTML. That is a bad trade for a page the
Director reads more often than the markdown. The generator is now a repo artifact like every other
instrument here, and the page can be regenerated from any checkout.

WHAT IT IS NOT. This is a renderer, not a gate. It asserts nothing about the document's contents --
`spec_consistency`, `prose_claims`, `spec_measures` and the rest do that. It only has to be faithful:
every section, table, diagram and generated block in the markdown appears on the page, and the
DESIGN-STATE rail is read from the section headings rather than hand-maintained, so a section that
changes status cannot be shown stale.

    scripts/spec-artifact.py --out <path.html>
"""
import argparse
import html
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
def _spec_model():
    """The one reader of the document -- and the one declaration of its path."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("spec_model", str(REPO / "scripts" / "spec-model.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MODEL = _spec_model()
SPEC = MODEL.SPEC
CSS = pathlib.Path(__file__).resolve().parent / "spec-artifact.css"

# Section-heading suffix -> (rail class, chip label or None). A heading with no suffix is `plain`:
# it carries no design state, which is different from carrying "approved".
STATE = [
    ("NOT YET COMPUTED", "todo", "not yet computed"),
    ("NOT YET DESIGNED", "todo", "not yet designed"),
    ("PROVISIONAL", "proposed", "provisional"),
    ("DESIGNED", "plain", None),
    ("APPROVED", "approved", "approved"),
]


def inline(s):
    """Markdown inline -> HTML. Escaping happens FIRST, so the patterns below see plain delimiters."""
    s = html.escape(s, quote=False)
    s = re.sub(r'`([^`]+)`', lambda m: "<code>%s</code>" % m.group(1), s)
    s = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', s)
    s = re.sub(r'(?<![\w*])\*([^*\n]+)\*(?![\w*])', r'<em>\1</em>', s)
    s = re.sub(r'~~([^~]+)~~', r'<del>\1</del>', s)
    s = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', s)
    return s


def table(rows):
    """rows[0] is the header; rows[1] is the alignment spec and is consumed for text-align."""
    align = []
    for cell in rows[1]:
        c = cell.strip()
        align.append("right" if c.endswith(":") and not c.startswith(":")
                     else "center" if c.startswith(":") and c.endswith(":") else "left")
    def cells(r, tag):
        out = []
        for i, c in enumerate(r):
            a = align[i] if i < len(align) else "left"
            out.append('<%s style="text-align:%s">%s</%s>' % (tag, a, inline(c.strip()), tag))
        return "".join(out)
    body = "".join("<tr>%s</tr>" % cells(r, "td") for r in rows[2:])
    return ('<div class="scroll"><table><thead><tr>%s</tr></thead><tbody>%s</tbody></table></div>'
            % (cells(rows[0], "th"), body))


def render(md):
    out, nav, parts, states = [], [], [], []
    lines = md.split("\n")
    i, n = 0, len(lines)
    in_historical = False

    while i < n:
        line = lines[i]

        # markers, comments and generated-block delimiters leave no trace on the page
        if line.startswith("<!--"):
            if "HISTORICAL" in line:
                in_historical = "END" not in line
            i += 1
            continue

        if line.startswith("```"):
            lang = line[3:].strip()
            j = i + 1
            buf = []
            while j < n and not lines[j].startswith("```"):
                buf.append(lines[j]); j += 1
            body = "\n".join(buf)
            if lang == "mermaid":
                out.append('<figure class="diagram"><pre class="mermaid">%s</pre></figure>'
                           % html.escape(body, quote=False))
            elif "seam 1" in body and "─" in body:
                # the contract schematic is the thesis of the document and gets its own treatment
                out.append('<figure class="schematic"><pre>%s</pre></figure>'
                           % html.escape(body, quote=False))
            else:
                cls = ' class="lang-%s"' % lang if lang else ""
                out.append('<div class="scroll"><pre class="code"><code%s>%s</code></pre></div>'
                           % (cls, html.escape(body, quote=False)))
            i = j + 1
            continue

        if line.startswith("|"):
            rows = []
            while i < n and lines[i].startswith("|"):
                rows.append([c for c in lines[i].strip().strip("|").split("|")])
                i += 1
            if len(rows) >= 2:
                out.append(table(rows))
            continue

        m = re.match(r'^(#{1,4})\s+(.*)$', line)
        if m:
            depth, text = len(m.group(1)), m.group(2).strip()
            if depth == 1:
                # The document title lives in the hero. Everything else at h1 is a PART divider --
                # the top-level partition of the ratified TOC, and load-bearing structure rather
                # than decoration. Dropping every h1 was correct when the title was the only one;
                # after the 18-section restructure it silently discarded four of them.
                pm = re.match(r'^Part\s+([IVXLC]+)\s*[—-]\s*(.*)$', text)
                if pm:
                    roman, ptitle = pm.group(1), pm.group(2).strip()
                    slug = "part-" + roman.lower()
                    parts.append((roman, ptitle))
                    out.append('<h1 class="part" id="%s"><span class="partnum">Part %s</span>'
                               '<span class="partt">%s</span></h1>'
                               % (slug, roman, inline(ptitle)))
                    nav.append('<p class="navpart"><a href="#%s">Part %s — %s</a></p>'
                               % (slug, roman, inline(ptitle)))
                i += 1
                continue
            if depth == 2:
                sm = re.match(r'^(\d+)\.\s+(.*)$', text)
                num, title = (sm.group(1) + ".", sm.group(2)) if sm else ("", text)
                cls, chip = "plain", None
                for suffix, c, label in STATE:
                    if title.upper().endswith("— " + suffix) or title.upper().endswith("- " + suffix):
                        title = re.split(r'\s+[—-]\s+', title)[0]
                        cls, chip = c, label
                        break
                slug = re.sub(r'[^a-z0-9]+', '-', (num + " " + title).lower()).strip('-')
                states.append((num.rstrip("."), cls))
                nav.append('<a href="#%s" class="%s"><span class="n">%s</span>'
                           '<span class="t">%s</span><span class="d"></span></a>'
                           % (slug, cls, num, inline(title)))
                chiphtml = '<span class="chip %s">%s</span>' % (cls, chip) if chip else ""
                out.append('<h2 id="%s"><span class="secnum">%s</span>'
                           '<span class="h2t">%s</span>%s</h2>'
                           % (slug, num, inline(title), chiphtml))
            else:
                slug = re.sub(r'[^a-z0-9]+', '-', text.lower()).strip('-')[:80]
                out.append('<h%d id="%s">%s</h%d>' % (depth, slug, inline(text), depth))
            i += 1
            continue

        if line.startswith(">"):
            buf = []
            while i < n and lines[i].startswith(">"):
                buf.append(lines[i].lstrip(">").strip()); i += 1
            paras = "\n".join(buf).split("\n\n")
            out.append("<blockquote>%s</blockquote>"
                       % "".join("<p>%s</p>" % inline(p.replace("\n", " ")) for p in paras if p.strip()))
            continue

        if re.match(r'^\s*([-*]|\d+\.)\s+', line):
            tag = "ul" if re.match(r'^\s*[-*]\s+', line) else "ol"
            items, cur, base = [], None, None
            while i < n and (re.match(r'^\s*([-*]|\d+\.)\s+', lines[i])
                             or (cur is not None and lines[i].startswith("  ") and lines[i].strip())):
                mm = re.match(r'^(\s*)([-*]|\d+\.)\s+(.*)$', lines[i])
                if mm:
                    indent = len(mm.group(1))
                    if base is None:
                        base = indent
                    if indent > base:                     # nested item: fold into the parent's text
                        cur = (cur or "") + " " + mm.group(3).strip()
                    else:
                        if cur is not None:
                            items.append(cur)
                        cur = mm.group(3).strip()
                else:
                    cur = (cur or "") + " " + lines[i].strip()
                i += 1
            if cur is not None:
                items.append(cur)
            out.append("<%s>%s</%s>" % (tag, "".join("<li>%s</li>" % inline(x) for x in items), tag))
            continue

        if line.strip() == "---":
            out.append("<hr/>")
            i += 1
            continue

        if not line.strip():
            i += 1
            continue

        buf = []
        while i < n and lines[i].strip() and not re.match(
                r'^(#{1,4}\s|\||```|>|\s*([-*]|\d+\.)\s|---\s*$|<!--)', lines[i]):
            buf.append(lines[i]); i += 1
        if buf:
            cls = ' class="historical"' if in_historical else ""
            out.append("<p%s>%s</p>" % (cls, inline(" ".join(buf))))
    return "\n".join(out), nav, parts, states


def head_facts():
    """Facts for the hero, taken from the tree rather than typed."""
    sha = subprocess.run(["git", "rev-parse", "--short=8", "HEAD"], capture_output=True, text=True,
                         cwd=str(REPO)).stdout.strip()
    branch = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"], capture_output=True,
                            text=True, cwd=str(REPO)).stdout.strip()
    spec = importlib_model()
    comp, grants, elem = spec.load()
    return sha, branch, len(comp), len(elem), len(grants)


def importlib_model():
    import importlib.util
    s = importlib.util.spec_from_file_location("spec_model", str(REPO / "scripts/spec-model.py"))
    m = importlib.util.module_from_spec(s)
    s.loader.exec_module(m)
    return m


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)

    md = SPEC.read_text(encoding="utf-8")
    sha, branch, ncomp, nelem, ngrants = head_facts()

    # the hero is assembled from the document's own opening, not duplicated by hand
    banner = re.search(r'^> (\*\*STATUS.*?)\n\n', md, re.S | re.M)
    banner_txt = inline(re.sub(r'\n> ?', ' ', banner.group(1)).strip()) if banner else ""
    goal = re.search(r'\*\*Goal\.\*\*(.*?)\n\n', md, re.S)
    goal_txt = inline(goal.group(1).replace("\n", " ").strip()) if goal else ""

    body, nav, parts, states = render(md)
    css = CSS.read_text(encoding="utf-8")

    # The design-state summary is DERIVED, like the rail. It used to be three hand-written words
    # in the template ("provisional: Sections 1, 4, 5"), which survived the 18-section restructure
    # naming sections that no longer carry that state -- the exact drift this generator's docstring
    # claims is impossible. A summary that cannot go stale has to be computed from the same source
    # the rail reads.
    def _summary(want):
        ns = [num for num, cls in states if cls == want and num]
        if not ns:
            return "none"
        return ("Section " if len(ns) == 1 else "Sections ") + ", ".join(ns)

    approved_txt, provisional_txt = _summary("approved"), _summary("proposed")
    parts_txt = "%d — %s" % (len(parts), ", ".join(t for _, t in parts)) if parts else "none"

    page = """<title>Target State System Architecture</title>
<style>%s</style>

<div class="wrap">
<nav aria-label="Sections and design state"><p class="navhead">Design state</p>%s<div class="navkey"><span class="approved"><i></i>approved</span><span class="proposed"><i></i>provisional</span><span class="todo"><i></i>not yet designed</span></div></nav>
<main>
<header class="hero"><p class="eyebrow">OpenStarbound · target-state architecture · task #204</p><h1>Target State System Architecture</h1><p class="standfirst">%s</p><div class="banner"><span class="chip proposed">work in progress</span><p>%s</p></div><dl class="meta"><div><dt>approved</dt><dd>%s</dd></div><div><dt>provisional</dt><dd>%s</dd></div><div><dt>parts</dt><dd>%s</dd></div><div><dt>components</dt><dd>%d in 4 zone directories</dd></div><div><dt>runtime elements</dt><dd>%d</dd></div><div><dt>grant rows</dt><dd>%d</dd></div><div><dt>snapshot of</dt><dd><code>%s</code> on <code>%s</code></dd></div></dl><p class="note">A rendered copy of <code>%s</code>, generated by <code>scripts/spec-artifact.py</code>. The repository holds the authoritative version; this page does not update with it.</p></header>
%s
</main>
</div>
""" % (css, "".join(nav), goal_txt, banner_txt, approved_txt, provisional_txt, parts_txt,
       ncomp, nelem, ngrants, sha, branch, SPEC.relative_to(REPO), body)

    pathlib.Path(args.out).write_text(page, encoding="utf-8")
    print("spec-artifact: wrote %s (%.1f KB, %d sections in %d parts)"
          % (args.out, len(page) / 1024, len(states), len(parts)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
