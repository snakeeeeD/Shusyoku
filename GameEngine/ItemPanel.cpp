#include "ItemPanel.h"
#include "PlayerDataManager.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "ItemTooltip.h"
#include "MaterialDataBase.h"
#include "RelicManager.h"
#include "TextureManager.h"
#include "UiWindow.h"
#include "Audio.h"
#include "Telemetry.h"
#include "GameUtils.h"
#include <algorithm>
#include <map>

struct ItemInfo { std::wstring name, desc; std::string category; bool valid = false; };

static ItemInfo GetItemInfo(const std::string& id)
{
	if (auto b = MaterialDataBase::GetBase(id))
		return { ToWString(b->name), ToWString(b->desc), "Core", true };
	if (auto m = MaterialDataBase::GetMaterial(id))
		return { ToWString(m->name), ToWString(m->desc), "Material", true };
	if (auto r = RelicManager::Get(id))
		return { ToWString(r->name), ToWString(r->desc), "Relic", true };
	return {};
}


void ItemPanel::OpenInventory()
{
	m_mode = Mode::Inventory;
	m_scroll = 0.0f; m_scrollTarget = 0.0f;
}

void ItemPanel::BackToInventory()
{
	m_mode = Mode::Inventory;
	m_editIdx = -1; m_locked = 0;
}

std::string ItemPanel::HandleClick(UiContext& ui, POINT m, bool canCraft)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	if (IsCraft()) return HandleCraftClick(m);
	if (IsInventory())
	{
		float x, y, w, h; GetCraftBtnRect(x, y, w, h);              // 「合成する」（戦闘中は押せない）
		if (canCraft && m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h)
		{
			Audio::PlaySE("Assets/Sound/se/click.mp3");
			m_mode = Mode::Craft;
			m_base.clear(); m_mods.clear(); m_editIdx = -1; m_locked = 0;
			m_scroll = 0.0f; m_scrollTarget = 0.0f;
		}
	}
	return "";
}

// 素材の一覧：スクロールの補間・はみ出した分を戻す
void ItemPanel::Update(UiContext& ui, float deltaTime, bool dragging)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	if (IsOpen())
	{
		float maxS = MatMaxScroll();
		const float OVER = 60.0f;
		if (m_scrollTarget < -OVER)       m_scrollTarget = -OVER;
		if (m_scrollTarget > maxS + OVER) m_scrollTarget = maxS + OVER;
		if (!dragging)
		{
			if (m_scrollTarget < 0.0f)      m_scrollTarget += (0.0f - m_scrollTarget) * min(1.0f, 12.0f * deltaTime);
			else if (m_scrollTarget > maxS) m_scrollTarget += (maxS - m_scrollTarget) * min(1.0f, 12.0f * deltaTime);
		}
		m_scroll += (m_scrollTarget - m_scroll) * min(1.0f, 15.0f * deltaTime);
	}
}

// 持ち物／合成 を出し分け
void ItemPanel::Draw(UiContext& ui, bool canCraft)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	if (IsInventory()) DrawInventory(ui, canCraft);
	else if (IsCraft()) DrawCraft(ui);
}

// 合成カードの ID「CRAFT:コア|素材|素材」をコアと素材に分ける
static bool ParseCraftId(const std::string& id, std::string& core, std::vector<std::string>& mods)
{
	core.clear(); mods.clear();
	if (id.rfind("CRAFT:", 0) != 0) return false;
	std::string rest = id.substr(6);
	size_t p = 0;
	while (true)
	{
		size_t q = rest.find('|', p);
		std::string s = rest.substr(p, q == std::string::npos ? std::string::npos : q - p);
		if (core.empty()) core = s; else mods.push_back(s);
		if (q == std::string::npos) break;
		p = q + 1;
	}
	return !core.empty();
}

// 枠に空きがある合成カード（デッキの番号）＝素材を足せるもの
std::vector<int> ItemPanel::CraftEditables() const
{
	std::vector<int> v;
	auto& deck = PlayerDataManager::GetData().deck;
	for (int i = 0; i < (int)deck.size(); i++)
	{
		std::string core; std::vector<std::string> mods;
		if (!ParseCraftId(deck[i], core, mods)) continue;
		const BaseDef* b = MaterialDataBase::GetBase(core);
		int slots = (b ? b->modSlots : 2) + RelicManager::SumValue("modSlots");
		if ((int)mods.size() < slots) v.push_back(i);
	}
	return v;
}

std::string ItemPanel::CraftRecipeId() const
{
	if (m_base.empty()) return "";
	std::string id = "CRAFT:" + m_base;
	for (auto& m : m_mods) id += "|" + m;
	return id;
}

