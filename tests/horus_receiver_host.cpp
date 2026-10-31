// Host harness for the exact fixed-memory decoder used on the M4.
#include "horus_rx.hpp"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
static void received(const horus::Position& p, void*) {
    printf("%s %u %ld %ld %ld %ld\n", p.callsign, p.sequence,
        (long)p.seconds, (long)p.latitude, (long)p.longitude, (long)p.altitude);
}
int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) return 2;
    FILE* file = fopen(argv[1], "rb");
    if (!file) return 3;
    if (argc == 3 && strcmp(argv[2], "parse") == 0) {
        uint8_t payload[256];
        const size_t size = fread(payload, 1, sizeof(payload), file);
        fclose(file);
        horus::Position p{};
        if (!horus::parse_v3(payload, size, p)) return 4;
        printf("%u %u %d %u %u %d\n", p.fields, p.millivolts, p.temperature, p.humidity, p.pressure, p.ascent);
        return 0;
    }
    horus::Receiver decoder(received, nullptr);
    int16_t iq[2];
    while (fread(iq, sizeof(iq), 1, file) == 1) {
        if (argc == 3) decoder.sample_radio_38400(iq[0], iq[1]);
        else decoder.sample(iq[0], iq[1]);
    }
    fclose(file);
    fprintf(stderr, "receiver_bytes=%zu packets=%u\n", sizeof(decoder), decoder.packets());
    return 0;
}
