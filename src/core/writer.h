#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "convert.h"

namespace y8 {

// The Y8SQ block: the header, then the chunks. What MSAVE writes and MLOAD
// reads. See bytecode.md, "ブロック".
std::vector<std::uint8_t> writeBlock(const Sequence& seq);

// The output's name, fitted to MSX-DOS: the input's stem in capitals, eight
// characters at most, ASCII letters and digits, anything else '_'. `replaced`
// says a character outside ASCII was among them.
std::string outputBaseName(const std::string& inputPath, bool& truncated, bool& replaced);

} // namespace y8
