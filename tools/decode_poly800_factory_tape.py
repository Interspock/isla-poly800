#!/usr/bin/env python3
"""Decode a Korg Poly-800 MkI factory cassette WAV/SoundDiviner .sdp.

This tool does not contain or redistribute any factory binary asset. It accepts
an external PCM16 mono WAV or an .sdp ZIP containing the WAV, validates the
cassette framing/checksum, decodes the 64 21-byte programs and emits the 47
stock sound parameters used by ISLA Poly-800.
"""

from __future__ import annotations

import argparse
import array
import csv
import io
import pathlib
import sys
import wave
import zipfile

PROGRAMS = [10 * bank + slot for bank in range(1, 9) for slot in range(1, 9)]
PARAMS = [11,12,13,14,15,16,17,18,21,22,23,24,25,26,27,31,32,33,
          41,42,43,44,45,46,48,51,52,53,54,55,56,61,62,63,64,65,66,
          71,72,73,74,75,76,81,82,83,84]
RANGES = {
    11:(1,3),12:(1,2),13:(0,1),14:(0,1),15:(0,1),16:(0,1),17:(0,31),18:(1,2),
    21:(1,3),22:(1,2),23:(0,1),24:(0,1),25:(0,1),26:(0,1),27:(0,31),
    31:(0,12),32:(0,3),33:(0,15),41:(0,99),42:(0,15),43:(0,2),44:(1,2),
    45:(0,15),46:(1,2),48:(0,1),
    51:(0,31),52:(0,31),53:(0,31),54:(0,31),55:(0,31),56:(0,31),
    61:(0,31),62:(0,31),63:(0,31),64:(0,31),65:(0,31),66:(0,31),
    71:(0,31),72:(0,31),73:(0,31),74:(0,31),75:(0,31),76:(0,31),
    81:(0,15),82:(0,15),83:(0,15),84:(0,15),
}
FACTORY_META = bytes.fromhex(
    "00 b1 00 09 00 0b 00 03 02 01 00 "
    "ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff"
)


def load_wav_bytes(path: pathlib.Path) -> bytes:
    data = path.read_bytes()
    if data.startswith(b"RIFF") and data[8:12] == b"WAVE":
        return data
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as zf:
            candidates = []
            for name in zf.namelist():
                head = zf.read(name)[:12]
                if head.startswith(b"RIFF") and head[8:12] == b"WAVE":
                    candidates.append(name)
            if len(candidates) != 1:
                raise ValueError(f"expected exactly one WAV in {path.name}, found {len(candidates)}")
            return zf.read(candidates[0])
    raise ValueError("input must be a PCM WAV or an .sdp/ZIP containing exactly one WAV")


def wav_samples(data: bytes) -> tuple[int, array.array]:
    with wave.open(io.BytesIO(data), "rb") as wf:
        if wf.getnchannels() != 1 or wf.getsampwidth() != 2 or wf.getcomptype() != "NONE":
            raise ValueError("factory cassette decoder expects mono 16-bit PCM")
        rate = wf.getframerate()
        raw = wf.readframes(wf.getnframes())
    samples = array.array("h")
    samples.frombytes(raw)
    if sys.byteorder != "little":
        samples.byteswap()
    return rate, samples


def transition_bits(rate: int, samples: array.array) -> list[int]:
    peak = max(abs(x) for x in samples)
    threshold = max(512, int(peak * 0.15))
    state = 1 if samples[0] > threshold else (-1 if samples[0] < -threshold else 0)
    transitions = []
    for i, x in enumerate(samples[1:], 1):
        if state <= 0 and x > threshold:
            state = 1
            transitions.append(i)
        elif state >= 0 and x < -threshold:
            state = -1
            transitions.append(i)
    if len(transitions) < 1000:
        raise ValueError("not enough FSK transitions found")
    short = rate / (2.0 * 1550.0)
    long = rate / (2.0 * 700.0)
    split = (short + long) * 0.5
    return [1 if (b - a) < split else 0 for a, b in zip(transitions, transitions[1:])]


def framed_bytes(bits: list[int], offset: int) -> tuple[bytes, list[bool]]:
    values = bytearray()
    valid = []
    for pos in range(offset, len(bits) - 10, 11):
        frame = bits[pos:pos + 11]
        valid.append(frame[0] == 0 and frame[9] == 1 and frame[10] == 1)
        value = 0
        for bit in range(8):
            value |= frame[1 + bit] << bit
        values.append(value)
    return bytes(values), valid


