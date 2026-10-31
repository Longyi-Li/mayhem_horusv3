"""Independent ASN.1 fixtures, FEC errors, noisy complex RF and real recording."""
from pathlib import Path
import ctypes
import subprocess
import numpy as np
from scipy.signal import hilbert, resample_poly
import asn1tools

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'test-build'
schema = asn1tools.compile_files(str(ROOT / 'horusdemodlib/horusbinaryv3/HorusBinaryV3.asn1'), 'uper')
codec = ctypes.CDLL(str(OUT / 'codec.so'))

def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xffff
    return crc

def frame(size, sequence=123, corrupt=0, invalid_crc=False):
    record = dict(payloadCallsign='AK6WZ-11', sequenceNumber=sequence,
                  timeOfDaySeconds=45296, latitude=3777490, longitude=-12241940,
                  altitudeMeters=31415, gnssSatellitesVisible=9,
                  milliVolts={'battery': 2900}, via='sondehub')
    encoded = schema.encode('Telemetry', record)
    assert len(encoded) <= size-2
    payload = encoded + bytes(size-2-len(encoded))
    payload = crc16(payload).to_bytes(2, 'little') + payload
    if invalid_crc:
        payload = bytes([payload[0] ^ 1]) + payload[1:]
    packet = ctypes.create_string_buffer(codec.horus_l2_get_num_tx_data_bytes(size))
    codec.horus_l2_encode_tx_packet(packet, ctypes.create_string_buffer(payload), size)
    packet = bytearray(packet.raw)
    for n in range(corrupt):
        packet[2+n*7] ^= 1
    return packet

def modulate(packet, offset=0, phase=0, noise=0, ppm=0):
    data = np.frombuffer(bytes(packet), dtype=np.uint8)
    symbols = np.unpackbits(data).reshape(-1,2) @ np.array([2,1])
    symbols = np.concatenate([np.tile(np.arange(4), 12), symbols])
    count = round(len(symbols)*24*(1+ppm/1e6))
    indices = np.minimum((np.arange(count)/(24*(1+ppm/1e6))).astype(int),len(symbols)-1)
    frequency = (-405 + 270*symbols[indices] + offset)
    angle = np.cumsum(2*np.pi*frequency/2400)
    signal = 5000*np.exp(1j*angle)
    rng = np.random.default_rng(12345)
    signal += noise*(rng.normal(size=count)+1j*rng.normal(size=count))
    return np.concatenate([np.zeros(phase, dtype=complex),signal,np.zeros(120,dtype=complex)])

def run(name, signal, input38400=False):
    path = OUT / f'{name}.iq'
    iq = np.column_stack([signal.real, signal.imag])
    np.clip(iq,-32768,32767).astype('<i2').tofile(path)
    args = [str(OUT/'receiver'), str(path)] + (['38400'] if input38400 else [])
    result = subprocess.run(args, capture_output=True,text=True,check=True)
    return result.stdout.strip().splitlines()

passed = 0
for extra, expected in [
    ({'milliVolts': {'battery': 2900, 'solar': 4000}}, '1 2900 0 0 0 0'),
    ({'velocityHorizontalKilometersPerHour': 512, 'gnssSatellitesVisible': 31,
      'ascentRateCentimetersPerSecond': -456, 'pressurehPa-x10': 10001,
      'temperatureCelsius-x10': {'internal': -123, 'external': 234},
      'humidityPercentage': 45, 'milliVolts': {'battery': 3100}}, '31 3100 -123 45 10001 -456'),
    ({'temperatureCelsius-x10': {'external': 0}, 'humidityPercentage': 0}, '6 0 0 0 0 0'),
    ({'extraSensors': [{'name': 'sensor'}], 'milliVolts': {'battery': 2900}}, '0 0 0 0 0 0'),
]:
    record = dict(payloadCallsign='AK6WZ-11', sequenceNumber=123,
                  timeOfDaySeconds=45296, latitude=3777490, longitude=-12241940,
                  altitudeMeters=31415)
    record.update(extra)
    path = OUT/'optional_fields.bin'
    path.write_bytes(schema.encode('Telemetry', record))
    result = subprocess.run([str(OUT/'receiver'), str(path), 'parse'], capture_output=True, text=True, check=True)
    assert result.stdout.strip() == expected, (extra, result.stdout, expected)
print('PASS: optional voltage, temperature, humidity, pressure, ascent, zero values and extra-sensor fallback')

