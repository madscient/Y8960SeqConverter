#include <array>
#include <string>

#include "convert.h"
#include "source.h"
#include "testutil.h"
#include "writer.h"

namespace {

using test::bytes;
using test::check;
using test::checkBytes;

// The ROM's OPLL_SETUSER (Y8960BasicExtension src/dev/opllex.asm), written out
// here the way the ROM reads the record, so that recordFromOpll is checked
// against the reader rather than against itself. The record's offsets are the
// ROM's VP_* and VO_*.
std::array<std::uint8_t, 8> setUser(const y8::PackedVoice& r) {
    std::array<std::uint8_t, 8> o{};
    o[0] = r[5];
    o[1] = r[10];
    o[2] = r[2];
    o[3] = static_cast<std::uint8_t>((r[7] & 0xC0) | (((r[11] >> 4) | (r[11] << 4)) & 0x10) |
                                     (((r[6] >> 5) | (r[6] << 3)) & 0x08) |
                                     (((r[0] >> 1) | (r[0] << 7)) & 0x07));
    o[4] = r[3];
    o[5] = r[8];
    o[6] = r[4];
    o[7] = r[9];
    return o;
}

void recordRoundTrip() {
    for (int b3 = 0; b3 < 256; ++b3) {
        if (b3 & 0x20) continue;  // always 0 on the OPLL
        std::array<std::uint8_t, 8> o = {0x31, 0x11, 0x0E, static_cast<std::uint8_t>(b3),
                                         0xD9, 0xB2, 0x11, 0xF4};
        y8::PackedVoice r = y8::recordFromOpll(o.data());
        std::array<std::uint8_t, 8> back = setUser(r);
        check(back == o, "record round trip, byte 3 = " + std::to_string(b3));
    }
}

void packPreset() {
    // Piano 1: C0h 0Ah, no transpose; the modulator 31h 0Eh D9h 11h, the
    // carrier 11h 00h B2h F4h, in the order 20h 40h 60h 80h.
    const y8::PackedVoice piano = y8::packVoice(y8::presetVoice(0));
    checkBytes("packed Piano 1", std::vector<std::uint8_t>(piano.begin(), piano.end()),
               bytes({0x0A, 0x00, 0x0E, 0xD9, 0x11, 0x31, 0x00, 0x00, 0xB2, 0xF4, 0x11, 0x00}));
    y8::VoiceRecord b{};
    b[8] = 0x80;  // half a semitone rounds up
    b[9] = 0x01;
    b[10] = 0xFF;
    b[21] = 0xFF;
    const y8::PackedVoice p = y8::packVoice(b);
    check(p[1] == 2, "the transpose rounds half up");
    check(p[0] == 0x0F, "C0h keeps bit3-0");
    check(p[6] == 0x03, "the waveform keeps bit1-0");
    b[8] = 0x7F;
    check(y8::packVoice(b)[1] == 1, "the transpose rounds down below half");
}

bool convertBytes(const std::vector<std::uint8_t>& file, y8::Sequence& seq,
                  const y8::LoadOptions& load = {}) {
    y8::Diagnostics diag;
    y8::Song song;
    bool ok = y8::loadSong(file, "test", load, song, diag) &&
              y8::convert(song, y8::ConvertOptions(), "test", seq, diag);
    for (const y8::Diagnostic& d : diag.all()) std::cerr << "  " << d.format() << "\n";
    return ok;
}

void opllMelody() {
    // Melody 9: nine offsets, channel 1 at 12h, the others at one shared FFh.
    std::vector<std::uint8_t> f = bytes({0x12, 0x00});
    for (int i = 1; i < 9; ++i) {
        f.push_back(0x19);
        f.push_back(0x00);
    }
    // @2, the loudest, O4C for 24, a rest of 12
    for (int b : {0x72, 0x60, 0x25, 0x18, 0x00, 0x0C, 0xFF, 0xFF}) f.push_back(static_cast<std::uint8_t>(b));

    y8::Sequence seq;
    check(convertBytes(f, seq), "OPLLDRV melody converts");
    check(seq.tracks.size() == 1, "channels with nothing in them take no track");
    if (seq.tracks.empty()) return;
    check(seq.tracks[0].device == 1 && seq.tracks[0].channel == 0, "FM1 is OPLLEX1 channel 0");
    // T75, V15, the YM2413's instrument 2 (bank 0), and the source's O4C an
    // octave down, where it sounds.
    checkBytes("OPLLDRV melody", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0x81, 0x7F, 0x82, 0x02, 0x41, 0x00, 0x18, 0x0C, 0x0C, 0xFF}));
}

