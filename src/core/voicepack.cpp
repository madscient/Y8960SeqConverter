#include "voicedata.h"

namespace y8 {
namespace {

// The MSX-AUDIO layout (the ROM's BV_* and BVO_*).
constexpr int kBasicTranFrac = 8;
constexpr int kBasicTranWhole = 9;
constexpr int kBasicFb = 10;
constexpr int kBasicMod = 16;
constexpr int kBasicCar = 24;
constexpr int kBasicMult = 0;
constexpr int kBasicTl = 1;
constexpr int kBasicAr = 2;
constexpr int kBasicSl = 3;
constexpr int kBasicWave = 5;

// Chunk 01 (VP_* and VO_*).
constexpr int kPackedFb = 0;
constexpr int kPackedTran = 1;
constexpr int kPackedMod = 2;
constexpr int kPackedCar = 7;
constexpr int kPackedTl = 0;
constexpr int kPackedAr = 1;
constexpr int kPackedSl = 2;
constexpr int kPackedMult = 3;
constexpr int kPackedWave = 4;

void packOperator(const VoiceRecord& b, int from, PackedVoice& p, int to) {
    p[static_cast<std::size_t>(to + kPackedTl)] = b[static_cast<std::size_t>(from + kBasicTl)];
    p[static_cast<std::size_t>(to + kPackedAr)] = b[static_cast<std::size_t>(from + kBasicAr)];
    p[static_cast<std::size_t>(to + kPackedSl)] = b[static_cast<std::size_t>(from + kBasicSl)];
    p[static_cast<std::size_t>(to + kPackedMult)] = b[static_cast<std::size_t>(from + kBasicMult)];
    // BASIC keeps flags of its own above the two bits.
    p[static_cast<std::size_t>(to + kPackedWave)] =
        static_cast<std::uint8_t>(b[static_cast<std::size_t>(from + kBasicWave)] & 0x03);
}

} // namespace

PackedVoice packVoice(const VoiceRecord& b) {
    PackedVoice p{};
    p[kPackedFb] = static_cast<std::uint8_t>(b[kBasicFb] & 0x0F);
    p[kPackedTran] = static_cast<std::uint8_t>(b[kBasicTranWhole] + (b[kBasicTranFrac] >= 0x80 ? 1 : 0));
    packOperator(b, kBasicMod, p, kPackedMod);
    packOperator(b, kBasicCar, p, kPackedCar);
    return p;
}

} // namespace y8
