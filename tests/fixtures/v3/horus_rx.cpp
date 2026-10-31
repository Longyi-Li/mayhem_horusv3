// SPDX-License-Identifier: GPL-2.0-or-later
#include "horus_rx.hpp"
#include <string.h>
extern "C" {
#include "horus_l2.h"
}

namespace horus {
namespace {
// Exact 160-sample period: 135 Hz is bin 9 at 2400 Hz.
const int16_t sine[160] = {
    0, 1286, 2571, 3851, 5126, 6393, 7649, 8894, 10126, 11341, 12539, 13718, 14876, 16011, 17121, 18204, 19260, 20286, 21280, 22242, 23170, 24062, 24916, 25732, 26509, 27245, 27938, 28589, 29196, 29757, 30273, 30742, 31163, 31537, 31862, 32137, 32364, 32540, 32666, 32742, 32767, 32742, 32666, 32540, 32364, 32137, 31862, 31537, 31163, 30742, 30273, 29757, 29196, 28589, 27938, 27245, 26509, 25732, 24916, 24062, 23170, 22242, 21280, 20286, 19260, 18204, 17121, 16011, 14876, 13718, 12539, 11341, 10126, 8894, 7649, 6393, 5126, 3851, 2571, 1286, 0, -1286, -2571, -3851, -5126, -6393, -7649, -8894, -10126, -11341, -12539, -13718, -14876, -16011, -17121, -18204, -19260, -20286, -21280, -22242, -23170, -24062, -24916, -25732, -26509, -27245, -27938, -28589, -29196, -29757, -30273, -30742, -31163, -31537, -31862, -32137, -32364, -32540, -32666, -32742, -32767, -32742, -32666, -32540, -32364, -32137, -31862, -31537, -31163, -30742, -30273, -29757, -29196, -28589, -27938, -27245, -26509, -25732, -24916, -24062, -23170, -22242, -21280, -20286, -19260, -18204, -17121, -16011, -14876, -13718, -12539, -11341, -10126, -8894, -7649, -6393, -5126, -3851, -2571, -1286};
class Bits {
   public:
    Bits(const uint8_t* data, size_t bytes)
        : data_(data), end_(bytes * 8) {}
    uint32_t get(unsigned count) {
        if (pos_ + count > end_) {
            valid = false;
            return 0;
        }
        uint32_t value = 0;
        while (count--) {
            value = (value << 1) | ((data_[pos_ / 8] >> (7 - pos_ % 8)) & 1);
            ++pos_;
        }
        return value;
    }
    bool valid{true};