int ItemPanel::CraftModSlots() const
{
	if (m_base.empty()) return 2;
	const BaseDef* b = MaterialDataBase::GetBase(m_base);
	return (b ? b->modSlots : 2) + RelicManager::SumValue("modSlots");
}

void ItemPanel::GetCraftSlotRect(int i, float& x, float& y, float& w, float& h) const
{
	int nSlots = 1 + CraftModSlots();
	float sw = 150.0f, gap = 20.0f;
	if (nSlots > 4) { sw = 110.0f; gap = 12.0f; }   // 枠が多い時は縮小
	w = sw; h = 60.0f; y = 130.0f;
	float totalW = nSlots * sw + (nSlots - 1) * gap;
	float startX = m_sw / 2.0f - totalW / 2.0f;
	x = startX + i * (sw + gap);
}

void ItemPanel::GetCraftBtnRect(float& x, float& y, float& w, float& h) const
{
	w = 160.0f; h = 48.0f;
	x = m_sw / 2.0f - 80.0f; y = m_sh - 100.0f;
}

static const char* CoreTypeIcon(const std::string& type)
{
	if (type == "Attack") return "icon_attack";   // 剣
	if (type == "Skill")  return "icon_block";    // 盾
	if (type == "Power")  return "icon_buff";     // ↑
	return "";
}
// 素材が付けられるコア型のアイコン一覧
static std::vector<const char*> MatFitIcons(const std::string& id)
{
	std::vector<const char*> v;
	auto m = MaterialDataBase::GetMaterial(id);
	if (!m) return v;
	bool all = m->entries.count("all") > 0;
	if (all || m->entries.count("Attack")) v.push_back("icon_attack");
	if (all || m->entries.count("Skill"))  v.push_back("icon_block");
	if (all || m->entries.count("Power"))  v.push_back("icon_buff");
	return v;
}

// ===== アイテムのマス（インベントリ・合成で共通） =====
static constexpr float ITEM_BADGE_W = 30.0f, ITEM_BADGE_H = 18.0f;
static void ItemBadgePos(float x, float y, float w, float& bx, float& by)   // マスの右上の角（上の行との隙間8pxに収まる高さ）
{
	bx = x + w - ITEM_BADGE_W + 6.0f; by = y - ITEM_BADGE_H / 2.0f + 2.0f;
}
static float ItemNameLeft(float x, int n, float size)   // アイコン n 個（少し重ねて詰める）の右
{
	return x + 8.0f + (n ? size + (n - 1) * (size - 2.0f) + 6.0f : 0.0f);
}

float ItemPanel::DrawItemIcons(UiContext& ui, const std::vector<const char*>& icons, float x, float y, float h, float size, float a)
{
	for (int k = 0; k < (int)icons.size(); k++)
		if (icons[k] && icons[k][0])
			ui.sprite->DrawSprite(TextureManager::Get(icons[k]),
				x + 8.0f + k * (size - 2.0f), y + (h - size) / 2.0f, size, size, 0.0f, XMFLOAT4(1, 1, 1, a));
	return ItemNameLeft(x, (int)icons.size(), size);
}

void ItemPanel::DrawItemBadge(UiContext& ui, float x, float y, float w, float a)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float bx, by; ItemBadgePos(x, y, w, bx, by);
	ui.sprite->DrawSprite(white, bx, by, ITEM_BADGE_W, ITEM_BADGE_H, 0.0f, XMFLOAT4(0.10f, 0.08f, 0.06f, a));
	UiWindow::Frame(ui.sprite, white, bx, by, ITEM_BADGE_W, ITEM_BADGE_H, XMFLOAT4(0.85f, 0.65f, 0.25f, a));
}

void ItemPanel::DrawItemBadgeText(UiContext& ui, int count, float x, float y, float w, float a)
{
	float bx, by; ItemBadgePos(x, y, w, bx, by);
	wchar_t c[16]; swprintf_s(c, L"×%d", count);
	ui.text->DrawTextCentered(c, bx + ITEM_BADGE_W / 2.0f, by, ITEM_BADGE_W, 12.0f, D2D1::ColorF(1.0f, 0.85f, 0.4f, a), ITEM_BADGE_H);
}