// Melody 9 with channel 1 alone, its events `ev`.
std::vector<std::uint8_t> opllOneChannel(const std::vector<int>& ev) {
    std::vector<std::uint8_t> f = bytes({0x12, 0x00});
    for (int i = 1; i < 9; ++i) {
        f.push_back(0x00);
        f.push_back(0x00);
    }
    for (int b : ev) f.push_back(static_cast<std::uint8_t>(b));
    f.push_back(0xFF);
    return f;
}

void opllRomVoice() {
    // 82h 01 (Piano 2, whose preset transposes up an octave), 70h, O4C for 24.
    y8::Sequence seq;
    check(convertBytes(opllOneChannel({0x82, 0x01, 0x70, 0x25, 0x18}), seq),
          "OPLLDRV ROM voice converts");
    if (seq.voices.size() != 1) {
        check(false, "one voice");
        return;
    }
    // FM-BIOS plays the ROM's voice untransposed.
    y8::PackedVoice want = y8::packVoice(y8::presetVoice(1));
    want[1] = 0;
    checkBytes("OPLLDRV ROM voice", seq.voices[0].record,
               std::vector<std::uint8_t>(want.begin(), want.end()));
}

void opllLoadOnly() {
    // 82h and 83h load the user voice and leave the instrument as it is: here
    // FM-BIOS's starting 11, attenuation 3 (V12).
    y8::Sequence seq;
    check(convertBytes(opllOneChannel({0x82, 0x01, 0x25, 0x18}), seq),
          "OPLLDRV 82h without 70h converts");
    if (seq.tracks.size() != 1) {
        check(false, "one track");
        return;
    }
    check(seq.voices.empty(), "a user voice nothing selects is not carried");
    checkBytes("OPLLDRV 82h without 70h", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0x82, 0x0B, 0x81, 0x73, 0x41, 0x00, 0x18, 0xFF}));
}

void opllUnused() {
    // Melody 9 with channel 1 alone: the other eight offsets are 0.
    std::vector<std::uint8_t> f = bytes({0x12, 0x00});
    for (int i = 1; i < 9; ++i) {
        f.push_back(0x00);
        f.push_back(0x00);
    }
    for (int b : {0x25, 0x18, 0xFF}) f.push_back(static_cast<std::uint8_t>(b));
    y8::Sequence seq;
    check(convertBytes(f, seq), "OPLLDRV with unused channels converts");
    check(seq.tracks.size() == 1, "an offset of 0 is a channel the song does not use");
}

void opllRhythm() {
    // Melody 6 and rhythm: the rhythm first, then channels 1-6.
    std::vector<std::uint8_t> f = bytes({0x0E, 0x00});
    for (int i = 1; i < 7; ++i) {
        f.push_back(0x16);
        f.push_back(0x00);
    }
    // the bass drum at attenuation 0, then bass and hi-hat for 12
    for (int b : {0xB0, 0x00, 0x31, 0x0C, 0xFF, 0x00, 0x00, 0x00, 0xFF}) {
        f.push_back(static_cast<std::uint8_t>(b));
    }
    f[0x16] = 0xFF;
    y8::Sequence seq;
    check(convertBytes(f, seq), "OPLLDRV rhythm converts");
    if (seq.tracks.size() != 1) {
        check(false, "one rhythm track");
        return;
    }
    check(seq.tracks[0].channel == 10, "the rhythm is channel 10");
    // The bass drum at level 15 and the hi-hat at the default attenuation 3,
    // level 12, each its own plain volume; the other three ride along with the
    // hi-hat's. No accent.
    checkBytes("OPLLDRV rhythm", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0xD8, 0x10, 0x0F, 0xD8, 0x0F, 0x0C, 0xA8, 0x00, 0xC8, 0x11, 0x0C,
                      0xFF}));
}

// Rhythm and melody 6, the rhythm alone, its events `ev`.
std::vector<std::uint8_t> opllRhythmOnly(const std::vector<int>& ev) {
    std::vector<std::uint8_t> f = bytes({0x0E, 0x00});
    for (int i = 1; i < 7; ++i) {
        f.push_back(0x00);
        f.push_back(0x00);
    }
    for (int b : ev) f.push_back(static_cast<std::uint8_t>(b));
    f.push_back(0xFF);
    return f;
}

