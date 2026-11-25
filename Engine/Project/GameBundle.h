#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Rice {
namespace Bundle {
namespace fs = std::filesystem;
// The Windows executable is followed by length-prefixed relative filenames and
// file contents. The footer locates the payload without relying on the EXE name.
inline constexpr std::string_view signature = "Rice.GameBundle.1";
inline constexpr auto footerBytes = sizeof(uint64_t) + signature.size();

template<class T> inline void Put(std::ostream& out, T value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}
template<class T> inline T Get(std::istream& in) {
    T value{};
    if (!in.read(reinterpret_cast<char*>(&value), sizeof(value)))
        throw std::runtime_error("Incomplete Rice game bundle");
    return value;
}
inline void Copy(std::istream& in, std::ostream& out, uint64_t size) {
    std::array<char, 65536> bytes;
    while (size) {
        auto n = static_cast<std::streamsize>(std::min<uint64_t>(size, bytes.size()));
        if (!in.read(bytes.data(), n) || !out.write(bytes.data(), n))
            throw std::runtime_error("Could not copy game data");
        size -= n;
    }
}
inline bool Locate(std::ifstream& in, uint64_t& start, uint64_t& end) {
    in.seekg(0, std::ios::end);
    auto size = in.tellg();
    if (size < static_cast<std::streamoff>(footerBytes)) return false;
    end = static_cast<uint64_t>(size) - footerBytes;
    in.seekg(static_cast<std::streamoff>(end));
    std::array<char, signature.size()> tag{};
    return in.read(reinterpret_cast<char*>(&start), sizeof(start)) &&
           in.read(tag.data(), tag.size()) && start > 0 && start < end &&
           std::string_view(tag.data(), tag.size()) == signature;
}
inline fs::path Relative(const std::u8string& name) {
    if (name.find_first_of(u8"\\:<>|?*\"") != name.npos || name.find(u8'\0') != name.npos)
        throw std::runtime_error("Invalid game data path");
    fs::path path(name);
    if (path.empty() || path.has_root_path()) throw std::runtime_error("Expected a relative game data path");
    for (const auto& part : path) {
        const auto& leaf = part.native();
        if (leaf.empty() || leaf.back() == '.' || leaf.back() == ' ')
            throw std::runtime_error("Invalid game data path component");
    }
    return path;
}
}

inline bool IsGameBundle(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    uint64_t start = 0, end = 0;
    return in && Bundle::Locate(in, start, end);
}
inline void WriteGameBundle(const std::filesystem::path& host, const std::filesystem::path& staging,
                            const std::filesystem::path& output) {
    namespace fs = std::filesystem;
    if (fs::exists(output)) throw std::runtime_error("Game file already exists");
    std::ifstream launcher(host, std::ios::binary);
    if (!launcher || !fs::is_regular_file(staging / "RicePlayer.exe"))
        throw std::runtime_error("The standalone game runtime is missing");
    // Finish a private file on the destination volume before publishing it.
    // Concurrent exports must never truncate another user's completed build.
    auto temporary = output.parent_path() / (".rice-build-" + std::to_string(std::random_device{}()));
    if (!fs::create_directory(temporary)) throw std::runtime_error("Could not reserve space for the game file");
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{temporary};
    const auto complete = temporary / "game";
    std::ofstream out(complete, std::ios::binary);
    auto start = fs::file_size(host);
    Bundle::Copy(launcher, out, start);
    for (const auto& file : fs::recursive_directory_iterator(staging)) {
        if (file.is_symlink()) throw std::runtime_error("Game data must contain regular files");
        if (!file.is_regular_file()) continue;
        auto name = file.path().lexically_relative(staging).generic_u8string();
        Bundle::Relative(name);
        if (name.size() > 32767) throw std::runtime_error("Game data path is too long");
        Bundle::Put(out, static_cast<uint32_t>(name.size()));
        out.write(reinterpret_cast<const char*>(name.data()), name.size());
        auto size = file.file_size();
        Bundle::Put(out, static_cast<uint64_t>(size));
        std::ifstream in(file.path(), std::ios::binary);
        Bundle::Copy(in, out, size);
    }
    Bundle::Put(out, static_cast<uint64_t>(start));
    out.write(Bundle::signature.data(), Bundle::signature.size());
    out.close();
    if (!out) throw std::runtime_error("Could not finish writing the game file");
    std::error_code ec;
    fs::create_hard_link(complete, output, ec);
    if (ec) fs::copy_file(complete, output); // FAT/exFAT also support portable builds.
}
inline void ExtractGameBundle(const std::filesystem::path& file, const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(directory) || !fs::is_empty(directory))
        throw std::runtime_error("Expected an empty game runtime directory");
    std::ifstream in(file, std::ios::binary);
    uint64_t start = 0, end = 0;
    if (!Bundle::Locate(in, start, end)) throw std::runtime_error("Invalid Rice game file");
    in.seekg(static_cast<std::streamoff>(start));
    while (static_cast<uint64_t>(in.tellg()) < end) {
        auto n = Bundle::Get<uint32_t>(in);
        if (!n || n > 32767) throw std::runtime_error("Invalid game data filename");
        std::u8string name(n, u8'\0');
        in.read(reinterpret_cast<char*>(name.data()), n);
        auto size = Bundle::Get<uint64_t>(in);
        auto pos = static_cast<uint64_t>(in.tellg());
        if (pos > end || size > end - pos) throw std::runtime_error("Invalid game data size");
        auto path = directory / Bundle::Relative(name);
        fs::create_directories(path.parent_path());
        if (fs::exists(path)) throw std::runtime_error("Duplicate game data filename");
        std::ofstream out(path, std::ios::binary);
        Bundle::Copy(in, out, size);
        out.close();
        if (!out) throw std::runtime_error("Could not extract game data");
    }
}
}
