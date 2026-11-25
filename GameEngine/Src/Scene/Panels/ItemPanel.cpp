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
			m_scroll = m_scrollTarget = 0.0f;   // 一覧はいつも一番上から
		}
	}
	return "";
}

// 合成の下の一覧：スクロールの補間・はみ出した分を戻す
void ItemPanel::Update(UiContext& ui, float deltaTime, bool dragging)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	if (!IsCraft()) { m_scroll = m_scrollTarget = 0.0f; return; }
	float maxS = ListMaxScroll();
	const float OVER = 60.0f;   // 引っぱりすぎの上限
	m_scrollTarget = (std::max)(-OVER, (std::min)(maxS + OVER, m_scrollTarget));
	if (!dragging)              // 離したら範囲の中へ戻す
	{
		if (m_scrollTarget < 0.0f)      m_scrollTarget += (0.0f - m_scrollTarget) * (std::min)(1.0f, 12.0f * deltaTime);
		else if (m_scrollTarget > maxS) m_scrollTarget += (maxS - m_scrollTarget) * (std::min)(1.0f, 12.0f * deltaTime);
	}
	m_scroll += (m_scrollTarget - m_scroll) * (std::min)(1.0f, 15.0f * deltaTime);
}
float ItemPanel::ListMaxScroll() const
{
	int matRows = ((int)CraftMods().size() + CRAFT_COLS - 1) / CRAFT_COLS;
	float bottom = MatTop() + m_scroll + matRows * (TILE_H + TILE_GAP) - TILE_GAP;   // スクロールしていない時の一覧の下端
	float maxS = bottom - (CRAFT_BOTTOM - LIST_FADE);   // 一番下の行が、はっきり見える範囲（薄くなる手前）に入るまで
	return maxS > 0.0f ? maxS : 0.0f;
}
float ItemPanel::ListAlpha(float y) const
{
	const float top = LIST_Y, bottom = CRAFT_BOTTOM - TILE_H - LIST_FADE;   // マスの上端がこの間なら、はっきり見える（ListMaxScroll と同じ境目）
	if (y < top - LIST_FADE || y > bottom + LIST_FADE) return 0.0f;
	float a = 1.0f;
	if (y < top)    a = (y - (top - LIST_FADE)) / LIST_FADE;
	if (y > bottom) a = (std::min)(a, (bottom + LIST_FADE - y) / LIST_FADE);
	return a;
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

// 合成の枠：コア＋素材。「＋」の分をあけて、窓の中身の真ん中に並べる（多い時は細くする）
void ItemPanel::GetCraftSlotRect(int i, float& x, float& y, float& w, float& h) const
{
	float left = CraftPanelRect().x + PANEL_PAD, room = CRAFT_LEFT_W;   // 窓の中身の幅いっぱい
	int n = 1 + CraftModSlots();
	w = (std::min)(SLOT_MAX_W, (room - PLUS_W - (n - 1) * SLOT_GAP) / n);
	h = SLOT_H; y = SLOT_Y;
	float used = n * w + (n - 1) * SLOT_GAP + PLUS_W;
	x = left + (room - used) / 2.0f + i * (w + SLOT_GAP) + (i > 0 ? PLUS_W : 0.0f);
}

void ItemPanel::GetCraftBtnRect(float& x, float& y, float& w, float& h) const
{
	if (IsCraft())   // 出来上がりのカードの右（高さはカードの真ん中）
	{
		URect c = PreviewCardRect();
		w = CRAFT_BTN_W; h = CRAFT_BTN_H; x = c.x + c.w + SIDE_GAP; y = c.y + c.h / 2.0f - h / 2.0f;
		return;
	}
	w = 160.0f; h = 48.0f; x = m_sw / 2.0f - w / 2.0f; y = m_sh - 100.0f;   // 持ち物の「合成する」
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

// マスを1つ描く（ホバーで浮いて金の縁。持ち物・合成で共通）。戻り値＝浮いた分
float ItemPanel::DrawTile(UiContext& ui, float x, float y, float w, float h, XMFLOAT4 col, bool hov)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float lift = hov ? 3.0f : 0.0f;
	UiWindow::Button(ui.sprite, white, x, y - lift, w, h, col);
	if (hov) UiWindow::Frame(ui.sprite, white, x, y - lift, w, h, XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f));
	return lift;
}