void opllRhythmAll() {
    // every instrument at attenuation 7, level 8, then bass and hi-hat twice
    y8::Sequence seq;
    check(convertBytes(opllRhythmOnly({0xBF, 0x07, 0x31, 0x0C, 0x31, 0x0C}), seq),
          "OPLLDRV rhythm, one volume for all converts");
    if (seq.tracks.size() != 1) {
        check(false, "one rhythm track");
        return;
    }
    // One level for all five is A9. It is written though 8 is what the reader
    // resets to: a repeat of the sequence does not reset it. The second
    // strike sets nothing again.
    checkBytes("OPLLDRV rhythm, one volume for all", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0xA9, 0x08, 0xA8, 0x00, 0xC8, 0x11, 0x0C, 0xC8, 0x11, 0x0C, 0xFF}));
}

void opllRhythmNoBits() {
    // A volume naming no instrument sets none (FM-BIOS's next_event2): the
    // strike keeps the starting attenuation 3, level 12.
    y8::Sequence seq;
    check(convertBytes(opllRhythmOnly({0xA0, 0x07, 0x31, 0x0C}), seq),
          "OPLLDRV rhythm volume naming none converts");
    if (seq.tracks.size() != 1) {
        check(false, "one rhythm track");
        return;
    }
    checkBytes("OPLLDRV rhythm volume naming none", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0xA9, 0x0C, 0xA8, 0x00, 0xC8, 0x11, 0x0C, 0xFF}));
}

void opllSustain() {
    // 80h sets the sustain and 81h releases it, as FM-BIOS's OPLDRV does.
    y8::Sequence seq;
    check(convertBytes(opllOneChannel({0x71, 0x80, 0x25, 0x18, 0x81, 0x25, 0x18}), seq),
          "OPLLDRV sustain converts");
    if (seq.tracks.size() != 1) return;
    checkBytes("OPLLDRV sustain", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0xE0, 0x20, 0x20, 0xDF, 0x82, 0x01, 0x81, 0x73, 0x41, 0x00, 0x18,
                      0xE0, 0x20, 0x00, 0xDF, 0x00, 0x18, 0xFF}));
}

void opllLegatoRest() {
    // Legato on, O4C, a rest, legato off, O4D: OPLDRV's rest touches no key,
    // so the C sounds through it and the D continues it.
    y8::Sequence seq;
    check(convertBytes(opllOneChannel({0x71, 0x85, 0x25, 0x18, 0x00, 0x0C, 0x84, 0x27, 0x18}), seq),
          "OPLLDRV legato rest converts");
    if (seq.tracks.size() != 1) return;
    checkBytes("OPLLDRV legato rest", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0x82, 0x01, 0x81, 0x73, 0x41, 0x00, 0x18, 0x0E, 0x0C, 0x45, 0x02,
                      0x18, 0xFF}));
}

void opllQuantZero() {
    // OPLDRV takes Q AND 7, 0 being the whole length: Q0 is Q8, Q9 is Q1.
    y8::Sequence seq;
    check(convertBytes(opllOneChannel({0x71, 0x86, 0x00, 0x25, 0x18, 0x86, 0x09, 0x25, 0x18}), seq),
          "OPLLDRV Q0 converts");
    if (seq.tracks.size() != 1) return;
    checkBytes("OPLLDRV Q0", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0x82, 0x01, 0x81, 0x73, 0x41, 0x00, 0x18, 0x83, 0x01, 0x00, 0x18,
                      0xFF}));
}

