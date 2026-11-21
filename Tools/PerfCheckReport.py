"""Validate the packaged performance capture before using its frame timings."""

import argparse
import csv
import json
import re
from hashlib import sha256
from math import ceil, isfinite
from pathlib import Path
from statistics import mean, median

START, END, TICK_HZ = 210, 240, 60
BUDGET_MS = 1000 / TICK_HZ
VIEWPORT = (1920, 1080)
RUN_ORDER = ("1-Moving", "1-Stationary", "2-Stationary", "2-Moving", "3-Moving", "3-Stationary")
TIME = "EchoCrowd/SimSeconds"
BUILD_INPUT_DIRS = ("Source", "Config", "Content")
PROJECT_FILE = "EchoAssault.uproject"


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def build_input_snapshot(root):
    project = root / PROJECT_FILE
    paths = [project]
    for directory in BUILD_INPUT_DIRS:
        paths.extend(path for path in (root / directory).rglob("*") if path.is_file())

    hashes = {}
    for path in sorted(paths):
        digest = sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        hashes[path.relative_to(root).as_posix()] = digest.hexdigest()

    # Hash paths as well as contents so that renames change the fingerprint.
    encoded = json.dumps(hashes, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return {
        "scope": [*BUILD_INPUT_DIRS, PROJECT_FILE],
        "file_count": len(hashes),
        "sha256": sha256(encoded).hexdigest(),
        "files": hashes,
    }


def saved_viewport(path):
    content = path.read_text(encoding="utf-8-sig")
    values = tuple(re.findall(rf"^ResolutionSize{axis}=(\d+)\s*$", content, re.MULTILINE) for axis in ("X", "Y"))
    require(all(len(axis) == 1 for axis in values), "Saved resolution is missing or ambiguous")
    actual = tuple(int(axis[0]) for axis in values)
    require(actual == VIEWPORT, f"Saved resolution {actual[0]}x{actual[1]}, expected {VIEWPORT[0]}x{VIEWPORT[1]}")
    return actual


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def distribution(values):
    return {"min": min(values), "mean": mean(values), "max": max(values)}


def timings(values):
    samples = sorted(values)
    require(all(isfinite(value) and value >= 0 for value in samples), "Invalid timing sample")
    return {
        "mean_ms": mean(samples),
        "p95_ms": samples[ceil(len(samples) * .95) - 1],
        "p99_ms": samples[ceil(len(samples) * .99) - 1],
        "max_ms": samples[-1],
    }


def measure_case(case, run):
    require(run["exit_code"] == 0, f"Game exit code {run['exit_code']}")
    log = (case / "run.log").read_text(encoding="utf-8-sig", errors="replace")
    require("EchoCrowdPreview: spawned=500 requested=500 ranged=100 layout=rings" in log, "Expected 400 melee + 100 ranged at start")
    require(not re.search(r": Error:|Fatal error:|Assertion failed:", log), "Runtime error in run.log")
    saved = saved_viewport(case / "user/Saved/Config/Windows/GameUserSettings.ini")
    captures = list((case / "user").rglob("*.csv"))
    require(len(captures) == 1, f"Expected one fresh CSV, found {len(captures)}")
    warmup = None
    rows = []
    with captures[0].open(encoding="utf-8-sig", newline="") as capture:
        for row in csv.DictReader(capture):
            value = (row.get(TIME) or "").strip()
            if value in ("", TIME):
                continue
            sim_seconds = float(value)
            if START - .1 <= sim_seconds < START:
                warmup = row
            elif START <= sim_seconds < END:
                rows.append(row)
    require(len(rows) == (END - START) * TICK_HZ, f"Expected 1800 samples in [210,240), found {len(rows)}")

    def counters(name):
        return [int(row[f"EchoCrowd/{name}"]) for row in rows]

    sim = [float(row[TIME]) for row in rows]
    require(abs(sim[0] - START) < .001 and abs(sim[-1] - (END - 1 / TICK_HZ)) < .001
            and all(abs(b - a - 1 / TICK_HZ) < .001 for a, b in zip(sim, sim[1:])), "Discontinuous measurement window")
    require(set(zip(counters("ViewportWidth"), counters("ViewportHeight"))) == {VIEWPORT}, "Runtime viewport differs from 1920x1080")
    require(set(counters("Echoes")) == {3}, "Three echoes were not active for the full sample")
    require(set(counters("MinRank")) == {3}, "One or more weapons were below rank 3")
    echo_shots = [counters(f"Echo{slot}Shots") for slot in (1, 2, 3)]
    require(warmup is not None and int(warmup["EchoCrowd/Echoes"]) == 3, "Warm-up did not establish three echoes")
    warmup_shots = [int(warmup[f"EchoCrowd/Echo{slot}Shots"]) for slot in (1, 2, 3)]
    require(all(before > 0 and shots[-1] > before for before, shots in zip(warmup_shots, echo_shots)),
            "An echo did not replay attacks before and during the sample")

    alive, ranged = counters("Alive"), counters("RangedAlive")
    melee = [total - ranged_count for total, ranged_count in zip(alive, ranged)]
    require(max(alive) == 500 and max(ranged) == 100 and max(melee) == 400 and min(ranged) >= 0 and min(melee) >= 0,
            f"Crowd target not reached or exceeded (max total/ranged/melee: {max(alive)}/{max(ranged)}/{max(melee)})")
    travel = [float(row["EchoCrowd/TravelCm"]) for row in rows]
    travel_delta = travel[-1] - travel[0]
    if run["mode"] == "Moving":
        require(travel_delta > 0, "Moving route did not travel")
    else:
        require(travel == [0] * len(rows), "Stationary route moved")
    hp = [float(row["EchoCrowd/PlayerHP"]) for row in rows]
    require(all(value == 512 for value in hp), "Player HP changed during the benchmark")
    hits = counters("PlayerHits")
    kills = counters("Kills")
    echo_kills = counters("EchoKills")
    require(kills[-1] > kills[0] and echo_kills[-1] > echo_kills[0], "Actual combat or echo kills were missing")

    frame_ms = [float(row["FrameTime"]) for row in rows]
    require(all(value > 0 for value in frame_ms), "Invalid frame time")
    frame_times = timings(frame_ms)
    optional_timings = {
        "game_thread": "GameThreadTime",
        "gpu": "GPUTime",
        "physics_wait": "Exclusive/GameThread/EventWait/EndPhysics",
        "enemy_update": "EchoCrowd/GameThread/EnemyUpdate",
        "enemy_refill": "EchoCrowd/GameThread/EnemyRefill",
    }
    return {
        "status": "MeasurementValid",
        "capture": str(captures[0].relative_to(case)),
        "saved_viewport": saved,
        "runtime_viewport": VIEWPORT,
        "samples": len(rows),
        "alive": distribution(alive),
        "ranged": distribution(ranged),
        "melee": distribution(melee),
        "echo_shots_before_sample": warmup_shots,
        "echo_shots_during_sample": [shots[-1] - before for before, shots in zip(warmup_shots, echo_shots)],
        "player_travel_cm": travel_delta,
        "player_hits_during_sample": hits[-1] - hits[0],
        "player_hp": hp[0],
        "kills": kills[-1] - kills[0],
        "echo_kills": echo_kills[-1] - echo_kills[0],
        "average_fps": 1000 / frame_times["mean_ms"],
        "over_budget_frames": sum(value > BUDGET_MS for value in frame_ms),
        "p95_within_budget": frame_times["p95_ms"] <= BUDGET_MS,
        "frame": frame_times,
        "additional_timings": {
            label: timings([float(row[column]) for row in rows])
            for label, column in optional_timings.items() if column in rows[0]
        },
    }


def report_case(report, name):
    case = report / name
    try:
        manifest = read_json(report / "run.json")
        run = next(item for item in manifest["runs"] if item["name"] == name)
        result = measure_case(case, run)
    except (OSError, ValueError, KeyError, TypeError, StopIteration) as error:
        result = {"status": "MeasurementInvalid", "reason": str(error)}
    write_json(case / "result.json", result)
    require(result["status"] == "MeasurementValid", result.get("reason"))
    print(f"{name}: {result['average_fps']:.1f} FPS, p95 {result['frame']['p95_ms']:.2f} ms, "
          f"{result['over_budget_frames']}/{result['samples']} over budget")


def summarize(report):
    manifest = read_json(report / "run.json")
    require(tuple(run["name"] for run in manifest["runs"]) == RUN_ORDER, "Required six runs are missing or out of order")
    captures = {name: read_json(report / name / "result.json") for name in RUN_ORDER}
    require(all(run["status"] == "MeasurementValid" for run in captures.values()), "At least one capture is invalid")
    cases = {}
    for mode in ("Moving", "Stationary"):
        runs = [result for name, result in captures.items() if name.endswith(f"-{mode}")]
        cases[mode] = {
            "median_average_fps": median(run["average_fps"] for run in runs),
            "median_p95_ms": median(run["frame"]["p95_ms"] for run in runs),
            "over_budget_frames": sum(run["over_budget_frames"] for run in runs),
            "samples": sum(run["samples"] for run in runs),
            "all_p95_within_budget": all(run["p95_within_budget"] for run in runs),
        }
    write_json(report / "summary.json", {
        "status": "MeasurementValid",
        "git_head": manifest["git_head"],
        "dirty_worktree": bool(manifest["git_status"]),
        "executable_sha256": manifest["executable_sha256"],
        "build_inputs": manifest.get("build_inputs", {"status": "NotRecorded"}),
        "sample_game_seconds": [START, END],
        "frame_budget_ms": BUDGET_MS,
        "cases": cases,
        "runs": captures,
    })
    print(json.dumps(cases, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    command = parser.add_mutually_exclusive_group(required=True)
    command.add_argument("--case", nargs=2, metavar=("REPORT_DIR", "RUN"))
    command.add_argument("--report", type=Path)
    command.add_argument("--settings", type=Path, help="Validate only the saved resolution of an existing run")
    command.add_argument("--build-inputs", type=Path, metavar="PROJECT_DIR", help="Print a project input snapshot as JSON")
    args = parser.parse_args()
    try:
        if args.case:
            report_case(Path(args.case[0]), args.case[1])
        elif args.report:
            summarize(args.report)
        elif args.build_inputs:
            print(json.dumps(build_input_snapshot(args.build_inputs), indent=2))
        else:
            print(f"Saved resolution {saved_viewport(args.settings)} matches the target")
    except (OSError, ValueError) as error:
        parser.exit(1, f"MeasurementInvalid: {error}\n")


if __name__ == "__main__":
    main()
