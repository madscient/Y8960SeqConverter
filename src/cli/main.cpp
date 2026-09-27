#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "convert.h"
#include "diag.h"
#include "source.h"
#include "writer.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
// The manifest makes everything this process writes UTF-8. A console reads the
// bytes in its own output code page, so it is switched for the run and put back
// afterwards: the setting belongs to the console, which outlives the process.
// A pipe or a file takes the bytes as they are and is left alone.
class ConsoleUtf8 {
public:
    ConsoleUtf8() {
        DWORD mode = 0;
        if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) ||
            GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode)) {
            previous_ = GetConsoleOutputCP();
            SetConsoleOutputCP(CP_UTF8);
        }
    }
    ~ConsoleUtf8() {
        if (previous_ == 0) return;
        // What is still buffered has to reach the console before its code page
        // changes back.
        std::cout.flush();
        std::cerr.flush();
        SetConsoleOutputCP(previous_);
    }

private:
    UINT previous_ = 0;
};
#endif

void usage() {
    std::cout << "y8conv - OPLLDRV and MuSICA data to a Y8960 sequence\n"
                 "\n"
                 "  y8conv <input> [options]\n"
                 "\n"
                 "  -o <base name>        the name the output takes, without an extension\n"
                 "  --out-dir <folder>    where to write it; the input's folder by default\n"
                 "  --format opll|musica  what the input is; told from the data by default\n"
                 "  --base <hex>          where a file without a BSAVE header was loaded\n"
                 "  --rom-voices <file>   OPLLDRV's 64 ROM voices, 512 bytes out of the machine's ROM\n"
                 "  --drop <channel>      leave a channel out: FM1-FM9, RHY, PSG1-PSG3, SCC1-SCC5\n"
                 "  -h, --help            this text\n"
                 "\n"
                 "It writes <NAME>.SQ, the sequence Y8960 BASIC Extension's CALL MLOAD reads.\n";
}

bool readFile(const std::string& path, std::vector<std::uint8_t>& data) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

bool writeFile(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
    return static_cast<bool>(out);
}

std::string directoryOf(const std::string& path) {
    std::size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? std::string() : path.substr(0, cut + 1);
}

std::string withSeparator(std::string dir) {
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir.push_back('/');
    return dir;
}

bool parseHex(const std::string& text, std::uint16_t& out) {
    std::string t = text;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t = t.substr(2);
    if (!t.empty() && (t.back() == 'h' || t.back() == 'H')) t.pop_back();
    if (t.empty() || t.size() > 4) return false;
    unsigned v = 0;
    for (char c : t) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
        else return false;
    }
    out = static_cast<std::uint16_t>(v);
    return true;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    ConsoleUtf8 console;
#endif
    std::string input;
    std::string base;
    std::string outDir;
    bool haveOutDir = false;
    std::string romVoicesPath;
    y8::LoadOptions load;
    y8::ConvertOptions conv;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        }
        if (arg == "-o" || arg == "--out-dir" || arg == "--format" || arg == "--base" ||
            arg == "--rom-voices" || arg == "--drop") {
            if (i + 1 >= argc) {
                std::cerr << "y8conv: " << arg << " needs a value\n";
                return 2;
            }
            std::string value = argv[++i];
            if (arg == "-o") {
                base = value;
            } else if (arg == "--out-dir") {
                outDir = value;
                haveOutDir = true;
            } else if (arg == "--format") {
                if (value == "opll") {
                    load.format = y8::Format::Opll;
                } else if (value == "musica") {
                    load.format = y8::Format::Musica;
                } else {
                    std::cerr << "y8conv: --format is opll or musica\n";
                    return 2;
                }
            } else if (arg == "--base") {
                std::uint16_t v = 0;
                if (!parseHex(value, v)) {
                    std::cerr << "y8conv: --base takes a hexadecimal address, such as A600\n";
                    return 2;
                }
                load.base = v;
            } else if (arg == "--rom-voices") {
                romVoicesPath = value;
            } else {
                for (char& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                conv.drop.push_back(value);
            }
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "y8conv: '" << arg << "' is not an option\n";
            return 2;
        }
        if (!input.empty()) {
            std::cerr << "y8conv: only one input file at a time\n";
            return 2;
        }
        input = arg;
    }

    if (input.empty()) {
        usage();
        return 2;
    }

    std::vector<std::uint8_t> file;
    if (!readFile(input, file)) {
        std::cerr << "y8conv: cannot read '" << input << "'\n";
        return 1;
    }
    if (!romVoicesPath.empty() && !readFile(romVoicesPath, conv.romVoices)) {
        std::cerr << "y8conv: cannot read '" << romVoicesPath << "'\n";
        return 1;
    }

    y8::Diagnostics diag;
    y8::Song song;
    y8::Sequence seq;
    bool ok = y8::loadSong(file, input, load, song, diag) && y8::convert(song, conv, input, seq, diag);

    bool truncated = false;
    bool replaced = false;
    base = y8::outputBaseName(base.empty() ? input : base, truncated, replaced);
    if (replaced) {
        diag.warning(input, 0, 0,
                     "MSX-DOS names are ASCII; the characters that are not became '_' in '" + base + "'");
    }
    if (truncated) {
        diag.warning(input, 0, 0,
                     "the output name does not fit MSX-DOS's eight characters; it is '" + base + "'");
    }

    for (const y8::Diagnostic& d : diag.all()) std::cerr << d.format() << "\n";
    if (!ok) {
        std::cerr << "y8conv: " << diag.errorCount() << " error(s); nothing was written\n";
        return 1;
    }

    const std::vector<std::uint8_t> block = y8::writeBlock(seq);
    const std::string dir = haveOutDir ? withSeparator(outDir) : directoryOf(input);
    const std::string path = dir + base + ".SQ";
    if (!writeFile(path, block)) {
        std::cerr << "y8conv: cannot write '" << path << "'\n";
        return 1;
    }
    std::cout << path << " (" << block.size() << " bytes)\n";
    for (const y8::Track& t : seq.tracks) {
        std::cout << "  track " << t.number << ": " << t.name << ", " << t.bytes.size() << " bytes\n";
    }
    return 0;
}
