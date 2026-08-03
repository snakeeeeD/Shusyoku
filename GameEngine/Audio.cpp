#include "Audio.h"
#include <unordered_map>

#define MINIAUDIO_IMPLEMENTATION
#include "External/miniaudio/miniaudio.h"

static const int SE_VOICES = 6;   // ìØÇ∂SEÇìØéûÇ…âΩå¬Ç‹Ç≈èdÇÀÇÈÇ©
struct SeVoicePool { std::vector<ma_sound*> voices; int next = 0; };
static std::unordered_map<std::string, SeVoicePool> g_seCache;
static std::unordered_map<std::string, float> g_seVol;
static std::unordered_map<std::string, float> g_bgmVolMap;   // ã»Ç≤Ç∆ÇÃî{ó¶

static ma_engine g_engine;
static ma_sound  g_bgm;
static bool g_inited = false, g_bgmActive = false;
static std::string g_bgmPath;
static float g_bgmVol = 0.5f;

bool Audio::Init()
{
    if (ma_engine_init(NULL, &g_engine) != MA_SUCCESS) return false;
    g_inited = true; return true;
}

void Audio::Shutdown()
{
    if (!g_inited) return;
    for (auto& kv : g_seCache)
        for (auto* s : kv.second.voices) { ma_sound_uninit(s); delete s; }
    g_seCache.clear();
    if (g_bgmActive) ma_sound_uninit(&g_bgm);
    ma_engine_uninit(&g_engine);
    g_inited = false;
}

void Audio::PlaySE(const std::string& path)
{
    if (!g_inited) return;
    auto& pool = g_seCache[path];
    if (pool.voices.empty())
    {
        for (int i = 0; i < SE_VOICES; i++)
        {
            ma_sound* s = new ma_sound();
            if (ma_sound_init_from_file(&g_engine, path.c_str(), MA_SOUND_FLAG_DECODE, NULL, NULL, s) != MA_SUCCESS) { delete s; break; }
            pool.voices.push_back(s);
        }
        if (pool.voices.empty()) return;
    }
    ma_sound* s = pool.voices[pool.next];
    pool.next = (pool.next + 1) % (int)pool.voices.size();

    float v = g_seVol.count(path) ? g_seVol[path] : 1.0f;
    ma_sound_set_volume(s, v);
    ma_sound_seek_to_pcm_frame(s, 0);
    ma_sound_start(s);
}

void Audio::PlayBGM(const std::string& path, float volume)
{
    if (!g_inited) return;
    if (g_bgmActive && g_bgmPath == path) return;                        // ìØÇ∂BGMÇ»ÇÁåpë±
    if (g_bgmActive) { ma_sound_uninit(&g_bgm); g_bgmActive = false; }
    if (ma_sound_init_from_file(&g_engine, path.c_str(), MA_SOUND_FLAG_STREAM, NULL, NULL, &g_bgm) != MA_SUCCESS) return;
    float tv = g_bgmVolMap.count(path) ? g_bgmVolMap[path] : 1.0f;
    ma_sound_set_looping(&g_bgm, MA_TRUE);
    ma_sound_set_volume(&g_bgm, g_bgmVol * tv);
    ma_sound_start(&g_bgm);
    g_bgmActive = true; g_bgmPath = path;
}
void Audio::StopBGM()
{
    if (g_bgmActive) { ma_sound_uninit(&g_bgm); g_bgmActive = false; g_bgmPath.clear(); }
}

void Audio::SetMasterVolume(float v) { if (g_inited) ma_engine_set_volume(&g_engine, v); }
void Audio::SetBgmVolume(float v)
{
    g_bgmVol = v;
    if (g_bgmActive)
    {
        float tv = g_bgmVolMap.count(g_bgmPath) ? g_bgmVolMap[g_bgmPath] : 1.0f;
        ma_sound_set_volume(&g_bgm, v * tv);
    }
}
void Audio::SetBgmTrackVolume(const std::string& path, float v) { g_bgmVolMap[path] = v; }
void Audio::SetSeVolume(const std::string& path, float v) { g_seVol[path] = v; }