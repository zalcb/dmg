import pathlib
import subprocess
import sys
import tempfile


runner = str(pathlib.Path(sys.argv[1]).resolve())


def run(*args, expected=0):
    result = subprocess.run([runner, *args], capture_output=True, text=True)
    assert result.returncode == expected, result.stdout + result.stderr
    return result.stdout


def state(output):
    return next(line for line in output.splitlines() if line.startswith("cycles="))


with tempfile.TemporaryDirectory() as directory:
    trace = pathlib.Path(directory) / "cpu.log"
    plain = run("--cycles", "4194304")
    logged = run("--cycles", "4194304", "--trace", str(trace))
    assert state(plain) == state(logged) == state(run("--cycles", "4194304"))
    expected = (
        "cycles=4194312 steps=499322 frames=59 samples=48000 "
        "state=09d978f6327875d6 frame=2f37b78b2c51d661 audio=575451d81434adad"
    )
    assert state(plain) == expected, plain
    first = trace.read_text().splitlines()[0]
    assert first == (
        "A:01 F:B0 B:00 C:13 D:00 E:D8 H:01 L:4D "
        "SP:FFFE PC:0100 PCMEM:21,00,C0,34"
    ), first
    rom = bytearray(32768)
    program = bytearray()
    for byte in b"Passed":
        program.extend((0x3E, byte, 0xE0, 1, 0x3E, 0x81, 0xE0, 2))
    program.extend((0x18, 0xFE))
    rom[0x100:0x100 + len(program)] = program
    path = pathlib.Path(directory) / "serial.gb"
    path.write_bytes(rom)
    assert "Passed" in run("--rom", str(path), "--cycles", "4096", "--expect", "Passed")
    run("--rom", str(path), "--cycles", "4096", "--expect", "Missing", expected=2)
    for value in ("-1", "0", "abc", "18446744073709551615"):
        run("--cycles", value, expected=1)

print("determinism, trace, serial and cycle-bound checks passed")