// 名前：入りきらない時だけ縮める（最小11px）。縦は中央。matFade＝合成画面の素材欄のスクロールの端で薄くする
void ItemPanel::DrawItemName(UiContext& ui, const wchar_t* s, float left, float right, float y, float h, D2D1_COLOR_F c, bool matFade)
{
	float sz = 15.0f, tw = ui.text->Measure(s, sz).width, room = right - left;
	if (tw > room && tw > 0.0f) sz = (std::max)(11.0f, sz * room / tw);
	float ty = y + (h - ui.text->Measure(s, sz).height) / 2.0f;
	float aT = matFade ? MatAlphaAt(ty) : 1.0f, aB = matFade ? MatAlphaAt(ty + sz) : 1.0f;
	if (aT <= 0.0f && aB <= 0.0f) return;
	ui.text->DrawTextGradientV(s, left, ty, sz, c, aT, aB);
}

void ItemPanel::DrawCraft(UiContext& ui)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");

	auto matName = [](const std::string& id) -> std::wstring {
		if (auto b = MaterialDataBase::GetBase(id)) return ToWString(b->name);
		if (auto m = MaterialDataBase::GetMaterial(id)) return ToWString(m->name);
		return L"?";
		};

	// --- スプライト ---
	ui.sprite->Begin();
	ui.sprite->DrawSprite(white, 0, 0, (float)m_sw, (float)m_sh, 0.0f,
		XMFLOAT4(0.0f, 0.0f, 0.05f, 0.9f));

	// 枠
	int nSlots = 1 + CraftModSlots();
	for (int i = 0; i < nSlots; i++)
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		bool filled = (i == 0) ? !m_base.empty() : (int)m_mods.size() >= i;
		bool locked = (i > 0 && i - 1 < m_locked);   // もう付いている素材（外せない）
		UiWindow::Button(ui.sprite, white, x, y, w, h,
			locked ? XMFLOAT4(0.5f, 0.42f, 0.2f, 1.0f) : filled ? XMFLOAT4(0.25f, 0.35f, 0.5f, 1.0f) : XMFLOAT4(0.15f, 0.15f, 0.2f, 1.0f));
	}
	// 所持素材
	
	auto bs = CraftBases();
	for (int i = 0; i < (int)bs.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		UiWindow::Button(ui.sprite, white, x, y, w, h, XMFLOAT4(0.4f, 0.3f, 0.2f, 1.0f));
		if (auto b = MaterialDataBase::GetBase(bs[i].first)) DrawItemIcons(ui, { CoreTypeIcon(b->type) }, x, y, h, 22.0f, 1.0f);
		DrawItemBadge(ui, x, y, w, 1.0f);
	}
	{	// 素材を足せる合成カード（コアの後ろに紫で）
		auto eds = CraftEditables();
		for (int k = 0; k < (int)eds.size(); k++) {
			float x, y, w, h; GetCraftBaseRect((int)bs.size() + k, x, y, w, h);
			bool sel = (m_editIdx == eds[k]);
			UiWindow::Button(ui.sprite, white, x, y, w, h, sel ? XMFLOAT4(0.55f, 0.35f, 0.65f, 1.0f) : XMFLOAT4(0.35f, 0.22f, 0.42f, 1.0f));
		}
	}
	auto md = CraftMods();
	const BaseDef* cb = m_base.empty() ? nullptr : MaterialDataBase::GetBase(m_base);
	for (int i = 0; i < (int)md.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		float a = MatAlphaAt(y + h * 0.5f); if (a <= 0.0f) continue;
		auto mdef = MaterialDataBase::GetMaterial(md[i].first);
		bool compat = cb && mdef && mdef->entryFor(cb->type);
		if (compat && mdef->entryFor(cb->type)->slot == "none" && CraftModSlots() == 1) compat = false;
		XMFLOAT4 bc = compat ? XMFLOAT4(0.2f, 0.3f, 0.3f, 1.0f) : XMFLOAT4(0.12f, 0.12f, 0.14f, 1.0f);
		bc.w *= a;
		UiWindow::Button(ui.sprite, white, x, y, w, h, bc);
		DrawItemIcons(ui, MatFitIcons(md[i].first), x, y, h, 16.0f, a);
		float ba = MatAlphaAt(y + 2.0f); if (ba > 0.0f) DrawItemBadge(ui, x, y, w, ba);   // バッジの高さで薄くする
	}
	
		// 作成ボタン
		{
			float x, y, w, h; GetCraftBtnRect(x, y, w, h);
			bool ready = !m_base.empty() && (int)m_mods.size() > m_locked;   // 新しい素材が1つ以上
			bool hov = UiHover(x, y, w, h, ui.input->GetMousePos(), "craftmake", m_lastHoverKey);   // ホバーSE＋判定
			float dy = -HoverLift::Get(x, y, hov, 4.0f);
			UiWindow::Button(ui.sprite, white, x, y + dy, w, h,
				ready ? XMFLOAT4(0.3f, 0.55f, 0.3f, 1.0f) : XMFLOAT4(0.25f, 0.25f, 0.25f, 1.0f));
		}
	// プレビューカード（本体）
	std::string rid = CraftRecipeId();
	const CardData* prev = rid.empty() ? nullptr : CardDataBase::Get(rid);
	if (prev)
	{
		float bx = m_sw / 2.0f - CardVisual::CARD_W / 2.0f;
		float by = 210.0f;
		CardVisual::DrawBase(ui.sprite, white, bx, by, 1.2f, 0.0f,
			CardVisual::GetCardColor(prev->type), prev, ui.time);
	}

	// 見出しの下地ウィンドウ
	{
		float hx = m_sw / 2.0f - (6 * 150.0f + 5 * 8.0f) / 2.0f;
		UiWindow::Draw(ui.sprite, white, hx - 10.0f, CoreTop() - 30.0f, 120.0f, 28.0f);
		UiWindow::Draw(ui.sprite, white, hx - 10.0f, MatTop() - 32.0f, 150.0f, 28.0f);
	}

	ui.sprite->End();

	// --- テキスト ---
	ui.text->Begin();
	// 枠の高さの中で、文字の上下を真ん中に（左寄せの文字用）
	auto midY = [&](const wchar_t* s, float size, float y, float h) {
		return y + (h - ui.text->Measure(s, size, FontRole::Body).height) / 2.0f; };
	ui.text->DrawText(L"Craft", m_sw / 2.0f - 30.0f, 70.0f, 28.0f, D2D1::ColorF(1, 1, 1));

	for (int i = 0; i < nSlots; i++)
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		std::wstring t = (i == 0) ? L"Base" : (L"Mod" + std::to_wstring(i));
		if (i == 0 && !m_base.empty()) t = matName(m_base);
		if (i > 0 && (int)m_mods.size() >= i) t = matName(m_mods[i - 1]);
		ui.text->DrawText(t.c_str(), x + 10.0f, midY(t.c_str(), 18.0f, y, h), 18.0f, D2D1::ColorF(1, 1, 1));
	}
	// 見出し
	float hdrX = m_sw / 2.0f - (6 * 150.0f + 5 * 8.0f) / 2.0f;   // 箱の左端に揃える
	ui.text->DrawTextCentered(L"CORE", hdrX - 10.0f + 60.0f, CoreTop() - 30.0f, 120.0f, 18.0f, D2D1::ColorF(1, 0.85f, 0.5f), 28.0f);
	ui.text->DrawTextCentered(L"MATERIAL", hdrX - 10.0f + 75.0f, MatTop() - 32.0f, 150.0f, 18.0f, D2D1::ColorF(0.6f, 0.9f, 0.9f), 28.0f);

	auto bases = CraftBases();
	for (int i = 0; i < (int)bases.size(); i++)
	{
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		DrawItemName(ui, matName(bases[i].first).c_str(), ItemNameLeft(x, 1, 22.0f), x + w - 8.0f, y, h, D2D1::ColorF(1, 1, 1), false);
		DrawItemBadgeText(ui, bases[i].second, x, y, w, 1.0f);
	}
	{	// 素材を足せる合成カード：「コア名 付いている数/枠」
		auto eds = CraftEditables();
		for (int k = 0; k < (int)eds.size(); k++) {
			float x, y, w, h; GetCraftBaseRect((int)bases.size() + k, x, y, w, h);
			std::string core; std::vector<std::string> ms;
			ParseCraftId(PlayerDataManager::GetData().deck[eds[k]], core, ms);
			const BaseDef* b = MaterialDataBase::GetBase(core);
			int slots = (b ? b->modSlots : 2) + RelicManager::SumValue("modSlots");
			wchar_t buf[64]; swprintf_s(buf, L"%s %d/%d", matName(core).c_str(), (int)ms.size(), slots);
			DrawItemName(ui, buf, x + 8.0f, x + w - 8.0f, y, h, D2D1::ColorF(1, 0.9f, 1), false);
		}
	}
	auto mods = CraftMods();
	for (int i = 0; i < (int)mods.size(); i++) 
	{
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		float nx = ItemNameLeft(x, (int)MatFitIcons(mods[i].first).size(), 16.0f);
		DrawItemName(ui, matName(mods[i].first).c_str(), nx, x + w - 8.0f, y, h, D2D1::ColorF(1, 1, 1), true);
		float ba = MatAlphaAt(y + 2.0f); if (ba > 0.0f) DrawItemBadgeText(ui, mods[i].second, x, y, w, ba);
	}
	{
		float x, y, w, h; GetCraftBtnRect(x, y, w, h);
		POINT mp = ui.input->GetMousePos();
		float dy = (mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h) ? -4.0f : 0.0f;
		ui.text->DrawTextCentered(L"Make", x + w / 2.0f, y + dy, w, 20.0f, D2D1::ColorF(1, 1, 1), h);
	}
	if (prev)
		CardVisual::DrawTexts(ui.text, prev, nullptr,
			m_sw / 2.0f - CardVisual::CARD_W / 2.0f, 210.0f, 1.2f, 0.0f, 1.0f);
	// プレビューの右に、コストを変えているレリック（例：錬成の触媒：コスト-1）
	if (prev)
	{
		float nx = m_sw / 2.0f - CardVisual::CARD_W / 2.0f + CardVisual::CARD_W * 1.2f + 16.0f, ny = 214.0f;
		for (auto& rid2 : PlayerDataManager::GetData().relics)
			if (auto r = RelicManager::Get(rid2))
				if (int v = r->ValueOf("craftCostDown"))
				{
					std::wstring s = ToWString(r->name) + (v > 0 ? L"：コスト-" : L"：コスト+") + std::to_wstring(v > 0 ? v : -v);
					ui.text->DrawText(s.c_str(), nx, ny, 13.0f, v > 0 ? D2D1::ColorF(0.6f, 0.95f, 0.6f) : D2D1::ColorF(1.0f, 0.6f, 0.5f));
					ny += 18.0f;
				}
	}
	ui.text->End();

	std::string hid = HoveredItem(ui.input->GetMousePos());
	if (!hid.empty()) DrawItemTooltip(ui, hid, ui.input->GetMousePos());
}

