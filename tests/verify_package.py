"""Check Mayhem additive checksums, version pairing and package contents."""
from pathlib import Path
import hashlib
import struct

root = Path(__file__).resolve().parents[1]
package = root if (root/'SDCARD').exists() else root/'output/HorusV3-PRALINE-v4'
firmware = package / 'SDCARD/FIRMWARE/portapack-horus-n_261025_horus4-PRALINE.bin'
data = firmware.read_bytes()
assert len(data) == 4 * 1024 * 1024
assert sum(struct.unpack('<%dI' % (len(data)//4), data)) & 0xffffffff == 0
version = b'n_261025_horus4'
assert version in data
apps = sorted((package/'SDCARD/APPS').glob('*.ppma'))
assert len(apps) == 94
expected = int(hashlib.md5(version+b'\n').hexdigest()[:8],16) # CMake uses echo VERSION | md5sum
for path in apps:
    blob = path.read_bytes()
    assert len(blob) % 4 == 0, path
    assert sum(struct.unpack('<%dI' % (len(blob)//4),blob)) & 0xffffffff == 0, path
    assert struct.unpack_from('<I',blob,12)[0] == expected, path
horus = (package/'SDCARD/APPS/horus_rx.ppma').read_bytes()
assert horus[16:32].split(b'\0')[0] == b'Horus V3'
assert horus[76:80] == b'PHV3'
offset = struct.unpack_from('<I',horus,80)[0]
assert 84 <= offset < len(horus)-4
print(f'PASS: PRALINE firmware size/checksum/version; all {len(apps)} apps checksum/version; Horus header and embedded baseband')
print(f'Horus app={len(horus)} bytes, M0={offset} bytes, M4={len(horus)-offset-4} bytes')
entries=[]
for path in sorted((package/'SDCARD').rglob('*')):
    if path.is_file():
        entries.append(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.relative_to(package).as_posix())
(package/'SHA256SUMS.txt').write_text('\n'.join(entries)+'\n')


