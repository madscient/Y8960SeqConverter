#pragma once

#include <string>
#include <vector>

namespace y8 {

enum class Severity { Warning, Error };

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string file;
    int line = 0;    // 1 based; 0 when the message is about the file as a whole
    int column = 0;  // 1 based; 0 when there is no column to point at
    std::string message;

    std::string format() const;
};

// Collects everything one run has to say. The compiler does not stop at the
// first error: a bad track does not keep the others from being checked.
class Diagnostics {
public:
    void error(const std::string& file, int line, int column, std::string message);
    void warning(const std::string& file, int line, int column, std::string message);

    bool hasErrors() const { return errors_ > 0; }
    int errorCount() const { return errors_; }
    const std::vector<Diagnostic>& all() const { return items_; }

private:
    std::vector<Diagnostic> items_;
    int errors_ = 0;
};

} // namespace y8
