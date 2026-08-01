#include "Audio.h"
#define MINIAUDIO_IMPLEMENTATION
#include "External/miniaudio/miniaudio.h"

static ma_engine g_engine;
static ma_sound  g_bgm;
static bool g_inited = false, g_bgmActive = false;
static std::string g_bgmPath;

bool Audio::Init()
{
    if (ma_engine_init(NULL, &g_engine) != MA_SUCCESS) return false;
    g_inited = true; return true;
}
void Audio::Shutdown()
{
    if (!g_inited) return;
    if (g_bgmActive) ma_sound_uninit(&g_bgm);
    ma_engine_uninit(&g_engine);
    g_inited = false;
}
void Audio::PlaySE(const std::string& path)
{
    if (g_inited) ma_engine_play_sound(&g_engine, path.c_str(), NULL);   // Œ‚‚¿‚Á‚Ï‚È‚µ
}
void Audio::PlayBGM(const std::string& path, float volume)
{
    if (!g_inited) return;
    if (g_bgmActive && g_bgmPath == path) return;                        // “¯‚¶BGM‚È‚çŒp‘±
    if (g_bgmActive) { ma_sound_uninit(&g_bgm); g_bgmActive = false; }
    if (ma_sound_init_from_file(&g_engine, path.c_str(), MA_SOUND_FLAG_STREAM, NULL, NULL, &g_bgm) != MA_SUCCESS) return;
    ma_sound_set_looping(&g_bgm, MA_TRUE);
    ma_sound_set_volume(&g_bgm, volume);
    ma_sound_start(&g_bgm);
    g_bgmActive = true; g_bgmPath = path;
}
void Audio::StopBGM()
{
    if (g_bgmActive) { ma_sound_uninit(&g_bgm); g_bgmActive = false; g_bgmPath.clear(); }
}
void Audio::SetMasterVolume(float v) { if (g_inited) ma_engine_set_volume(&g_engine, v); }