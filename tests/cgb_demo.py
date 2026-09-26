"""Generate an original, asset-free animated CGB color-card ROM."""

import argparse
import pathlib


class Program:
    def __init__(self, origin=0x150):
        self.origin = origin
        self.code = bytearray()
        self.labels = {}
        self.branches = []

    def emit(self, *values):
        self.code.extend(values)

    def word(self, opcode, value):
        self.code.extend((opcode, value & 255, value >> 8))

    def write(self, register, value):
        self.code.extend((0x3E, value, 0xE0, register))

    def label(self, name):
        self.labels[name] = len(self.code)

    def branch(self, opcode, name):
        self.code.extend((opcode, 0))
        self.branches.append((len(self.code) - 1, name))

    def finish(self):
        for offset, name in self.branches:
            displacement = self.labels[name] - offset - 1
            assert -128 <= displacement <= 127
            self.code[offset] = displacement & 255
        return self.code


def cartridge(program, flag=0x80, title=b"CHROMA LAB"):
    rom = bytearray(32768)
    rom[0x100:0x104] = bytes((0x00, 0xC3, 0x50, 0x01))
    rom[0x134:0x134 + len(title)] = title
    rom[0x143] = flag
    rom[0x14D] = (-sum(rom[0x134:0x14D]) - 25) & 255
    code = program.finish()
    rom[program.origin:program.origin + len(code)] = code
    return rom


FONT = {
    "A": (14, 17, 17, 31, 17, 17, 17),
    "B": (30, 17, 17, 30, 17, 17, 30),
    "C": (15, 16, 16, 16, 16, 16, 15),
    "G": (15, 16, 16, 23, 17, 17, 15),
    "H": (17, 17, 17, 31, 17, 17, 17),
    "L": (16, 16, 16, 16, 16, 16, 31),
    "M": (17, 27, 21, 21, 17, 17, 17),
    "O": (14, 17, 17, 17, 17, 17, 14),
    "R": (30, 17, 17, 30, 20, 18, 17),
    " ": (0, 0, 0, 0, 0, 0, 0),
}


def color_bytes(colors):
    data = bytearray()
    for red, green, blue in colors:
        value = red | (green << 5) | (blue << 10)
        data.extend((value & 255, value >> 8))
    return data


def draw_text(pixels, message, top, scale):
    left = (160 - (len(message) * 6 - 1) * scale) // 2
    for index, character in enumerate(message):
        for row, bits in enumerate(FONT[character]):
            for column in range(5):
                if bits & (1 << (4 - column)):
                    for dy in range(scale):
                        for dx in range(scale):
                            pixels[top + row * scale + dy][left + (index * 6 + column) * scale + dx] = 3


def artwork():
    pixels = [[0] * 160 for _ in range(144)]
    for y in range(40, 112):
        for x in range(8, 152):
            radius = abs(x - 79) + abs(y - 75) * 2
            pixels[y][x] = (3 if radius % 24 < 3 else 1 + (radius // 12) % 2)
    for y in (34, 116):
        for x in range(8, 152):
            pixels[y][x] = 2

    draw_text(pixels, "CHROMA", 12, 2)
    draw_text(pixels, "CGB COLOR LAB", 127, 1)
    banks = [bytearray(16), bytearray(16)]
    lookup = [{bytes(16): 0}, {bytes(16): 0}]
    tilemap, attributes = bytearray(1024), bytearray(1024)
    for ty in range(18):
        for tx in range(20):
            bank = tx % 2
            flip = (tx + ty) % 3 == 0
            tile = bytearray()
            for row in pixels[ty * 8:ty * 8 + 8]:
                colors = row[tx * 8:tx * 8 + 8]
                if flip:
                    colors = colors[::-1]
                tile.extend(sum(((color >> plane) & 1) << (7 - x) for x, color in enumerate(colors)) for plane in range(2))
            key = bytes(tile)
            if key not in lookup[bank]:
                lookup[bank][key] = len(banks[bank]) // 16
                banks[bank].extend(tile)
            tilemap[ty * 32 + tx] = lookup[bank][key]
            palette = min(7, tx * 8 // 20) if 4 <= ty <= 14 else 0
            attributes[ty * 32 + tx] = palette | (bank << 3) | (0x20 if flip else 0)
    assert all(len(bank) < 0xF0 * 16 for bank in banks)
    return banks, tilemap, attributes


def demo_rom():
    program = Program()
    program.emit(0xF3)
    program.write(0x40, 0)
    payload = bytearray()

    def store(data):
        address = 0x1000 + len(payload)
        payload.extend(data)
        return address

    def copy(data, destination):
        source = store(data)
        program.word(0x11, source)
        program.word(0x21, destination)
        program.word(0x01, len(data))
        loop = f"copy_{source}"
        program.label(loop)
        program.emit(0x1A, 0x13, 0x22, 0x0B, 0x78, 0xB1)
        program.branch(0x20, loop)

    accents = [(4, 25, 31), (5, 15, 31), (15, 9, 31), (29, 7, 26),
               (31, 10, 13), (31, 20, 5), (25, 28, 4), (5, 27, 18)]
    palettes = bytearray()
    for color in accents:
        bright = tuple(min(31, component // 2 + 15) for component in color)
        palettes.extend(color_bytes([(2, 3, 7), color, bright, (29, 31, 31)]))
    for register in (0x68, 0x6A):
        program.write(register, 0x80)
        source = store(palettes)
        program.word(0x11, source)
        program.emit(0x06, 64)
        loop = f"palette_{register}"
        program.label(loop)
        program.emit(0x1A, 0x13, 0xE0, register + 1, 0x05)
        program.branch(0x20, loop)
    banks, tilemap, attributes = artwork()
    for bank in range(2):
        program.write(0x4F, bank)
        copy(banks[bank], 0x8000)
        copy(tilemap if bank == 0 else attributes, 0x9800)
    sparkle = bytes((0x18, 0x18, 0x18, 0x18, 0x7E, 0x7E, 0xFF, 0xFF,
                     0xFF, 0xFF, 0x7E, 0x7E, 0x18, 0x18, 0x18, 0x18))
    copy(sparkle, 0x8F00)
    objects = bytearray(160)
    for index in range(8):
        objects[index * 4:index * 4 + 4] = bytes((64 + (index % 3) * 16, 12 + index * 20, 0xF0, 8 | index))
    copy(objects, 0xFE00)
    program.write(0x4F, 0)
    program.write(0x40, 0x93)
    program.label("visible")
    program.emit(0xF0, 0x44, 0xFE, 144)
    program.branch(0x30, "visible")
    program.label("vblank")
    program.emit(0xF0, 0x44, 0xFE, 144)
    program.branch(0x38, "vblank")
    program.word(0x21, 0xFE01)
    program.emit(0x06, 8)
    program.label("animate")
    program.emit(0x34, 0x23, 0x23, 0x23, 0x23, 0x05)
    program.branch(0x20, "animate")
    program.branch(0x18, "visible")
    assert program.origin + len(program.code) <= 0x1000
    rom = cartridge(program)
    assert 0x1000 + len(payload) <= len(rom)
    rom[0x1000:0x1000 + len(payload)] = payload
    checksum = sum(rom[:0x14E]) + sum(rom[0x150:])
    rom[0x14E:0x150] = (checksum & 0xFFFF).to_bytes(2, "big")
    return rom


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    args.output.write_bytes(demo_rom())
    print(f"Wrote original animated CGB demo: {args.output}")