std::string ItemPanel::HandleCraftClick(POINT m)
{
	// 枠クリックで解除（最初に判定）
	int nSlots = 1 + CraftModSlots();
	for (int i = 0; i < nSlots; i++)
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h)
		{
			if (i == 0) { if (m_editIdx >= 0) { m_mods.clear(); m_editIdx = -1; m_locked = 0; } m_base.clear(); }   // 足している途中なら全部やめる
			else if (i - 1 >= m_locked && (int)m_mods.size() >= i) m_mods.erase(m_mods.begin() + (i - 1));   // もう付いている素材は外せない
			return "";
		}
	}
	// 作成ボタン
	{
		float x, y, w, h; GetCraftBtnRect(x, y, w, h);
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h) return DoCraft();
	}
  // 現在スロットで使っている数を数えるヘルパー
	auto usedCount = [&](const std::string& id) {
		int n = (m_base == id && m_editIdx < 0) ? 1 : 0;   // 足す時はコアを使わない
		for (int k = m_locked; k < (int)m_mods.size(); k++) if (m_mods[k] == id) n++;   // もう付いている素材は数えない
		return n;
		};

	// 核クリック → 土台へ
	auto bases = CraftBases();
	for (int i = 0; i < (int)bases.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h)
		{
			const std::string& id = bases[i].first;
			if (m_editIdx >= 0) { m_editIdx = -1; m_locked = 0; m_mods.clear(); m_base.clear(); }   // 足すのをやめて新しく作る
			int used = usedCount(id) - (m_base == id ? 1 : 0);
			if (used < PlayerDataManager::MaterialCount(id)) {
				m_base = id;
				// 新しい核に効果のない素材は外す
				if (const BaseDef* nb = MaterialDataBase::GetBase(id))
					m_mods.erase(std::remove_if(m_mods.begin(), m_mods.end(),
						[&](const std::string& mid) {
							auto mm = MaterialDataBase::GetMaterial(mid);
							return !mm || !mm->entryFor(nb->type);
						}), m_mods.end());
				while ((int)m_mods.size() > CraftModSlots()) m_mods.pop_back();  // 枠が減ったら余りを外す
			}
			return "";
		}
	}
	// 合成カードクリック → 素材を足す（コアの一覧の後ろに並ぶ）
	{
		auto eds = CraftEditables();
		for (int k = 0; k < (int)eds.size(); k++) {
			float x, y, w, h; GetCraftBaseRect((int)bases.size() + k, x, y, w, h);
			if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h)
			{
				std::string core; std::vector<std::string> ms;
				if (ParseCraftId(PlayerDataManager::GetData().deck[eds[k]], core, ms))
				{ m_editIdx = eds[k]; m_base = core; m_mods = ms; m_locked = (int)ms.size(); }
				return "";
			}
		}
	}
	// 素材クリック → 修飾へ
	auto mods = CraftMods();
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		if (y + h <= MatTop() || y >= MatBottom()) continue;   // 上に完全に出た/下端以降の行だけ除外（箱は帯の下に潜る）
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h) {
			const std::string& id = mods[i].first;
			if (m_base.empty()) return "";                         // 先にBaseを選ぶ
			const BaseDef* b = MaterialDataBase::GetBase(m_base);
			const MaterialDef* mdef = MaterialDataBase::GetMaterial(id);
			if (!b || !mdef || !mdef->entryFor(b->type)) return "";     // この核に効果なし→弾く
			if (mdef->entryFor(b->type)->slot == "none" && CraftModSlots() == 1) return "";  // 効果なし素材は1枠コア不可
			if ((int)m_mods.size() < CraftModSlots() && usedCount(id) < PlayerDataManager::MaterialCount(id))
				m_mods.push_back(id);
			return "";
		}
	}
	return "";
}