std::vector<std::uint8_t> musicaFile(std::uint16_t base, const std::vector<std::uint8_t>& body) {
    std::vector<std::uint8_t> f = {0xFE, static_cast<std::uint8_t>(base & 0xFF),
                                   static_cast<std::uint8_t>(base >> 8)};
    const unsigned end = base + static_cast<unsigned>(body.size()) - 1;
    f.push_back(static_cast<std::uint8_t>(end & 0xFF));
    f.push_back(static_cast<std::uint8_t>(end >> 8));
    f.push_back(static_cast<std::uint8_t>(base & 0xFF));
    f.push_back(static_cast<std::uint8_t>(base >> 8));
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

// A header of mode 1 whose channel `ch` (0-16) has its sequence at `seq`.
std::vector<std::uint8_t> musicaHeader(int ch, std::uint16_t seq) {
    std::vector<std::uint8_t> b(35, 0);
    b[0] = 1;
    b[static_cast<std::size_t>(1 + ch * 2)] = static_cast<std::uint8_t>(seq & 0xFF);
    b[static_cast<std::size_t>(2 + ch * 2)] = static_cast<std::uint8_t>(seq >> 8);
    return b;
}

void musicaLoop() {
    const std::uint16_t base = 0xA000;
    std::vector<std::uint8_t> body = musicaHeader(0, base + 35);
    // the block at 29h three times; O5C for 12
    for (int b : {0x29, 0xA0, 0x03, 0x00, 0x00, 0x00, 0x31, 0x0C, 0xFF}) body.push_back(static_cast<std::uint8_t>(b));
    y8::Sequence seq;
    check(convertBytes(musicaFile(base, body), seq), "MuSICA loop converts");
    if (seq.tracks.size() != 1) {
        check(false, "one track");
        return;
    }
    // The first round sets the voice (MuSICA's 7Ah) and the volume, which the
    // later rounds do not, so it is written out and the other two loop. The
    // loop's body names its octave outright. The FM's O5C is Y8960's O4C.
    checkBytes("MuSICA loop", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0x82, 0x0A, 0x81, 0x7F, 0x00, 0x0C,
                      0x42, 0x80, 0x04, 0x00, 0x0C, 0xE2, 0x02, 0xF8, 0xFF, 0xFF}));
}

void fmLowest() {
    const std::uint16_t base = 0xA000;
    std::vector<std::uint8_t> body = musicaHeader(0, base + 35);
    // the block at 29h once; O1C for 12
    for (int b : {0x29, 0xA0, 0x01, 0x00, 0x00, 0x00, 0x01, 0x0C, 0xFF}) body.push_back(static_cast<std::uint8_t>(b));
    y8::Sequence seq;
    check(convertBytes(musicaFile(base, body), seq), "FM O1 converts");
    if (seq.tracks.size() != 1) {
        check(false, "one track");
        return;
    }
    // MuSICA's FM O1C sounds as C0: octave 0.
    checkBytes("FM O1", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0x82, 0x0A, 0x81, 0x7F, 0x80, 0x00, 0x00, 0x0C, 0xFF}));
}

// One FM block played once, the given commands in it.
bool fmBlock(const std::vector<int>& block, y8::Sequence& seq) {
    const std::uint16_t base = 0xA000;
    std::vector<std::uint8_t> body = musicaHeader(0, base + 35);
    for (int x : {0x29, 0xA0, 0x01, 0x00, 0x00, 0x00}) body.push_back(static_cast<std::uint8_t>(x));
    for (int x : block) body.push_back(static_cast<std::uint8_t>(x));
    body.push_back(0xFF);
    return convertBytes(musicaFile(base, body), seq) && seq.tracks.size() == 1;
}

void legatoBracket() {
    // MuSICA's "q6 (d i50) d i": Q6, legato on, O6D, vibrato, legato off,
    // O6D, vibrato off. The first D is held when it ends, so the second
    // continues it, and the second is cut by Q6.
    y8::Sequence seq;
    check(fmBlock({0x86, 0x06, 0x85, 0x3F, 0x1E, 0x89, 0x32, 0x84, 0x3F, 0x1E, 0x89, 0x00}, seq),
          "legato bracket converts");
    if (seq.tracks.empty()) return;
    checkBytes("legato bracket", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0x82, 0x0A, 0x81, 0x7F, 0x40, 0x02, 0x1E,
                      0x83, 0x06, 0x45, 0x02, 0x1E, 0xFF}));
}

void legatoLate() {
    // O6D, legato on, O6D: the first ends with legato off and is keyed off,
    // so the second starts afresh.
    y8::Sequence seq;
    check(fmBlock({0x3F, 0x1E, 0x85, 0x3F, 0x1E}, seq), "late legato converts");
    if (seq.tracks.empty()) return;
    checkBytes("late legato", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0x82, 0x0A, 0x81, 0x7F, 0x40, 0x02, 0x1E, 0x02, 0x1E, 0xFF}));
}

