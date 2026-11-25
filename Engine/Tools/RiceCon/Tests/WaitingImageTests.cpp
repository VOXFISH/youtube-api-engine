#include "Streaming/WaitingImage.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace RiceController::Streaming;
int main(int argc, char** argv) {
    try {
        const std::filesystem::path directory = argc > 1 ? argv[1] : "TestData/WaitingImage";
        std::filesystem::create_directories(directory);
        const auto path = directory / L"대기.bmp";
        std::vector<unsigned char> bmp(62);
        bmp[0] = 'B'; bmp[1] = 'M'; bmp[2] = 62; bmp[10] = 54; bmp[14] = 40;
        bmp[18] = 2; bmp[22] = 1; bmp[26] = 1; bmp[28] = 24; bmp[34] = 8;
        bmp[56] = 255; bmp[58] = 255;
        { std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(bmp.data()), bmp.size()); }
        const auto utf8Path = path.u8string();
        auto result = LoadWaitingImageUtf8(std::string(utf8Path.begin(), utf8Path.end()), 16, 16);
        if (result.pixels.size() != 16 * 16 * 4 || result.pixels[0] != 0 || result.pixels[3] != 255 ||
            result.pixels[(4 * 16) * 4] < 200 || result.pixels[(11 * 16 + 15) * 4 + 1] < 200 ||
            result.pixels[(12 * 16) * 4] != 0)
            throw std::runtime_error("Image fit/black bars/RGBA mismatch");
        bool rejected = false;
        try { LoadWaitingImage(directory / "missing.png", 16, 16); } catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Missing waiting image must reject source replacement");
        { std::ofstream file(path, std::ios::binary); file << "invalid"; }
        rejected = false;
        try { LoadWaitingImage(path, 16, 16); } catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Malformed waiting image accepted");
        std::cout << "Waiting image tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
