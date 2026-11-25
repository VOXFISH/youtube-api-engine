#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
#include <string_view>
namespace RiceController::Streaming {
struct RgbaImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
};
// Prepare before changing any live source. Keeps aspect ratio, composites alpha
// over black, and returns immutable-by-convention top-down RGBA for reuse.
RgbaImage LoadWaitingImage(const std::filesystem::path& path, int width, int height);
RgbaImage LoadWaitingImageUtf8(std::string_view path, int width, int height);
}