std::string ItemPanel::DoCraft()
{
	if (m_base.empty()) return "";
	if ((int)m_mods.size() <= m_locked) return "";   // 新しい素材が1つ以上あれば作れる（枠は全部埋まっていなくていい）

	// 必要数を集計して所持チェック（足す時は、コアともう付いている素材は使わない）
	std::map<std::string, int> need;
	if (m_editIdx < 0) need[m_base]++;
	for (int k = m_locked; k < (int)m_mods.size(); k++) need[m_mods[k]]++;
	for (auto& kv : need)
		if (PlayerDataManager::MaterialCount(kv.first) < kv.second) return "";   // 足りない

	std::string rid = CraftRecipeId();           // 消費前に確定

	// 消費
	for (auto& kv : need)
		PlayerDataManager::AddMaterial(kv.first, -kv.second);

	// デッキに合成カードを追加（足した時は、そのカードを素材が増えた ID に置き換える）
	auto& deck = PlayerDataManager::GetData().deck;
	if (m_editIdx >= 0 && m_editIdx < (int)deck.size()) deck[m_editIdx] = rid;
	else deck.push_back(rid);
	std::string mats;
	for (auto& kv : need)
		for (int k = 0; k < kv.second; k++) { if (!mats.empty()) mats += ","; mats += kv.first; }
	Telemetry::Instance().Log("craft", { {"id", rid}, {"mats", mats} });
	PlayerDataManager::Save();

	m_base.clear(); m_mods.clear(); m_editIdx = -1; m_locked = 0;

	return rid;   // 出現演出は窓の外（休憩の強化・イベントの呪いと共通）
}

