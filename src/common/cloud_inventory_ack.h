#pragma once
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CloudInventoryAck {
struct File {
    std::string name;
    uint64_t timestamp;
};

inline bool Number(std::string_view text, uint64_t& number) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

// Serving metadata is not an acknowledgement: Steam may fail or cancel the
// download afterwards. Read only Steam's remote baseline, never local SHA/mtime:
// local edits awaiting upload must retain their independent lifecycle.
inline bool CanReuse(const std::string& cache, uint32_t appId,
                     uint64_t clientCN, uint64_t servedCN, const std::vector<File>& files) {
    if (cache.empty() || clientCN != servedCN) return false;
    const auto app = std::to_string(appId);
    std::unordered_map<std::string, uint64_t> remoteTimes;
    uint64_t nativeCN = 0;
    bool haveCN = false;
    size_t at = 0;
    // Native VDF uses quoted keys/values and braces. Parse once so a large save
    // inventory remains linear in the cache size, rather than scanning per file.
    auto next = [&](std::string& value) -> char {
        value.clear();
        while (at < cache.size()) {
            if (cache[at] == ' ' || cache[at] == '\t' || cache[at] == '\r' || cache[at] == '\n') { ++at; continue; }
            if (cache.compare(at, 2, "//") == 0) {
                at = cache.find('\n', at);
                if (at == std::string::npos) { at = cache.size(); break; }
                continue;
            }
            break;
        }
        if (at == cache.size()) return 0;
        const char kind = cache[at++];
        if (kind == '{' || kind == '}') return kind;
        if (kind != '"') return '?';
        while (at < cache.size()) {
            char ch = cache[at++];
            if (ch == '"') return 's';
            if (ch == '\\' && at < cache.size() && (cache[at] == '\\' || cache[at] == '"')) ch = cache[at++];
            value += ch;
        }
        return '?';
    };
    std::vector<std::string> path;
    std::string key, value;
    while (true) {
        const char token = next(key);
        if (!token) break;
        if (token == '}') { if (path.empty()) return false; path.pop_back(); continue; }
        if (token != 's') return false;
        const char kind = next(value);
        if (kind == '{') { path.push_back(key); continue; }
        if (kind != 's') return false;
        if (path.size() == 1 && path[0] == app && key == "ChangeNumber") {
            if (haveCN || !Number(value, nativeCN)) return false;
            haveCN = true;
        } else if (path.size() == 2 && path[0] == app && key == "remotetime") {
            uint64_t timestamp = 0;
            if (!Number(value, timestamp) || !remoteTimes.emplace(path[1], timestamp).second) return false;
        }
    }
    if (!path.empty() || !haveCN || nativeCN != servedCN) return false;
    for (const auto& file : files) {
        const auto found = remoteTimes.find(file.name);
        if (!file.timestamp || found == remoteTimes.end() || found->second != file.timestamp) return false;
    }
    return true;
}
}
