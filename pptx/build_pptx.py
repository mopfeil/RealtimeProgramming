"""PowerPoint version of the beamer slides, for presenting.

    python pptx/build_pptx.py build/slides.pdf pdf/RealtimeProgramming_Slides.pptx
    python pptx/build_pptx.py --check

Every page of slides.pdf becomes one slide with the page as a full-size
picture (1920 x 1080, 128-colour PNG). The speaker notes of a slide are the
script-only text that follows its frame in chapters/*.tex: the explanations
the script has and the slides leave out.

pptx/sources.sha256 records a hash of the slide sources (main.tex,
preamble.tex, chapters/*.tex, the included example files and this script)
at the time the deck was built. --check compares it with the current
sources and fails when the deck is out of date.
"""
import argparse
import glob
import hashlib
import io
import os
import re
import sys
from difflib import SequenceMatcher

# The packages below are imported where they are used, so that --check runs
# with a plain python3 (e.g. in the git pre-commit hook).

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HASH_FILE = os.path.join(HERE, "sources.sha256")
WIDTH_PX = 1920
COLORS = 128


# ------------------------------------------------------------------ sources
def chapter_files():
    main = open(os.path.join(ROOT, "main.tex")).read()
    return [os.path.join(ROOT, m + ".tex")
            for m in re.findall(r"^\\input\{(chapters/[^}]+)\}", main, re.M)]


def source_files():
    files = [os.path.join(ROOT, "main.tex"), os.path.join(ROOT, "preamble.tex"),
             os.path.abspath(__file__)] + chapter_files()
    files += sorted(glob.glob(os.path.join(ROOT, "examples", "*", "*.ino")))
    files += sorted(glob.glob(os.path.join(ROOT, "examples", "*", "*.asm")))
    return files


def sources_hash():
    h = hashlib.sha256()
    for f in source_files():
        h.update(os.path.relpath(f, ROOT).encode() + b"\0")
        h.update(open(f, "rb").read())
    return h.hexdigest()


# ------------------------------------------------------------------ notes
def label_numbers():
    """\\ref targets from the script build (build/script.aux)."""
    aux = os.path.join(ROOT, "build", "script.aux")
    if not os.path.exists(aux):
        return {}
    return dict(re.findall(r"\\newlabel\{([^}]+)\}\{\{([^}]*)\}", open(aux).read()))


def to_text(tex, labels):
    from pylatexenc.latex2text import LatexNodes2Text
    tex = re.sub(r"(?<!\\)%.*", "", tex)
    tex = re.sub(r"\\(section|subsection)\*?\{[^}]*\}", "", tex)
    tex = re.sub(r"\\label\{[^}]*\}", "", tex)
    tex = re.sub(r"\\ref\{([^}]*)\}", lambda m: labels.get(m.group(1), "?"), tex)
    tex = re.sub(r"\\wokwiexample\{([^}]*)\}\{([^}]*)\}\{[^}]*\}",
                 lambda m: f"[Listing: {m.group(2)}, examples/{m.group(1)}/sketch.ino]", tex)
    tex = re.sub(r"\\edsimexample\{([^}]*)\}\{([^}]*)\}\{([^}]*)\}\{[^}]*\}",
                 lambda m: f"[Listing: {m.group(3)}, examples/{m.group(1)}/{m.group(2)}.asm]", tex)
    tex = re.sub(r"\\lstinputlisting(\[[^\]]*\])?\{([^}]*)\}", r"[Listing: \2]", tex)
    # pylatexenc drops \texttt and \url together with their argument
    tex = re.sub(r"\\(texttt|url)\{((?:[^{}]|\{[^{}]*\})*)\}", r"\2", tex)
    text = LatexNodes2Text(math_mode="text").latex_to_text(tex)
    text = re.sub(r"[ \t]+", " ", text)
    return re.sub(r"\n\s*\n\s*", "\n\n", text).strip()


def frames():
    """[(title, notes)] in document order; notes = text after the frame."""
    labels = label_numbers()
    out = []
    for f in chapter_files():
        src = open(f).read()
        starts = list(re.finditer(r"\\begin\{frame\}(\[[^\]]*\])?(\{((?:[^{}]|\{[^{}]*\})*)\})?", src))
        for i, m in enumerate(starts):
            end = src.find("\\end{frame}", m.end())
            stop = starts[i + 1].start() if i + 1 < len(starts) else len(src)
            title = to_text(m.group(3) or "", labels)
            out.append((title, to_text(src[end + len("\\end{frame}"):stop], labels)))
    return out


def norm(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


def page_title(page):
    """Frame title: the text in the title bar of a metropolis slide."""
    import pymupdf
    r = page.rect
    return page.get_text("text", clip=pymupdf.Rect(0, 0, r.width, r.height * 0.12)).strip()


def match_notes(doc, frs):
    """Notes per page: walk pages and frames in order, match by title."""
    notes, k = [""] * len(doc), 0
    for p in range(len(doc)):
        t = norm(page_title(doc[p]))
        if not t:
            continue
        for j in range(k, min(k + 6, len(frs))):
            if SequenceMatcher(None, t, norm(frs[j][0])).ratio() > 0.85:
                notes[p], k = frs[j][1], j + 1
                break
    return notes


# ------------------------------------------------------------------ deck
def page_png(page):
    import pymupdf
    from PIL import Image
    z = WIDTH_PX / page.rect.width
    pix = page.get_pixmap(matrix=pymupdf.Matrix(z, z))
    im = Image.frombytes("RGB", (pix.width, pix.height), pix.samples)
    im = im.quantize(colors=COLORS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    b = io.BytesIO()
    im.save(b, "PNG", optimize=True)
    b.seek(0)
    return b


def build(pdf, out):
    import pymupdf
    from pptx import Presentation
    from pptx.util import Emu
    doc = pymupdf.open(pdf)
    notes = match_notes(doc, frames())
    prs = Presentation()
    r = doc[0].rect
    prs.slide_width = Emu(12192000)                     # 13.333 in
    prs.slide_height = Emu(round(12192000 * r.height / r.width))
    blank = prs.slide_layouts[6]
    for p, page in enumerate(doc):
        s = prs.slides.add_slide(blank)
        s.shapes.add_picture(page_png(page), 0, 0, prs.slide_width, prs.slide_height)
        if notes[p]:
            s.notes_slide.notes_text_frame.text = notes[p]
    prs.core_properties.title = "Realtime Programming: slides"
    prs.core_properties.author = "Prof. Dr. Markus Pfeil"
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    prs.save(out)
    with open(HASH_FILE, "w") as f:
        f.write(sources_hash() + "\n")
    with_notes = sum(1 for n in notes if n)
    print(f"{out}: {len(doc)} slides, {with_notes} with speaker notes")


def check():
    want = open(HASH_FILE).read().strip() if os.path.exists(HASH_FILE) else ""
    if want != sources_hash():
        print("PowerPoint deck is out of date: run 'make pptx' and commit "
              "pdf/RealtimeProgramming_Slides.pptx and pptx/sources.sha256", file=sys.stderr)
        return 1
    print("PowerPoint deck is up to date")
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("pdf", nargs="?")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    if a.check:
        sys.exit(check())
    if not (a.pdf and a.out):
        ap.error("pdf and out are required")
    build(a.pdf, a.out)