void ItemPanel::GetCraftBaseRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 150.0f; h = 38.0f; float gap = 8.0f; int cols = 4;
	float startX = m_sw / 2.0f - (cols * w + (cols - 1) * gap) / 2.0f;
	x = startX + (i % cols) * (w + gap);
	y = CoreTop() - 16.0f + (i / cols) * (h + gap);  // ← スクロール反映
}

void ItemPanel::GetCraftModRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 150.0f; h = 38.0f; float gap = 8.0f; int cols = 6;
	float startX = m_sw / 2.0f - (cols * w + (cols - 1) * gap) / 2.0f;
	x = startX + (i % cols) * (w + gap);
	y = MatTop() + (i / cols) * (h + gap) - m_scroll;
}

float ItemPanel::MatMaxScroll() const
{
	int cols = 4; float h = 38.0f, gap = 8.0f;
	int rows = ((int)CraftMods().size() + cols - 1) / cols;
	float contentH = rows * (h + gap);
	float visibleH = MatBottom() - MatTop();	float maxS = contentH - visibleH;
	return maxS > 0.0f ? maxS : 0.0f;
}

float ItemPanel::MatTop() const
{
	int coreCols = IsCraft() ? 6 : 4;   // インベントリはコアの並び(GetCraftBaseRect=4列)に合わせる
	int coreRows = ((int)CraftBases().size() + coreCols - 1) / coreCols;
	if (coreRows < 1) coreRows = 1;
	return CoreTop() + coreRows * (38.0f + 8.0f) + (IsCraft() ? 34.0f : 56.0f);   // インベントリは見出し・説明の分だけ広く
}

float ItemPanel::MatBottom() const
{
	return m_sh - (IsCraft() ? 100.0f : 122.0f);   // Makeボタンの上に余白を確保（インベントリは窓の下に合成ボタン）
}

