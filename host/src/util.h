// SPDX-License-Identifier: GPL-3.0-or-later
// Small helpers for the bridge host: UTF conversion, JSON output, timing.
#pragma once

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace vg {

inline std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

inline std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// VST3 String128 / char16 -> UTF-8.
inline std::string utf8(const char16_t* s) {
    std::wstring w;
    for (; s && *s; ++s) w.push_back((wchar_t)*s);
    return utf8(w);
}

// Unix absolute paths (as passed by the Android app) -> Wine's Z: drive.
inline std::wstring toWinPath(std::wstring p) {
    if (!p.empty() && p[0] == L'/') p = L"Z:" + p;
    for (auto& c : p) if (c == L'/') c = L'\\';
    return p;
}

inline double nowMs() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}

// Minimal streaming JSON writer (objects/arrays, auto commas).
class Json {
public:
    Json& beginObj(const char* key = nullptr) { sep(key); out_ += '{'; first_.push_back(true); return *this; }
    Json& endObj() { out_ += '}'; first_.pop_back(); return *this; }
    Json& beginArr(const char* key = nullptr) { sep(key); out_ += '['; first_.push_back(true); return *this; }
    Json& endArr() { out_ += ']'; first_.pop_back(); return *this; }
    Json& str(const char* key, const std::string& v) { sep(key); quote(v); return *this; }
    Json& num(const char* key, double v) {
        sep(key);
        if (!std::isfinite(v)) { out_ += "null"; return *this; }
        char b[64];
        if (v == std::floor(v) && std::fabs(v) < 1e15) snprintf(b, sizeof b, "%.0f", v);
        else snprintf(b, sizeof b, "%.6g", v);
        out_ += b;
        return *this;
    }
    Json& boolean(const char* key, bool v) { sep(key); out_ += v ? "true" : "false"; return *this; }
    const std::string& text() const { return out_; }

private:
    void sep(const char* key) {
        if (!first_.empty()) { if (!first_.back()) out_ += ','; first_.back() = false; }
        if (key) { quote(key); out_ += ':'; }
    }
    void quote(const std::string& s) {
        out_ += '"';
        for (unsigned char c : s) {
            switch (c) {
            case '"': out_ += "\\\""; break;
            case '\\': out_ += "\\\\"; break;
            case '\n': out_ += "\\n"; break;
            case '\r': out_ += "\\r"; break;
            case '\t': out_ += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out_ += b; }
                else out_ += (char)c;
            }
        }
        out_ += '"';
    }
    std::string out_;
    std::vector<bool> first_;
};

}  // namespace vg
