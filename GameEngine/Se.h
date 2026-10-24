#pragma once
#include <string>
#include <chrono>
#include <unordered_map>
#include "Audio.h"

// 効果音の窓口。
//  - エフェクトに付く音（爆発など）は effects.json の "se" に書く → EffectManager::Play が見た目と同時に鳴らす
//  - エフェクトの無い場面の音は、ここに名前を並べてコードから Se::Play(Se::Xxx)
// ファイルは Assets/Sound/se/ に置く
namespace Se
{
    inline std::string Path(const std::string& file) { return "Assets/Sound/se/" + file; }

    inline constexpr const char* PlayerHurt = "player_hurt.wav";   // 自分が攻撃を受けた（ブロックで全部防いだ時は shield）
    inline constexpr const char* Shield     = "shield.mp3";        // ブロック
    inline constexpr const char* TrapSet    = "trap_set.wav";      // 罠を置いた
    inline constexpr const char* Coin       = "coin.wav";          // ショップで購入
    inline constexpr const char* Upgrade    = "upgrade.wav";       // カード強化

    // 同じ音が同じ瞬間に何重にも鳴る（爆発が24個同時など）と割れるので、
    // 同じファイルは minInterval 秒以内の重複を間引く
    inline void Play(const std::string& file, float minInterval = 0.06f)
    {
        if (file.empty()) return;
        using clock = std::chrono::steady_clock;
        static std::unordered_map<std::string, clock::time_point> s_last;
        auto now = clock::now();
        auto it = s_last.find(file);
        if (it != s_last.end() && std::chrono::duration<float>(now - it->second).count() < minInterval) return;
        s_last[file] = now;
        Audio::PlaySE(Path(file));
    }
}
