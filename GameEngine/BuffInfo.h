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
            { L"脆弱",       L"与えるダメージが25%減少する",                      {0.5f, 0.3f, 0.3f, 1.0f} },  // Weak
            { L"防御DOWN",   L"ブロック値が{value}低下する",                      {0.3f, 0.3f, 0.6f, 1.0f} },  // DefenseDown
            { L"脆化",       L"得るブロックが25%減少する",                        {0.4f, 0.4f, 0.6f, 1.0f} },  // Frail
            { L"被ダメUP",   L"受けるダメージが50%増加する",                      {0.8f, 0.2f, 0.2f, 1.0f} },  // Vulnerable
            { L"束縛",       L"移動できない",                                     {0.4f, 0.2f, 0.1f, 1.0f} },  // Root
            { L"鈍化",       L"移動距離が{value}減少する",                        {0.5f, 0.5f, 0.3f, 1.0f} },  // Slow
            { L"火傷",       L"移動するたびに{value}ダメージを受ける",            {0.9f, 0.4f, 0.1f, 1.0f} },  // Burn
            { L"毒",         L"毎ターン{value}ダメージを受け、1減少する",         {0.5f, 0.0f, 0.8f, 1.0f} },  // Poison
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
        return desc;
    }
};