#pragma once
#include "MaterialData.h"
#include <string>
#include <unordered_map>

class MaterialDataBase {
public:
    static void Load(const std::string& path);
    static const BaseDef* GetBase(const std::string& id);
    static const MaterialDef* GetMaterial(const std::string& id);
private:
    static std::unordered_map<std::string, BaseDef>     s_bases;
    static std::unordered_map<std::string, MaterialDef> s_materials;
};