for size in (32,48,64,96,128):
    for offset, phase, noise, ppm in ((0,0,0,0),(40,11,1500,0),(-40,23,1500,100),
                                    (-130,7,1000,100),(-100,19,1500,0),
                                    (100,13,1500,0),(130,22,1000,-100)):
        lines = run(f'synthetic_{size}_{offset}', modulate(frame(size),offset,phase,noise,ppm))
        assert lines == ['AK6WZ-11 123 45296 3777490 -12241940 31415'], (size,offset,lines)
        passed += 1
    lines = run(f'fec_{size}',modulate(frame(size,corrupt=2)))
    assert len(lines) == 1 and lines[0].startswith('AK6WZ-11 123 '),lines
    passed += 1

assert run('noise_only', 2000*(np.random.default_rng(7).normal(size=240000)+1j*np.random.default_rng(8).normal(size=240000))) == []
passed += 1
assert run('bad_crc',modulate(frame(32,invalid_crc=True))) == []
assert run('truncated',modulate(frame(32)[:20])) == []
two = np.concatenate([modulate(frame(32,sequence=123)), np.zeros(24000), modulate(frame(32,sequence=124))])
assert run('two_packets',two) == ['AK6WZ-11 123 45296 3777490 -12241940 31415','AK6WZ-11 124 45296 3777490 -12241940 31415']
passed += 3
bad_then_good = np.concatenate([modulate(frame(32,invalid_crc=True)), np.zeros(480), modulate(frame(32,sequence=124))])
assert run('reacquire_after_bad_frame',bad_then_good) == ['AK6WZ-11 124 45296 3777490 -12241940 31415']
passed += 1
noisy_idle = 2000*(np.random.default_rng(17).normal(size=24000)+1j*np.random.default_rng(18).normal(size=24000))
assert run('noisy_idle_then_packet',np.concatenate([noisy_idle,modulate(frame(32))])) == ['AK6WZ-11 123 45296 3777490 -12241940 31415']
# One flipped UW bit is recoverable when the preceding preamble identifies
# a fresh RS41 burst. The UW itself is not part of the Golay-protected body.
uw_error = bytearray(frame(32)); uw_error[0] ^= 1
assert run('uw_bit_error',modulate(uw_error)) == ['AK6WZ-11 123 45296 3777490 -12241940 31415']
passed += 2
print(f'PASS: {passed} synthetic/noise/FEC cases')

signal384 = resample_poly(modulate(frame(32)), 16, 1)
signal384 *= np.exp(2j*np.pi*405*np.arange(len(signal384))/38400)
assert run('frontend38400', signal384, True) == ['AK6WZ-11 123 45296 3777490 -12241940 31415']
print('PASS: 38.4 kHz anti-alias/filter/decimation path')
for offset in range(-130, 131, 10):
    signal384 = resample_poly(modulate(frame(32), offset=offset, phase=17, noise=1000), 16, 1)
    signal384 *= np.exp(2j*np.pi*405*np.arange(len(signal384))/38400)
    assert run(f'frontend_offset_{offset}', signal384, True) == ['AK6WZ-11 123 45296 3777490 -12241940 31415'], offset
print('PASS: 38.4 kHz frontend frequency sweep -130 through +130 Hz in 10-Hz steps')

# Official Horus V3 8 kHz SSB recording, not generated by our receiver.
audio = np.fromfile(ROOT/'horusdemodlib/samples/horus_v3_100bd_8000_s16.raw', dtype='<i2').astype(float)
spectrum = abs(np.fft.rfft(audio[:min(len(audio),80000)]))
peaks = np.argsort(spectrum)[-20:]
print('Recording strongest bins (Hz):', np.round(peaks*8000/min(len(audio),80000),1))
analytic = hilbert(audio)
# Reference recording modem tones are located at 1000,1270,1540,1810 Hz.
for center in (1405, 1500, 1205, 1155, 1000):
    baseband = analytic*np.exp(-2j*np.pi*center*np.arange(len(audio))/8000)
    signal = resample_poly(baseband,3,10)
    lines = run(f'official_{center}', signal)
    if lines:
        print(f'PASS official recording, center={center}: {len(lines)} packets; first: {lines[0]}')
        assert len(lines) == 9 and lines[0] == '4FSKTEST-V2 0 3 23 34 56'
        signal384 = resample_poly(baseband,24,5)
        signal384 *= np.exp(2j*np.pi*405*np.arange(len(signal384))/38400)
        lines384 = run('official_frontend38400',signal384,True)
        assert lines384 == lines, (lines384,lines)
        print('PASS: official recording through 38.4 kHz frontend, all 9 packets identical')
        break
else:
    raise AssertionError('Official Horus V3 recording did not decode')
