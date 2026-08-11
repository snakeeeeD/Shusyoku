#pragma once
#include "BuffType.h"
#include <string>
#include <DirectXMath.h>

using namespace DirectX;

struct BuffInfo
{
    std::wstring name;
    std::wstring description;
    XMFLOAT4 color;
    std::string texture;

    static const BuffInfo& Get(BuffType type)
    {
        static const BuffInfo infos[] = {
            // バフ
            { L"攻撃UP",     L"攻撃力が{value}上昇する",                          {0.9f, 0.3f, 0.1f, 1.0f} },  // AttackUp
            { L"射程UP", L"攻撃範囲が{value}広がる",                              {0.9f, 0.7f, 0.2f, 1.0f} },  // RangeUp
            { L"防御UP",     L"ブロック値が{value}上昇する",                      {0.2f, 0.5f, 0.9f, 1.0f} },  // DefenseUp
            { L"バリケード",  L"ブロックが次ターンに持ち越される",                {0.3f, 0.4f, 0.8f, 1.0f} },  // Barricade
            { L"棘",         L"隣接攻撃を受けた時{value}の反射ダメージ",          {0.7f, 0.5f, 0.2f, 1.0f} },  // Thorns
            { L"モメンタム",  L"移動1マスにつきブロック+{value}",                 {0.3f, 0.6f, 0.9f, 1.0f} },  // Momentum
            { L"移動UP",     L"移動距離が{value}増加する",                        {0.2f, 0.8f, 0.2f, 1.0f} },  // MoveUp
            { L"チャージ",    L"移動1マスにつき次の攻撃+{value}ダメージ",         {0.9f, 0.6f, 0.1f, 1.0f} },  // Charge
            { L"ヒットアンドラン", L"移動時に隣接する敵に{value}ダメージ",        {0.9f, 0.2f, 0.2f, 1.0f} },  // HitAndRun
            { L"リポジション", L"移動後、次の攻撃の射程+{value}",                 {0.4f, 0.7f, 0.4f, 1.0f} },  // Reposition
            // デバフ
            { L"攻撃DOWN",   L"攻撃力が{value}低下する",                          {0.6f, 0.2f, 0.2f, 1.0f} },  // AttackDown
            { L"脱力",       L"与ダメージが25%減少する（残り{value}ターン）",     {0.5f, 0.3f, 0.3f, 1.0f} },  // Weak
            { L"防御DOWN",   L"ブロック値が{value}低下する",                      {0.3f, 0.3f, 0.6f, 1.0f} },  // DefenseDown
            { L"虚弱",       L"ブロック値が25%減少する（残り{value}ターン）",     {0.4f, 0.4f, 0.6f, 1.0f} },  // Frail
            { L"弱体",       L"受けるダメージが50%増加する（残り{value}ターン）", {0.8f, 0.2f, 0.2f, 1.0f} },  // Vulnerable
            { L"拘束",       L"移動できない（残り{value}ターン）",                                     {0.4f, 0.2f, 0.1f, 1.0f} },  // Root
            { L"鈍化",       L"移動距離が{value}減少する",                        {0.5f, 0.5f, 0.3f, 1.0f} },  // Slow
            { L"火傷",       L"移動するたびに{value}ダメージを受ける",            {0.9f, 0.4f, 0.1f, 1.0f} },  // Burn
            { L"毒",         L"毎ターン{value}ダメージを受け、1減少する",         {0.5f, 0.0f, 0.8f, 1.0f} },  // Poison

            { L"攻撃UP(今)", L"このターン攻撃力が{value}上昇する", {0.9f, 0.45f, 0.15f, 1.0f} },  // AttackUpTurn
            { L"闘気", L"毎ターン開始時、攻撃力が{value}上昇する", {0.95f, 0.35f, 0.05f, 1.0f} },  // AttackGrowth
            { L"狂乱", L"与ダメージ+{pct}%／被ダメージ+{pct}%／移動で自傷{self}", {0.85f, 0.15f, 0.15f, 1.0f} },  // Frenzy
            { L"毒の瘴気", L"毎ターン開始時 全ての敵に毒{value}", {0.4f, 0.7f, 0.3f, 1.0f} },  // NoxiousFumes
            { L"毒の脈動", L"毒のダメージ回数+{value}", {0.5f, 0.8f, 0.3f, 1.0f} },  // ToxicRhythm
            { L"移動不可", L"このターン移動できない", {0.5f, 0.5f, 0.6f, 1.0f} },  // MoveLock
            { L"受け返し", L"攻撃を完全に防ぐと 敵に{value}ダメージ", {0.4f, 0.7f, 1.0f, 1.0f} },  // Riposte
            { L"研磨", L"ナイフのダメージ+{value}", {0.7f, 0.7f, 0.8f, 1.0f} },  // KnifePower
            { L"投擲術", L"ナイフが遠距離攻撃になる", {0.6f, 0.75f, 0.9f, 1.0f} },  // KnifeThrow
            { L"刃の守り", L"カードを使うたび ブロック{value}", {0.3f, 0.6f, 0.9f, 1.0f} },  // CardBlock
            { L"刃の心得", L"毎ターン ナイフを{value}枚生成", {0.7f, 0.7f, 0.8f, 1.0f} },  // KnifeGen
            { L"火事場の力", L"HP半分以下で攻撃+{value}", {0.9f, 0.4f, 0.2f, 1.0f} },  // LastStand
            { L"決死の覚悟", L"HP1/4以下で攻撃+{value}",  {0.9f, 0.2f, 0.2f, 1.0f} },  // DeepStand
        };
        return infos[static_cast<int>(type)];
    }

    static std::wstring GetDescription(BuffType type, int value)
    {
        std::wstring desc = Get(type).description;

        std::wstring placeholder = L"{value}";
        size_t pos = desc.find(placeholder);
        if (pos != std::wstring::npos)
            desc.replace(pos, placeholder.size(), std::to_wstring(value));

        // {pct} = value × 50（スタック%表示・全て置換）
        std::wstring pct = L"{pct}";
        std::wstring pctVal = std::to_wstring(value * 50);
        size_t pp;
        while ((pp = desc.find(pct)) != std::wstring::npos)
            desc.replace(pp, pct.size(), pctVal);

        // {self} = 移動自傷の実値（基準1 × 被ダメ倍率）
        std::wstring self = L"{self}";
        std::wstring selfVal = std::to_wstring(1 * (100 + 50 * value) / 100);
        size_t ps;
        while ((ps = desc.find(self)) != std::wstring::npos)
            desc.replace(ps, self.size(), selfVal);

        return desc;
    }

    // 二値デバフ（値でなく残りターンで表示するもの）
    static bool IsDurationBased(BuffType t)
    {
        return t == BuffType::Weak || t == BuffType::Vulnerable
            || t == BuffType::Frail || t == BuffType::Root;
    }

};