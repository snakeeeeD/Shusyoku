#pragma once
#include "BuffManager.h"
#include "BuffType.h"
#include "CardData.h"
#include "Player.h"
#include "GameUtils.h"
#include <string>

namespace CardEffect
{
    // 毒付与
    inline void ApplyPoison(BuffManager& target, int value, int duration)
    {
        Buff buff;
        buff.type = BuffType::Poison;
        buff.value = value;
        buff.duration = duration;
        buff.name = L"毒";
        buff.description = L"毎ターン{value}ダメージ";
        target.AddBuff(buff);
    }

    // 攻撃力ダウン付与
    inline void ApplyAttackDown(BuffManager& target, int value, int duration)
    {
        Buff buff;
        buff.type = BuffType::AttackDown;
        buff.value = value;
        buff.duration = duration;
        buff.name = L"攻撃力DOWN";
        buff.description = L"攻撃力が{value}下がる";
        target.AddBuff(buff);
    }

    // 防御力ダウン付与
    inline void ApplyDefenseDown(BuffManager& target, int value, int duration)
    {
        Buff buff;
        buff.type = BuffType::DefenseDown;
        buff.value = value;
        buff.duration = duration;
        buff.name = L"防御DOWN";
        buff.description = L"ブロック量が{value}下がる";
        target.AddBuff(buff);
    }

    // onHitEffect適用（敵に対して）
    inline void ApplyOnHitEffect(const CardEffectData& effect, BuffManager& target)
    {
        if (!effect.hasEffect) return;
        if (effect.buffType == "Poison")
            ApplyPoison(target, effect.value, effect.duration);
        else if (effect.buffType == "AttackDown")
            ApplyAttackDown(target, effect.value, effect.duration);
        else if (effect.buffType == "DefenseDown")
            ApplyDefenseDown(target, effect.value, effect.duration);
    }

    // プレイヤーへの効果適用
    inline void ApplyEffectToPlayer(const CardEffectData& effect, Player* player)
    {
        if (!effect.hasEffect) return;

        if (effect.type == CardEffectType::ApplyBuff)
        {
            Buff buff;
            buff.value = effect.value;
            buff.duration = effect.duration;
            buff.type = StringToBuffType(effect.buffType);

            // buffTypeに応じて名前と説明を設定
            if (effect.buffType == "AttackUp")
            {
                buff.name = L"攻撃力UP";
                buff.description = L"攻撃力+" + std::to_wstring(effect.value);
            }
            else if (effect.buffType == "DefenseUp")
            {
                buff.name = L"防御UP";
                buff.description = L"ブロック量+" + std::to_wstring(effect.value);
            }
            else if (effect.buffType == "MoveUp")
            {
                buff.name = L"移動UP";
                buff.description = L"移動力+" + std::to_wstring(effect.value);
            }
            else
            {
                buff.name = ToWString(effect.buffType);
                buff.description = L"+" + std::to_wstring(effect.value);
            }

            player->GetBuffManager().AddBuff(buff);
        }
        else if (effect.type == CardEffectType::Heal)
            player->Heal(effect.value);
        else if (effect.type == CardEffectType::AddEnergy)
            player->AddEnergy(effect.value);
    }
}