// 名前：入りきらない時だけ縮める（最小11px）。縦は中央
void ItemPanel::DrawItemName(UiContext& ui, const wchar_t* s, float left, float right, float y, float h, D2D1_COLOR_F c)
{
	float sz = 15.0f, tw = ui.text->Measure(s, sz).width, room = right - left;
	if (tw > room && tw > 0.0f) sz = (std::max)(11.0f, sz * room / tw);
	ui.text->DrawTextGradientV(s, left, y + (h - ui.text->Measure(s, sz).height) / 2.0f, sz, c, 1.0f, 1.0f);
}

void ItemPanel::DrawCraft(UiContext& ui)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	POINT mp = ui.input->GetMousePos();
	const XMFLOAT4 gold(1.0f, 0.85f, 0.2f, 1.0f);
	const URect panel = CraftPanelRect(), card = PreviewCardRect(), desc = DescRect();
	const float bnW = 360.0f, bnH = 64.0f, bnY = panel.y - bnH + 8.0f;   // 見出しの布（持ち物の窓と同じ）

	auto bases = CraftBases();
	auto eds = CraftEditables();
	auto mods = CraftMods();
	const BaseDef* cb = m_base.empty() ? nullptr : MaterialDataBase::GetBase(m_base);
	auto fits = [&](const std::string& id) {   // 今のコアに付けられる素材か
		auto mdef = MaterialDataBase::GetMaterial(id);
		if (!cb || !mdef || !mdef->entryFor(cb->type)) return false;
		return !(mdef->entryFor(cb->type)->slot == "none" && CraftModSlots() == 1);   // 効果なし素材は1枠のコアには付けられない
	};
	const int nSlots = 1 + CraftModSlots();
	std::string rid = CraftRecipeId();
	const CardData* prev = rid.empty() ? nullptr : CardDataBase::Get(rid);
	const bool ready = !m_base.empty() && (int)m_mods.size() > m_locked;   // 新しい素材が1つ以上
	float bx, by, bw, bh; GetCraftBtnRect(bx, by, bw, bh);
	bool btnHov = UiHover(bx, by, bw, bh, mp, "craftmake", m_lastHoverKey);
	float btnDy = -HoverLift::Get(bx, by, btnHov, 4.0f);
	const float cardX = card.x + (card.w - CardVisual::CARD_W) / 2.0f;   // DrawBase に渡す位置（拡大の中心を合わせる）
	const float cardY = card.y + (card.h - CardVisual::CARD_H) / 2.0f;

	// 説明を出す物：マウスの下の持ち物。なければ、マウスの下の枠に入っている物
	const std::string hid = HoveredItem(mp);
	std::string descId = hid;
	for (int i = 0; descId.empty() && i < nSlots; i++)
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		if (mp.x < x || mp.x > x + w || mp.y < y || mp.y > y + h) continue;
		if (i == 0) descId = m_base;
		else if ((int)m_mods.size() >= i) descId = m_mods[i - 1];
	}

	// ---- 絵 ----
	ui.sprite->Begin();
	ui.sprite->DrawSprite(white, 0, UiContext::BAR_H, (float)m_sw, (float)m_sh - UiContext::BAR_H, 0.0f,
		XMFLOAT4(0.05f, 0.03f, 0.09f, 0.96f));                            // 持ち物の窓と同じ濃さ（後ろのマップが透けない）
	UiWindow::Draw(ui.sprite, white, panel.x, panel.y, panel.w, panel.h);
	ui.sprite->DrawSprite(TextureManager::Get("ui_banner"), m_sw / 2.0f - bnW / 2.0f, bnY, bnW, bnH, 0.0f, XMFLOAT4(1, 1, 1, 1));

	for (int i = 0; i < nSlots; i++)   // 合成の枠
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		bool filled = (i == 0) ? !m_base.empty() : (int)m_mods.size() >= i;
		bool locked = (i > 0 && i - 1 < m_locked);   // もう付いている素材（外せない）
		UiWindow::Button(ui.sprite, white, x, y, w, h,
			locked ? XMFLOAT4(0.5f, 0.42f, 0.2f, 1.0f) : filled ? XMFLOAT4(0.25f, 0.35f, 0.5f, 1.0f) : XMFLOAT4(0.15f, 0.15f, 0.2f, 1.0f));
		if (i == 0) UiWindow::Frame(ui.sprite, white, x, y, w, h, gold);   // コアの枠は金の縁
	}
	UiWindow::Draw(ui.sprite, white, desc.x, desc.y, desc.w, desc.h);    // 説明の下地
	if (prev) CardVisual::DrawBase(ui.sprite, white, cardX, cardY, PREVIEW_SCALE, 0.0f, CardVisual::GetCardColor(prev->type), prev, ui.time);
	else      UiWindow::Draw(ui.sprite, white, card.x, card.y, card.w, card.h);   // まだ空：カードの形の枠だけ
	UiWindow::Button(ui.sprite, white, bx, by + btnDy, bw, bh,
		ready ? XMFLOAT4(0.3f, 0.55f, 0.3f, 1.0f) : XMFLOAT4(0.25f, 0.25f, 0.25f, 1.0f));

	ui.sprite->DrawSprite(white, panel.x + PANEL_PAD, LIST_Y - 8.0f, panel.w - PANEL_PAD * 2.0f, 1.5f, 0.0f,
		XMFLOAT4(1.0f, 0.85f, 0.5f, 0.35f));                              // 上と下の区切り
	std::vector<float> baseLift(bases.size()), edLift(eds.size()), modLift(mods.size());
	for (int i = 0; i < (int)bases.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		float a = ListAlpha(y); if (a <= 0.0f) continue;   // 一覧の端で薄く、外は描かない
		baseLift[i] = DrawTile(ui, x, y, w, h, XMFLOAT4(0.45f, 0.33f, 0.2f, a), hid == bases[i].first);
		if (auto b = MaterialDataBase::GetBase(bases[i].first)) DrawItemIcons(ui, { CoreTypeIcon(b->type) }, x, y - baseLift[i], h, 22.0f, a);
		DrawItemBadge(ui, x, y - baseLift[i], w, a);
	}
	for (int k = 0; k < (int)eds.size(); k++) {
		float x, y, w, h; GetCraftEditRect(k, x, y, w, h);
		float a = ListAlpha(y); if (a <= 0.0f) continue;
		bool hov = a >= 0.6f && mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h;
		XMFLOAT4 col = (m_editIdx == eds[k]) ? XMFLOAT4(0.55f, 0.35f, 0.65f, 1.0f) : XMFLOAT4(0.35f, 0.22f, 0.42f, 1.0f); col.w *= a;
		edLift[k] = DrawTile(ui, x, y, w, h, col, hov);
	}
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		float a = ListAlpha(y); if (a <= 0.0f) continue;
		bool ok = fits(mods[i].first);
		XMFLOAT4 col = ok ? XMFLOAT4(0.2f, 0.33f, 0.36f, 1.0f) : XMFLOAT4(0.12f, 0.12f, 0.14f, 1.0f); col.w *= a;
		modLift[i] = DrawTile(ui, x, y, w, h, col, ok && hid == mods[i].first);
		DrawItemIcons(ui, MatFitIcons(mods[i].first), x, y - modLift[i], h, 16.0f, (ok ? 1.0f : 0.45f) * a);
		DrawItemBadge(ui, x, y - modLift[i], w, a);
	}
	ui.sprite->End();

	// ---- 文字 ----
	ui.text->Begin();
	{
		TextRenderer::RoleScope heading(*ui.text, FontRole::Heading);
		ui.text->DrawOutlinedTextInRect(L"合成", m_sw / 2.0f - bnW / 2.0f, bnY, bnW, bnH, 26.0f,
			D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);
	}
	for (int i = 0; i < nSlots; i++)   // 枠に入っている物の名前（空きの枠は何も書かない）
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		std::string id = (i == 0) ? m_base : ((int)m_mods.size() >= i ? m_mods[i - 1] : "");
		if (id.empty()) continue;
		DrawItemName(ui, GetItemInfo(id).name.c_str(), x + 10.0f, x + w - 8.0f, y, h, D2D1::ColorF(1, 1, 1));
	}
	{
		float x, y, w, h; GetCraftSlotRect(0, x, y, w, h);
		ui.text->DrawTextInRect(L"＋", x + w, y, PLUS_W, h, 24.0f, D2D1::ColorF(1.0f, 0.85f, 0.4f));
	}
	if (prev) CardVisual::DrawTexts(ui.text, prev, nullptr, cardX, cardY, PREVIEW_SCALE, 0.0f, 1.0f);   // まだ空の時は枠だけ
	ui.text->DrawTextInRect(L"合成する", bx, by + btnDy, bw, bh, 26.0f, ready ? D2D1::ColorF(1, 1, 1) : D2D1::ColorF(0.55f, 0.55f, 0.55f));

	// 素材の効果の説明
	if (!descId.empty())
	{
		ItemInfo info = GetItemInfo(descId);
		const BaseDef* db = MaterialDataBase::GetBase(descId);
		float tx = desc.x + 16.0f, ty = desc.y + 10.0f;
		ui.text->DrawText(info.name.c_str(), tx, ty, 18.0f, db ? D2D1::ColorF(1, 0.85f, 0.5f) : D2D1::ColorF(0.6f, 0.9f, 0.9f));
		std::wstring note;   // 名前の右の補足
		if (db) note = L"コア（素材の枠 " + std::to_wstring(db->modSlots + RelicManager::SumValue("modSlots")) + L"）";
		else if (!fits(descId)) note = m_base.empty() ? L"先にコアを選ぶ" : L"今のコアには付けられない";
		if (!note.empty())
			ui.text->DrawText(note.c_str(), tx + ui.text->Measure(info.name.c_str(), 18.0f).width + 14.0f, ty + 3.0f, 14.0f,
				db ? D2D1::ColorF(0.75f, 0.7f, 0.6f) : D2D1::ColorF(1.0f, 0.5f, 0.45f));
		std::wstring body = info.desc;   // 「攻撃:… / スキル:…」は1行ずつに
		for (size_t p; (p = body.find(L" / ")) != std::wstring::npos; ) body.replace(p, 3, L"\n");
		ui.text->DrawText(body.c_str(), tx, ty + 30.0f, 14.0f, D2D1::ColorF(0.9f, 0.9f, 0.9f));
	}
	{	// コストを変えているレリック（例：錬成の触媒：コスト-1）は「合成する」の下
		float ny = by + bh + 12.0f;
		for (auto& relicId : PlayerDataManager::GetData().relics)
			if (auto r = RelicManager::Get(relicId))
				if (int v = r->ValueOf("craftCostDown"))
				{
					std::wstring s = ToWString(r->name) + (v > 0 ? L"：コスト-" : L"：コスト+") + std::to_wstring(v > 0 ? v : -v);
					ui.text->DrawText(s.c_str(), bx, ny, 13.0f,
						v > 0 ? D2D1::ColorF(0.6f, 0.95f, 0.6f) : D2D1::ColorF(1.0f, 0.6f, 0.5f));
					ny += 18.0f;
				}
	}

	// 下の持ち物：見出し・名前・個数
	const float colL = GridRect(0, 0, 0.0f, CRAFT_COLS).x, colEd = GridRect(CRAFT_CORE_COLS, 0, 0.0f, CRAFT_COLS).x;
	auto headAlpha = [&](float y) { return ListAlpha(y + HEAD_H); };   // 見出しは、すぐ下のマスと一緒に薄くする
	const float coreHead = CoreTop() - HEAD_H, matHead = MatTop() - HEAD_H;
	if (float a = headAlpha(coreHead); a > 0.0f)
	{
		ui.text->DrawText(L"コア", colL, coreHead, 16.0f, D2D1::ColorF(1, 0.85f, 0.5f, a));
		ui.text->DrawText(L"空き枠のある合成カード", colEd, coreHead, 16.0f, D2D1::ColorF(0.85f, 0.65f, 1.0f, a));
		if (bases.empty()) ui.text->DrawText(L"まだ持っていない", colL + 8.0f, CoreTop() + 10.0f, 14.0f, D2D1::ColorF(0.5f, 0.5f, 0.5f, a));
		if (eds.empty())   ui.text->DrawText(L"なし", colEd + 8.0f, CoreTop() + 10.0f, 14.0f, D2D1::ColorF(0.5f, 0.5f, 0.5f, a));
	}
	if (float a = headAlpha(matHead); a > 0.0f)
	{
		ui.text->DrawText(L"素材", colL, matHead, 16.0f, D2D1::ColorF(0.6f, 0.9f, 0.9f, a));
		if (mods.empty()) ui.text->DrawText(L"まだ持っていない", colL + 8.0f, MatTop() + 10.0f, 14.0f, D2D1::ColorF(0.5f, 0.5f, 0.5f, a));
	}
	for (int i = 0; i < (int)bases.size(); i++)
	{
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		float a = ListAlpha(y); if (a <= 0.0f) continue;
		y -= baseLift[i];
		DrawItemName(ui, GetItemInfo(bases[i].first).name.c_str(), ItemNameLeft(x, 1, 22.0f), x + w - 8.0f, y, h, D2D1::ColorF(1, 1, 1, a));
		DrawItemBadgeText(ui, bases[i].second, x, y, w, a);
	}
	for (int k = 0; k < (int)eds.size(); k++)   // 「コア名 付いている数/枠」
	{
		float x, y, w, h; GetCraftEditRect(k, x, y, w, h);
		float a = ListAlpha(y); if (a <= 0.0f) continue;
		y -= edLift[k];
		std::string core; std::vector<std::string> ms;
		ParseCraftId(PlayerDataManager::GetData().deck[eds[k]], core, ms);
		const BaseDef* b = MaterialDataBase::GetBase(core);
		int slots = (b ? b->modSlots : 2) + RelicManager::SumValue("modSlots");
		wchar_t buf[64]; swprintf_s(buf, L"%s %d/%d", GetItemInfo(core).name.c_str(), (int)ms.size(), slots);
		DrawItemName(ui, buf, x + 8.0f, x + w - 8.0f, y, h, D2D1::ColorF(1, 0.9f, 1, a));
	}
	for (int i = 0; i < (int)mods.size(); i++)
	{
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		float a = ListAlpha(y); if (a <= 0.0f) continue;
		y -= modLift[i];
		bool ok = fits(mods[i].first);
		float nx = ItemNameLeft(x, (int)MatFitIcons(mods[i].first).size(), 16.0f);
		DrawItemName(ui, GetItemInfo(mods[i].first).name.c_str(), nx, x + w - 8.0f, y, h, ok ? D2D1::ColorF(1, 1, 1, a) : D2D1::ColorF(0.55f, 0.55f, 0.58f, a));
		DrawItemBadgeText(ui, mods[i].second, x, y, w, a);
	}
	ui.text->End();
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
		if (ListAlpha(y) < 0.6f) continue;   // 一覧の端で薄い行は押せない
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
			float x, y, w, h; GetCraftEditRect(k, x, y, w, h);
			if (ListAlpha(y) < 0.6f) continue;
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
		if (ListAlpha(y) < 0.6f) continue;
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

