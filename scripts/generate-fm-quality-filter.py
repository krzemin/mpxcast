#!/usr/bin/env python3
"""Print the C SOS constants for fm_quality.c (development-only SciPy dependency)."""

import numpy as np
from scipy import signal

SAMPLE_RATE_HZ = 240_000
PASSBAND_HZ = [16_500, 17_500]
STOPBAND_HZ = [15_000, 19_000]
PASSBAND_LOSS_DB = 1
STOPBAND_ATTENUATION_DB = 60

order, cutoff = signal.buttord(
    PASSBAND_HZ, STOPBAND_HZ, PASSBAND_LOSS_DB, STOPBAND_ATTENUATION_DB,
    fs=SAMPLE_RATE_HZ,
)
sos = signal.butter(order, cutoff, btype="bandpass", fs=SAMPLE_RATE_HZ, output="sos")
# Distribute the small overall gain across sections to avoid tiny intermediate values.
gain = sos[0, 0]
sos[0, :3] /= gain
sos[:, :3] *= gain ** (1 / len(sos))
sos = sos.astype(np.float32)

print(f"/* Generated Butterworth bandpass: order {2 * order}, {len(sos)} biquads. */")
print(f"#define FM_QUALITY_SOS_COUNT {len(sos)}u")
for name, columns in [("b", slice(0, 3)), ("a", slice(3, 6))]:
    print(f"static const float fm_quality_{name}[FM_QUALITY_SOS_COUNT * 3u] = {{")
    for row in sos[:, columns]:
        print("    " + ", ".join(f"{value:.9e}f" for value in row) + ",")
    print("};")
