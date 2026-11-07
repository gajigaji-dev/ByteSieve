"""ByteSieve 앱 아이콘(res/bytesieve.ico)을 만든다. Python 표준 라이브러리만 쓴다.

    python tools/make_icon.py            # res/bytesieve.ico 를 다시 만든다

빌드에는 필요 없다(만든 .ico 를 저장소에 둔다). 디자인을 바꾸면 이 스크립트를 고친 뒤 다시 실행한다.

모양: 강조색(파랑) 둥근 사각형 바탕에 흰 체(그릇 모양, 구멍 세 줄)와 그 아래로 떨어지는 바이트 조각 세 개.
크기마다 4×4 슈퍼샘플링으로 가장자리를 부드럽게 하고, 16·24·32·48·64·256 픽셀을 PNG 로 담는다
(Windows Vista 이후 ICO 는 PNG 항목을 지원한다).
"""

import math
import os
import struct
import zlib

SIZES = (16, 24, 32, 48, 64, 256)
SS = 4  # 슈퍼샘플링 배율

BG_TOP = (0x1E, 0x88, 0xE5)
BG_BOTTOM = (0x0B, 0x5C, 0xAD)
WHITE = (0xFF, 0xFF, 0xFF)
BYTE = (0xFF, 0xD5, 0x4F)  # 떨어지는 조각(노랑)


def inside_round_rect(x, y, x0, y0, x1, y1, r):
    """(x, y) 가 둥근 사각형 안이면 True. 좌표는 0..1 단위."""
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def sample(x, y, small):
    """0..1 좌표의 색과 불투명도(RGBA). small 이면 작은 크기용으로 모양을 굵게 한다."""
    # 바탕: 둥근 사각형, 위→아래 그라데이션
    if not inside_round_rect(x, y, 0.02, 0.02, 0.98, 0.98, 0.20):
        return (0, 0, 0, 0)
    t = y
    bg = tuple(int(BG_TOP[i] * (1 - t) + BG_BOTTOM[i] * t) for i in range(3))

    # 체: 위가 평평한 반원 그릇(테두리 + 안쪽 격자 구멍)
    cx, cy, rad = 0.5, 0.30, 0.34 if not small else 0.36
    rim = 0.06 if not small else 0.09
    d = math.hypot(x - cx, y - cy)
    in_bowl = y >= cy and d <= rad
    # 그릇 위 테두리(가로 막대)
    if abs(y - cy) <= rim / 2 and abs(x - cx) <= rad + rim * 0.6:
        return WHITE + (255,)
    if in_bowl:
        if d >= rad - rim:
            return WHITE + (255,)  # 둥근 테두리
        # 안쪽: 흰 판에 구멍(바탕색 점) 격자. 작은 크기에서는 구멍을 생략해 뭉개지지 않게 한다.
        if small:
            return WHITE + (255,)
        gx = (x - cx) / 0.085
        gy = (y - cy - 0.06) / 0.085
        hole = (gx - round(gx)) ** 2 + (gy - round(gy)) ** 2 <= 0.30 ** 2 and gy > -0.3
        return (bg + (255,)) if hole else (WHITE + (255,))

    # 떨어지는 바이트 조각: 그릇 아래 작은 사각형 세 개
    pieces = ((0.38, 0.78), (0.53, 0.86), (0.62, 0.74)) if not small else ((0.40, 0.82), (0.60, 0.82))
    half = 0.045 if not small else 0.07
    for px, py in pieces:
        if abs(x - px) <= half and abs(y - py) <= half:
            return BYTE + (255,)
    return bg + (255,)


def render(size):
    """size×size RGBA 바이트(위에서 아래로)."""
    small = size <= 24
    rows = []
    for j in range(size):
        row = bytearray()
        for i in range(size):
            acc = [0, 0, 0, 0]
            for sj in range(SS):
                for si in range(SS):
                    r, g, b, a = sample((i + (si + 0.5) / SS) / size, (j + (sj + 0.5) / SS) / size, small)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = SS * SS
            a = acc[3] / n
            if acc[3] > 0:
                row += bytes((int(acc[0] / acc[3]), int(acc[1] / acc[3]), int(acc[2] / acc[3]), int(a)))
            else:
                row += b"\0\0\0\0"
        rows.append(bytes(row))
    return rows


def png(size, rows):
    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    raw = b"".join(b"\0" + r for r in rows)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8비트 RGBA
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    images = [png(s, render(s)) for s in SIZES]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b""
    for s, data in zip(SIZES, images):
        dim = 0 if s >= 256 else s
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "bytesieve.ico")
    with open(out, "wb") as f:
        f.write(header + entries + b"".join(images))
    print("wrote", os.path.normpath(out))


if __name__ == "__main__":
    main()
