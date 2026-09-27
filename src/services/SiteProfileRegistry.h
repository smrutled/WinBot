#ifndef WINBOT_SITEPROFILEREGISTRY_H
#define WINBOT_SITEPROFILEREGISTRY_H

#include "Common.h"
#include <filesystem>
#include <vector>

struct SiteProfileItem {
    std::string name;
    std::string selector;
    std::string extract; // "text", "html", or attribute name like "href"
    bool isArray{false};
};

struct SiteProfile {
    std::string urlMatch;
    std::vector<SiteProfileItem> items;
};

class SiteProfileRegistry {
public:
    SiteProfileRegistry(std::filesystem::path profilesDir);

    // Reload all profiles from disk
    void reload();

    // Find the first profile that matches the URL (using substring match)
    const SiteProfile* match(std::string_view url) const;

    // Save a new profile to disk and reload
    ToolResult saveProfile(std::string_view name, const json& profileJson);

private:
    std::filesystem::path m_profilesDir;
    std::vector<SiteProfile> m_profiles;
};

#endif // WINBOT_SITEPROFILEREGISTRY_H