   private:
    const uint8_t* data_;
    size_t pos_{}, end_;
};
}  // namespace

bool parse_v3(const uint8_t* payload, size_t size, Position& out) {
    if (size < 16) return false;
    Bits b(payload, size);
    b.get(1);                         // ASN.1 extension-present bit; extensions follow the root fields.
    const auto optional = b.get(11);  // Root optional field presence map.
    const unsigned length = b.get(4) + 1;
    if (length > 15) return false;
    static const char alphabet[] = "-/0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    Position result{};
    for (unsigned n = 0; n < length; ++n) result.callsign[n] = alphabet[b.get(6)];
    result.sequence = b.get(16);
    const uint32_t seconds = b.get(17), lat = b.get(25), lon = b.get(26), alt = b.get(16);
    if (!b.valid || seconds > 86401 || lat > 18000000 || lon > 36000000 || alt > 51000) return false;
    result.seconds = static_cast<int32_t>(seconds) - 1;
    result.latitude = static_cast<int32_t>(lat) - 9000000;
    result.longitude = static_cast<int32_t>(lon) - 18000000;
    result.altitude = static_cast<int32_t>(alt) - 1000;
    // Complex extraSensors precede the remaining optionals. Keep the mandatory
    // fix when present; never guess their variable-length encoding.
    if (!(optional & (1u << 10))) {
        if (optional & (1u << 9)) b.get(10);
        if (optional & (1u << 8)) b.get(5);
        if (optional & (1u << 7)) {
            result.ascent = int32_t(b.get(16)) - 32767;
            result.fields |= 16;
        }
        if (optional & (1u << 6)) {
            result.pressure = b.get(14);
            result.fields |= 8;
        }
        if (optional & (1u << 5)) {
            const auto mask = b.get(4);
            for (unsigned n = 0; n < 4; ++n)
                if (mask & (8u >> n)) {
                    const int32_t value = int32_t(b.get(11)) - 1023;
                    if (n == 0 || !(result.fields & 2)) {
                        result.temperature = value;
                        result.fields |= 2;
                    }
                }
        }
        if (optional & (1u << 4)) {
            result.humidity = b.get(7);
            result.fields |= 4;
        }
        if (optional & (1u << 3)) {
            const auto mask = b.get(4);
            for (unsigned n = 0; n < 4; ++n)
                if (mask & (8u >> n)) {
                    const auto value = b.get(14);
                    if (n == 0) {
                        result.millivolts = value;
                        result.fields |= 1;
                    }
                }
        }
        if (!b.valid) return false;
    }
    out = result;
    return true;
}

Receiver::Receiver(Callback callback, void* context)
    : callback_(callback), context_(context) {
    reset();
}
void Receiver::reset() {
    for (auto& lane : lanes_) lane = Lane{};
    for (auto& tone : tones_) tone = Tone{};
    ring_ = oscillator_ = 0;
    memset(filter_i_, 0, sizeof(filter_i_));
    memset(filter_q_, 0, sizeof(filter_q_));
    filter_pos_ = filter_count_ = radio_phase_ = 0;
    samples_ = packets_ = 0;
    last_crc_ = 0;
    last_packet_sample_ = 0;
}
void Receiver::sample_radio_38400(int16_t i, int16_t q) {
    // Lowest tone is the displayed RF frequency. Shift by -405 Hz here,
    // keeping stock Radiosonde radio tuning and startup unchanged.
    const unsigned phase = (uint64_t(radio_phase_) * 160) >> 32;
    const int32_t sn = sine[phase], cs = sine[(phase + 40) % 160];
    radio_phase_ += 45298483u;  // round(405 / 38400 * 2^32)
    const int32_t re = (int64_t(i) * cs + int64_t(q) * sn) >> 16;
    const int32_t im = (int64_t(q) * cs - int64_t(i) * sn) >> 16;
    sample_38400(re, im);
}
void Receiver::sample_38400(int16_t i, int16_t q) {
    // 38.4k -> 2.4k anti-alias filter. 800 Hz cutoff, Hamming window, Q15 taps.
    static const int16_t taps[193] = {0, -1, -2, -4, -5, -6, -7, -9, -10, -11, -12, -13, -14, -15, -16, -16, -16, -16, -15, -14, -12, -10, -7, -4, 0, 4, 9, 14, 20, 26, 32, 38, 44, 50, 55, 60, 63, 66, 68, 69, 68, 66, 62, 56, 48, 39, 28, 15, 0, -16, -34, -52, -71, -91, -111, -130, -149, -167, -183, -197, -208, -216, -221, -222, -218, -210, -196, -177, -153, -123, -88, -47, 0, 52, 109, 171, 237, 306, 379, 454, 531, 609, 687, 765, 841, 915, 985, 1052, 1114, 1171, 1221, 1265, 1302, 1331, 1352, 1365, 1369, 1365, 1352, 1331, 1302, 1265, 1221, 1171, 1114, 1052, 985, 915, 841, 765, 687, 609, 531, 454, 379, 306, 237, 171, 109, 52, 0, -47, -88, -123, -153, -177, -196, -210, -218, -222, -221, -216, -208, -197, -183, -167, -149, -130, -111, -91, -71, -52, -34, -16, 0, 15, 28, 39, 48, 56, 62, 66, 68, 69, 68, 66, 63, 60, 55, 50, 44, 38, 32, 26, 20, 14, 9, 4, 0, -4, -7, -10, -12, -14, -15, -16, -16, -16, -16, -15, -14, -13, -12, -11, -10, -9, -7, -6, -5, -4, -2, -1, 0};
    filter_i_[filter_pos_] = i;
    filter_q_[filter_pos_] = q;
    const unsigned newest = filter_pos_;
    filter_pos_ = (filter_pos_ + 1) % 193;
    if (++filter_count_ < 16) return;
    filter_count_ = 0;
    int64_t re = 0, im = 0;
    unsigned pos = newest;
    for (unsigned n = 0; n < 193; ++n) {
        re += static_cast<int32_t>(filter_i_[pos]) * taps[n];
        im += static_cast<int32_t>(filter_q_[pos]) * taps[n];
        pos = pos == 0 ? 192 : pos - 1;
    }
    // FIR overshoot can exceed the int16 range even for bounded input.
    re >>= 15;
    im >>= 15;
    if (re > 32767) re = 32767;
    if (re < -32768) re = -32768;
    if (im > 32767) im = 32767;
    if (im < -32768) im = -32768;
    sample(re, im);
}
void Receiver::sample(int16_t i, int16_t q) {
    static const unsigned steps[4] = {133, 151, 9, 27};  // -405,-135,135,405 Hz
    uint64_t best = 0;
    unsigned winner = 0;
    for (unsigned t = 0; t < 4; ++t) {
        const unsigned phase = (oscillator_ * steps[t]) % 160;
        const int32_t s = sine[phase], c = sine[(phase + 40) % 160];
        // Conjugate mixing; divide before accumulating to bound all sums.
        const int16_t re = (static_cast<int32_t>(i) * c + static_cast<int32_t>(q) * s) >> 16;
        const int16_t im = (static_cast<int32_t>(q) * c - static_cast<int32_t>(i) * s) >> 16;
        auto& tone = tones_[t];
        tone.re += re - tone.history_re[ring_];
        tone.im += im - tone.history_im[ring_];
        tone.history_re[ring_] = re;
        tone.history_im[ring_] = im;
        const uint64_t energy = static_cast<int64_t>(tone.re) * tone.re + static_cast<int64_t>(tone.im) * tone.im;
        if (energy > best) {
            best = energy;
            winner = t;
        }
    }
    if (++samples_ >= phases) symbol(lanes_[ring_], winner);
    ring_ = (ring_ + 1) % phases;
    oscillator_ = (oscillator_ + 1) % 160;
}
void Receiver::symbol(Lane& lane, unsigned value) {
    // Keep searching while collecting: a false/aborted frame must not hide
    // the next real packet for the maximum (almost ten-second) frame length.
    lane.sync = (lane.sync << 2) | value;
    const unsigned uw_errors = __builtin_popcount((lane.sync ^ 0x2424u) & 0xffffu);
    const unsigned preamble_errors = __builtin_popcount((lane.sync >> 16) ^ 0x1b1bu);
    if ((!lane.collecting && uw_errors == 0) || (uw_errors <= 1 && preamble_errors <= 2)) {
        memset(lane.data, 0, sizeof(lane.data));
        lane.data[0] = lane.data[1] = 0x24;
        lane.bits = 16;
        lane.collecting = true;
        return;
    }
    if (!lane.collecting) return;
    lane.data[lane.bits / 8] |= value << (6 - lane.bits % 8);
    lane.bits += 2;
    if (lane.bits % 8) return;
    const unsigned bytes = lane.bits / 8;
    static const unsigned lengths[] = {32, 48, 64, 96, 128};
    for (unsigned size : lengths) {
        if (bytes == static_cast<unsigned>(horus_l2_get_num_tx_data_bytes(size)) && packet(lane.data, size)) {
            lane.collecting = false;
            lane.sync = 0;
            return;
        }
    }
    if (bytes == sizeof(lane.data)) {
        lane.collecting = false;
        lane.sync = 0;
    }
}
bool Receiver::packet(const uint8_t* data, size_t size) {
    uint8_t encoded[249]{}, decoded[128]{};
    memcpy(encoded, data, horus_l2_get_num_tx_data_bytes(size));
    horus_l2_decode_rx_packet(decoded, encoded, size);
    const uint16_t crc = decoded[0] | (decoded[1] << 8);
    if (crc != horus_l2_gen_crc16(decoded + 2, size - 2)) return false;
    Position position{};
    if (!parse_v3(decoded + 2, size - 2, position)) return false;
    // Adjacent symbol phases usually recover the same packet. Report once.
    if (packets_ && crc == last_crc_ && samples_ - last_packet_sample_ < 2400) return true;
    ++packets_;
    last_crc_ = crc;
    last_packet_sample_ = samples_;
    if (callback_) callback_(position, context_);
    return true;
}
}  // namespace horus