URect ItemPanel::GridRect(int col, int row, float top, int cols) const
{
	const float tw = IsCraft() ? CRAFT_TILE_W : TILE_W;
	float x0 = IsCraft() ? CraftPanelRect().x + PANEL_PAD                          // 合成：左の部分の左端から
	                     : m_sw / 2.0f - (cols * tw + (cols - 1) * TILE_GAP) / 2.0f;   // 持ち物：真ん中にそろえる
	return { x0 + col * (tw + TILE_GAP), top + row * (TILE_H + TILE_GAP), tw, TILE_H };
}
URect ItemPanel::CraftPanelRect() const   // 中身の幅＋左右の余白
{
	float w = CRAFT_LEFT_W + PANEL_PAD * 2.0f;
	return { m_sw / 2.0f - w / 2.0f, CRAFT_TOP, w, CRAFT_BOTTOM - CRAFT_TOP };
}
URect ItemPanel::PreviewCardRect() const   // 画面の真ん中
{
	float w = CardVisual::CARD_W * PREVIEW_SCALE, h = CardVisual::CARD_H * PREVIEW_SCALE;
	return { m_sw / 2.0f - w / 2.0f, PREVIEW_Y, w, h };
}
URect ItemPanel::DescRect() const          // カードの左（窓の左端からカードの手前まで）
{
	float left = CraftPanelRect().x + PANEL_PAD;
	return { left, PREVIEW_Y, PreviewCardRect().x - SIDE_GAP - left, DESC_H };
}
void ItemPanel::GetCraftBaseRect(int i, float& x, float& y, float& w, float& h) const
{
	URect r = IsCraft() ? GridRect(i % CRAFT_CORE_COLS, i / CRAFT_CORE_COLS, CoreTop(), CRAFT_COLS)
	                    : GridRect(i % MAT_COLS, i / MAT_COLS, CoreTop() - 16.0f, MAT_COLS);
	x = r.x; y = r.y; w = r.w; h = r.h;
}
void ItemPanel::GetCraftEditRect(int k, float& x, float& y, float& w, float& h) const
{
	const int cols = CRAFT_COLS - CRAFT_CORE_COLS;   // コアの右の列
	URect r = GridRect(CRAFT_CORE_COLS + k % cols, k / cols, CoreTop(), CRAFT_COLS);
	x = r.x; y = r.y; w = r.w; h = r.h;
}
void ItemPanel::GetCraftModRect(int i, float& x, float& y, float& w, float& h) const
{
	int cols = IsCraft() ? CRAFT_COLS : MAT_COLS;
	URect r = GridRect(i % cols, i / cols, MatTop(), cols);
	x = r.x; y = r.y; w = r.w; h = r.h;
}
float ItemPanel::MatTop() const
{
	if (IsCraft())   // コアと空き枠のカードの、行の多いほうの下＋見出し
	{
		int edCols = CRAFT_COLS - CRAFT_CORE_COLS;
		int coreRows = ((int)CraftBases().size() + CRAFT_CORE_COLS - 1) / CRAFT_CORE_COLS;
		int edRows = ((int)CraftEditables().size() + edCols - 1) / edCols;
		int rows = (std::max)(1, (std::max)(coreRows, edRows));
		return CoreTop() + rows * (TILE_H + TILE_GAP) + HEAD_H;
	}
	int coreRows = ((int)CraftBases().size() + MAT_COLS - 1) / MAT_COLS;
	if (coreRows < 1) coreRows = 1;
	return CoreTop() + coreRows * (TILE_H + TILE_GAP) + 56.0f;   // 持ち物は見出し・説明の分だけ広く
}
float ItemPanel::MatBottom() const { return m_sh - 122.0f; }   // 持ち物の窓の下（下に「合成する」）
float ItemPanel::CoreTop() const
{
	if (IsCraft()) return LIST_Y + HEAD_H - m_scroll;   // 下の一覧はスクロールする（コア→空き枠→素材が一緒に動く）
	return 210.0f + RelicManager::BarExtraHeight(m_sw);   // 持ち物は見出しと窓の分だけ下げる
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
	auto hit = [&](float x, float y, float w, float h) {   // 合成の一覧の端で薄い行は反応しない
		return mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h && (!IsCraft() || ListAlpha(y) >= 0.6f); };
	auto bases = CraftBases();
	for (int i = 0; i < (int)bases.size(); i++) { float x, y, w, h; GetCraftBaseRect(i, x, y, w, h); if (hit(x, y, w, h)) return bases[i].first; }
	auto mods = CraftMods();
	for (int i = 0; i < (int)mods.size(); i++)  { float x, y, w, h; GetCraftModRect(i, x, y, w, h);  if (hit(x, y, w, h)) return mods[i].first; }
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

	const float gridW = MAT_COLS * TILE_W + (MAT_COLS - 1) * TILE_GAP;   // 素材の列が一番広い
	const float px = m_sw / 2.0f - gridW / 2.0f - 24.0f, pw = gridW + 48.0f;
	const float py = CoreTop() - 66.0f, ph = MatBottom() + 10.0f - py;
	const float bnW = 360.0f, bnH = 64.0f, bnY = py - bnH + 8.0f;     // 見出しの布は窓の上の縁に少し重ねる

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
		baseLift[i] = DrawTile(ui, x, y, w, h, XMFLOAT4(0.45f, 0.33f, 0.2f, 1.0f), hid == bases[i].first);
		if (auto b = MaterialDataBase::GetBase(bases[i].first)) {
			const char* ic = CoreTypeIcon(b->type);
			if (ic[0]) DrawItemIcons(ui, { ic }, x, y - baseLift[i], h, 22.0f, 1.0f);
		}
		DrawItemBadge(ui, x, y - baseLift[i], w, 1.0f);
	}
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		if (y + h <= MatTop() || y >= MatBottom()) continue;   // 上に完全に出た/下端以降の行だけ除外
		modLift[i] = DrawTile(ui, x, y, w, h, XMFLOAT4(0.2f, 0.33f, 0.36f, 1.0f), hid == mods[i].first);
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
		DrawItemName(ui, GetItemInfo(bases[i].first).name.c_str(), ItemNameLeft(x, 1, 22.0f), x + w - 8.0f, y, h, D2D1::ColorF(1, 1, 1));
		DrawItemBadgeText(ui, bases[i].second, x, y, w, 1.0f);
	}
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		if (y + h <= MatTop() || y + h > MatBottom()) continue;   // 帯に潜る行は文字を描かない（D2D文字は帯で隠せないため）
		y -= modLift[i];
		float nx = ItemNameLeft(x, (int)MatFitIcons(mods[i].first).size(), 16.0f);
		DrawItemName(ui, GetItemInfo(mods[i].first).name.c_str(), nx, x + w - 8.0f, y, h, D2D1::ColorF(1, 1, 1));
		DrawItemBadgeText(ui, mods[i].second, x, y, w, 1.0f);
	}
	ui.text->DrawTextCentered(L"合成する", bx + bw / 2.0f, by - (btnHov ? 4.0f : 0.0f), bw, 20.0f,
		canCraft ? D2D1::ColorF(1, 1, 1) : D2D1::ColorF(0.55f, 0.55f, 0.55f), bh);
	ui.text->DrawTextCentered(canCraft ? L"コア＋素材でカードを合成できる" : L"戦闘中は合成できない",
		bx + bw / 2.0f, by + bh + 4.0f, 320.0f, 14.0f, D2D1::ColorF(0.75f, 0.75f, 0.75f), 20.0f);
	ui.text->End();

	if (!hid.empty()) DrawItemTooltip(ui, hid, mp);
}

