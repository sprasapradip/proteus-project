#!/usr/bin/env python3
"""Packs the captured data into data/data.js for engine.html.

Inputs (all real captures, see README.txt):
  data/sim.jsonl   sim_capture run of the _sim_x5 controller + countdown unit
  data/clone.txt   git clone --progress of the repository
  data/ls.txt      ls of the site folder in that fresh clone
  data/build.txt   tools/build.sh in that fresh clone
  ../../test/test-report.txt   latest tools/test.sh report
"""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"

sim, logs = [], []
for line in (DATA / "sim.jsonl").read_text().splitlines():
    if not line.startswith("{"):
        continue                      # simavr "Loaded ..." banner lines
    d = json.loads(line)
    sim.append([d["t"], d["lamps"], d["walk"], d["stop"], d["disp"], d["frame"]])
    for l in d["log"]:
        logs.append([d["t"], l])

clone = [l.rstrip() for l in (DATA / "clone.txt").read_text().splitlines()]
# git prints a progress line and then the same line with ", done."; keep the final one
clone = [l for i, l in enumerate(clone)
         if not (i + 1 < len(clone) and clone[i + 1].startswith(l.split(":")[0] + ":") and "done" not in l)]

report = (HERE.parent.parent / "test" / "test-report.txt").read_text().splitlines()
start = next(i for i, l in enumerate(report) if l.startswith("=== countdown link"))
tests = [l for l in report[start:] if l.strip() and not l.startswith("===")]
tests = [re.sub(r"\s{3,}", "   ", l) for l in tests]

out = {
    "sim": sim,
    "logs": logs,
    "clone": clone,
    "ls": (DATA / "ls.txt").read_text().split(),
    "build": [l.replace("sites/site-01-narayangarh-pulchowk/", "") for l in
              (DATA / "build.txt").read_text().splitlines() if l.strip()],
    "tests": tests,
}
(DATA / "data.js").write_text("window.DATA = " + json.dumps(out) + ";\n")
print("sim frames", len(sim), "log lines", len(logs), "clone", len(clone), "tests", len(tests))
