#pragma once
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace Rice {
inline std::string NewAssetID() {
    std::random_device random;
    std::ostringstream id;
    id << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) id << std::setw(8) << random();
    return id.str();
}
}
