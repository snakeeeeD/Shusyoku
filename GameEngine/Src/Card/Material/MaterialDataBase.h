#pragma once
#include "MaterialData.h"
#include <string>
#include <unordered_map>

class MaterialDataBase {
public:
    static void Load(const std::string& path);
    static void Reload(const std::string& path);   // 全部消してから読み直す（エディタの保存後）
    // JSON1件分の文字列から作る（エディタのプレビュー用。ゲームと同じ読み方）
    static bool ParseBaseText(const std::string& jsonText, BaseDef& out);
    static bool ParseMaterialText(const std::string& jsonText, MaterialDef& out);
    static const BaseDef* GetBase(const std::string& id);
    static const MaterialDef* GetMaterial(const std::string& id);

    static std::string RandomId(int layer = 0);   // クラフト可能素材からランダム（layer=0で全層）

    static const std::unordered_map<std::string, BaseDef>& AllBases() { return s_bases; }
    static const std::unordered_map<std::string, MaterialDef>& AllMaterials() { return s_materials; }

private:
    static std::unordered_map<std::string, BaseDef>     s_bases;
    static std::unordered_map<std::string, MaterialDef> s_materials;
};