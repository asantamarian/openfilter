# Generates the normalised order-12 elliptic low-pass prototype used for Brickwall cuts
# (docs/spec/03-dsp.md section 2). Paste the output into src/dsp/design/EllipticTable.h.
#
#   uv run --with scipy python tools/gen_elliptic.py
from scipy.signal import ellipap

z, p, k = ellipap(12, 0.05, 100.0)
z = sorted([x for x in z if x.imag > 0], key=lambda c: c.imag)
p = sorted([x for x in p if x.imag > 0], key=lambda c: c.imag)
print(f"inline constexpr double kEllipticGain = {k!r};")
print("inline constexpr double kEllipticZeroFreq[6] = {" + ", ".join(repr(float(x.imag)) for x in z) + "};")
print("inline constexpr double kEllipticPoleRe[6] = {" + ", ".join(repr(float(x.real)) for x in p) + "};")
print("inline constexpr double kEllipticPoleIm[6] = {" + ", ".join(repr(float(x.imag)) for x in p) + "};")
