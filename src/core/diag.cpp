#include "diag.h"

namespace y8 {

std::string Diagnostic::format() const {
    std::string s = file;
    if (line > 0) {
        s += ":" + std::to_string(line);
        if (column > 0) s += ":" + std::to_string(column);
    }
    s += (severity == Severity::Error) ? ": error: " : ": warning: ";
    s += message;
    return s;
}

void Diagnostics::error(const std::string& file, int line, int column, std::string message) {
    items_.push_back({Severity::Error, file, line, column, std::move(message)});
    ++errors_;
}

void Diagnostics::warning(const std::string& file, int line, int column, std::string message) {
    items_.push_back({Severity::Warning, file, line, column, std::move(message)});
}

} // namespace y8
