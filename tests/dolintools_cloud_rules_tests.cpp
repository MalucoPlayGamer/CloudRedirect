#include "cloud_serving_rules.h"
#include "cloud_inventory_ack.h"
#include "protobuf_wrapper_layout.h"
#include "legacy_stats_inventory.h"
#include <iostream>
#include <fstream>
#include <iterator>
#include <stdexcept>
using AutoCloudUtil::AutoCloudRuleNative;
class LegacyProviderFixture final : public ICloudProvider {
public:
    int listings = 0, downloads = 0;
    bool complete = true, available = true;
    std::vector<FileInfo> files;
    const char* Name() const override { return "fixture"; }
    bool Init(const std::string&) override { return true; }
    void Shutdown() override {}
    bool IsAuthenticated() const override { return true; }
    bool Upload(const std::string&,const uint8_t*,size_t) override { return false; }
    bool Remove(const std::string&) override { return false; }
    ExistsStatus CheckExists(const std::string&) override { return ExistsStatus::Missing; }
    bool Download(const std::string&,std::vector<uint8_t>&) override { ++downloads; return false; }
    std::vector<FileInfo> List(const std::string&) override { return files; }
    bool ListChecked(const std::string&,std::vector<FileInfo>& out,bool* all) override {
        ++listings; out=files; if(all)*all=complete; return available;
    }
};
int main(int argc, char** argv) {
    int checks = 0;
    auto check = [&](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); ++checks; std::cout << "PASS " << message << '\n'; };
    LegacyProviderFixture provider;
    provider.files = {{"123/400/stats.json"},{"123/0/stats.json"},{"999/730/stats.json"},
        {"123/0/blobs/Playtime/440.bin"},{"123/400/blobs/stats.json"},
        {"123/400suffix/stats.json"},{"123/0/blobs/Playtime/../730.bin"}};
    LegacyStatsInventory::Cache discovery;
    LegacyStatsInventory::Inventory legacyInventory;
    check(discovery.Read(provider,123,legacyInventory) && legacyInventory.stats==std::unordered_set<uint32_t>{400}
        && legacyInventory.playtime==std::unordered_set<uint32_t>{440}, "legacy discovery isolates the account and excludes current stats, save blobs and invalid paths");
    for(uint32_t app=1;app<=1897;++app) {
        discovery.Read(provider,123,legacyInventory);
        if(legacyInventory.stats.count(app)) {std::vector<uint8_t> data;provider.Download("legacy",data);}
    }
    check(provider.listings==1 && provider.downloads==1, "1897 registered apps perform one metadata listing and download only the one existing legacy stats file");
    discovery.Read(provider,999,legacyInventory);
    check(provider.listings==2 && legacyInventory.stats.count(730), "switching accounts gets its own complete inventory");
    discovery.Clear(); provider.files.clear();discovery.Read(provider,123,legacyInventory);discovery.Read(provider,123,legacyInventory);
    check(provider.listings==3 && legacyInventory.stats.empty() && legacyInventory.playtime.empty(), "an empty complete listing is reused without missing-file probes");
    discovery.Clear(); provider.available=false;
    const auto now=std::chrono::steady_clock::now();
    check(!discovery.Read(provider,123,legacyInventory,now), "a failed metadata listing is not accepted as empty");
    for(int i=0;i<1897;++i) discovery.Read(provider,123,legacyInventory,now+std::chrono::seconds(1));
    check(provider.listings==4, "provider failures cannot trigger one retry per AppID");
    provider.available=true;provider.complete=false;
    check(!discovery.Read(provider,123,legacyInventory,now+std::chrono::minutes(6)), "a partial metadata listing is deferred rather than losing legacy data");
    provider.complete=true;provider.files={{"123/400/stats.json"}};
    check(discovery.Read(provider,123,legacyInventory,now+std::chrono::minutes(12)) && legacyInventory.stats.count(400), "metadata discovery recovers after a transient or incomplete listing");
    discovery.Clear();discovery.Read(provider,123,legacyInventory);
    check(provider.listings==7, "reinitializing the cloud provider invalidates its session inventory");
    ProtobufWrapperLayout::Layout layout;
    if (argc == 2) {
        std::ifstream source(argv[1], std::ios::binary);
        std::vector<uint8_t> code((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
        check(!code.empty() && ProtobufWrapperLayout::Detect(code.data(),code.size(),layout)
            && layout.header==48 && layout.body==56, "the installed Steam transport is decoded correctly from its actual machine code without executing it");
    }
    const uint8_t modern[] = {0x49,0x8B,0x40,0x30,0x90,0x4D,0x8B,0x49,0x38,0x4D,0x8B,0x40,0x38};
    const uint8_t legacy[] = {0x49,0x8B,0x40,0x28,0x90,0x4D,0x8B,0x49,0x30,0x4D,0x8B,0x40,0x30};
    check(ProtobufWrapperLayout::Detect(modern,sizeof(modern),layout) && layout.header==48 && layout.body==56, "current Steam transport uses header at 48 and body at 56");
    check(ProtobufWrapperLayout::Detect(legacy,sizeof(legacy),layout) && layout.header==40 && layout.body==48, "legacy Steam transport retains its original header/body layout");
    check(!ProtobufWrapperLayout::Detect(modern,sizeof(modern)-1,layout), "truncated transport code is rejected before installing hooks");
    auto inconsistent = std::vector<uint8_t>(modern,modern+sizeof(modern)); inconsistent.back()=0x30;
    check(!ProtobufWrapperLayout::Detect(inconsistent.data(),inconsistent.size(),layout), "disagreeing request/response offsets cannot select a layout");
    check(!ProtobufWrapperLayout::Detect(modern+4,sizeof(modern)-4,layout), "body offsets require an independently matching header load");
    ProtobufWrapperLayout::Metadata metadata;
    check(ProtobufWrapperLayout::MetadataFor({0x28,0x30},metadata) && metadata.connection==0x1c && metadata.message==0x20, "legacy message identity offsets remain compatible");
    check(ProtobufWrapperLayout::MetadataFor({0x30,0x38},metadata) && metadata.connection==0x24 && metadata.message==0x28, "current message identity offsets follow the validated wrapper layout");
    check(!ProtobufWrapperLayout::MetadataFor({0x38,0x40},metadata) && !metadata.connection && !metadata.message, "unknown metadata layouts cannot enable message injection");
    check(!ProtobufWrapperLayout::MetadataFor({0x28,0x38},metadata), "inconsistent header/body metadata layouts are rejected");
    const std::string cache = "\"3669870\"\n{\n\"ChangeNumber\" \"2\"\n\"slot-0_auto-6-player\"\n{\n\"remotetime\" \"100\"\n\"localtime\" \"200\"\n\"syncstate\" \"2\"\n}\n}\n";
    const std::vector<CloudInventoryAck::File> inventory = {{"slot-0_auto-6-player", 100}};
    check(CloudInventoryAck::CanReuse(cache,3669870,2,2,inventory), "an acknowledged cloud baseline permits local changes awaiting upload");
    check(!CloudInventoryAck::CanReuse(cache,3669870,1,2,inventory), "a client behind the served generation never receives a cached empty delta");
    check(!CloudInventoryAck::CanReuse(cache,3669870,3,2,inventory), "a client ahead of cloud retains the interrupted-upload protection path");
    auto pending = cache; pending.replace(pending.find("\"100\""),5,"\"0\"");
    check(!CloudInventoryAck::CanReuse(pending,3669870,2,2,inventory), "Control Resonant downloads with remotetime zero must receive the inventory again");
    check(!CloudInventoryAck::CanReuse("",3669870,2,2,inventory), "a missing native cache never acknowledges delivery");
    check(!CloudInventoryAck::CanReuse(cache.substr(0,cache.size()-2),3669870,2,2,inventory), "an incompletely written native cache never acknowledges delivery");
    check(!CloudInventoryAck::CanReuse(cache,3669870,2,2,{{"missing-save",100}}), "a partially downloaded inventory cannot suppress missing files");
    check(!CloudInventoryAck::CanReuse(cache,3669870,2,2,{{"slot-0_auto-6-player",101}}), "different remote baselines retain native conflict resolution");
    check(!CloudInventoryAck::CanReuse(cache,400,2,2,inventory), "another app's acknowledgement cannot leak across games");
    check(!CloudServingRules::ShouldCacheChangelist(1,true,0), "a protective empty delta from an interrupted upload is never cached as a full inventory");
    check(CloudServingRules::ShouldCacheChangelist(1,false,1), "after interruption clears, the corrected Windows inventory can be served and cached");
    check(CloudServingRules::ShouldCacheChangelist(1,true,1), "a real nonempty manifest delta retains normal caching");
    check(CloudServingRules::ShouldCacheChangelist(2,false,0), "a confirmed empty cloud generation can still be cached");
    check(!CloudServingRules::ShouldCacheChangelist(0,false,0), "a placeholder at generation zero does not pin an unknown cloud state");
    AutoCloudRuleNative windows, android;
    windows.root = "WinAppDataLocalLow"; windows.path = "MoonMonsterStudios/SpaceControl";
    windows.pattern = "*.spacecontrol"; windows.platforms = AutoCloudUtil::ParseAutoCloudPlatformMask("Windows");
    android = windows; android.root = "AndroidExternalData"; android.platforms = AutoCloudUtil::ParseAutoCloudPlatformMask("Android");
    auto rules = CloudServingRules::ForCurrentPlatform({windows, android});
    auto roots = CloudServingRules::RootsForFile(rules, "MoonMonsterStudios/SpaceControl/save.spacecontrol");
    check(roots.size() == 1 && roots[0] == "%WinAppDataLocalLow%", "Space Control is listed once under Windows, never under Android");
    check(roots.size() <= 1, "one physical save stays within the game's one-file quota without KV quota injection");
    check(CloudServingRules::ForCurrentPlatform({android}).empty(), "a foreign-platform rule cannot be served just because its filename matches");
    check(CloudServingRules::RootsForFile(rules,"MoonMonsterStudios/SpaceControl/save.txt").empty(), "the game's save extension is respected");
    check(CloudServingRules::RootsForFile(rules,"MoonMonsterStudios/SpaceControl/sub/save.spacecontrol").empty(), "non-recursive rules exclude child folders");
    windows.recursive = true; windows.path = "\\MoonMonsterStudios\\SpaceControl\\";
    check(CloudServingRules::RootsForFile(CloudServingRules::ForCurrentPlatform({windows}),"moonmonsterstudios/spacecontrol/sub/save.spacecontrol").size()==1, "recursive, normalized and case-insensitive Windows paths continue to work");
    auto duplicate = windows;
    check(CloudServingRules::RootsForFile(CloudServingRules::ForCurrentPlatform({windows,duplicate}),"MoonMonsterStudios/SpaceControl/save.spacecontrol").size()==1, "duplicate rules cannot duplicate a root entry");
    duplicate.cloudRoot = "WinMyDocuments";
    check(CloudServingRules::RootsForFile(CloudServingRules::ForCurrentPlatform({windows,duplicate}),"MoonMonsterStudios/SpaceControl/save.spacecontrol").size()==2, "legitimate multiple roots on the same platform are preserved");
    std::cout << checks << " DolinTools CloudRedirect rule checks passed.\n";
}
