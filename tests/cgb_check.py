"""Exercise CGB model selection, physical-clock timing and RGB capture."""

import pathlib
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

from cgb_demo import Program, cartridge, demo_rom


RUNNER = str(pathlib.Path(sys.argv[1]).resolve())


def run(*args, expected=0):
    result = subprocess.run([RUNNER, *map(str, args)], capture_output=True, text=True)
    assert result.returncode == expected, result.stdout + result.stderr
    return result


def summary(result):
    line = next(line for line in result.stdout.splitlines() if line.startswith("cycles="))
    return dict(field.split("=") for field in line.split())


def model_probe(flag):
    program = Program()
    program.emit(0xFE, 0x11)
    program.branch(0x28, "cgb")
    program.write(1, ord("D"))
    program.branch(0x18, "send")
    program.label("cgb")
    program.write(1, ord("C"))
    program.label("send")
    program.write(2, 0x81)
    program.emit(0x18, 0xFE)
    return cartridge(program, flag)


def timing_probe(double_speed):
    program = Program()
    program.write(0x40, 0x91)
    program.write(0x26, 0x80)
    program.write(0x24, 0x77)
    program.write(0x25, 0x11)
    program.write(0x11, 0x80)
    program.write(0x12, 0xF0)
    program.write(0x13, 0x40)
    program.write(0x14, 0x83)
    if double_speed:
        program.write(0x4D, 1)
        program.emit(0x10, 0)
    program.emit(0x18, 0xFE)
    return cartridge(program)


def check_models(directory):
    for flag in (0, 0x80, 0xC0):
        path = directory / f"model-{flag:02x}.gb"
        path.write_bytes(model_probe(flag))
        for model in ("auto", "dmg", "cgb"):
            if flag == 0xC0 and model == "dmg":
                rejected = run("--rom", path, "--model", model, expected=1)
                assert "CGB-only" in rejected.stderr, rejected.stderr
                continue
            expected = "C" if model == "cgb" or (model == "auto" and flag) else "D"
            result = run("--rom", path, "--model", model, "--cycles", 4096, "--expect", expected)
            assert expected in result.stdout, result.stdout
        default = run("--rom", path, "--cycles", 4096)
        explicit = run("--rom", path, "--model", "auto", "--cycles", 4096)
        assert summary(default) == summary(explicit)
    boot = directory / "boot.bin"
    boot.write_bytes(bytes(256))
    rejected = run("--rom", path, "--boot", boot, expected=1)
    assert "CGB boot ROMs are not supported" in rejected.stderr
    path = directory / "model-00.gb"
    run("--rom", path, "--boot", boot, "--cycles", 4096, "--expect", "D")
    rejected = run("--rom", path, "--model", "cgb", "--boot", boot, expected=1)
    assert "CGB boot ROMs are not supported" in rejected.stderr
    run("--model", "invalid", expected=1)
    run("--model", expected=1)


def check_timing(directory):
    results = []
    for double_speed in (False, True):
        path = directory / f"timing-{double_speed}.gb"
        path.write_bytes(timing_probe(double_speed))
        results.append(summary(run("--rom", path, "--cycles", 4194304)))
    normal, fast = results
    assert int(normal["frames"]) in (59, 60), normal
    assert int(fast["frames"]) in (29, 30), fast
    assert abs(int(normal["samples"]) - 48000) <= 1, normal
    assert abs(int(fast["samples"]) - 24000) <= 4, fast
    assert normal["state"] != fast["state"]
    full_fast = summary(run("--rom", path, "--cycles", 8388608))
    assert abs(int(full_fast["frames"]) - int(normal["frames"])) <= 1
    assert abs(int(full_fast["samples"]) - int(normal["samples"])) <= 4


def check_cartridge_bounds(directory):
    path = directory / "invalid.gb"
    path.write_bytes(bytes(0x14F))
    assert "Invalid cartridge size" in run("--rom", path, expected=1).stderr
    path.write_bytes(bytes(0x10000))
    assert "exceeds the ROM size" in run("--rom", path, expected=1).stderr
    run("--cycles", 2**64 - 8192, expected=1)


def check_video(directory):
    path = directory / "chroma.gb"
    frame, video = directory / "frame.ppm", directory / "video.rgb"
    path.write_bytes(demo_rom())
    assert path.read_bytes() == demo_rom()
    arguments = ("--rom", path, "--cycles", 4194304)
    captured = summary(run(*arguments, "--frame", frame, "--video", video))
    assert captured == summary(run(*arguments))
    header = b"P6\n160 144\n255\n"
    image = frame.read_bytes()
    assert image.startswith(header) and len(image) == len(header) + 160 * 144 * 3
    rgb = image[len(header):]
    colors = set(zip(rgb[::3], rgb[1::3], rgb[2::3]))
    assert len(colors) == 18 and any(r != g or g != b for r, g, b in colors), colors
    frames = video.read_bytes()
    frame_size = 160 * 144 * 3
    assert len(frames) == int(captured["frames"]) * frame_size
    assert frames[-frame_size:] == rgb
    assert frames[-frame_size:] != frames[-2 * frame_size:-frame_size]
    dmg = summary(run(*arguments, "--model", "dmg", "--frame", frame))
    assert dmg["frame"] != captured["frame"]
    rgb = frame.read_bytes()[len(header):]
    assert all(r == g == b for r, g, b in zip(rgb[::3], rgb[1::3], rgb[2::3]))
    short = run("--rom", path, "--cycles", 4, "--frame", frame, expected=1)
    assert "no completed frames" in short.stderr
    run(*arguments, "--video", directory / "missing" / "output.rgb", expected=1)
    run(*arguments, "--frame", directory / "missing" / "output.ppm", expected=1)


with tempfile.TemporaryDirectory() as temporary:
    directory = pathlib.Path(temporary)
    check_models(directory)
    check_cartridge_bounds(directory)
    check_timing(directory)
    check_video(directory)

print("CGB model selection, speed timing, RGB output and original animated demo checks passed")