void musicaFold() {
    const std::uint16_t base = 0xA000;
    std::vector<std::uint8_t> body = musicaHeader(0, base + 35);
    // A B A B A B, A at 50h and B at 53h: A plays O5C, B O5D
    const int a = 0x50, b = 0x53;
    for (int i = 0; i < 3; ++i) {
        for (int blk : {a, b}) {
            body.push_back(static_cast<std::uint8_t>(blk));
            body.push_back(0xA0);
            body.push_back(0x01);
        }
    }
    body.push_back(0x00);
    body.push_back(0x00);
    body.resize(0x50, 0);
    for (int x : {0x31, 0x0C, 0xFF, 0x33, 0x0C, 0xFF}) body.push_back(static_cast<std::uint8_t>(x));
    y8::Sequence seq;
    check(convertBytes(musicaFile(base, body), seq), "MuSICA fold converts");
    if (seq.tracks.size() != 1) {
        check(false, "one track");
        return;
    }
    // The first round of A B, then a loop of two more.
    checkBytes("MuSICA fold", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0x82, 0x0A, 0x81, 0x7F, 0x00, 0x0C, 0x02, 0x0C,
                      0x42, 0x80, 0x04, 0x00, 0x0C, 0x02, 0x0C, 0xE2, 0x02, 0xF6, 0xFF, 0xFF}));
}

void psgVoice() {
    const std::uint16_t base = 0xA000;
    std::vector<std::uint8_t> body = musicaHeader(9, base + 35);  // PSG1
    // the block at 28h once; then the block: voice at 34h, V15, Q4, O4C for 4,
    // a rest of 2; the voice: attack 1 a step every interrupt, decay 4 every
    // interrupt, sustain 8, release 1 every third; noise period 3, tone and noise
    for (int x : {0x28, 0xA0, 0x01, 0x00, 0x00}) body.push_back(static_cast<std::uint8_t>(x));
    body.resize(0x28, 0);
    for (int x : {0x83, 0x34, 0xA0, 0x6F, 0x86, 0x04, 0x25, 0x04, 0x00, 0x02, 0xFF}) {
        body.push_back(static_cast<std::uint8_t>(x));
    }
    body.resize(0x34, 0);
    for (int x : {0x11, 0x14, 0x08, 0x31, 0x03, 0x00}) body.push_back(static_cast<std::uint8_t>(x));
    y8::Sequence seq;
    check(convertBytes(musicaFile(base, body), seq), "PSG voice converts");
    if (seq.tracks.size() != 1) {
        check(false, "one track");
        return;
    }
    check(seq.tracks[0].device == 0 && seq.tracks[0].channel == 0, "PSG1 is SSGS channel 0");
    // Envelope 1, the noise period, tone and noise (the SSGS's @64), V15, Q4,
    // the note and the rest.
    checkBytes("PSG voice", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0xB2, 0x01, 0xE0, 0x06, 0x03, 0x00, 0x82, 0x40, 0x81, 0x7F,
                      0x83, 0x04, 0x00, 0x04, 0x0C, 0x02, 0xFF}));
    check(seq.envelopes.size() == 1, "one envelope");
    if (seq.envelopes.size() == 1) {
        const y8::Envelope& e = seq.envelopes[0];
        check(e.ar == 0x11 && e.dr == 0x14 && e.sl == 8 && e.rr == 0x31,
              "the envelope's rates are MuSICA's bytes");
    }
}

void envelopeRates() {
    bool exact = false;
    check(y8::envelopeRate(0xF1, exact) == 0xF1 && exact, "15 interrupts, 1 a step, as it is");
    check(y8::envelopeRate(0x1F, exact) == 0x1F && exact, "1 interrupt, 15 a step, as it is");
    check(y8::envelopeRate(0x22, exact) == 0x22 && exact, "a pair outside MuSICA's 33, as it is");
    check(y8::envelopeRate(0x30, exact) == 0xF1 && !exact, "a step of 0 is nearest the slowest");
    check(y8::envelopeRate(0x0F, exact) == 0xF1 && !exact,
          "a counter of 0, 256 interrupts, is nearest the slowest");
}

void envelopeChunk() {
    y8::Sequence seq;
    seq.envelopes.push_back({0x11, 0x14, 8, 0x31});
    std::vector<std::uint8_t> b = y8::writeBlock(seq);
    checkBytes("envelope chunk", std::vector<std::uint8_t>(b.begin() + 7, b.end()),
               bytes({0x04, 0x05, 0x00, 0x01, 0x11, 0x14, 0x08, 0x31}));
}

