#pragma once
#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace RiceController::Streaming {
// YouTube Studio supplies a server URL and a stream key separately. Keeping the
// allowed server separate also keeps a copied key out of settings and messages.
inline std::string YouTubeIngestUrl(std::string_view server, std::string_view key) {
    constexpr std::string_view scheme = "rtmps://";
    if (!server.starts_with(scheme) || server.size() > 512)
        throw std::invalid_argument("Enter the YouTube Studio RTMPS server URL");
    server.remove_prefix(scheme.size());
    auto split = server.find('/');
    if (split == server.npos) throw std::invalid_argument("The YouTube server URL needs its stream path");
    auto authority = server.substr(0, split);
    auto host = authority;
    if (host.ends_with(":443")) host.remove_suffix(4);
    constexpr std::string_view suffix = ".rtmps.youtube.com";
    if (!host.ends_with(suffix))
        throw std::invalid_argument("Use the RTMPS ingestion server shown in YouTube Studio");
    auto prefix = host.substr(0, host.size() - suffix.size());
    auto alnum = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    if (prefix.empty() || !std::all_of(prefix.begin(), prefix.end(), [&](char c){return alnum(c) || c=='-';}))
        throw std::invalid_argument("Invalid YouTube ingestion server name");
    auto path = server.substr(split);
    auto query = path.find('?');
    auto options = query == path.npos ? std::string_view{} : path.substr(query);
    path = path.substr(0, query);
    if (path.ends_with('/')) path.remove_suffix(1);
    if (path != "/live2" || (!options.empty() && options != "?backup=1"))
        throw std::invalid_argument("Use the server URL without the stream key");
    if (key.empty() || key.size() > 512 ||
        !std::all_of(key.begin(), key.end(), [&](char c){return alnum(c) || c=='-' || c=='_';}))
        throw std::invalid_argument("Enter a valid YouTube stream key");
    return std::string(scheme) + std::string(authority) + std::string(path) + "/" + std::string(key) + std::string(options);
}
}
