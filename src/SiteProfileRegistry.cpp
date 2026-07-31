#include "SiteProfileRegistry.h"
#include <fstream>

SiteProfileRegistry::SiteProfileRegistry(std::filesystem::path profilesDir)
    : m_profilesDir(std::move(profilesDir)) {
    if (!std::filesystem::exists(m_profilesDir)) {
        std::error_code ec;
        std::filesystem::create_directories(m_profilesDir, ec);
    }
    reload();
}

void SiteProfileRegistry::reload() {
    m_profiles.clear();
    if (!std::filesystem::exists(m_profilesDir)) return;

    for (const auto& entry : std::filesystem::directory_iterator(m_profilesDir)) {
        if (entry.path().extension() == ".json") {
            try {
                std::ifstream f(entry.path());
                json j = json::parse(f);

                SiteProfile profile;
                profile.urlMatch = j.value("url_match", "");
                if (j.contains("extract_items") && j["extract_items"].is_array()) {
                    for (const auto& itemJ : j["extract_items"]) {
                        SiteProfileItem item;
                        item.name = itemJ.value("name", "");
                        item.selector = itemJ.value("selector", "");
                        item.extract = itemJ.value("extract", "text");
                        item.isArray = itemJ.value("is_array", false);
                        profile.items.push_back(item);
                    }
                }
                m_profiles.push_back(std::move(profile));
                WINBOT_INFO("Loaded site profile: {} (match: {})", entry.path().filename().string(), profile.urlMatch);
            } catch (const std::exception& e) {
                WINBOT_ERROR("Failed to load profile {}: {}", entry.path().string(), e.what());
            }
        }
    }
}

const SiteProfile* SiteProfileRegistry::match(std::string_view url) const {
    for (const auto& profile : m_profiles) {
        if (!profile.urlMatch.empty() && url.find(profile.urlMatch) != std::string_view::npos) {
            return &profile;
        }
    }
    return nullptr;
}

ToolResult SiteProfileRegistry::saveProfile(std::string_view name, const json& profileJson) {
    if (!profileJson.contains("url_match")) {
        return err("Profile missing 'url_match'");
    }
    
    std::string safeName(name);
    for (char& c : safeName) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    if (safeName.empty()) safeName = "unnamed_profile";

    std::filesystem::path p = m_profilesDir / (safeName + ".json");
    try {
        std::ofstream f(p);
        f << profileJson.dump(4);
    } catch (const std::exception& e) {
        return err(std::format("Failed to save profile: {}", e.what()));
    }

    reload();
    return ok(std::format("Profile '{}' saved to {}", safeName, p.string()));
}
