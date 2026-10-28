#pragma once
#include "autocloud_util.h"

// Only serve roots belonging to the current platform. Listing a Windows save a
// second time under an Android rule corrupts Steam's root choice and file quota.
namespace CloudServingRules {
inline bool ShouldCacheChangelist(uint64_t changeNumber, bool onlyDelta, size_t fileCount) {
    // An empty protective delta (e.g. interrupted upload) is not a served full
    // inventory. Caching it pins Steam to an empty response after the pending
    // upload is cleared, so it never receives the corrected Windows root.
    return (changeNumber > 0 || fileCount > 0) && (!onlyDelta || fileCount > 0);
}
struct Rule {
    std::string token, cloudPath, pattern;
    bool recursive = false;
};
inline std::vector<Rule> ForCurrentPlatform(const std::vector<AutoCloudUtil::AutoCloudRuleNative>& rules) {
    std::vector<Rule> result;
    for (const auto& rule : rules) {
        if (!AutoCloudUtil::AutoCloudRuleMatchesCurrentPlatform(rule.platforms)) continue;
        const auto& root = rule.cloudRoot.empty() ? rule.root : rule.cloudRoot;
        if (root.empty()) continue;
        Rule serving;
        serving.token = "%" + root + "%";
        serving.cloudPath = rule.path;
        for (auto& c : serving.cloudPath) if (c == '\\') c = '/';
        while (!serving.cloudPath.empty() && serving.cloudPath.front() == '/') serving.cloudPath.erase(0, 1);
        while (!serving.cloudPath.empty() && serving.cloudPath.back() == '/') serving.cloudPath.pop_back();
        serving.pattern = rule.pattern.empty() ? "*" : rule.pattern;
        serving.recursive = rule.recursive;
        result.push_back(std::move(serving));
    }
    return result;
}
inline std::vector<std::string> RootsForFile(const std::vector<Rule>& rules, const std::string& filename) {
    std::vector<std::string> result;
    for (const auto& rule : rules) {
        std::string relative;
        if (rule.cloudPath.empty()) relative = filename;
        else {
            const auto prefix = rule.cloudPath + "/";
            if (filename.size() <= prefix.size() || AutoCloudUtil::ToLowerAscii(filename.substr(0, prefix.size())) != AutoCloudUtil::ToLowerAscii(prefix)) continue;
            relative = filename.substr(prefix.size());
        }
        if (!rule.recursive && relative.find('/') != std::string::npos) continue;
        const auto slash = relative.find_last_of('/');
        const auto leaf = slash == std::string::npos ? relative : relative.substr(slash + 1);
        const auto& target = rule.pattern.find('/') == std::string::npos ? leaf : relative;
        if (!AutoCloudUtil::WildcardMatchInsensitive(rule.pattern, target)) continue;
        if (std::find(result.begin(), result.end(), rule.token) == result.end()) result.push_back(rule.token);
    }
    return result;
}
}
