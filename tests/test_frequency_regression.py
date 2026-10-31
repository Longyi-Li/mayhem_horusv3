"""Run after test_horus.py; compare its IQ fixtures against the v3 baseline."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'test-build'
for offset in (-100, 0, 100):
    path = out / f'frontend_offset_{offset}.iq'
    decoded = []
    for binary in (out / 'v3-baseline/receiver', out / 'receiver'):
        result = subprocess.run([str(binary), str(path), '38400'],
                                capture_output=True, text=True, check=True)
        decoded.append(result.stdout.strip())
    assert decoded[1] == 'AK6WZ-11 123 45296 3777490 -12241940 31415', decoded
    assert bool(decoded[0]) == (offset == 0), decoded
    print(f'offset={offset:+d} Hz: v3 packets={int(bool(decoded[0]))}, v4 packets={int(bool(decoded[1]))}')
print('PASS: same noisy IQ files fail at +/-100 Hz in v3 and decode in v4')
