#pragma once

enum class DisplayMode { Windowed, Borderless };

struct SettingsData
{
    DisplayMode displayMode = DisplayMode::Borderless;
    float bgmVolume = 0.6f;    // 0..1
    float seVolume = 0.8f;    // 0..1
    bool  screenShake = true;
};

class Settings
{
public:
    static void Load();
    static void Save();
    static SettingsData& Get() { return s_data; }

private:
    static SettingsData s_data;
    static const char* PATH;
};

// 実行時に表示モード切替（ウィンドウ適用＋保存）。定義は main.cpp
void SetDisplayMode(DisplayMode mode);