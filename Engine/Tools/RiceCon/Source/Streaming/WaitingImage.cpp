#include "WaitingImage.h"
#include <algorithm>
#include <fstream>
#include <memory>
#include <stdexcept>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "Engine/Dependencies/stb_image/stb_image.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
extern "C" {
#include <libswscale/swscale.h>
}
namespace RiceController::Streaming {
RgbaImage LoadWaitingImageUtf8(std::string_view path, int width, int height) {
    return LoadWaitingImage(std::filesystem::path(std::u8string(path.begin(), path.end())), width, height);
}
RgbaImage LoadWaitingImage(const std::filesystem::path& path, int width, int height) {
    if (path.empty()) throw std::invalid_argument("Select a waiting image before replacing the game");
    if (width < 16 || height < 16 || width > 7680 || height > 4320)
        throw std::invalid_argument("Invalid waiting frame size");
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open waiting image; existing source preserved");
    const auto size = input.tellg();
    if (size <= 0 || size > 32 * 1024 * 1024) throw std::runtime_error("Waiting image exceeds 32 MiB");
    std::vector<stbi_uc> encoded(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(encoded.data()), size)) throw std::runtime_error("Cannot read waiting image");
    int sourceWidth = 0, sourceHeight = 0, channels = 0;
    if (!stbi_info_from_memory(encoded.data(), static_cast<int>(encoded.size()), &sourceWidth, &sourceHeight, &channels) ||
        sourceWidth <= 0 || sourceHeight <= 0 || sourceWidth > 16384 || sourceHeight > 16384 ||
        static_cast<std::int64_t>(sourceWidth) * sourceHeight > 32 * 1024 * 1024)
        throw std::runtime_error("Invalid or oversized waiting image (PNG/JPEG/BMP)");
    auto* decoded = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &sourceWidth, &sourceHeight, &channels, 4);
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> image(decoded, stbi_image_free);
    if (!image) throw std::runtime_error("Cannot decode waiting image; existing source preserved");
    const double scale = std::min(static_cast<double>(width) / sourceWidth, static_cast<double>(height) / sourceHeight);
    const int fittedWidth = std::max(1, static_cast<int>(sourceWidth * scale));
    const int fittedHeight = std::max(1, static_cast<int>(sourceHeight * scale));
    std::vector<std::uint8_t> fitted(static_cast<std::size_t>(fittedWidth) * fittedHeight * 4);
    std::unique_ptr<SwsContext, decltype(&sws_freeContext)> scaler(sws_getContext(sourceWidth, sourceHeight, AV_PIX_FMT_RGBA,
        fittedWidth, fittedHeight, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
    if (!scaler) throw std::runtime_error("Cannot resize waiting image");
    const std::uint8_t* source[] = {decoded, nullptr, nullptr, nullptr};
    const int sourceStride[] = {sourceWidth * 4, 0, 0, 0};
    std::uint8_t* target[] = {fitted.data(), nullptr, nullptr, nullptr};
    const int targetStride[] = {fittedWidth * 4, 0, 0, 0};
    if (sws_scale(scaler.get(), source, sourceStride, 0, sourceHeight, target, targetStride) != fittedHeight)
        throw std::runtime_error("Cannot resize waiting image");
    RgbaImage result{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    for (std::size_t i = 3; i < result.pixels.size(); i += 4) result.pixels[i] = 255;
    const int left = (width - fittedWidth) / 2, top = (height - fittedHeight) / 2;
    for (int y = 0; y < fittedHeight; ++y) for (int x = 0; x < fittedWidth; ++x) {
        const auto src = (static_cast<std::size_t>(y) * fittedWidth + x) * 4;
        const auto dst = (static_cast<std::size_t>(top + y) * width + left + x) * 4;
        for (int c = 0; c < 3; ++c)
            result.pixels[dst + c] = static_cast<std::uint8_t>((static_cast<unsigned>(fitted[src + c]) * fitted[src + 3] + 127) / 255);
    }
    return result;
}
}
