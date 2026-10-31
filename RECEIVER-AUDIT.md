# Receiver comparison and Horus bandwidth reference

## What is shared with Radiosonde

The Horus v3 GUI build and stock Radiosonde configure the same 2,457,600
complex samples/s, 1,750,000 Hz analog bandwidth request, 24-tap first
8:1 decimator and 32-tap second 8:1 decimator. Both output 38,400 complex
samples/s and start their DSP thread after configuring the decimators.
Both use the same ReceiverModel tuning function and PRALINE tuning table.

ReceiverModel applies a -Fs/4 offset (614,400 Hz here). The first decimator
undoes that translation. PRALINE additionally uses an analog offset plus
FPGA quarter-rate translation. These paths are common to both apps.
The Horus app has no special FPGA DC-block or LO programming.

The previously mentioned upstream sample-rate tuning bug is already
addressed in this tree's update_sampling_rate(), which explicitly retunes
after changing the rate. Adding a second startup retune does not address
the independently reproduced decoder problem below.

## What differs after the common front end

Stock proc_sonde.hpp uses square_taps_38k4_1t_p from ais_baseband.hpp.
These are four complex taps, not a narrow 100-baud Horus filter.
MatchedFilter::execute_once computes positive/negative tone magnitudes
and returns their difference at 19,200 samples/s. Separate 4,800 and
9,600-symbol timing loops feed the original radiosonde packet decoders.
That broad binary demodulator is not directly reusable for four-tone Horus.

The custom Horus modem instead translates the lowest RF tone +405 Hz to
the four-tone center, low-pass filters at 800 Hz (roughly 1.6 kHz total
channel width), and decimates to 2,400 samples/s. Four 10-ms coherent
correlators then identify the symbols at -405,-135,+135,+405 Hz.

A one-symbol rectangular coherent correlator at 100 baud has its first
frequency-response zeros at +/-100 Hz from its reference tone. This is
NOT an 800-Hz channel-filter cutoff problem. Keeping the reference tones
fixed creates narrow blind spots even though the signal is inside the
channel passband. The identical noisy IQ fixtures decode at nominal
frequency in v3, fail at -100/+100 Hz in v3, and decode at all three in v4.

This does not establish the actual RF error or the cause of the user's
nominal-frequency failure. Their device may have a different actual
transmit/receive offset. No on-device IQ recording was available.

## Upstream reference (checked online 2026-10-31)

Horus GUI master, horusgui/gui.py:
DEFAULT_ESTIMATOR_MIN = 100
DEFAULT_ESTIMATOR_MAX = 4000
https://github.com/projecthorus/horus-gui/blob/master/horusgui/gui.py

These are frequency acquisition bounds (a 3.9 kHz search window), NOT
matched-filter -3 dB bandwidth values.

horusdemodlib master, start_rtlsdr.sh:
RXBANDWIDTH=10000, FSK_LOWER=1000, FSK_UPPER=11000.
This is a configurable 10 kHz estimator search window at 48 kHz IQ.
https://github.com/projecthorus/horusdemodlib/blob/master/start_rtlsdr.sh

horusdemodlib master, src/fsk.c:
- fsk_demod_freq_est() estimates tone frequencies from an FFT.
- A known-tone-spacing mask may constrain the tone pattern.
- Demodulator oscillators use those estimated frequencies.
- Each downconverted tone is integrated over Ts = Fs/Rs samples.
- Thus at 100 baud its per-tone filter also integrates 10 ms, but its
  center follows the measured tone instead of remaining fixed.
- The FFT target bin width is 0.1*Rs, or 10 Hz at 100 baud.
https://github.com/projecthorus/horusdemodlib/blob/master/src/fsk.c

The documented tone spacing is nominally 270 Hz and the tone centers
span 810 Hz. That span is not itself an occupied-bandwidth specification.
https://github.com/projecthorus/horusdemodlib/wiki/2---Modem-Details

## Version 4 implementation

Keep Radiosonde's front end and the existing 800-Hz channel filter.
Search three carrier hypotheses simultaneously: 0,+90,-90 Hz relative
to the selected frequency. Each uses the original full 10-ms integration.
Eight timing phases per carrier retain 24 packet collectors overall;
only the extra tone-filter state increases RAM usage.

This is a bounded search suited to this device, NOT a full port of the
upstream FFT estimator or its 3.9/10 kHz acquisition window. Validation
covers a continuous -130..+130 Hz sweep in 10-Hz steps with added noise,
all five payload sizes at +/-100 and +/-130 Hz, and the nine packets in
the upstream reference recording. It performs no hardware frequency
hopping. Wider drift, RF acquisition and device DSP timing need hardware
validation.
