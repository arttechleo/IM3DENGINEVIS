#!/usr/bin/env python3
"""
Pure-Python SPZ → PLY converter.
No external 'spz' package required — decodes the binary format directly.
Requires: numpy only (auto-installed if missing).

SPZ binary layout (after gzip decompress):
  Header:   16 bytes  — magic / version / numPoints / shDegree / fractionalBits / flags / reserved
  Positions: numPoints × 9 bytes  — 3 × 24-bit signed little-endian fixed-point
  Alphas:    numPoints × 1 byte   — uint8, logit-quantised opacity
  Colors:    numPoints × 3 bytes  — uint8 per channel, SH-DC-quantised
  Scales:    numPoints × 3 bytes  — uint8, log-quantised
  Rotations: numPoints × 3 bytes  — int8 xyz (version < 3); or
             numPoints × 4 bytes  — uint8 "smallest-three" (version ≥ 3)
  SH:        numPoints × shDim × 3 bytes  — int8, skipped if shDegree == 0

Usage:
  python3 ConvertSpzToPly.py <input.spz> <output.ply>
"""

# ──────────────────────── numpy bootstrap ────────────────────────
import sys
import subprocess
import importlib


def _ensure_numpy() -> None:
    try:
        import numpy  # noqa: F401
        print("ConvertSpzToPly: numpy ok", flush=True)
        return
    except ImportError:
        pass
    print("ConvertSpzToPly: numpy not found — installing...", flush=True)
    try:
        result = subprocess.run(
            [sys.executable, "-m", "pip", "install", "numpy",
             "--quiet", "--disable-pip-version-check"],
            timeout=120,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if result.returncode != 0:
            print(f"ConvertSpzToPly: pip install numpy failed:\n{result.stdout}",
                  file=sys.stderr, flush=True)
            sys.exit(2)
        importlib.invalidate_caches()
        import numpy  # noqa: F401
        print("ConvertSpzToPly: numpy installed ok", flush=True)
    except Exception as exc:
        print(f"ConvertSpzToPly: FATAL: cannot install numpy: {exc}",
              file=sys.stderr, flush=True)
        sys.exit(2)


_ensure_numpy()

# ──────────────────────── main imports ───────────────────────────
import gzip
import os
import struct

import numpy as np

# ──────────────────────── constants ──────────────────────────────
SPZ_MAGIC       = 0x5053474E  # "NGSP" little-endian
COLOR_SCALE     = 0.15        # SH DC coefficient quantisation scale
SQRT1_2         = np.float32(0.7071067811865476)


def _sh_dim(degree: int) -> int:
    """Number of higher-order SH coefficients per colour channel (DC excluded)."""
    return {0: 0, 1: 3, 2: 8, 3: 15, 4: 24}.get(degree, 0)


# ──────────────────────── SPZ loader ─────────────────────────────

def load_spz(path: str):
    """
    Decode a .spz file.

    Returns
    -------
    positions  : (N, 3) float32  — world-space XYZ
    opacity    : (N,)   float32  — raw logit (pre-sigmoid), as stored in PLY
    colors     : (N, 3) float32  — f_dc RGB SH coefficients
    scales     : (N, 3) float32  — log scale (as stored in PLY)
    rotations  : (N, 4) float32  — normalised quaternion, WXYZ order
    num_points : int
    """
    with gzip.open(path, "rb") as fh:
        raw_bytes = fh.read()

    data = np.frombuffer(raw_bytes, dtype=np.uint8)

    # ── Header (16 bytes) ────────────────────────────────────────
    if len(data) < 16:
        raise ValueError(f"File too small ({len(data)} bytes) to be a valid .spz")

    magic, version, num_points = struct.unpack_from("<III", raw_bytes, 0)
    sh_degree, frac_bits, flags, _reserved = struct.unpack_from("BBBB", raw_bytes, 12)

    if magic != SPZ_MAGIC:
        raise ValueError(f"Bad SPZ magic: 0x{magic:08X} (expected 0x{SPZ_MAGIC:08X})")
    if version < 1 or version > 4:
        raise ValueError(f"Unsupported SPZ version: {version}")

    print(
        f"ConvertSpzToPly: header — version={version} numPoints={num_points} "
        f"shDegree={sh_degree} fractionalBits={frac_bits} flags=0x{flags:02x}",
        flush=True,
    )

    sh_dim  = _sh_dim(sh_degree)
    scale_f = np.float32(1.0 / (1 << frac_bits))
    offset  = 16

    # ── Positions — 3 × 24-bit signed LE fixed-point ─────────────
    n_pos_bytes = num_points * 9
    if offset + n_pos_bytes > len(data):
        raise ValueError("Truncated SPZ: not enough bytes for positions")

    pos_raw = data[offset : offset + n_pos_bytes].reshape(num_points, 3, 3)
    offset += n_pos_bytes

    # Assemble 24-bit values into int32 then sign-extend via << 8 >> 8
    p = (pos_raw[:, :, 0].astype(np.int32)
         | (pos_raw[:, :, 1].astype(np.int32) << 8)
         | (pos_raw[:, :, 2].astype(np.int32) << 16))
    p = (p << 8) >> 8   # arithmetic right-shift sign-extends from bit 23
    positions = p.astype(np.float32) * scale_f   # shape (N, 3)

    # ── Alphas — uint8, logit-quantised ──────────────────────────
    if offset + num_points > len(data):
        raise ValueError("Truncated SPZ: not enough bytes for alphas")

    a_raw = data[offset : offset + num_points].astype(np.float32)
    offset += num_points
    a_raw = np.clip(a_raw, 1, 254)                        # avoid log(0) / log(inf)
    opacity = np.log(a_raw / (255.0 - a_raw)).astype(np.float32)  # inverse sigmoid

    # ── Colors — uint8 RGB, SH-DC-quantised ──────────────────────
    if offset + num_points * 3 > len(data):
        raise ValueError("Truncated SPZ: not enough bytes for colors")

    c_raw   = data[offset : offset + num_points * 3].reshape(num_points, 3).astype(np.float32)
    offset += num_points * 3
    colors  = (c_raw / 255.0 - 0.5) / COLOR_SCALE        # SH DC coefficient

    # ── Scales — uint8, log-quantised ────────────────────────────
    if offset + num_points * 3 > len(data):
        raise ValueError("Truncated SPZ: not enough bytes for scales")

    s_raw   = data[offset : offset + num_points * 3].reshape(num_points, 3).astype(np.float32)
    offset += num_points * 3
    scales  = s_raw / 16.0 - 10.0                         # log scale (as expected by PLY)

    # ── Rotations ─────────────────────────────────────────────────
    if version < 3:
        # 3 bytes per point: int8 x, y, z; derive w = sqrt(1 - |xyz|²)
        if offset + num_points * 3 > len(data):
            raise ValueError("Truncated SPZ: not enough bytes for rotations")

        r_raw = data[offset : offset + num_points * 3].view(np.int8).reshape(num_points, 3)
        offset += num_points * 3

        # Scale by SQRT1_2 / 127 — components are quantised to the same
        # [-sqrt(0.5), +sqrt(0.5)] range used by the "smallest-three" scheme,
        # guaranteeing |xyz|² ≤ 0.75 and therefore a real w.
        rxyz = r_raw.astype(np.float32) * (SQRT1_2 / np.float32(127.0))
        rw   = np.sqrt(np.maximum(np.float32(0.0),
                                  np.float32(1.0) - (rxyz ** 2).sum(axis=1)))
        q = np.column_stack([rw, rxyz[:, 0], rxyz[:, 1], rxyz[:, 2]])
        # Normalise to handle the int8 value -128 edge-case (slightly > SQRT1_2).
        q_norm = np.linalg.norm(q, axis=1, keepdims=True)
        rotations = (q / np.maximum(q_norm, np.float32(1e-8))).astype(np.float32)
    else:
        # 4 bytes per point: "smallest-three" packed uint32
        if offset + num_points * 4 > len(data):
            raise ValueError("Truncated SPZ: not enough bytes for rotations")

        r_raw  = data[offset : offset + num_points * 4].reshape(num_points, 4)
        offset += num_points * 4

        comp = (r_raw[:, 0].astype(np.uint32)
                | (r_raw[:, 1].astype(np.uint32) << 8)
                | (r_raw[:, 2].astype(np.uint32) << 16)
                | (r_raw[:, 3].astype(np.uint32) << 24))

        i_largest = (comp >> 30).astype(np.int32)
        work      = comp.copy()

        # Unpack 3 smallest components (10 bits each: 1 sign + 9 magnitude, LSB first)
        decoded = np.zeros((num_points, 3), dtype=np.float32)
        for k in range(3):
            mag  = (work & np.uint32(0x1FF)).astype(np.float32)
            neg  = ((work >> 9) & np.uint32(1)).astype(bool)
            work = work >> 10
            vals = SQRT1_2 * mag / np.float32(511.0)
            vals[neg] *= -1.0
            decoded[:, k] = vals

        largest   = np.sqrt(np.maximum(np.float32(0.0),
                                       np.float32(1.0) - (decoded ** 2).sum(axis=1)))
        q = np.zeros((num_points, 4), dtype=np.float32)

        # Place each decoded component at its correct WXYZ slot
        for il in range(4):
            mask = (i_largest == il)
            if not mask.any():
                continue
            q[mask, il] = largest[mask]
            slot = [j for j in range(4) if j != il]   # e.g. [1,2,3] when il=0
            for k, j in enumerate(slot):
                q[mask, j] = decoded[mask, k]

        q_norm = np.linalg.norm(q, axis=1, keepdims=True)
        rotations = (q / np.maximum(q_norm, np.float32(1e-8))).astype(np.float32)

    # ── SH rest — advance offset but do not decode ───────────────
    if sh_dim > 0:
        sh_bytes = num_points * sh_dim * 3
        if offset + sh_bytes > len(data):
            raise ValueError("Truncated SPZ: not enough bytes for SH coefficients")
        offset += sh_bytes

    return positions, opacity, colors, scales, rotations, num_points


# ──────────────────────── PLY writer ─────────────────────────────

def write_ply(
    path: str,
    positions,   # (N, 3)
    opacity,     # (N,)
    colors,      # (N, 3)
    scales,      # (N, 3)
    rotations,   # (N, 4) WXYZ
) -> int:
    """Write a binary-little-endian 3DGS PLY. Returns number of vertices written."""
    N = len(positions)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)

    header = (
        "ply\n"
        "format binary_little_endian 1.0\n"
        f"element vertex {N}\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "property float nx\n"
        "property float ny\n"
        "property float nz\n"
        "property float f_dc_0\n"
        "property float f_dc_1\n"
        "property float f_dc_2\n"
        "property float opacity\n"
        "property float scale_0\n"
        "property float scale_1\n"
        "property float scale_2\n"
        "property float rot_0\n"
        "property float rot_1\n"
        "property float rot_2\n"
        "property float rot_3\n"
        "end_header\n"
    )

    zeros_n3 = np.zeros((N, 3), dtype=np.float32)

    # Stack all 17 floats per vertex into one contiguous (N, 17) array
    verts = np.concatenate([
        positions.astype(np.float32),     # x y z
        zeros_n3,                          # nx ny nz (normals unused by splat renderers)
        colors.astype(np.float32),         # f_dc_0 f_dc_1 f_dc_2
        opacity.astype(np.float32).reshape(N, 1),  # opacity
        scales.astype(np.float32),         # scale_0 scale_1 scale_2
        rotations.astype(np.float32),      # rot_0 rot_1 rot_2 rot_3 (wxyz)
    ], axis=1)

    assert verts.shape == (N, 17), f"Unexpected vertex shape: {verts.shape}"

    with open(path, "wb") as fh:
        fh.write(header.encode("ascii"))
        fh.write(verts.tobytes())

    return N


# ──────────────────────── entry point ────────────────────────────

def main() -> int:
    if len(sys.argv) < 3:
        print("Usage: ConvertSpzToPly.py <input.spz> <output.ply>", file=sys.stderr)
        return 1

    in_path  = sys.argv[1]
    out_path = sys.argv[2]

    if not os.path.isfile(in_path):
        print(f"ConvertSpzToPly: input not found: {in_path}", file=sys.stderr)
        return 1

    try:
        positions, opacity, colors, scales, rotations, num_points = load_spz(in_path)
    except Exception as exc:
        print(f"ConvertSpzToPly: load_spz failed: {exc}", file=sys.stderr, flush=True)
        return 1

    try:
        written = write_ply(out_path, positions, opacity, colors, scales, rotations)
    except Exception as exc:
        print(f"ConvertSpzToPly: write_ply failed: {exc}", file=sys.stderr, flush=True)
        return 1

    ply_size = os.path.getsize(out_path)
    print(
        f"ConvertSpzToPly: Converted {written} points spz->ply  "
        f"({ply_size / 1024 / 1024:.1f} MB)  ->  {out_path}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
