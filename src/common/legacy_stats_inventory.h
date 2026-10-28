#pragma once
#include "cloud_provider.h"
#include <charconv>
#include <chrono>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace LegacyStatsInventory {
struct Inventory {
    std::unordered_set<uint32_t> stats, playtime;
};
inline bool ParseId(std::string_view text, uint32_t& id) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
inline Inventory Classify(uint32_t accountId, const std::vector<ICloudProvider::FileInfo>& files) {
    Inventory result;
    const auto account = std::to_string(accountId) + "/";
    const auto playtime = account + "0/blobs/Playtime/";
    for (const auto& file : files) {
        if (file.isDeleteMarker || file.path.compare(0,account.size(),account) != 0) continue;
        const auto rest = std::string_view(file.path).substr(account.size());
        const auto slash = rest.find('/');
        uint32_t id = 0;
        if (slash != std::string_view::npos && rest.substr(slash + 1) == "stats.json"
            && ParseId(rest.substr(0,slash),id) && id) result.stats.insert(id);
        if (file.path.compare(0,playtime.size(),playtime) == 0) {
            const auto name = std::string_view(file.path).substr(playtime.size());
            if (name.size() > 4 && name.substr(name.size()-4) == ".bin"
                && ParseId(name.substr(0,name.size()-4),id) && id) result.playtime.insert(id);
        }
    }
    return result;
}

// Frozen legacy formats need one complete metadata listing per account/session.
// Failed listings are never cached as empty; backoff prevents repeated seeding
// from turning a provider outage into one network request per registered app.
class Cache {
    struct Entry {
        Inventory inventory;
        bool complete = false;
        std::chrono::steady_clock::time_point retryAfter{};
    };
    std::mutex mutex;
    std::unordered_map<uint32_t, Entry> accounts;
public:
    void Clear() { std::lock_guard<std::mutex> lock(mutex); accounts.clear(); }
    bool Read(ICloudProvider& provider, uint32_t account, Inventory& out,
              std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        out = {};
        if (!account) return false;
        std::lock_guard<std::mutex> lock(mutex);
        auto& entry = accounts[account];
        if (!entry.complete) {
            if (now < entry.retryAfter) return false;
            std::vector<ICloudProvider::FileInfo> files;
            if (!provider.ListLegacyMetadata(account,files)) {
                entry.retryAfter = now + std::chrono::minutes(5);
                return false;
            }
            entry.inventory = Classify(account,files);
            entry.complete = true;
        }
        out = entry.inventory;
        return true;
    }
};
}
