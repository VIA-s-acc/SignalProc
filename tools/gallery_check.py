"""Compare freshly rendered gallery SVGs with the committed ones by their text.

Run times, sizes, clock times and the status line (CPU load, cores of the machine)
change from run to run and from machine to machine, so they are blanked (also when a
narrow cell cuts them to «16.3…» or «13м…») before the comparison, and layout is ignored.
"""

import html
import re
import sys
from pathlib import Path

UNITS = r"(?:µs|мкс|ms|мс|s|с|KB|КБ|MB|МБ|GB|ГБ|B|Б|к/с|fps|Гц|Hz)"
NUMBER = re.compile(r"\d+(?:[.,]\d+)?\s*(?:%|[A-Za-zА-Яа-яµ]{0,3}…|" + UNITS + r"(?![A-Za-zА-Яа-я]))")
CLOCK = re.compile(r"\b\d{1,2}:\d{2}:\d{2}\b")


def text_of(path: Path) -> str:
    parts = [html.unescape(p) for p in re.findall(r">([^<]*)<", path.read_text(encoding="utf-8"))]
    kept = [CLOCK.sub("#", NUMBER.sub("#", p)) for p in parts if not p.strip().startswith("CPU")]
    return re.sub(r"\s+", "", "".join(kept))


def main() -> int:
    rendered, committed = Path(sys.argv[1]), Path(sys.argv[2])
    a = sorted(p.name for p in rendered.glob("*.svg"))
    b = sorted(p.name for p in committed.glob("*.svg"))
    stale = sorted(set(a) ^ set(b))
    stale += [n for n in sorted(set(a) & set(b)) if text_of(rendered / n) != text_of(committed / n)]
    if not stale:
        print(f"{committed}: {len(b)} images are current")
        return 0
    lang = committed.name
    print(f"{committed}: stale images: {', '.join(stale)}")
    print(f"update them with: signals-gallery --out {committed} --lang {lang}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
