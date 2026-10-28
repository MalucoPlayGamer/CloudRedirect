// DolinTools adaptation, based on CloudRedirect (MIT, Selectively11).
#pragma once
// Caller includes its platform/common FileUtil facade before this header.
#include "json.h"
#include <filesystem>
#include <fstream>
#include <regex>
#include <algorithm>

inline std::vector<std::string> DolinLuaDirectories(const std::string& steam) {
    std::vector<std::string> result;
    auto add = [&](std::string path) {
        if (path.empty()) return;
        std::replace(path.begin(), path.end(), '/', '\\');
        auto p = FileUtil::Utf8ToPath(path);
        if (!p.is_absolute()) p = FileUtil::Utf8ToPath(steam) / p;
        path = FileUtil::PathToUtf8(p.lexically_normal());
        if (path.back() != '\\') path += '\\';
        if (std::find(result.begin(), result.end(), path) == result.end()) result.push_back(path);
    };
    // Read the active loader's TOML rather than assuming config/stplug-in.
    for (auto relative : {"greentools.toml", "greentools/greentools.toml", "opensteamtool.toml"}) {
        std::ifstream input(FileUtil::Utf8ToPath(steam) / FileUtil::Utf8ToPath(relative));
        if (!input) continue;
        bool lua = false, paths = false;
        std::string line;
        const std::regex strings(R"cr(("(?:[^"\\]|\\.)*"|'[^']*'))cr");
        while (std::getline(input, line)) {
            // Ignore comments and recognize array delimiters only outside quoted paths.
            char quote = 0; bool escaped = false; size_t closing = std::string::npos;
            for (size_t i = 0; i < line.size(); ++i) {
                const char c = line[i];
                if (escaped) { escaped = false; continue; }
                if (quote == '"' && c == '\\') { escaped = true; continue; }
                if (quote) { if (c == quote) quote = 0; continue; }
                if (c == '"' || c == '\'') quote = c;
                else if (c == '#') { line.resize(i); break; }
                else if (c == ']') closing = i;
            }
            auto first = line.find_first_not_of(" \t\r");
            if (first == std::string::npos || line[first] == '#') continue;
            if (!paths && line[first] == '[') { lua = std::regex_match(line, std::regex(R"(^\s*\[\s*lua\s*\]\s*$)")); continue; }
            if (!lua) continue;
            if (!paths) {
                if (!std::regex_search(line, std::regex(R"(^\s*paths\s*=\s*\[)"))) continue;
                line = line.substr(line.find('=') + 1); paths = true;
            }
            for (std::sregex_iterator it(line.begin(), line.end(), strings), end; it != end; ++it) {
                const auto value = (*it)[0].str();
                if (value.front() == '\'') add(value.substr(1, value.size() - 2));
                else { auto parsed = Json::Parse(value); if (parsed.type == Json::Type::String) add(parsed.str()); }
            }
            if (closing != std::string::npos) paths = false;
        }
        if (!result.empty()) break;
    }
    if (result.empty()) { add("config/stplug-in"); add("config/lua"); }
    return result;
}
inline std::string DolinLuaPrimary(const std::string& steam) { return DolinLuaDirectories(steam).front(); }
