import argparse
import os
import pathlib
import re
import shlex
import statistics
import subprocess
import tempfile


parser = argparse.ArgumentParser(description="Compare identical full-core workloads against a3f8b97")
parser.add_argument("--cycles", type=int, default=41943040)
parser.add_argument("--runs", type=int, default=3)
args = parser.parse_args()
if args.cycles <= 0 or args.runs <= 0:
    parser.error("cycles and runs must be positive")
root = pathlib.Path(__file__).resolve().parent.parent
subprocess.run(["make", "headless"], cwd=root, check=True)
flags = shlex.split(subprocess.check_output(
    ["pkg-config", "--cflags", "--libs", "raylib"], text=True
))


def output(command, cwd):
    return subprocess.check_output(command, cwd=cwd, text=True)


with tempfile.TemporaryDirectory(prefix="dmg-baseline-") as directory:
    baseline = pathlib.Path(directory)
    archive = subprocess.check_output(["git", "archive", "a3f8b97"], cwd=root)
    subprocess.run(["tar", "-x", "-C", directory], input=archive, check=True)
    (baseline / "tests").mkdir()
    for name in ("core_machine.h", "core_runner.c"):
        source = output(["git", "show", f"a85c4c4:tests/{name}"], root)
        source = re.sub(r'"../core/[^/]+/([^/]+\.h)"', r'"../include/\1"', source)
        source = source.replace("apu_cleanup(&m->apu);", "free(m->apu.audio_buffer);")
        (baseline / "tests" / name).write_text(source)
    command = [os.environ.get("CC", "clang"), "-std=c18", "-O3", "-Iinclude", "-Isrc", "-Itests",
               "tests/core_runner.c", *sorted(str(p.relative_to(baseline)) for p in (baseline / "lib").glob("*.c")),
               *flags, "-lm", "-o"]
    subprocess.run([*command, "baseline"], cwd=baseline, check=True)
    cpu = baseline / "lib/cpu.c"
    source = cpu.read_text()
    call = "    log_cpu_state(cpu); /* log the previous CPU state */"
    assert source.count(call) == 1
    cpu.write_text(source.replace(call, ""))
    subprocess.run([*command, "baseline-no-trace-control"], cwd=baseline, check=True)
    final = str(root / "build/release/gb-headless")
    cases = {
        "baseline, original per-instruction flush": [str(baseline / "baseline"), "--trace", "/dev/null"],
        "baseline, logging removed CONTROL": [str(baseline / "baseline-no-trace-control")],
        "final, buffered explicit trace": [final, "--trace", "/dev/null"],
        "final, default trace off": [final],
    }
    reference = None
    medians = {}
    for name, invocation in cases.items():
        times = []
        for _ in range(args.runs):
            result = output([*invocation, "--cycles", str(args.cycles)], root)
            lines = result.splitlines()
            state = next(line for line in lines if line.startswith("cycles="))
            if reference is None:
                reference = state
            assert state == reference, (name, state, reference)
            timing = next(line for line in lines if line.startswith("seconds="))
            times.append(float(timing.split()[0].split("=")[1]))
            print(name, timing, flush=True)
        medians[name] = statistics.median(times)
    for executable, name in ((str(baseline / "baseline"), "original"), (final, "final")):
        output([executable, "--cycles", "4096", "--trace", str(baseline / name)], root)
    assert (baseline / "original").read_bytes() == (baseline / "final").read_bytes()
    print(reference)
    print("Explicit traces match byte-for-byte at 4096 cycles.")
    print("Median seconds:", medians)
    print("Default speedup:", medians[list(cases)[0]] / medians[list(cases)[3]])
    print("Core-only speedup (logging removed from baseline control):",
          medians[list(cases)[1]] / medians[list(cases)[3]])
