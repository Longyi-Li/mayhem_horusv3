# Horus V3 receiver for H4M + HackRF Pro / PRALINE

**Hardware feedback:** On October 31, 2026, the device owner reported that v4 reception works perfectly on their H4M / HackRF Pro (PRALINE). This is user-reported reception testing; range and flight recovery have not been measured.

Experimental Mayhem build: `n_261025_horus4`, based on nightly `n_261025`.

## Changes in version 4

The decoder now searches three carrier hypotheses (0 and +/-90 Hz) simultaneously, removing reproduced +/-100 Hz coherent-filter blind spots. Software validation covers -130 through +130 Hz in 10-Hz steps with noise. This is a bounded search, not the full upstream FFT estimator. See RECEIVER-AUDIT.md for the Radiosonde and upstream Horus comparison.

The GUI is copied from Mayhem's Radiosonde app: frequency/gain controls,
RSSI and channel meters, audio volume, telemetry, coordinates, SD logging,
QR location and map. Packet sounds use the same audio setup.

Receiver startup now follows Radiosonde: 2.4576 MHz sampling, its two
11 kHz decimation filters, the same buffer arrangement, and starting the
DSP thread only after configuring the filters. The Radiosonde binary-FSK
modem is replaced with the Horus V3 four-tone modem and packet decoder.
There is no Reacquire RX button or automatic frequency toggling. The
previous workaround did not resolve reception on the user's device.

CRC checking is always enabled. Missing sensor fields show `--`.
Voltage, temperature, humidity, pressure and ascent are shown when present.
When variable-length extraSensors are present, this implementation keeps
only the mandatory tracking fields. Extension fields are ignored.
VSpeed otherwise uses the change in received altitude over at least 10 s.

## Install

1. Download and extract [the v4 install ZIP](HorusV3-PRALINE-n_261025_horus4.zip). Copy the contents of `SDCARD` to the SD card root,
   merging APPS and FIRMWARE. Keep a backup of your current APPS folder.
2. Open Utilities -> Flash Utility and select
   `portapack-horus-n_261025_horus4-PRALINE.bin`.
3. After flashing, About should show `n_261025_horus4`.
4. Open Receive -> Horus V3. Tune to **431.051000 MHz** for your RS41.

Use the included matching firmware and all matching apps together.
The new build changes firmware symbol addresses; the app alone is not a
validated installation on stock Mayhem. This image is only for **HackRF
Pro / PRALINE**, not HackRF One/R9/R10 or PortaRF.

## Receiving

Supports Horus V3 at 100 baud with nominal 270 Hz tone spacing.
The displayed frequency is the RS41's lowest FSK tone. The modem shifts
the signal center by 405 Hz digitally, leaving the radio model's hidden
offset at zero. Tune in 50 Hz steps if needed. The bounded digital search runs continuously; there is no RF scanning or continuous AFC.
Your transmitter remains configured for one transmission every 10 seconds.

Log creates `LOGS/HORUS_<timestamp>.TXT`. Each accepted packet stores the
receive timestamp, callsign, sequence, payload UTC seconds, latitude,
longitude and altitude. QR and map use the last received valid position;
no position is extrapolated. Maps use the same SD map data as Radiosonde.

## Verification

Software checks cover all five payload lengths, independently encoded
ASN.1 fields, optional sensor fields, Golay correction, CRC rejection,
noise, timing/frequency offsets, aborted frames, consecutive bursts and
sync-word corruption. The new 38.4 kHz mixer/filter path also decodes all
nine packets in Project Horus's real reference recording. These checks
pass with AddressSanitizer and UndefinedBehaviorSanitizer as well.

The ARM build, external symbol placement, combined app size, firmware/app
checksums and version pairing are checked before packaging.

The owner confirmed v4 resolves reception on their device. The included software checks do not measure RF sensitivity, sustained DSP timing, range, or flight recovery reliability.

## Source and rebuilding

Mayhem base: fd8edf69e326ab8b9be67c88de7eb2aaf5468c8f
https://github.com/portapack-mayhem/mayhem-firmware

Horus codec/reference recording: 8cc43de5567ce82132d46c5e37897f9bcc09a755
https://github.com/projecthorus/horusdemodlib

`source.patch` contains the app, build integration and vendored codec.
Apply it to the Mayhem base with recursive submodules using `git apply --ignore-whitespace ../source.patch` (the base contains CRLF files). Use LF line endings
for build scripts and the upstream dockerfile-nogit toolchain, then run:

```sh
export VERSION_STRING=n_261025_horus4
cmake -S . -B build-horus -G Ninja -DBOARD=PRALINE
cmake --build build-horus -j 6 --target firmware
```

Output: build-horus/firmware/portapack-mayhem-firmware.bin and
build-horus/firmware/application/*.ppma.

For host tests, clone Mayhem as `mayhem-firmware` and the Horus reference repository as `horusdemodlib` inside this repository, then apply `source.patch` to the Mayhem checkout at the base revision above. Install Linux GCC/G++, Python, NumPy, SciPy, asn1tools:

```sh
bash tests/build_host.sh
python3 tests/test_horus.py
bash tests/sanitize_host.sh
bash tests/check_v3_regression.sh
```

`python3 tests/verify_package.py` checks the extracted package with Python's
standard library. SHA256SUMS.txt records hashes for the SD card payload.

Mayhem/app code: GPL-2.0-or-later. Vendored Horus codec: LGPL-2.1.
Original Radiosonde authors' attribution is retained. License copies are
included. No PR/issue was submitted and no hardware was flashed here.
