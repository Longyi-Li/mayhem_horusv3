# Standalone Horus V3 for PortaPack H4M + HackRF Pro (PRALINE)

Experimental local build based on Mayhem nightly `n_261025`.
About shows `n_261025_horus1` in the matching custom firmware.

This receives and decodes on the device itself. A computer or phone is not
needed while receiving. No transmitter firmware changes are required.

## Install

1. Back up your SD card's APPS folder. Keep the official n_261025 PRALINE
   firmware available for reverting:
   https://github.com/portapack-mayhem/mayhem-firmware/releases/tag/nightly-tag-2026-10-25
2. Extract the install ZIP. Copy the contents of `SDCARD` onto the root of
   the PortaPack's SD card, merging APPS and FIRMWARE.
3. On the PortaPack, open Utilities -> Flash Utility and select
   `portapack-horus-n_261025_horus1-PRALINE.bin` from FIRMWARE. Use a charged
   battery or stable USB power and let the update finish.
4. Open Receive -> Horus V3.

The app and custom firmware MUST be used together. The custom firmware was
linked with different internal addresses from the stock nightly. Copying
only horus_rx.ppma to the stock firmware has not been validated and can crash.
The package includes all matching external apps, not just Horus, so existing
apps use the same firmware ABI and version identifier.

**Only for HackRF Pro / PRALINE. Do not flash this image onto HackRF One/R9/R10
or PortaRF.** H4M is the screen/control assembly; the underlying radio board
determines firmware compatibility.

## Your RS41

- Mode: Horus V3, 100 baud, nominal 270 Hz tone spacing.
- Default frequency field: **431.051000 MHz**, the LOWEST FSK tone frequency
  configured in RS41ng, rather than the 431.050000 MHz SSB dial frequency.
- One packet every 10 seconds, one repeat, as in your config.
- The receiver centers its four matched filters 405 Hz above the field value.
- Adjust the receive frequency in 50 Hz steps if the transmitter/receiver
  oscillators are offset. This prototype has no automatic frequency search
  or frequency-error tracking. You can first find the tones using Mayhem's
  Audio RX/spectrum, then return to this app.
- Start with RF amplifier OFF and modest LNA/VGA gain when testing nearby.

The screen shows callsign, sequence number, latitude/longitude in decimal
degrees, altitude in meters, payload UTC, accepted packet count, and time
since the last packet. More than 30 seconds without a packet is marked STALE.
The protocol's 0,0 location is shown as unavailable, not a valid GPS fix.

## Verified and not yet verified

Verified in software:

- ARM firmware and app compile/link for PRALINE with Mayhem's GCC 9.2.1 toolchain.
- External-app symbol placement and app/baseband combined size checked by
  upstream export/build tools.
- Independent ASN.1 encoder used to generate known positions and callsigns.
- All V3 payload lengths: 32, 48, 64, 96, 128 bytes.
- Golay correction, scrambling, interleaving, V3 CRC and mandatory UPER fields.
- Synthetic complex signals, added noise, +/-40 Hz offsets, symbol timing
  offsets, 100 ppm clock offset, invalid CRC, truncation, repeated packets.
- 24 kHz anti-alias FIR and 10:1 decimation.
- Nine packets from Project Horus's real V3 audio sample, including through
  the 24 kHz frontend.
- Noise-only test produces no accepted packets.
- All decoder cases repeated with AddressSanitizer and UndefinedBehaviorSanitizer.
- Firmware and all 94 external apps pass additive checksums and version pairing.

**Not tested on physical H4M/PRALINE hardware:** display layout, radio IQ
orientation/tuning, RF sensitivity, sustained DSP timing, gain behavior, and
recovery reliability. This is a compiled experimental prototype, not a
flight-qualified receiver. First test it against your RS41 on the ground
and confirm that displayed coordinates match a known location.

Supported here: V3 mandatory tracking fields at 100 baud / 270 Hz spacing.
Extra sensors and extension fields may be present but are not displayed.
Other baud rates, V2, automatic AFC, map navigation, logging and SondeHub
upload are not implemented in this first version.

## Sources and rebuilding

- Mayhem base: `fd8edf69e326ab8b9be67c88de7eb2aaf5468c8f`
  https://github.com/portapack-mayhem/mayhem-firmware
- Horus codec and recording: `8cc43de5567ce82132d46c5e37897f9bcc09a755`
  https://github.com/projecthorus/horusdemodlib
- `source.patch` contains all application/build changes and vendored codec.
- Host tests are included under `tests`.
- Firmware source remains in the local `mayhem-firmware` checkout.

To reproduce: check out the Mayhem base revision with recursive submodules,
apply source.patch, use LF line endings for executable scripts, and build
the upstream `dockerfile-nogit` image. Configure/build with:

```sh
export VERSION_STRING=n_261025_horus1
cmake -S . -B build-horus -G Ninja -DBOARD=PRALINE
cmake --build build-horus -j 6 --target firmware
```

The final firmware is `build-horus/firmware/portapack-mayhem-firmware.bin`.
External apps are under `build-horus/firmware/application/*.ppma`.

For the host decoder tests, place `tests`, `mayhem-firmware` (patched) and
`horusdemodlib` (the revision above) beside each other. In Linux with GCC/G++,
Python 3, NumPy, SciPy and asn1tools installed, run `bash tests/build_host.sh`
and `python3 tests/test_horus.py`. The recording is read from the Horus repo.
`python3 tests/verify_package.py` checks the extracted install package using
only the Python standard library.

Mayhem/new app code is GPL-2.0-or-later; upstream Horus codec is LGPL-2.1.
License copies and provenance accompany the package. No upstream PR or issue
has been submitted, and no hardware has been flashed by this chat.
