#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace test {

inline int& failures() {
    static int n = 0;
    return n;
}

inline std::string hex(const std::vector<std::uint8_t>& v) {
    static const char* digits = "0123456789ABCDEF";
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s.push_back(' ');
        s.push_back(digits[v[i] >> 4]);
        s.push_back(digits[v[i] & 0x0F]);
    }
    return s;
}

inline std::vector<std::uint8_t> bytes(std::initializer_list<int> list) {
    std::vector<std::uint8_t> v;
    for (int b : list) v.push_back(static_cast<std::uint8_t>(b));
    return v;
}

inline void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL " << what << "\n";
        ++failures();
    }
}

inline void checkBytes(const std::string& what, const std::vector<std::uint8_t>& got,
                       const std::vector<std::uint8_t>& want) {
    if (got != want) {
        std::cerr << "FAIL " << what << "\n  got  " << hex(got) << "\n  want " << hex(want) << "\n";
        ++failures();
    }
}

inline int report(const char* name) {
    if (failures() == 0) {
        std::cout << name << ": all checks passed\n";
        return 0;
    }
    std::cerr << name << ": " << failures() << " check(s) failed\n";
    return 1;
}

} // namespace test
