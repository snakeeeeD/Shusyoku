#pragma once
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include "EffectPart.h"

// 種類の名前（"burst" "sheet" …）から、その種類のデータの読み方を探す。
// 種類の一覧は EffectKinds.cpp の表だけにある（種類の中身は、種類ごとのファイル）
namespace EffectKinds
{
    // 1つぶんの JSON を読んでデータを作る関数（各種類のクラスが static で持つ）
    using LoadFn = std::unique_ptr<EffectPartDef>(*)(const nlohmann::json& j);

    // type の種類の読み方で j を読む。知らない種類なら nullptr
    std::unique_ptr<EffectPartDef> Load(const std::string& type, const nlohmann::json& j);
}