def decode_dump(rate: int, samples: array.array) -> bytes:
    bits = transition_bits(rate, samples)
    matches = []
    for offset in range(11):
        values, valid = framed_bytes(bits, offset)
        pos = values.find(b"\xb3\xbf\x00")
        if pos >= 0 and pos + 1636 <= len(values) and all(valid[pos:pos + 1636]):
            matches.append(values[pos:pos + 1636])
    if len(matches) != 1:
        raise ValueError(f"expected one fully-framed factory dump candidate, found {len(matches)}")
    dump = matches[0]
    if dump[259:290] != FACTORY_META:
        raise ValueError("factory metadata block does not match known MkI cassette layout")
    expected = sum(dump[3:1635]) & 0xff
    if dump[1635] != expected:
        raise ValueError(f"cassette checksum mismatch: stored 0x{dump[1635]:02x}, computed 0x{expected:02x}")
    return dump


def bits_le(bits: list[int], start: int, count: int) -> int:
    return sum(bits[start + i] << i for i in range(count))


def unpack_program(block: bytes) -> dict[int, int]:
    if len(block) != 21:
        raise ValueError("Poly-800 program block must be 21 bytes")
    bits = [(byte >> bit) & 1 for byte in block for bit in range(8)]
    d = {}
    d[11] = bits_le(bits, 0, 2) + 1
    d[21] = bits_le(bits, 2, 2) + 1
    d[12] = bits_le(bits, 4, 2) + 1
    d[22] = bits_le(bits, 6, 2) + 1
    for p, bit in zip((13,14,15,16), range(8,12)): d[p] = bits[bit]
    for p, bit in zip((23,24,25,26), range(12,16)): d[p] = bits[bit]
    d[32] = bits_le(bits, 16, 2)
    d[18] = bits[22] + 1
    d[48] = 1 - bits[23]
    d[31] = bits_le(bits, 24, 4)
    d[33] = bits_le(bits, 28, 4)
    d[45] = bits_le(bits, 32, 4)
    d[44] = bits[36] + 1
    d[17] = bits_le(bits, 40, 2) | (bits_le(bits, 37, 3) << 2)
    d[27] = bits_le(bits, 42, 5)
    d[41] = bits_le(bits, 48, 7)
    d[46] = bits[55] + 1
    d[81] = bits_le(bits, 56, 4)
    d[82] = bits_le(bits, 60, 4)
    d[83] = bits_le(bits, 64, 4)
    d[84] = bits_le(bits, 68, 4)
    start = 72
    for p in (51,52,53,54,55,56,61,62,63,64,65,66,71,72,73,74,75,76):
        d[p] = bits_le(bits, start, 5)
        start += 5
    d[42] = bits_le(bits, 162, 4)
    d[43] = bits_le(bits, 166, 2)
    for p in PARAMS:
        lo, hi = RANGES[p]
        if not lo <= d[p] <= hi:
            raise ValueError(f"decoded p{p}={d[p]} outside {lo}..{hi}")
    return d


def decode_programs(dump: bytes):
    payload = dump[290:1634]
    if len(payload) != 64 * 21:
        raise ValueError("factory patch payload is not 64 x 21 bytes")
    return [(program, unpack_program(payload[i*21:(i+1)*21])) for i, program in enumerate(PROGRAMS)]


def write_csv(rows, out) -> None:
    writer = csv.writer(out, lineterminator="\n")
    writer.writerow(["program"] + [f"p{p}" for p in PARAMS])
    for program, values in rows:
        writer.writerow([program] + [values[p] for p in PARAMS])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=pathlib.Path, help="factory cassette WAV or SoundDiviner .sdp")
    parser.add_argument("-o", "--output", type=pathlib.Path, help="write canonical CSV instead of stdout")
    args = parser.parse_args()
    try:
        rate, samples = wav_samples(load_wav_bytes(args.input))
        dump = decode_dump(rate, samples)
        rows = decode_programs(dump)
        if args.output:
            with args.output.open("w", newline="", encoding="utf-8") as out:
                write_csv(rows, out)
        else:
            write_csv(rows, sys.stdout)
        print(f"decoded 64/64 programs; cassette checksum 0x{dump[1635]:02x} OK; {rate} Hz PCM source", file=sys.stderr)
    except (OSError, ValueError, wave.Error, zipfile.BadZipFile) as exc:
        print(f"factory cassette decode failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
