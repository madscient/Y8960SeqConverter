#include "writer.h"

#include <cctype>

namespace y8 {
namespace {

void putWord(std::vector<std::uint8_t>& out, unsigned v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void putChunk(std::vector<std::uint8_t>& out, std::uint8_t type,
              const std::vector<std::uint8_t>& body) {
    out.push_back(type);
    putWord(out, static_cast<unsigned>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
}

constexpr std::uint8_t kChunkTrack = 0x00;
constexpr std::uint8_t kChunkVoice = 0x01;
constexpr std::uint8_t kChunkWave = 0x02;
constexpr std::uint8_t kChunkEnvelope = 0x04;

} // namespace

std::vector<std::uint8_t> writeBlock(const Sequence& seq) {
    std::vector<std::uint8_t> out;
    const char* magic = "Y8SQ";
    out.insert(out.end(), magic, magic + 4);
    out.push_back(0x01);
    putWord(out, 0);  // the size, once it is known

    for (const Track& t : seq.tracks) {
        std::vector<std::uint8_t> body;
        body.push_back(static_cast<std::uint8_t>(t.number));
        body.push_back(static_cast<std::uint8_t>(t.device));
        body.push_back(static_cast<std::uint8_t>(t.channel));
        body.insert(body.end(), t.bytes.begin(), t.bytes.end());
        putChunk(out, kChunkTrack, body);
    }

    for (std::size_t i = 0; i < seq.voices.size(); ++i) {
        std::vector<std::uint8_t> body;
        body.push_back(static_cast<std::uint8_t>(i));
        body.insert(body.end(), seq.voices[i].record.begin(), seq.voices[i].record.end());
        putChunk(out, seq.voices[i].isWave ? kChunkWave : kChunkVoice, body);
    }

    for (std::size_t i = 0; i < seq.envelopes.size(); ++i) {
        const Envelope& e = seq.envelopes[i];
        putChunk(out, kChunkEnvelope,
                 {static_cast<std::uint8_t>(i + 1), static_cast<std::uint8_t>(e.ar),
                  static_cast<std::uint8_t>(e.dr), static_cast<std::uint8_t>(e.sl),
                  static_cast<std::uint8_t>(e.rr)});
    }

    out[5] = static_cast<std::uint8_t>(out.size() & 0xFF);
    out[6] = static_cast<std::uint8_t>((out.size() >> 8) & 0xFF);
    return out;
}

std::string outputBaseName(const std::string& inputPath, bool& truncated, bool& replaced) {
    std::size_t slash = inputPath.find_last_of("/\\");
    std::string stem = (slash == std::string::npos) ? inputPath : inputPath.substr(slash + 1);
    std::size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos && dot != 0) stem = stem.substr(0, dot);

    // The path is UTF-8, and a character that is not ASCII becomes one '_'
    // however many bytes it takes. A byte that does not begin a well formed
    // sequence counts as a character of its own.
    std::string name;
    replaced = false;
    for (std::size_t i = 0; i < stem.size();) {
        unsigned char u = static_cast<unsigned char>(stem[i]);
        if (u < 0x80) {
            name.push_back(std::isalnum(u) ? static_cast<char>(std::toupper(u)) : '_');
            ++i;
            continue;
        }
        std::size_t len = (u >= 0xF0 && u <= 0xF4) ? 4 : (u >= 0xE0) ? 3 : (u >= 0xC2) ? 2 : 1;
        if (u > 0xF4) len = 1;
        for (std::size_t k = 1; k < len; ++k) {
            unsigned char c = (i + k < stem.size()) ? static_cast<unsigned char>(stem[i + k]) : 0;
            if ((c & 0xC0) != 0x80) {
                len = 1;
                break;
            }
        }
        name.push_back('_');
        replaced = true;
        i += len;
    }
    if (name.empty()) name = "OUTPUT";
    truncated = name.size() > 8;
    if (truncated) name.resize(8);
    return name;
}

} // namespace y8
