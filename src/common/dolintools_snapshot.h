// DolinTools: complete, verified cloud recovery copies. Included inside CloudRedirectCli.
static bool DolinSafeRelative(const std::string& value) {
    if (value.empty() || value.front() == '/' || value.back() == '/' || value.find('\\') != std::string::npos
        || value.find(':') != std::string::npos || value.find('\0') != std::string::npos) return false;
    std::istringstream parts(value); std::string part;
    while (std::getline(parts, part, '/')) if (part.empty() || part == "." || part == "..") return false;
    return true;
}
static std::string DolinSha(const std::vector<uint8_t>& bytes) {
    auto hash = FileUtil::SHA1(bytes.data(), bytes.size());
    if (hash.size() != 20) throw std::runtime_error("Could not hash a recovery file.");
    const char* hex = "0123456789abcdef"; std::string value;
    for (auto byte : hash) { value += hex[byte >> 4]; value += hex[byte & 15]; }
    return value;
}
static std::filesystem::path DolinSnapshotFile(const std::filesystem::path& root, const std::string& relative) {
    if (!DolinSafeRelative(relative)) throw std::runtime_error("Unsafe recovery path.");
    auto result = root / FileUtil::Utf8ToPath(relative);
    if (!FileUtil::IsPathWithin(FileUtil::PathToUtf8(root), FileUtil::PathToUtf8(result))) throw std::runtime_error("Unsafe recovery folder.");
    auto current = root;
    if (FileUtil::IsPathRedirectingReparsePoint(FileUtil::PathToUtf8(current))) throw std::runtime_error("Linked recovery folder.");
    for (const auto& component : FileUtil::Utf8ToPath(relative)) {
        current /= component;
        if (FileUtil::IsPathRedirectingReparsePoint(FileUtil::PathToUtf8(current))) throw std::runtime_error("Linked recovery path.");
    }
    return result;
}
static std::vector<uint8_t> DolinRead(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("A recovery file is missing or unreadable.");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}
static std::string CmdDolinSnapshot(const std::string& provider, const std::string& account,
    const std::string& app, const std::string& directory, bool restore, bool erase) {
    auto prov = CreateCloudProvider(provider);
    if (!prov || !prov->Init(GetTokenPath(provider)) || !prov->IsAuthenticated()) return JsonError("Cloud provider is not authenticated.");
    struct Shutdown { ICloudProvider* p; ~Shutdown() { p->Shutdown(); } } shutdown { prov.get() };
    try {
        if (account.empty() || app.empty() || account.find_first_not_of("0123456789") != std::string::npos
            || app.find_first_not_of("0123456789") != std::string::npos) return JsonError("Invalid account or AppID.");
        auto root = std::filesystem::absolute(FileUtil::Utf8ToPath(directory));
        const std::string prefix = account + "/" + app + "/";
        const auto journalPath = DolinSnapshotFile(root, "snapshot.json");
        if (restore) {
            auto raw = DolinRead(journalPath);
            auto journal = Json::Parse(std::string(raw.begin(), raw.end()));
            if (journal["version"].integer() != 1 || journal["account"].str() != account || journal["app"].str() != app
                || journal["files"].type != Json::Type::Array) return JsonError("Invalid cloud recovery journal.");
            auto entries = journal["files"].arrVal;
            std::unordered_set<std::string> unique;
            // Preflight every file before uploading anything. Restore inventory metadata last.
            for (const auto& entry : entries) {
                const auto path = entry["path"].str();
                if (!DolinSafeRelative(path) || !unique.insert(path).second) return JsonError("Unsafe cloud recovery journal.");
                auto bytes = DolinRead(DolinSnapshotFile(root, entry["file"].str()));
                if (DolinSha(bytes) != entry["sha"].str() || bytes.size() != entry["size"].integer()) return JsonError("A recovery file failed integrity verification.");
            }
            std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                return (a["path"].str().rfind("blobs/", 0) == 0) > (b["path"].str().rfind("blobs/", 0) == 0);
            });
            // Restoring an old generation is a new change, so Steam must see a newer CN.
            int64_t newestCn = 0;
            std::vector<uint8_t> currentState;
            if (prov->Download(prefix + "state.cloudredirect", currentState)) {
                auto state = Json::Parse(std::string(currentState.begin(), currentState.end()));
                if (state.type != Json::Type::Object || state["cn"].type != Json::Type::Number) return JsonError("Invalid current cloud inventory; restore cancelled.");
                newestCn = state["cn"].integer();
            } else if (prov->CheckExists(prefix + "state.cloudredirect") != ICloudProvider::ExistsStatus::Missing)
                return JsonError("Could not verify the current cloud generation; restore cancelled.");
            for (const auto& entry : entries) if (entry["path"].str() == "state.cloudredirect") {
                auto bytes = DolinRead(DolinSnapshotFile(root, entry["file"].str()));
                auto state = Json::Parse(std::string(bytes.begin(), bytes.end()));
                newestCn = std::max(newestCn, state["cn"].integer());
            }
            for (const auto& entry : entries) {
                auto bytes = DolinRead(DolinSnapshotFile(root, entry["file"].str()));
                if (entry["path"].str() == "state.cloudredirect") {
                    auto state = Json::Parse(std::string(bytes.begin(), bytes.end()));
                    state.objVal["cn"] = Json::Number(newestCn + 1); state.objVal["session"] = Json::Object();
                    auto text = Json::Stringify(state); bytes.assign(text.begin(), text.end());
                } else if (entry["path"].str() == "cn.cloudredirect" || entry["path"].str() == "cn.dat") {
                    auto text = std::to_string(newestCn + 1); bytes.assign(text.begin(), text.end());
                }
                if (!prov->Upload(prefix + entry["path"].str(), bytes.data(), bytes.size())) return JsonError("Could not restore a cloud recovery file.");
            }
            return JsonObject({{"success", "true"}, {"files", JsonInt(entries.size())}});
        }
        if (std::filesystem::exists(journalPath)) return JsonError("Recovery destination already exists.");
        std::vector<ICloudProvider::FileInfo> files; bool complete = false;
        if (!prov->ListChecked(prefix, files, &complete) || !complete) return JsonError("Incomplete cloud listing; no files were deleted.");
        Json::Value journal = Json::Object(), entries = Json::Array();
        journal.objVal["version"] = Json::Number(1); journal.objVal["account"] = Json::String(account); journal.objVal["app"] = Json::String(app);
        std::map<std::string, std::string> hashes;
        for (const auto& file : files) {
            if (!file.path.empty() && file.path.back() == '/') continue;
            if (file.path.rfind(prefix, 0) != 0 || !DolinSafeRelative(file.path.substr(prefix.size()))) return JsonError("Unsafe provider path; no files were deleted.");
            std::vector<uint8_t> bytes;
            if (!prov->Download(file.path, bytes) || bytes.size() != file.size) return JsonError("Could not back up every cloud file; no files were deleted.");
            auto relative = "files/" + std::to_string(entries.arrVal.size()) + ".bin";
            auto target = DolinSnapshotFile(root, relative);
            std::filesystem::create_directories(target.parent_path());
            if (!FileUtil::AtomicWriteBinary(FileUtil::PathToUtf8(target), bytes.data(), bytes.size())) return JsonError("Could not write recovery copy; no files were deleted.");
            auto sha = DolinSha(bytes);
            if (DolinSha(DolinRead(target)) != sha) return JsonError("Recovery verification failed; no files were deleted.");
            auto entry = Json::Object(); entry.objVal["path"] = Json::String(file.path.substr(prefix.size()));
            entry.objVal["file"] = Json::String(relative); entry.objVal["size"] = Json::Number(bytes.size()); entry.objVal["sha"] = Json::String(sha);
            entries.arrVal.push_back(entry); hashes[file.path] = sha;
        }
        journal.objVal["files"] = entries;
        std::filesystem::create_directories(root);
        if (!FileUtil::AtomicWriteText(FileUtil::PathToUtf8(journalPath), Json::Stringify(journal))) return JsonError("Could not save recovery journal; no files were deleted.");
        if (erase) {
            std::vector<ICloudProvider::FileInfo> current; complete = false;
            if (!prov->ListChecked(prefix, current, &complete) || !complete) return JsonError("Cloud listing changed; recovery retained and deletion cancelled.");
            size_t count = 0;
            for (const auto& file : current) {
                if (!file.path.empty() && file.path.back() == '/') continue;
                ++count; std::vector<uint8_t> bytes;
                auto expected = hashes.find(file.path);
                if (expected == hashes.end() || !prov->Download(file.path, bytes) || DolinSha(bytes) != expected->second)
                    return JsonError("Cloud saves changed during backup; recovery retained and deletion cancelled.");
            }
            if (count != hashes.size()) return JsonError("Cloud listing changed; recovery retained and deletion cancelled.");
            for (const auto& item : hashes)
                if (!prov->Remove(item.first) && prov->CheckExists(item.first) != ICloudProvider::ExistsStatus::Missing)
                    return JsonError("Some cloud files could not be deleted; the complete recovery copy was retained.");
        }
        return JsonObject({{"success", "true"}, {"files", JsonInt(entries.arrVal.size())}});
    } catch (const std::exception& error) { return JsonError(error.what()); }
}
