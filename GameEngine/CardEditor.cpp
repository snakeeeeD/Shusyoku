#include "CardEditor.h"

#ifdef _DEBUG

#include "CardDataBase.h"
#include "CardVisual.h"
#include "EditorCardOverlay.h"
#include "EditorCommon.h"
#include "EditorHub.h"
#include "EnumNames.h"
#include "RangeShape.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace EditorCommon;

namespace
{
    constexpr float CELL = 46.0f;   // 射程プレビューの1マス(px)

    const char* CARD_PATH   = "Assets/Data/cards.json";
    const char* BACKUP_PATH = "Assets/Data/cards.json.bak";

    // ==================================================================
    // 新規作成のテンプレート（よく使う形を選んで作り、あとは中身を変えるだけにする）
    // ==================================================================
    struct CardTemplate
    {
        const char* label;    // メニューに出す名前
        const char* prefix;   // id の頭（ATK_ / SKL_ / POW_ …）
        const char* json;     // カードの中身（id は自動で付ける）
    };

    const CardTemplate TEMPLATES[] = {
        { u8"攻撃（単体）", "ATK_new",
          u8R"({"name":"新しい攻撃","type":"Attack","rarity":"Common","cost":1,"range":1,"rangeType":"Adjacent",
               "description":"敵に{value}ダメージ","upgrade":{"mainValue":3},
               "mainEffect":{"type":"Damage","value":6},"vfx":"slash","tags":["Strike"]})" },
        { u8"攻撃（範囲）", "ATK_new",
          u8R"({"name":"新しい範囲攻撃","type":"Attack","rarity":"Uncommon","cost":2,"range":1,"rangeType":"Area",
               "description":"周囲の敵に{value}ダメージ","upgrade":{"mainValue":3},
               "mainEffect":{"type":"Damage","value":5},"vfx":"slash_spin","tags":[]})" },
        { u8"攻撃＋デバフ", "ATK_new",
          u8R"({"name":"新しいデバフ攻撃","type":"Attack","rarity":"Common","cost":1,"range":1,"rangeType":"Adjacent",
               "description":"敵に{value}ダメージ\n脱力{onhit}","upgrade":{"mainValue":2,"onHitValue":1},
               "mainEffect":{"type":"Damage","value":5},
               "onHitEffect":{"type":"ApplyDebuff","value":1,"duration":1,"buffType":"Weak"},"vfx":"slash","tags":[]})" },
        { u8"防御（ブロック）", "SKL_new",
          u8R"({"name":"新しい防御","type":"Skill","rarity":"Common","cost":1,"range":0,"rangeType":"None",
               "description":"ブロック+{value}","upgrade":{"mainValue":3},
               "mainEffect":{"type":"Block","value":5},"tags":["Bulwark"]})" },
        { u8"ドロー", "SKL_new",
          u8R"({"name":"新しいドロー","type":"Skill","rarity":"Common","cost":0,"range":0,"rangeType":"None",
               "description":"カードを{value}枚引く","upgrade":{"mainValue":1},
               "mainEffect":{"type":"Draw","value":2},"tags":[]})" },
        { u8"パワー（永続バフ）", "POW_new",
          u8R"({"name":"新しいパワー","type":"Power","rarity":"Uncommon","cost":1,"range":0,"rangeType":"None",
               "description":"攻撃力+{value}","upgrade":{"mainValue":1},
               "mainEffect":{"type":"ApplyBuff","value":2,"duration":-1,"buffType":"AttackUp"},"tags":[]})" },
        { u8"移動", "MOV_new",
          u8R"({"name":"新しい移動","type":"Move","rarity":"Common","cost":1,"range":2,"rangeType":"Diamond",
               "description":"2マスまで移動","upgrade":{"range":1,"description":"3マスまで移動"},
               "mainEffect":{"type":"None","value":0},"tags":[]})" },
        { u8"罠を置く", "SKL_new",
          u8R"({"name":"新しい罠","type":"Skill","rarity":"Common","cost":1,"range":2,"rangeType":"Diamond",
               "description":"爆発する罠を置く（{value}ダメージ）","upgrade":{"mainValue":3},
               "mainEffect":{"type":"PlaceTrap","value":8,"trapType":"explosion"},"tags":["Trap"]})" },
        { u8"状態異常／呪い（使えない）", "STS_new",
          u8R"({"name":"新しい呪い","type":"Curse","cost":0,"range":0,"rangeType":"None",
               "description":"使用できない","mainEffect":{"type":"None","value":0},
               "unplayable":true,"generated":true,"tags":[]})" },
    };

    struct State
    {
        bool open = false;
        bool loaded = false;
        bool dirty = false;

        JsonFile file;                // cards.json そのもの

        int sel = -1;
        int typeFilter = -1;          // -1 = すべて
        int rarityFilter = -1;

        bool overlay = true;          // ゲーム画面にカードを出す
        bool overlayLeft = false;     // 左側に出す（既定は右）
        bool gridUpgraded = false;    // 射程プレビューを強化後で見る

        // プレビュー用：ゲームと同じ読み方で作った CardData
        std::string parsedText;
        bool parseOk = false;
        CardData base, upgraded;
        bool hasUpgrade = false;

        std::string status;
    };
    State s;

    ojson& Cards() { return s.file.doc["cards"]; }

    const std::vector<std::string>& TypeNames()     { static auto v = EnumNames::Names(EnumNames::CardTypes);       return v; }
    const std::vector<std::string>& RarityNames()   { static auto v = EnumNames::Names(EnumNames::CardRarities);    return v; }
    const std::vector<std::string>& RangeNames()    { static auto v = EnumNames::Names(EnumNames::RangeTypes);      return v; }
    const std::vector<std::string>& EffectNames()   { static auto v = EnumNames::Names(EnumNames::CardEffectTypes); return v; }
    const std::vector<std::string>& BuffNames()     { static auto v = EnumNames::Names(EnumNames::BuffTypes);       return v; }

    // カードの vfx に使える名前（effects.json ＋ CardExecutor の特別扱い）
    std::vector<std::string> VfxNames()
    {
        std::vector<std::string> v = EffectIds();
        for (const char* n : EnumNames::CardSpecialVfx) v.push_back(n);
        return v;
    }

    void LoadAll()
    {
        ReloadIdLists();
        if (!s.file.Load(CARD_PATH) || !s.file.doc.contains("cards") || !s.file.doc["cards"].is_array())
        {
            s.loaded = false;
            s.status = s.file.error.empty() ? "cards.json has no \"cards\" array" : s.file.error;
            return;
        }
        s.loaded = true;
        s.dirty = false;
        s.parsedText.clear();
        if (s.sel >= (int)Cards().size()) s.sel = -1;
        s.status = "Loaded " + std::to_string(Cards().size()) + " cards";
    }

    // ==================================================================
    // 検証（errors は保存を止める / warnings は表示だけ）
    // ==================================================================
    bool InList(const std::vector<std::string>& list, const std::string& v)
    {
        return std::find(list.begin(), list.end(), v) != list.end();
    }

    // 効果ブロック1つ。needType=false は allEnemyEffect（種類なし・バフ専用）
    void CheckEffect(const ojson& card, const char* key, bool needType, std::vector<std::string>& err)
    {
        if (!card.contains(key)) return;
        const ojson& e = card[key];
        std::string w = key;
        if (!e.is_object()) { err.push_back(w + " is not an object"); return; }
        if (!e.contains("value")) err.push_back(w + ": value is missing");   // ゲームは e["value"] で読む（無いと全カードの読み込みが止まる）
        if (needType)
        {
            if (!e.contains("type")) err.push_back(w + ": type is missing");
            else if (!EnumNames::Contains(EnumNames::CardEffectTypes, e.value("type", std::string())))
                err.push_back(w + ": unknown type " + e.value("type", std::string()));
        }
        std::string bt = e.value("buffType", std::string());
        if (!bt.empty() && !EnumNames::Contains(EnumNames::BuffTypes, bt)) err.push_back(w + ": unknown buffType " + bt);
        std::string cid = e.value("cardId", std::string());
        if (!cid.empty() && !InList(CardIds(), cid)) err.push_back(w + ": unknown cardId " + cid);
        std::string tt = e.value("trapType", std::string());
        if (!tt.empty() && !InList(TerrainIds(), tt)) err.push_back(w + ": unknown trapType " + tt);
    }

    void Validate(int index, std::vector<std::string>& err, std::vector<std::string>& warn)
    {
        const ojson& c = Cards()[index];

        // ゲームが c["..."] で直接読む項目（無いと cards.json 全体の読み込みが止まる）
        for (const char* key : { "id", "name", "type", "cost", "range", "rangeType", "description", "mainEffect" })
            if (!c.contains(key)) err.push_back(std::string(key) + " is missing");

        std::string id = c.value("id", std::string());
        if (id.empty()) err.push_back("id is empty");
        if (!id.empty() && id.back() == '+') err.push_back(u8"id must not end with '+' (強化版は自動で作られる)");
        for (int i = 0; i < (int)Cards().size(); i++)
            if (i != index && !id.empty() && Cards()[i].value("id", std::string()) == id)
            { err.push_back("duplicate id: " + id); break; }

        if (!EnumNames::Contains(EnumNames::CardTypes, c.value("type", std::string())))
            err.push_back("unknown type " + c.value("type", std::string()));
        if (c.contains("rarity") && !EnumNames::Contains(EnumNames::CardRarities, c.value("rarity", std::string())))
            err.push_back("unknown rarity " + c.value("rarity", std::string()));
        if (!EnumNames::Contains(EnumNames::RangeTypes, c.value("rangeType", std::string())))
            err.push_back("unknown rangeType " + c.value("rangeType", std::string()));

        CheckEffect(c, "mainEffect", true, err);
        CheckEffect(c, "subEffect", true, err);
        CheckEffect(c, "onHitEffect", true, err);
        CheckEffect(c, "onHitEffect2", true, err);
        CheckEffect(c, "onDiscardEffect", true, err);
        CheckEffect(c, "allEnemyEffect", false, err);

        if (c.contains("upgrade") && c["upgrade"].contains("rangeType")
            && !EnumNames::Contains(EnumNames::RangeTypes, c["upgrade"].value("rangeType", std::string())))
            err.push_back("upgrade: unknown rangeType " + c["upgrade"].value("rangeType", std::string()));

        // vfx（文字列 or 配列）
        if (c.contains("vfx"))
        {
            auto names = VfxNames();
            std::vector<std::string> list;
            if (c["vfx"].is_string()) list.push_back(c["vfx"].get<std::string>());
            else if (c["vfx"].is_array()) for (auto& v : c["vfx"]) if (v.is_string()) list.push_back(v.get<std::string>());
            for (auto& v : list) if (!InList(names, v)) err.push_back("unknown vfx " + v);
        }

        if (c.contains("tags") && !c["tags"].is_array()) warn.push_back(u8"tags is not an array (保存時に配列へ直すのがおすすめ)");
        if (!c.contains("rarity")) warn.push_back(u8"rarity is missing (Common 扱い)");
    }

    // 選択中のカードを、ゲームと同じ読み方（CardDataBase::ParseCardText）で CardData にする
    // ※ゲームの読み込みは const の json に c["..."] でアクセスするので、項目が無いと例外ではなく assert で落ちる。
    //   検証で問題が無いカードだけ読む
    void UpdateParsed()
    {
        if (s.sel < 0 || s.sel >= (int)Cards().size()) { s.parseOk = false; return; }
        std::string text = Cards()[s.sel].dump();
        if (text == s.parsedText) return;   // 変わっていなければ読み直さない
        s.parsedText = text;

        std::vector<std::string> err, warn;
        Validate(s.sel, err, warn);
        if (!err.empty()) { s.parseOk = false; return; }

        s.base = CardData();
        s.upgraded = CardData();
        s.parseOk = CardDataBase::ParseCardText(text, s.base, s.upgraded, s.hasUpgrade);
    }

    bool Save()
    {
        for (int i = 0; i < (int)Cards().size(); i++)
        {
            std::vector<std::string> err, warn;
            Validate(i, err, warn);
            if (!err.empty())
            {
                s.sel = i;
                s.status = "Save blocked: fix the problems of " + Cards()[i].value("id", std::string("?"));
                return false;
            }
        }
        if (!s.file.Save(CARD_PATH, BACKUP_PATH)) { s.status = s.file.error; return false; }
        ReloadIdLists();
        s.dirty = false;
        s.status = "Saved (backup: cards.json.bak)";
        return true;
    }

    // ==================================================================
    // JSON 編集の小さな部品（既定値なら項目ごと消して、JSON を散らかさない）
    // ==================================================================
    bool FlagKey(const char* label, ojson& obj, const char* key)
    {
        bool v = obj.value(key, false);
        if (!ImGui::Checkbox(label, &v)) return false;
        if (v) obj[key] = true; else obj.erase(key);
        return true;
    }

    bool IntKeyOmitZero(const char* label, ojson& obj, const char* key, float width = 80.0f)
    {
        int v = obj.value(key, 0);
        ImGui::SetNextItemWidth(width);
        if (!ImGui::InputInt(label, &v)) return false;
        if (v != 0) obj[key] = v; else obj.erase(key);
        return true;
    }

    std::string UniqueId(const std::string& base)
    {
        for (int n = 1;; n++)
        {
            std::string id = base + "_" + std::to_string(n);
            bool used = false;
            for (auto& c : Cards()) if (c.value("id", std::string()) == id) { used = true; break; }
            if (!used) return id;
        }
    }

    // ==================================================================
    // 左：カード一覧
    // ==================================================================
    void DrawList()
    {
        static char filter[32] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##find", "search id / name", filter, sizeof(filter));

        const char* types[] = { "All", "Attack", "Skill", "Move", "Power", "Status", "Curse" };
        int ti = s.typeFilter + 1;
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("##type", &ti, types, 7)) s.typeFilter = ti - 1;
        ImGui::SameLine();
        const char* rars[] = { "All", "Common", "Uncommon", "Rare" };
        int ri = s.rarityFilter + 1;
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("##rarity", &ri, rars, 4)) s.rarityFilter = ri - 1;

        if (ImGui::Button("New...")) ImGui::OpenPopup("NewCardMenu");
        if (ImGui::BeginPopup("NewCardMenu"))
        {
            ImGui::TextDisabled(u8"テンプレートから作る");
            for (auto& t : TEMPLATES)
                if (ImGui::Selectable(t.label))
                {
                    ojson body = ojson::parse(t.json);
                    ojson c;
                    c["id"] = UniqueId(t.prefix);   // id を先頭に（JSON の並びを既存カードとそろえる）
                    for (auto it = body.begin(); it != body.end(); ++it) c[it.key()] = it.value();
                    Cards().push_back(c);
                    s.sel = (int)Cards().size() - 1;
                    s.dirty = true;
                }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(s.sel < 0);
        if (ImGui::Button("Duplicate"))
        {
            ojson c = Cards()[s.sel];
            c["id"] = UniqueId(c.value("id", std::string("card")));
            Cards().push_back(c);
            s.sel = (int)Cards().size() - 1;
            s.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) ImGui::OpenPopup("ConfirmDeleteCard");
        ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("ConfirmDeleteCard", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete %s ?", Cards()[s.sel].value("id", std::string("?")).c_str());
            ImGui::TextDisabled(u8"デッキ・合成素材・イベントなどから参照されていると、そこで見つからなくなります");
            if (ImGui::Button("Delete"))
            {
                Cards().erase(Cards().begin() + s.sel);
                s.sel = -1;
                s.parsedText.clear();
                s.dirty = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::BeginChild("CardList", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (int i = 0; i < (int)Cards().size(); i++)
        {
            const ojson& c = Cards()[i];
            std::string id = c.value("id", std::string("?"));
            std::string name = c.value("name", std::string());
            std::string type = c.value("type", std::string());
            std::string rar = c.value("rarity", std::string("Common"));
            if (s.typeFilter >= 0 && type != types[s.typeFilter + 1]) continue;
            if (s.rarityFilter >= 0 && rar != rars[s.rarityFilter + 1]) continue;
            if (filter[0] && id.find(filter) == std::string::npos && name.find(filter) == std::string::npos) continue;

            std::vector<std::string> err, warn;
            Validate(i, err, warn);
            ImU32 col = !err.empty() ? IM_COL32(255, 110, 110, 255)
                : rar == "Rare" ? IM_COL32(255, 215, 80, 255)
                : rar == "Uncommon" ? IM_COL32(90, 210, 220, 255) : IM_COL32(225, 225, 225, 255);
            char label[200];
            snprintf(label, sizeof(label), "%s%s  %s  [%s %d]##%d", err.empty() ? "" : "! ",
                name.c_str(), id.c_str(), type.c_str(), c.value("cost", 0), i);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            if (ImGui::Selectable(label, s.sel == i)) s.sel = i;
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }

    // ==================================================================
    // 中央：カードの中身
    // ==================================================================
    // 効果ブロック1つ（mainEffect / subEffect / onHitEffect ...）
    void DrawEffectBlock(ojson& card, const char* key, const char* hint, bool required)
    {
        ImGui::PushID(key);
        bool isAllEnemy = std::string(key) == "allEnemyEffect";   // 種類なし・全敵へバフ/デバフ

        if (!card.contains(key))
        {
            if (ImGui::SmallButton((std::string("+ ") + key).c_str()))
            {
                card[key] = isAllEnemy
                    ? ojson{ { "buffType", "Weak" }, { "value", 1 }, { "duration", 1 } }
                    : ojson{ { "type", "Damage" }, { "value", 1 } };
                s.dirty = true;
            }
            ImGui::SameLine(); ImGui::TextDisabled("%s", hint);
            ImGui::PopID();
            return;
        }

        ojson& e = card[key];
        ImGui::SeparatorText(key);
        ImGui::TextDisabled("%s", hint);

        CardEffectType t = CardEffectType::None;
        if (!isAllEnemy)
        {
            if (ComboKey("type", e, "type", EffectNames(), "None", false, 140)) s.dirty = true;
            t = EnumNames::Parse(EnumNames::CardEffectTypes, e.value("type", std::string()), CardEffectType::None);
            ImGui::SameLine();
        }
        if (InputIntKey("value", e, "value", 0, 90)) s.dirty = true;

        bool buff = isAllEnemy || t == CardEffectType::ApplyBuff || t == CardEffectType::ApplyDebuff;
        if (buff)
        {
            ImGui::SameLine();
            if (ComboKey("buffType", e, "buffType", BuffNames(), "", false, 130)) s.dirty = true;
        }
        if (buff || e.contains("duration"))
        {
            ImGui::SameLine();
            if (IntKeyOmitZero("duration", e, "duration")) s.dirty = true;
        }
        if (t == CardEffectType::CreateCard || e.contains("cardId"))
        {
            if (ComboKey("cardId", e, "cardId", CardIds(), "", true, 180)) s.dirty = true;
        }
        if (t == CardEffectType::PlaceTrap || t == CardEffectType::PlaceTrapArea || e.contains("trapType"))
        {
            if (ComboKey("trapType", e, "trapType", TerrainIds(), "", true, 140)) s.dirty = true;
        }

        if (!required && ImGui::SmallButton((std::string("remove ") + key).c_str()))
        {
            card.erase(key);
            s.dirty = true;
        }
        ImGui::PopID();
    }

    // vfx は文字列1つか配列。元の形を保って書き戻す
    void DrawVfx(ojson& c)
    {
        std::vector<std::string> list;
        bool wasString = c.contains("vfx") && c["vfx"].is_string();
        if (wasString) list.push_back(c["vfx"].get<std::string>());
        else if (c.contains("vfx") && c["vfx"].is_array())
            for (auto& v : c["vfx"]) if (v.is_string()) list.push_back(v.get<std::string>());

        bool changed = false;
        int removeAt = -1;
        auto names = VfxNames();
        for (int i = 0; i < (int)list.size(); i++)
        {
            ImGui::PushID(i);
            ojson tmp = { { "v", list[i] } };
            if (ComboKey("##vfx", tmp, "v", names, "", false, 160)) { list[i] = tmp["v"].get<std::string>(); changed = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removeAt = i;
            ImGui::SameLine();
            ImGui::PopID();
        }
        if (removeAt >= 0) { list.erase(list.begin() + removeAt); changed = true; }
        if (ImGui::SmallButton("+ vfx")) { list.push_back(names.empty() ? "slash" : names.front()); changed = true; }
        ImGui::SameLine(); ImGui::TextDisabled(u8"使用時の演出（複数可）");

        if (!changed) return;
        if (list.empty()) c.erase("vfx");
        else if (list.size() == 1 && (wasString || !c.contains("vfx"))) c["vfx"] = list[0];
        else c["vfx"] = list;
        s.dirty = true;
    }

    // tags はカンマ区切りで編集
    void DrawTags(ojson& c)
    {
        std::string joined;
        if (c.contains("tags"))
        {
            if (c["tags"].is_array())
                for (auto& t : c["tags"]) { if (!joined.empty()) joined += ", "; if (t.is_string()) joined += t.get<std::string>(); }
            else if (c["tags"].is_string()) joined = c["tags"].get<std::string>();
        }
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", joined.c_str());
        ImGui::SetNextItemWidth(260);
        if (ImGui::InputText("tags", buf, sizeof(buf)))
        {
            ojson arr = ojson::array();
            std::string cur;
            for (const char* p = buf;; p++)
            {
                if (*p == ',' || *p == '\0')
                {
                    size_t a = cur.find_first_not_of(' '), b = cur.find_last_not_of(' ');
                    if (a != std::string::npos) arr.push_back(cur.substr(a, b - a + 1));
                    cur.clear();
                    if (*p == '\0') break;
                }
                else cur += *p;
            }
            c["tags"] = arr;
            s.dirty = true;
        }
        ImGui::SameLine(); ImGui::TextDisabled(u8"カンマ区切り（報酬の出やすさに使う軸）");
    }

    void DrawUpgrade(ojson& c)
    {
        ImGui::SeparatorText("upgrade (+)");
        if (!c.contains("upgrade"))
        {
            if (ImGui::SmallButton("+ upgrade")) { c["upgrade"] = ojson{ { "mainValue", 1 } }; s.dirty = true; }
            ImGui::SameLine(); ImGui::TextDisabled(u8"強化版（id+）を作らない");
            return;
        }
        ojson& u = c["upgrade"];
        ImGui::TextDisabled(u8"元の値に足す量（0 は書かない）");
        if (IntKeyOmitZero("mainValue", u, "mainValue")) s.dirty = true;
        ImGui::SameLine();
        if (IntKeyOmitZero("subValue", u, "subValue")) s.dirty = true;
        ImGui::SameLine();
        if (IntKeyOmitZero("onHitValue", u, "onHitValue")) s.dirty = true;
        if (IntKeyOmitZero("onHitDuration", u, "onHitDuration")) s.dirty = true;
        ImGui::SameLine();
        if (IntKeyOmitZero("onDiscardValue", u, "onDiscardValue")) s.dirty = true;
        ImGui::SameLine();
        if (IntKeyOmitZero("duration", u, "duration")) s.dirty = true;
        if (IntKeyOmitZero("cost", u, "cost")) s.dirty = true;
        ImGui::SameLine();
        if (IntKeyOmitZero("hits", u, "hits")) s.dirty = true;
        ImGui::SameLine();
        if (IntKeyOmitZero("range", u, "range")) s.dirty = true;
        if (ComboKey("rangeType", u, "rangeType", RangeNames(), "", true, 130)) s.dirty = true;

        bool overrideDesc = u.contains("description");
        if (ImGui::Checkbox(u8"説明文を差し替える", &overrideDesc))
        {
            if (overrideDesc) u["description"] = c.value("description", std::string());
            else u.erase("description");
            s.dirty = true;
        }
        if (overrideDesc && InputTextMultilineKey("description##up", u, "description", 360, 50)) s.dirty = true;   // 改行を含む説明も編集できるよう複数行

        if (ImGui::SmallButton("remove upgrade")) { c.erase("upgrade"); s.dirty = true; }
    }

    void DrawCard(ojson& c)
    {
        // ---- 基本 ----
        if (InputTextKey("id", c, "id", 200)) s.dirty = true;
        ImGui::SameLine(); ImGui::TextDisabled(u8"(変えるとデッキ・合成素材などの参照が切れる)");
        if (InputTextKey("name", c, "name", 200)) s.dirty = true;
        if (ComboKey("type", c, "type", TypeNames(), "Attack", false, 100)) s.dirty = true;
        ImGui::SameLine();
        if (ComboKey("rarity", c, "rarity", RarityNames(), "Common", false, 110)) s.dirty = true;
        ImGui::SameLine();
        if (InputIntKey("cost", c, "cost", 1, 80)) s.dirty = true;

        if (ComboKey("rangeType", c, "rangeType", RangeNames(), "None", false, 130)) s.dirty = true;
        ImGui::SameLine();
        if (InputIntKey("range", c, "range", 0, 80)) s.dirty = true;
        ImGui::SameLine();
        if (IntKeyOmitZero("hits", c, "hits")) s.dirty = true;

        if (InputTextMultilineKey("description", c, "description", 420, 60)) s.dirty = true;   // 改行を含む説明文も編集できるよう複数行
        {
            std::string ph;
            for (const char* p : EnumNames::CardPlaceholders) { ph += p; ph += " "; }
            ImGui::TextDisabled(u8"差し込み: %s", ph.c_str());
        }
        DrawTags(c);
        DrawVfx(c);

        // ---- フラグ ----
        ImGui::SeparatorText("flags");
        if (FlagKey("exhaust", c, "exhaust")) s.dirty = true;               ImGui::SameLine();
        if (FlagKey("pierce", c, "pierce")) s.dirty = true;                 ImGui::SameLine();
        if (FlagKey("dash", c, "dash")) s.dirty = true;                     ImGui::SameLine();
        if (FlagKey("retain", c, "retain")) s.dirty = true;                 ImGui::SameLine();
        if (FlagKey("ethereal", c, "ethereal")) s.dirty = true;
        if (FlagKey("unplayable", c, "unplayable")) s.dirty = true;         ImGui::SameLine();
        if (FlagKey("starter", c, "starter")) s.dirty = true;               ImGui::SameLine();
        if (FlagKey("generated", c, "generated")) s.dirty = true;           ImGui::SameLine();
        if (FlagKey("drawBeforeDiscard", c, "drawBeforeDiscard")) s.dirty = true;
        if (FlagKey("scaleByTrapCount", c, "scaleByTrapCount")) s.dirty = true; ImGui::SameLine();
        if (IntKeyOmitZero("selfDamage", c, "selfDamage")) s.dirty = true;  ImGui::SameLine();
        if (IntKeyOmitZero("endTurnDamage", c, "endTurnDamage")) s.dirty = true;
        // 呪い用：手札にある間、敵の予告を隠す／ターン終了時に手札にあると捨て札へカードを加える
        if (FlagKey("hideIntent", c, "hideIntent")) s.dirty = true;         ImGui::SameLine();
        if (FlagKey("permanent", c, "permanent")) s.dirty = true;           ImGui::SameLine();
        if (ComboKey("endTurnAddCard", c, "endTurnAddCard", CardIds(), "", true, 180)) s.dirty = true;

        // ---- 効果の部品 ----
        DrawEffectBlock(c, "mainEffect", u8"メインの効果（必須）", true);
        DrawEffectBlock(c, "subEffect", u8"サブ効果（ドロー・エナジー・自己バフなど）", false);
        DrawEffectBlock(c, "onHitEffect", u8"敵に当たった時の効果", false);
        DrawEffectBlock(c, "onHitEffect2", u8"当たった時の2つ目の効果", false);
        DrawEffectBlock(c, "onDiscardEffect", u8"捨てられた時の効果", false);
        DrawEffectBlock(c, "allEnemyEffect", u8"使った時に全ての敵へバフ／デバフ", false);

        DrawUpgrade(c);
    }

    // ==================================================================
    // 右：射程のプレビュー（プレイヤーは盤面中央）
    // ==================================================================
    void DrawRangePreview()
    {
        ImGui::SeparatorText("Range");
        if (!s.parseOk) { ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), u8"赤い「!」の問題を直すとプレビューが出ます"); return; }
        if (s.hasUpgrade) ImGui::Checkbox(u8"強化後で見る", &s.gridUpgraded);
        const CardData& d = (s.gridUpgraded && s.hasUpgrade) ? s.upgraded : s.base;

        GridCanvas g;
        g.Begin("range", CELL);
        g.DrawBoard();

        // 扇形などの向き：マウスのあるマスの方向
        int ax = 0, ay = -1;
        if (g.HoverCell() && !(g.hc == PLAYER_COL && g.hr == PLAYER_ROW))
            RangeShape::CardinalAim(PLAYER_COL, PLAYER_ROW, g.hc, g.hr, ax, ay);

        int count = 0;
        for (int r = 0; r < GRID_ROWS; r++)
            for (int c = 0; c < GRID_COLS; c++)
                if (RangeShape::Contains(PLAYER_COL, PLAYER_ROW, c, r, d.rangeType, d.range, 0, ax, ay))
                {
                    g.FillCell(c, r, IM_COL32(80, 160, 255, 130));
                    count++;
                }
        g.FillCell(PLAYER_COL, PLAYER_ROW, IM_COL32(60, 110, 200, 220));
        g.dl->AddText(ImVec2(g.Min(PLAYER_COL, PLAYER_ROW).x + CELL * 0.4f, g.Min(PLAYER_COL, PLAYER_ROW).y + CELL * 0.3f),
            IM_COL32(255, 255, 255, 255), "P");
        if (g.HoverCell()) g.OutlineCell(g.hc, g.hr, IM_COL32(255, 255, 255, 200));

        const char* rt = EnumNames::NameOf(EnumNames::RangeTypes, d.rangeType);
        ImGui::Text("%s  range %d  (%d cells)", rt ? rt : "?", d.range, count);
        if (d.rangeType == RangeType::None) ImGui::TextDisabled(u8"None = 対象を選ばない（自分に使う）");
        if (d.rangeType == RangeType::Cone) ImGui::TextDisabled(u8"Cone はマウスの方向に向く");
        ImGui::TextDisabled(u8"RangeShape::Contains で計算（画面右端が col 0）");
    }
}

// ======================================================================
// 公開関数
// ======================================================================
void CardEditor::Toggle()
{
    s.open = !s.open;
    if (s.open && !s.loaded) LoadAll();
}

bool CardEditor::IsOpen() { return s.open; }

void CardEditor::Draw()
{
    if (!s.open) return;

    ImGui::SetNextWindowSize(ImVec2(1250, 780), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Card Editor", &s.open))
    {
        ImGui::End();
        return;
    }

    if (!s.loaded)
    {
        ImGui::TextUnformatted(s.status.c_str());
        if (ImGui::Button("Retry")) LoadAll();
        ImGui::End();
        return;
    }

    UpdateParsed();

    // ---- 上部：保存まわり ----
    if (ImGui::Button("Save") && Save()) EditorHub::RequestCardReload("");
    ImGui::SameLine();
    if (ImGui::Button("Revert")) LoadAll();
    ImGui::SameLine();
    ImGui::BeginDisabled(s.sel < 0);
    if (ImGui::Button("Save & Add to Hand") && Save())
        EditorHub::RequestCardReload(Cards()[s.sel].value("id", std::string()));
    ImGui::SameLine();
    if (ImGui::Button("Save & Add Upgraded") && Save())
        EditorHub::RequestCardReload(Cards()[s.sel].value("id", std::string()) + "+");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (s.dirty) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "* unsaved");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", s.status.c_str());

    ImGui::Checkbox(u8"ゲーム画面にカードを表示", &s.overlay);
    ImGui::SameLine();
    ImGui::Checkbox(u8"左側に出す", &s.overlayLeft);
    if (EditorHub::IsCardReloadPending())   // 保存したが、まだゲーム側で読み直していない
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
            u8"保存済み：戦闘中に Debug の Battle タブを開くとゲームに反映（手札も作り直す）");
    }
    ImGui::Separator();

    // ---- 3列 ----
    ImGui::BeginChild("Left", ImVec2(320, 0), ImGuiChildFlags_ResizeX);
    DrawList();
    ImGui::EndChild();
    ImGui::SameLine();

    float rightW = GRID_COLS * CELL + 24;
    ImGui::BeginChild("Center", ImVec2(ImGui::GetContentRegionAvail().x - rightW - 8, 0), ImGuiChildFlags_Borders);
    if (s.sel >= 0 && s.sel < (int)Cards().size())
    {
        std::vector<std::string> err, warn;
        Validate(s.sel, err, warn);
        DrawProblems(err);
        for (auto& w : warn) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "? %s", w.c_str());
        DrawCard(Cards()[s.sel]);
    }
    else ImGui::TextDisabled("Select a card on the left");
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("Right", ImVec2(0, 0));
    if (s.sel >= 0) DrawRangePreview();
    ImGui::EndChild();

    ImGui::End();
}

void CardEditor::DrawGameOverlay(SpriteRenderer* sprite, TextRenderer* text)
{
    if (!s.open || !s.overlay || !s.loaded || !s.parseOk) return;
    EditorCardOverlay::Item items[2] = {
        { &s.base,     L"編集中", false },
        { &s.upgraded, L"強化後", true },
    };
    EditorCardOverlay::Draw(sprite, text, items, s.hasUpgrade ? 2 : 1, s.overlayLeft);
}

#else   // Release ではすべて何もしない

void CardEditor::Toggle() {}
bool CardEditor::IsOpen() { return false; }
void CardEditor::Draw() {}
void CardEditor::DrawGameOverlay(SpriteRenderer*, TextRenderer*) {}

#endif