float ItemPanel::MatAlphaAt(float y) const
{
	float top = MatTop(), bot = MatBottom();
	const float FT = 34.0f;   // 上：バンドより上に出た分だけフェード
	const float FB = 40.0f;   // 下フェード
	if (y < top - FT || y > bot) return 0.0f;
	float a = 1.0f;
	if (y < top)      a = (y - (top - FT)) / FT;   // top以上は薄くしない＝最上段が反応
	if (bot - y < FB) a = min(a, (bot - y) / FB);
	return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

float ItemPanel::CoreTop() const
{
	return IsCraft() ? 380.0f : 210.0f + RelicManager::BarExtraHeight(m_sw);   // クラフトはプレビューの下／インベントリは見出しと窓の分だけ下げる
}

std::vector<std::pair<std::string, int>> ItemPanel::CraftBases() const
{
	std::vector<std::pair<std::string, int>> v;
	for (auto& kv : PlayerDataManager::GetData().materials)
		if (kv.second > 0 && MaterialDataBase::GetBase(kv.first)) v.push_back({ kv.first, kv.second });
	return v;
}
std::vector<std::pair<std::string, int>> ItemPanel::CraftMods() const
{
	std::vector<std::pair<std::string, int>> v;
	for (auto& kv : PlayerDataManager::GetData().materials)
		if (kv.second > 0 && !MaterialDataBase::GetBase(kv.first)) v.push_back({ kv.first, kv.second });
	return v;
}

std::string ItemPanel::HoveredItem(POINT mp) const
{
	auto bases = CraftBases();
	for (int i = 0; i < (int)bases.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		if (mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h) return bases[i].first;
	}
	auto mods = CraftMods();
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		if (MatAlphaAt(y + h * 0.5f) < 0.6f) continue;   // 端で薄い行はホバー対象外（詳細を出さない）
		if (mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h) return mods[i].first;
	}
	return "";
}

void ItemPanel::DrawItemTooltip(UiContext& ui, const std::string& id, POINT mp)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	ItemTooltip::Draw(ui.sprite, ui.text, white, id,
		(float)mp.x + 16.0f, (float)mp.y + 8.0f, m_sw, m_sh, false);
}

