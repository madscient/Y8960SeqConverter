#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "diag.h"
#include "song.h"

namespace y8 {

struct LoadOptions {
    std::optional<Format> format;        // detected when empty
    std::optional<std::uint16_t> base;   // where a file without a BSAVE header sits
};

// Reads the driver's binary into a Song. `name` is for the messages only.
bool loadSong(const std::vector<std::uint8_t>& file, const std::string& name,
              const LoadOptions& options, Song& song, Diagnostics& diag);

} // namespace y8
