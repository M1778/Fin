#pragma once
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
// Guarded because another translation unit may include <windows.h> first, and
// because the unguarded header defines `min`/`max` as macros, which breaks any
// header that uses std::min.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <cstdint>
#include <cstring>
#include <mach-o/dyld.h>
#endif

namespace fin {

// The separator between paths in a search-path list, in `FIN_LIBS` and in
// `--fin-libs` alike. It is the platform's rather than a fixed `':'`: on Windows
// a path begins `C:\`, so splitting on a colon there turns one real path into a
// bogus relative `C` and a rootless `\libs`, and does it silently (ADR 0010).
#if defined(_WIN32)
inline constexpr char kSearchPathSeparator = ';';
#else
inline constexpr char kSearchPathSeparator = ':';
#endif

// Splits a separated search-path list, dropping empty entries.
//
// An empty entry is dropped rather than read as the working directory. PATH's
// convention that a leading, trailing or doubled separator means "here" makes a
// stray keystroke change where modules are loaded from, which is the same shape
// as the empty-PATH-entry hole that Unix shells spent decades closing.
inline std::vector<std::string> splitSearchPaths(const std::string& list) {
    std::vector<std::string> paths;
    std::string current;
    for (char c : list) {
        if (c == kSearchPathSeparator) {
            if (!current.empty()) paths.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) paths.push_back(current);
    return paths;
}

// The absolute path of the running executable, or an empty string when the
// platform will not say. Every branch is a platform call rather than `argv[0]`,
// which is whatever the caller chose to pass and is a bare name under a PATH
// lookup.
//
// Known limitation on Windows: the path is narrowed with `path::string()`, so a
// finc installed under a directory outside the active code page comes back
// mangled. That is not fixed here because it is the whole codebase's convention
// — every path in the compiler is a `std::string` — and fixing it in one
// function would only move the corruption one call further out.
inline std::string executablePath() {
#if defined(_WIN32)
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n =
            GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) { buf.resize(n); break; }
        // Truncated. Grow and retry, up to the long-path ceiling.
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2);
    }
    return std::filesystem::path(buf).string();
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);  // first call reports the length
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    buf.resize(std::strlen(buf.c_str()));
    // The result may contain symlinks or `..`; the caller walks upwards from it.
    std::error_code ec;
    const auto resolved = std::filesystem::canonical(buf, ec);
    return ec ? buf : resolved.string();
#else
    std::error_code ec;
    const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    return p.string();
#endif
}

// Where the standard library that ships with a given binary lives.
//
// One rule covers both layouts that exist. A release archive unpacks to
// `bin/finc` beside `lib/std` (ADR 0010, and the install rules in
// CMakeLists.txt), and a build tree puts `finc` in `build/` beside the source
// `lib/std`. `<exe dir>/../lib/std` is both of those.
//
// Returns nothing when the directory is absent, so a search path is never
// reported that would not be read. Today that is every build, because `lib/std`
// does not exist yet.
inline std::vector<std::string> bundledLibraryPathsFor(const std::string& exePath) {
    if (exePath.empty()) return {};
    const std::filesystem::path candidate =
        std::filesystem::path(exePath).parent_path().parent_path() / "lib" / "std";
    std::error_code ec;
    if (!std::filesystem::is_directory(candidate, ec)) return {};
    return {candidate.lexically_normal().string()};
}

// The bundled standard library of the binary now running.
inline std::vector<std::string> bundledLibraryPaths() {
    return bundledLibraryPathsFor(executablePath());
}

}
