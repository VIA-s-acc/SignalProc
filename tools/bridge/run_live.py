"""Liveness check of Signals scripts: run_live.py JOBS.

Every folder in JOBS holds first.py (built with the variable at its first value), second.py (built with the second
value) and var.txt (the Python name of the variable). The check takes the body of first.py, puts in the variable line
and the GRAPH line of second.py, appends the check of second.py and runs it on the data of first.npz with the
reference values of second.npz. A live script passes; a form that wrote the first value as a number fails.
"""

import contextlib
import io
import re
import sys
import traceback
from pathlib import Path

import numpy as np


def line_of(text, pattern):
    found = re.search(pattern, text, re.M)
    if not found:
        raise ValueError(f"no line {pattern!r}")
    return found.group(0)


def hybrid(job, var):
    first = (job / "first.py").read_text(encoding="utf-8")
    second = (job / "second.py").read_text(encoding="utf-8")
    cut_first = first.index("\nif graph_changed(")
    cut_second = second.index("\nif graph_changed(")
    body = first[:cut_first]
    assign = rf"^{re.escape(var)} = .*$"
    body = re.sub(assign, lambda _: line_of(second, assign), body, count=1, flags=re.M)
    body = re.sub(r"^GRAPH = .*$", lambda _: line_of(second, r"^GRAPH = .*$"), body, count=1, flags=re.M)
    body = body.replace('HERE / "first.npz"', 'HERE / "live.npz"')
    return body + second[cut_second:]


def merged_data(job):
    first = np.load(job / "first.npz", allow_pickle=False)
    second = np.load(job / "second.npz", allow_pickle=False)
    data = {k: first[k] for k in first.files if not k.startswith("ref.")}
    data.update({k: second[k] for k in second.files if k.startswith("ref.")})
    return data


def run(job):
    var = (job / "var.txt").read_text(encoding="utf-8").strip()
    np.savez(job / "live.npz", **merged_data(job))
    text = hybrid(job, var)
    (job / "live.py").write_text(text, encoding="utf-8")
    out = io.StringIO()
    scope = {"__name__": "__main__", "__file__": str(job / "live.py")}
    code = 0
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        try:
            exec(compile(text, str(job / "live.py"), "exec"), scope)
        except SystemExit as e:
            code = e.code if isinstance(e.code, int) else 1
        except Exception:
            traceback.print_exc()
            code = 1
    return code, out.getvalue()


def main():
    jobs = sorted(p for p in Path(sys.argv[1]).iterdir() if p.is_dir())
    failed = 0
    for job in jobs:
        try:
            code, out = run(job)
        except Exception:
            code, out = 1, traceback.format_exc()
        if code == 0:
            print(f"LIVE {job.name}")
        else:
            failed += 1
            tail = "\n".join(out.strip().splitlines()[-12:])
            print(f"DEAD {job.name}\n{tail}\nEND {job.name}")
    print(f"{len(jobs) - failed} of {len(jobs)} live")
    sys.exit(1 if failed else 0)


main()
