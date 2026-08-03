#pragma once
#include <string>

class Audio {
public:
    static bool Init();
    static void Shutdown();
    static void PlaySE(const std::string& path);
    static void PlayBGM(const std::string& path, float volume = 0.5f);   // ループ・同じ曲なら継続
    static void StopBGM();
    static void SetMasterVolume(float v);   // 全体
    static void SetBgmVolume(float v);      // BGMのみ（相対）
    static void SetBgmTrackVolume(const std::string& path, float v);
    static void SetSeVolume(const std::string& path, float v);
};