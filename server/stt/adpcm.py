"""IMA-ADPCM decoder for the stream Fidel's board sends.

Stream: 4-byte header (predictor int16 LE, step index 0..88, reserved 0), then
4-bit codes, first sample in the low nibble.
"""

from __future__ import annotations

import struct

HEADER_BYTES = 4

STEP_TABLE = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
)
INDEX_TABLE = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


class AdpcmError(ValueError):
    pass


def decode(stream: bytes) -> list[int]:
    """Returns the int16 samples of a header + codes stream."""
    if len(stream) < HEADER_BYTES:
        raise AdpcmError("stream shorter than its header")
    predictor, index, reserved = struct.unpack_from("<hBB", stream, 0)
    if index > 88 or reserved != 0:
        raise AdpcmError("bad header")
    out: list[int] = []
    for byte in stream[HEADER_BYTES:]:
        for code in (byte & 0x0F, byte >> 4):
            step = STEP_TABLE[index]
            vpdiff = step >> 3
            if code & 4:
                vpdiff += step
            if code & 2:
                vpdiff += step >> 1
            if code & 1:
                vpdiff += step >> 2
            predictor = predictor - vpdiff if code & 8 else predictor + vpdiff
            predictor = max(-32768, min(32767, predictor))
            index = max(0, min(88, index + INDEX_TABLE[code]))
            out.append(predictor)
    return out


def encode(samples: list[int], predictor: int = 0, index: int = 0) -> bytes:
    """Header + codes for int16 samples: the board's encoder, for tests and smoke checks."""
    out = bytearray(struct.pack("<hBB", predictor, index, 0))
    codes: list[int] = []
    for sample in samples:
        diff = sample - predictor
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        step = STEP_TABLE[index]
        vpdiff = step >> 3
        if diff >= step:
            code |= 4
            diff -= step
            vpdiff += step
        step >>= 1
        if diff >= step:
            code |= 2
            diff -= step
            vpdiff += step
        step >>= 1
        if diff >= step:
            code |= 1
            vpdiff += step
        predictor = predictor - vpdiff if code & 8 else predictor + vpdiff
        predictor = max(-32768, min(32767, predictor))
        index = max(0, min(88, index + INDEX_TABLE[code]))
        codes.append(code)
    if len(codes) % 2:
        codes.append(0)
    for i in range(0, len(codes), 2):
        out.append(codes[i] | (codes[i + 1] << 4))
    return bytes(out)


def to_pcm16le(samples: list[int]) -> bytes:
    return struct.pack(f"<{len(samples)}h", *samples)
