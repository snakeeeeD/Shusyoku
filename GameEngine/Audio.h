#pragma once
#include <string>

class Audio {
public:
    static bool Init();
    static void Shutdown();
    static void PlaySE(const std::string& path);
    static void PlayBGM(const std::string& path, float volume = 0.5f);   // ƒ‹[ƒvE“¯‚¶‹È‚È‚çŒp‘±
    static void StopBGM();
    static void SetMasterVolume(float v);
};