void ItemPanel::DrawInventory(UiContext& ui, bool canCraft)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	POINT mp = ui.input->GetMousePos();
	std::string hid = HoveredItem(mp);
	auto bases = CraftBases();
	auto mods = CraftMods();

	const float gridW = 6 * 150.0f + 5 * 8.0f;                         // 素材の6列が一番広い
	const float px = m_sw / 2.0f - gridW / 2.0f - 24.0f, pw = gridW + 48.0f;
	const float py = CoreTop() - 66.0f, ph = MatBottom() + 10.0f - py;
	const float bnW = 360.0f, bnH = 64.0f, bnY = py - bnH + 8.0f;     // 見出しの布は窓の上の縁に少し重ねる
	const XMFLOAT4 gold(1.0f, 0.85f, 0.2f, 1.0f);

	// マスを1つ描く（ホバーで浮いて金の縁）。戻り値＝浮いた分
	auto tile = [&](float x, float y, float w, float h, XMFLOAT4 col, bool hov) {
		float lift = hov ? 3.0f : 0.0f;
		UiWindow::Button(ui.sprite, white, x, y - lift, w, h, col);
		if (hov) UiWindow::Frame(ui.sprite, white, x, y - lift, w, h, gold);
		return lift;
	};

	float bx, by, bw, bh; GetCraftBtnRect(bx, by, bw, bh);
	bool btnHov = canCraft && mp.x >= bx && mp.x <= bx + bw && mp.y >= by && mp.y <= by + bh;

	ui.sprite->Begin();
	ui.sprite->DrawSprite(white, 0, UiContext::BAR_H, (float)m_sw, (float)m_sh - UiContext::BAR_H, 0.0f,
		XMFLOAT4(0.05f, 0.03f, 0.09f, 0.96f));                            // マップが透けない濃さ（イベントのカード選択と同じ）
	UiWindow::Draw(ui.sprite, white, px, py, pw, ph);
	ui.sprite->DrawSprite(TextureManager::Get("ui_banner"), m_sw / 2.0f - bnW / 2.0f, bnY, bnW, bnH, 0.0f, XMFLOAT4(1, 1, 1, 1));
	ui.sprite->DrawSprite(white, px + 24.0f, CoreTop() - 24.0f, pw - 48.0f, 1.5f, 0.0f, XMFLOAT4(1.0f, 0.85f, 0.5f, 0.35f));   // 区切り線
	ui.sprite->DrawSprite(white, px + 24.0f, MatTop() - 10.0f, pw - 48.0f, 1.5f, 0.0f, XMFLOAT4(0.6f, 0.9f, 0.9f, 0.35f));

	std::vector<float> baseLift(bases.size(), 0.0f), modLift(mods.size(), 0.0f);
	for (int i = 0; i < (int)bases.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		baseLift[i] = tile(x, y, w, h, XMFLOAT4(0.45f, 0.33f, 0.2f, 1.0f), hid == bases[i].first);
		if (auto b = MaterialDataBase::GetBase(bases[i].first)) {
			const char* ic = CoreTypeIcon(b->type);
			if (ic[0]) DrawItemIcons(ui, { ic }, x, y - baseLift[i], h, 22.0f, 1.0f);
		}
		DrawItemBadge(ui, x, y - baseLift[i], w, 1.0f);
	}
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		if (y + h <= MatTop() || y >= MatBottom()) continue;   // 上に完全に出た/下端以降の行だけ除外
		modLift[i] = tile(x, y, w, h, XMFLOAT4(0.2f, 0.33f, 0.36f, 1.0f), hid == mods[i].first);
		DrawItemIcons(ui, MatFitIcons(mods[i].first), x, y - modLift[i], h, 16.0f, 1.0f);
		if (y + h <= MatBottom()) DrawItemBadge(ui, x, y - modLift[i], w, 1.0f);   // 文字と同じく、帯に潜る行は出さない
	}

	// 合成する（戦闘中は押せない）
	UiWindow::Button(ui.sprite, white, bx, by - (btnHov ? 4.0f : 0.0f), bw, bh,
		canCraft ? XMFLOAT4(0.4f, 0.3f, 0.5f, 1.0f) : XMFLOAT4(0.22f, 0.22f, 0.24f, 1.0f));
	ui.sprite->End();

	ui.text->Begin();
	{
		TextRenderer::RoleScope heading(*ui.text, FontRole::Heading);
		ui.text->DrawOutlinedTextCentered(L"アイテム", m_sw / 2.0f, bnY, bnW, bnH, 26.0f,
			D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);
	}
	ui.text->DrawText(L"コア", px + 28.0f, CoreTop() - 50.0f, 18.0f, D2D1::ColorF(1, 0.85f, 0.5f));
	ui.text->DrawText(L"カードの土台。合成に1つ使う", px + 76.0f, CoreTop() - 47.0f, 13.0f, D2D1::ColorF(0.7f, 0.68f, 0.62f));
	ui.text->DrawText(L"素材", px + 28.0f, MatTop() - 36.0f, 18.0f, D2D1::ColorF(0.6f, 0.9f, 0.9f));
	ui.text->DrawText(L"コアに足して効果を加える", px + 76.0f, MatTop() - 33.0f, 13.0f, D2D1::ColorF(0.62f, 0.7f, 0.7f));
	if (bases.empty())
		ui.text->DrawTextCentered(L"まだ持っていない", m_sw / 2.0f, CoreTop() - 8.0f, 300.0f, 15.0f,
			D2D1::ColorF(0.55f, 0.55f, 0.55f), 22.0f);
	if (mods.empty())
		ui.text->DrawTextCentered(L"まだ持っていない", m_sw / 2.0f, MatTop() + 8.0f, 300.0f, 15.0f,
			D2D1::ColorF(0.55f, 0.55f, 0.55f), 22.0f);
	for (int i = 0; i < (int)bases.size(); i++) {   // 名前（アイコンの右）と個数（右上のバッジ）
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h); y -= baseLift[i];
		DrawItemName(ui, GetItemInfo(bases[i].first).name.c_str(), ItemNameLeft(x, 1, 22.0f), x + w - 8.0f, y, h, D2D1::ColorF(1, 1, 1), false);
		DrawItemBadgeText(ui, bases[i].second, x, y, w, 1.0f);
	}
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		if (y + h <= MatTop() || y + h > MatBottom()) continue;   // 帯に潜る行は文字を描かない（D2D文字は帯で隠せないため）
		y -= modLift[i];
		float nx = ItemNameLeft(x, (int)MatFitIcons(mods[i].first).size(), 16.0f);
		DrawItemName(ui, GetItemInfo(mods[i].first).name.c_str(), nx, x + w - 8.0f, y, h, D2D1::ColorF(1, 1, 1), false);
		DrawItemBadgeText(ui, mods[i].second, x, y, w, 1.0f);
	}
	ui.text->DrawTextCentered(L"合成する", bx + bw / 2.0f, by - (btnHov ? 4.0f : 0.0f), bw, 20.0f,
		canCraft ? D2D1::ColorF(1, 1, 1) : D2D1::ColorF(0.55f, 0.55f, 0.55f), bh);
	ui.text->DrawTextCentered(canCraft ? L"コア＋素材でカードを合成できる" : L"戦闘中は合成できない",
		bx + bw / 2.0f, by + bh + 4.0f, 320.0f, 14.0f, D2D1::ColorF(0.75f, 0.75f, 0.75f), 20.0f);
	ui.text->End();

	if (!hid.empty()) DrawItemTooltip(ui, hid, mp);
}

