#include "Settings.h"
#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

SettingsData Settings::s_data;
const char* Settings::PATH = "Assets/Data/settings.json";

void Settings::Load()
{
    std::ifstream file(PATH);
    if (!file) { Save(); return; }        // –³‚¯‚ê‚ÎŠù’è‚Åì¬
    try {
        json j; file >> j;
        std::string m = j.value("displayMode", "borderless");
        s_data.displayMode = (m == "borderless") ? DisplayMode::Borderless : DisplayMode::Windowed;
        s_data.bgmVolume = j.value("bgmVolume", 0.6f);
        s_data.seVolume = j.value("seVolume", 0.8f);
        s_data.screenShake = j.value("screenShake", true);
    }
    catch (...) { /* ‰ó‚ê‚Ä‚½‚çŠù’è‚Ì‚Ü‚Ü */ }
}

void Settings::Save()
{
    json j;
    j["displayMode"] = (s_data.displayMode == DisplayMode::Borderless) ? "borderless" : "windowed";
    j["bgmVolume"] = s_data.bgmVolume;
    j["seVolume"] = s_data.seVolume;
    j["screenShake"] = s_data.screenShake;
    std::ofstream file(PATH);
    if (file) file << j.dump(2);
}