// A MuSICA file in mode 0 whose rhythm (channel 7) plays the block `ev` once.
std::vector<std::uint8_t> musicaRhythm(const std::vector<int>& ev) {
    const std::uint16_t base = 0xA000;
    std::vector<std::uint8_t> body = musicaHeader(6, base + 35);
    body[0] = 0;
    for (int x : {0x28, 0xA0, 0x01, 0x00, 0x00}) body.push_back(static_cast<std::uint8_t>(x));
    body.resize(0x28, 0);
    for (int x : ev) body.push_back(static_cast<std::uint8_t>(x));
    body.push_back(0xFF);
    return musicaFile(base, body);
}

void musicaRhythmEvents() {
    // A volume naming no instrument sets none, and C1h waits (BGM.BIN): the
    // strike keeps MuSICA's starting attenuation 0, level 15.
    y8::Sequence seq;
    check(convertBytes(musicaRhythm({0xA0, 0x07, 0x31, 0x0C, 0xC1, 0x18}), seq),
          "MuSICA rhythm events convert");
    if (seq.tracks.size() != 1) {
        check(false, "one rhythm track");
        return;
    }
    checkBytes("MuSICA rhythm events", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0xA9, 0x0F, 0xA8, 0x00, 0xC8, 0x11, 0x0C, 0x0E, 0x18, 0xFF}));
}

void sccTrack() {
    const std::uint16_t base = 0xA000;
    std::vector<std::uint8_t> body = musicaHeader(12, base + 35);  // SCC1
    // the block at 28h once; O4C for 4 before any waveform
    for (int x : {0x28, 0xA0, 0x01, 0x00, 0x00}) body.push_back(static_cast<std::uint8_t>(x));
    body.resize(0x28, 0);
    for (int x : {0x25, 0x04, 0xFF}) body.push_back(static_cast<std::uint8_t>(x));
    y8::Sequence seq;
    check(convertBytes(musicaFile(base, body), seq), "SCC track converts");
    if (seq.tracks.size() != 1) {
        check(false, "one track");
        return;
    }
    check(seq.tracks[0].device == 7 && seq.tracks[0].channel == 0, "SCC1 is SCC channel 0");
    // The SCC's volume without the table, the preset waveform 0, MuSICA's
    // starting level 0 as V0, the note.
    checkBytes("SCC track", seq.tracks[0].bytes,
               bytes({0x84, 0x4B, 0xB3, 0x00, 0x85, 0x00, 0x81, 0x43, 0x00, 0x04, 0xFF}));
}

void voiceChunk() {
    y8::Sequence seq;
    const y8::PackedVoice v = y8::packVoice(y8::presetVoice(0));
    seq.voices.push_back({false, {v.begin(), v.end()}});
    std::vector<std::uint8_t> b = y8::writeBlock(seq);
    check(b.size() == 7 + 3 + 13, "an FM voice is 12 bytes and its index");
    check(b[7] == 0x01 && b[8] == 13 && b[9] == 0 && b[10] == 0, "the FM voice is chunk 01, slot 0");
}

void blockHeader() {
    y8::Sequence seq;
    y8::Track t;
    t.number = 3;
    t.device = 7;
    t.channel = 2;
    t.bytes = bytes({0xFF});
    seq.tracks.push_back(t);
    const y8::VoiceRecord w = y8::presetWave(0);
    seq.voices.push_back({true, {w.begin(), w.end()}});
    std::vector<std::uint8_t> b = y8::writeBlock(seq);
    check(b.size() == 7 + 3 + 4 + 3 + 33, "block size");
    check(b[5] == (b.size() & 0xFF) && b[6] == (b.size() >> 8), "the header holds the size");
    checkBytes("track chunk", std::vector<std::uint8_t>(b.begin() + 7, b.begin() + 14),
               bytes({0x00, 0x04, 0x00, 0x03, 0x07, 0x02, 0xFF}));
    check(b[14] == 0x02 && b[15] == 33 && b[16] == 0 && b[17] == 0, "the waveform is chunk 02, slot 0");
}

} // namespace

int main() {
    recordRoundTrip();
    packPreset();
    opllMelody();
    opllUnused();
    opllRomVoice();
    opllLoadOnly();
    opllRhythm();
    opllRhythmAll();
    opllRhythmNoBits();
    opllSustain();
    opllLegatoRest();
    opllQuantZero();
    musicaLoop();
    musicaFold();
    legatoBracket();
    legatoLate();
    fmLowest();
    psgVoice();
    blockHeader();
    envelopeRates();
    envelopeChunk();
    sccTrack();
    musicaRhythmEvents();
    voiceChunk();
    return test::report("convert_test");
}
