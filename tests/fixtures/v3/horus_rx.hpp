// SPDX-License-Identifier: GPL-2.0-or-later
// Fixed-memory Horus V3 receiver, 2400 complex samples/s, 100 symbols/s.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace horus {
struct Position {
    char callsign[16]{};
    uint16_t sequence{};
    uint16_t millivolts{}, pressure{};
    int16_t temperature{}, ascent{};
    uint8_t humidity{}, fields{};
    int32_t seconds{}, latitude{}, longitude{}, altitude{};
};
bool parse_v3(const uint8_t* payload, size_t size, Position& out);

class Receiver {
   public:
    using Callback = void (*)(const Position&, void*);
    Receiver(Callback callback, void* context);
    void reset();
    void sample(int16_t i, int16_t q);
    void sample_38400(int16_t i, int16_t q);
    void sample_radio_38400(int16_t i, int16_t q);
    uint32_t packets() const { return packets_; }

   private:
    static constexpr unsigned phases = 24;
    struct Lane {
        uint32_t sync{};
        uint16_t bits{};
        bool collecting{};
        uint8_t data[249]{};
    } lanes_[phases]{};
    struct Tone {
        int32_t re{}, im{};
        int16_t history_re[phases]{}, history_im[phases]{};
    } tones_[4]{};
    unsigned ring_{}, oscillator_{};
    int16_t filter_i_[193]{}, filter_q_[193]{};
    unsigned filter_pos_{}, filter_count_{};
    uint32_t radio_phase_{};
    uint32_t samples_{}, packets_{};
    uint16_t last_crc_{};
    uint32_t last_packet_sample_{};
    Callback callback_;
    void* context_;
    void symbol(Lane& lane, unsigned value);
    bool packet(const uint8_t* data, size_t size);
};
}  // namespace horus
