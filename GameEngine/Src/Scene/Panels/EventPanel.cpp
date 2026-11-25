#include "EventPanel.h"
#include "PlayerDataManager.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "CardTooltip.h"
#include "CardConfirm.h"
#include "ItemTooltip.h"
#include "MaterialDataBase.h"
#include "RelicManager.h"
#include "TextureManager.h"
#include "UiWindow.h"
#include "Audio.h"
#include "Se.h"
#include "ScreenShake.h"
#include "HpBar.h"
#include "Telemetry.h"
#include "GameUtils.h"
#include <cmath>

void EventPanel::Open(const std::string& id)
{
	m_open = true;
	m_id = id;
	m_def = EventDataBase::Roll(id);
	m_result = -1;
	m_pick = Pick::None;
}

// 選択肢の結果から、選んだ後に何を選ばせるか（events.json の outcome の type）
EventPanel::Pick EventPanel::PickOf(const EventChoice& c)
{
	for (auto& o : c.outcomes)
	{
		if (o.type == "removeCard")     return Pick::RemoveCard;
		if (o.type == "upgradeCard")    return Pick::UpgradeCard;
		if (o.type == "transformCard")  return Pick::TransformCard;
		if (o.type == "transformRelic") return Pick::TransformRelic;
	}
	return Pick::None;
}

// カード選択で、このカードを選べるか（強化：強化できるもの／削除・変化：外せるもの）
bool EventPanel::Pickable(Pick p, const std::string& id)
{
	if (p == Pick::UpgradeCard) return CardDataBase::CanUpgrade(id);
	if (p == Pick::RemoveCard || p == Pick::TransformCard) return CardDataBase::CanRemove(id);
	return true;
}

EventPanel::Request EventPanel::HandleInput(UiContext& ui)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	Request rq;
	HandleInputBody(ui, rq);
	rq.showCards.swap(m_showCards);   // 選択肢で手に入った呪いなど
	m_showCards.clear();
	return rq;
}

// 選択肢と結果／カード選択／レリック選択 を出し分け
void EventPanel::Draw(UiContext& ui)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	if (m_pick == Pick::None) DrawChoices(ui);
	else if (m_pick == Pick::TransformRelic) DrawRelicPicker(ui);
	else DrawCardPicker(ui);
}

// クリック（選択肢／結果画面で進む／カード・レリックを選ぶ）
void EventPanel::HandleInputBody(UiContext& ui, Request& rq)
{
	// カードピッカー中
	if (m_pick != Pick::None)
	{
		if (m_animIdx < 0 && ui.input->GetMouseButtonTrigger(0))
		{
			if (m_pick == Pick::TransformRelic)
			{
				int idx = RelicAt(ui.input->GetMousePos());
				if (idx >= 0)
				{
					Audio::PlaySE("Assets/Sound/se/click.mp3");
					std::string to = RelicManager::RandomDrop();   // 別レリック（未所持）
					if (!to.empty()) { m_animTo = to; m_animIdx = idx; m_animTimer = ANIM_DUR; }
					else { m_pick = Pick::None; m_result = m_pickChoice; }
				}
			}
			else
			{
				int idx = CardAt(ui.input->GetMousePos());
				if (idx >= 0)
				{
					Audio::PlaySE("Assets/Sound/se/click.mp3");
					auto& deck = PlayerDataManager::GetData().deck;
					std::string cur = deck[idx];
					auto start = [this, idx, cur]() {
						if (m_pick == Pick::RemoveCard) m_animTo = "";
						else if (m_pick == Pick::TransformCard) m_animTo = CardDataBase::RandomCard();
						else if (m_pick == Pick::UpgradeCard) m_animTo = CardDataBase::CanUpgrade(cur) ? (cur + "+") : cur;
						m_animIdx = idx;
						m_animTimer = ANIM_DUR;
						if (m_pick == Pick::UpgradeCard && m_animTo != cur) Se::Play(Se::Upgrade);   // イベントでの強化
						};
					if (m_pick == Pick::RemoveCard) CardConfirm::Open(ConfirmKind::Remove, cur, start);   // 確認してから
					else if (m_pick == Pick::UpgradeCard && CardDataBase::CanUpgrade(cur)) CardConfirm::Open(ConfirmKind::Upgrade, cur, start);
					else start();
				}
			}
		}
		return;
	}
	// 選択肢
	if (ui.input->GetMouseButtonTrigger(0))
	{
		if (m_result >= 0)
		{
			Audio::PlaySE("Assets/Sound/se/click.mp3");
			if (m_pending == Pending::Battle)
			{
				m_pending = Pending::None;
				m_open = false;
				rq.go = Request::Go::Battle; rq.param = m_battleParam;   // 戦闘へ（準備と切り替えは窓の外）
			}
			else if (m_pending == Pending::Shop)
			{
				m_pending = Pending::None;
				m_open = false;
				rq.go = Request::Go::Shop;
			}
			else if (m_pending == Pending::CardSelect)
			{
				m_pending = Pending::None;
				m_open = false;
				rq.go = Request::Go::CardSelect; rq.param = m_selectMode;
			}
			else if (m_pending == Pending::Treasure)
			{
				m_pending = Pending::None;
				m_id = "treasure_field";   // 財宝マスの中身を開く（呪いを払った後なので、デメリットなしで選ばせる）
				m_def = EventDataBase::Roll(m_id);
				m_result = -1;
				m_pick = Pick::None;
				// m_open は true のまま
			}
			else
			{
				m_open = false;
			}
		}
		else
		{
			const EventDef* e = Def();
			POINT m = ui.input->GetMousePos();
			if (e) for (int i = 0; i < (int)e->choices.size(); i++)
			{
				float x, y, w, h; ChoiceRect(i, x, y, w, h);
				if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h && ChoiceEnabled(e->choices[i]))
				{
					Audio::PlaySE("Assets/Sound/se/click.mp3");
					Telemetry::Instance().Log("event_choice", { {"event", m_id}, {"choice", i} });
					ApplyOutcomes(e->choices[i]);
					Pick pick = PickOf(e->choices[i]);
					if (pick != Pick::None) { m_pick = pick; m_pickChoice = i; m_pickScroll.Reset(); }
					else m_result = i;
					break;
				}
			}
		}
	}
}

// 毎フレーム：揺れ・表示HP・変わる演出・一覧のスクロール
void EventPanel::Update(UiContext& ui, float deltaTime)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	if (m_open)   // イベント中の揺れと表示HP（戦闘中は BattleScene が自前で回す）
	{
		ScreenShake::Update(deltaTime);
		if (m_hpDisplay >= 0.0f)
			m_hpDisplay = HpBar::StepDisplay(m_hpDisplay, PlayerDataManager::GetData().hp, deltaTime);
	}

	if (m_animIdx >= 0)
	{
		m_animTimer -= deltaTime;
		if (m_animTimer <= 0.0f)
		{
			if (m_pick == Pick::TransformRelic)
			{
				auto& relics = PlayerDataManager::GetData().relics;
				if (m_animIdx < (int)relics.size())
				{
					std::string oldId = relics[m_animIdx];
					PlayerDataManager::RemoveRelic(oldId);
					PlayerDataManager::AddRelic(m_animTo);
					m_gains.push_back({ "relic", m_animTo, 1 });   // 結果画面に「何に変わったか」を出す
				}
			}
			else
			{
				auto& deck = PlayerDataManager::GetData().deck;
				if (m_pick == Pick::RemoveCard) PlayerDataManager::RemoveCard(m_animIdx);
				else if (m_animIdx < (int)deck.size())
				{
					deck[m_animIdx] = m_animTo;
					PlayerDataManager::Save();
					m_gains.push_back({ "card", m_animTo, 1 });   // 変化・強化した後のカードを結果に出す
				}
			}
			m_animIdx = -1;
			m_pick = Pick::None;
			m_result = m_pickChoice;
		}
	}

	if (m_open && m_pick != Pick::None && m_pick != Pick::TransformRelic)   // イベントのカード選択：ホイールでスクロール
	{
		URect pn = CardPickerPanel();
		const float ch = CardVisual::CARD_H * 0.85f;
		int rows = ((int)PlayerDataManager::GetData().deck.size() + 7) / 8;
		m_pickScroll.top = 150.0f;
		m_pickScroll.bottom = pn.y + pn.h - 8.0f;
		float listH = rows * (ch + 16.0f);
		m_pickScroll.contentH = (listH <= m_pickScroll.bottom - m_pickScroll.top)
			? listH                          // 全部収まる時はスクロールさせない（最後の段が薄くならない）
			: listH + ScrollList::FADE;      // 収まらない時だけ、最後の段の下に薄くなる分の余白
		m_pickScroll.Wheel(Input::GetPendingWheel());   // シーンが消費する前に読む
		Input::ClearWheel();
		m_pickScroll.Update(deltaTime);
	}
}

void EventPanel::ChoiceRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 560.0f; h = 58.0f;   // 2段（行動／結果）が入る高さ
	x = m_sw / 2.0f - w / 2.0f;
	y = 280.0f + i * 72.0f;
}

void EventPanel::ApplyOutcomes(const EventChoice& c)
{
	auto& pd = PlayerDataManager::GetData();
	m_gains.clear();
	m_hpDisplay = -1.0f;
	for (auto& o : c.outcomes)
	{
		if (o.chance < 100 && rand() % 100 >= o.chance) continue;   // 確率つきの結果（省略＝100＝必ず）

		if (o.type == "hp")
		{
			int before = pd.hp;
			pd.hp += o.value; if (pd.hp > pd.maxHp) pd.hp = pd.maxHp; if (pd.hp < 0) pd.hp = 0;
			if (pd.hp != before) m_gains.push_back({ "hp", "", pd.hp - before });
			if (pd.hp < before)   // ダメージ：画面を揺らし、減った分をバーで徐々に縮める
			{
				m_hpDisplay = (float)before;
				ScreenShake::Add(ScreenShake::PowerForDamage(before - pd.hp));
			}
		}
		else if (o.type == "maxHp")
		{
			pd.maxHp += o.value; pd.hp += o.value; if (pd.hp < 1) pd.hp = 1;
			m_gains.push_back({ "maxHp", "", o.value });
		}
		else if (o.type == "gold")
		{
			int before = pd.gold;
			if (o.value > 0) PlayerDataManager::AddGold(o.value);
			else { pd.gold += o.value; if (pd.gold < 0) pd.gold = 0; }
			if (pd.gold != before) m_gains.push_back({ "gold", "", pd.gold - before });
		}
		else if (o.type == "material")
		{
			int n = o.value > 0 ? o.value : 1;
			PlayerDataManager::AddMaterial(o.param, n);
			m_gains.push_back({ "material", o.param, n });
		}
		else if (o.type == "randomMaterial") {
			std::string mid = MaterialDataBase::RandomId(o.value);   // value=層(0=全)
			if (!mid.empty()) { PlayerDataManager::AddMaterial(mid, 1); m_gains.push_back({ "material", mid, 1 }); }
		}
		else if (o.type == "relic")
		{
			std::string rid = o.param.empty() ? RelicManager::RandomUnowned("event")     // 空＝イベント用のレリック
				: o.param == "random" ? RelicManager::RandomDrop()                       // random＝普通のレリック（レア度ごとの確率）
				: (o.param == "common" || o.param == "uncommon" || o.param == "rare")
					? RelicManager::RandomUnowned(o.param)                               // レア度を指定してランダム
				: o.param;
			if (rid.empty()) rid = RelicManager::RandomDrop();
			if (!rid.empty()) { PlayerDataManager::AddRelic(rid); m_gains.push_back({ "relic", rid, 1 }); }
		}
		else if (o.type == "addCard")
		{
			std::string cid = o.param;
			if (cid == "randomRare")   // ランダムなレアカード（選ばせない。報酬と同じく、デッキとレリックの軸に寄る）
			{
				auto picks = CardDataBase::PickRewardCards(1, true, PlayerDataManager::GetData().deck, /*rareOnly*/true);
				cid = picks.empty() ? "" : picks[0];
			}
			if (!cid.empty())
			{
				PlayerDataManager::AddCard(cid);
				m_gains.push_back({ "card", cid, 1 });
				if (const CardData* d = CardDataBase::Get(cid); d && d->type == CardType::Curse)
					m_showCards.push_back(cid);   // 呪いは手に入る瞬間を見せる（HandleInput の戻り値で頼む）
			}
		}
		else if (o.type == "battle") { if (m_pending == Pending::None) { m_pending = Pending::Battle; m_battleParam = o.param; } }
		else if (o.type == "shop") { if (m_pending == Pending::None) m_pending = Pending::Shop; }
		else if (o.type == "treasure") { if (m_pending == Pending::None) m_pending = Pending::Treasure; }
		else if (o.type == "pickCard") { if (m_pending == Pending::None) { m_pending = Pending::CardSelect; m_selectMode = o.param; } }
	}
	PlayerDataManager::Save();
}

void EventPanel::DrawChoices(UiContext& ui)
{
	const EventDef* e = Def();
	if (!e) { m_open = false; return; }
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	POINT mp = ui.input->GetMousePos();
	int n = (int)e->choices.size();
	bool result = (m_result >= 0);
	URect p = PanelRect(n, result ? (int)m_gains.size() : 0);
	{   // ダメージを受けた時の揺れ（ScreenShakeはワールド単位なのでピクセルに換算）
		float sx, sz; ScreenShake::GetOffset(sx, sz);
		p.x += sx * 40.0f; p.y += sz * 40.0f;
	}
	float cx = p.x + p.w / 2.0f;
	const float bnW = 440.0f, bnH = 80.0f, bnY = p.y - 40.0f;   // タイトルの布バナー（窓の上端に乗せる）

	// 選択肢の文を「行動」と「結果（…）」に分ける（events.jsonは書き換えずに2段表示）
	auto splitLabel = [](const std::wstring& s, std::wstring& act, std::wstring& eff) {
		size_t k = s.find(L'（');
		if (k == std::wstring::npos) k = s.find(L'(');
		act = (k == std::wstring::npos) ? s : s.substr(0, k);
		eff = (k == std::wstring::npos) ? L"" : s.substr(k);
		};

	// ---- スプライト ----
	ui.sprite->Begin();
	ui.sprite->DrawSprite(white, 0, UiContext::BAR_H, (float)m_sw, (float)m_sh - UiContext::BAR_H, 0.0f,
		XMFLOAT4(0.05f, 0.03f, 0.09f, 0.96f));
	UiWindow::Draw(ui.sprite, white, p.x, p.y, p.w, p.h);
	ui.sprite->DrawSprite(TextureManager::Get("ui_banner"), cx - bnW / 2.0f, bnY, bnW, bnH, 0.0f, XMFLOAT4(1, 1, 1, 1));
	ui.sprite->DrawSprite(white, cx - 280.0f, p.y + 166.0f, 560.0f, 2.0f, 0.0f, XMFLOAT4(0.85f, 0.7f, 0.3f, 0.5f));   // 説明と選択肢の区切り

	if (m_result < 0)
	{
		for (int i = 0; i < n; i++)
		{
			float x, y, w, h; ChoiceRect(i, x, y, w, h);
			bool en = ChoiceEnabled(e->choices[i]);
			char key[16]; sprintf_s(key, "evc%d", i);
			bool hov = en && UiHover(x, y, w, h, mp, key, m_lastHoverKey);   // ホバーSE＋判定
			float yy = hov ? y - 4.0f : y;                                    // 浮く
			UiWindow::Draw(ui.sprite, white, x, yy, w, h);
			if (!en)
				ui.sprite->DrawSprite(white, x + 6, yy + 6, w - 12, h - 12, 0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.45f));   // 無効=暗く
			else if (hov)
				ui.sprite->DrawSprite(white, x + 6, yy + 6, w - 12, h - 12, 0.0f, XMFLOAT4(0.55f, 0.45f, 0.75f, 0.45f)); // ホバー強調
		}
	}
	else   // 結果：受け取った物の一覧＋「クリックで進む」のピル
	{
		for (int i = 0; i < (int)m_gains.size(); i++)
		{
			const auto& g = m_gains[i];
			URect r = GainRect(i, p);
			bool canHover = (g.kind == "material" || g.kind == "relic" || g.kind == "card");
			UiWindow::Draw(ui.sprite, white, r.x, r.y, r.w, r.h,
				canHover && r.has(mp) ? XMFLOAT4(1.0f, 0.95f, 0.75f, 1.0f) : XMFLOAT4(1, 1, 1, 1));
			float ix = r.x + 10.0f, iy = r.y + 5.0f, is = 34.0f;   // 左端のアイコン
			if (g.kind == "material")
				ui.sprite->DrawSprite(TextureManager::Get(MaterialDataBase::GetBase(g.id) ? "mat_core" : "mat_material"),
					ix, iy, is, is, 0.0f, XMFLOAT4(1, 1, 1, 1));
			else if (g.kind == "gold")
				ui.sprite->DrawSprite(TextureManager::Get("icon_gold"), ix, iy, is, is, 0.0f, XMFLOAT4(1.0f, 0.9f, 0.3f, 1));
			else if (g.kind == "hp" || g.kind == "maxHp")
				ui.sprite->DrawSprite(TextureManager::Get("icon_hp"), ix, iy, is, is, 0.0f, XMFLOAT4(1.0f, 0.5f, 0.5f, 1));
			else if (g.kind == "relic")
				if (const RelicDef* rd = RelicManager::Get(g.id))
					if (auto tex = TextureManager::Get(rd->icon))
						ui.sprite->DrawSprite(tex, ix, iy, is, is, 0.0f, RelicManager::RarityColor(rd->rarity));
			else if (g.kind == "card")
				if (const CardData* d = CardDataBase::Get(g.id))
				{
					const float s = 0.3f;   // 小さいカードの絵
					CardVisual::DrawBase(ui.sprite, white, ix + 2.0f - CardVisual::CARD_W * (1 - s) / 2.0f,
						r.y + 1.0f - CardVisual::CARD_H * (1 - s) / 2.0f, s, 0.0f, CardVisual::GetCardColor(d->type), d, 0.0f);
				}

			if (g.kind == "hp" && g.value < 0)   // 減ったHPをバトルと同じバーで（オレンジが徐々に縮む）
			{
				const auto& pdh = PlayerDataManager::GetData();
				HPBarInfo bar;
				bar.currentHP = pdh.hp; bar.maxHP = pdh.maxHp;
				bar.displayHP = (m_hpDisplay >= 0.0f) ? m_hpDisplay : (float)pdh.hp;
				HpBar::Draw(ui.sprite, white, r.x + 150.0f, r.y + 14.0f, r.w - 164.0f, 16.0f, bar, ui.time);
			}
		}
		UiWindow::Draw(ui.sprite, white, cx - 120.0f, p.y + p.h - 64.0f, 240.0f, 40.0f);
	}
	ui.sprite->End();

	// ---- 文字 ----
	ui.text->Begin();
	ui.text->DrawOutlinedTextCentered(ToWString(e->title).c_str(), cx, bnY, bnW, bnH, 28.0f,
		D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);

	if (m_result < 0)
	{
		ui.text->DrawTextCentered(ToWString(e->desc).c_str(), cx, p.y + 60.0f, 560.0f, 19.0f,
			D2D1::ColorF(0.92f, 0.92f, 0.92f), 90.0f);
		for (int i = 0; i < n; i++)
		{
			float x, y, w, h; ChoiceRect(i, x, y, w, h);
			bool en = ChoiceEnabled(e->choices[i]);
			bool hov = en && mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h;
			float yy = hov ? y - 4.0f : y;
			std::wstring act, eff; splitLabel(ToWString(e->choices[i].label), act, eff);
			if (eff.empty())   // 結果の書き無し＝1段で中央
				ui.text->DrawText(act.c_str(), x + 18.0f, yy + 17.0f, 20.0f,
					en ? D2D1::ColorF(1, 1, 1) : D2D1::ColorF(0.4f, 0.4f, 0.4f));
			else
			{
				ui.text->DrawText(act.c_str(), x + 18.0f, yy + 7.0f, 20.0f,
					en ? D2D1::ColorF(1, 1, 1) : D2D1::ColorF(0.4f, 0.4f, 0.4f));
				ui.text->DrawText(eff.c_str(), x + 22.0f, yy + 33.0f, 14.0f,
					en ? D2D1::ColorF(1.0f, 0.85f, 0.45f) : D2D1::ColorF(0.4f, 0.4f, 0.4f));
			}
		}
	}
	else
	{
		ui.text->DrawTextCentered(ToWString(e->choices[m_result].result).c_str(), cx, p.y + 60.0f,
			560.0f, 21.0f, D2D1::ColorF(0.8f, 1.0f, 0.8f), 90.0f);
		for (int i = 0; i < (int)m_gains.size(); i++)
		{
			const auto& g = m_gains[i];
			URect r = GainRect(i, p);
			std::wstring label, tag;
			D2D1::ColorF col(1, 1, 1);
			std::wstring sign = (g.value >= 0) ? L"+" : L"";
			if (g.kind == "gold") { label = sign + std::to_wstring(g.value) + L" ゴールド"; col = D2D1::ColorF(1.0f, 0.9f, 0.3f); }
			else if (g.kind == "hp") { label = sign + std::to_wstring(g.value) + L" HP"; col = g.value >= 0 ? D2D1::ColorF(0.5f, 1.0f, 0.5f) : D2D1::ColorF(1.0f, 0.45f, 0.45f); }
			else if (g.kind == "maxHp") { label = sign + std::to_wstring(g.value) + L" 最大HP"; col = D2D1::ColorF(1.0f, 0.6f, 0.6f); }
			else if (g.kind == "card") { const CardData* d = CardDataBase::Get(g.id); label = d ? d->name : ToWString(g.id); tag = L"カード"; }
			else { std::wstring ds; ItemTooltip::Info(g.id, label, ds, tag); if (g.value > 1) label += L" ×" + std::to_wstring(g.value); }
			ui.text->DrawText(label.c_str(), r.x + 54.0f, r.y + 11.0f, 18.0f, col);
			if (!tag.empty())
				ui.text->DrawText(tag.c_str(), r.x + r.w - 72.0f, r.y + 14.0f, 13.0f, D2D1::ColorF(0.7f, 0.8f, 1.0f));
			if (g.kind == "hp" && g.value < 0)   // バーの上に今のHP
			{
				const auto& pdh = PlayerDataManager::GetData();
				wchar_t hb[32]; swprintf_s(hb, L"%d / %d", pdh.hp, pdh.maxHp);
				ui.text->DrawOutlinedTextCentered(hb, r.x + 150.0f + (r.w - 164.0f) / 2.0f, r.y + 14.0f, r.w - 164.0f, 16.0f,
					13.0f, D2D1::ColorF(1, 1, 1), D2D1::ColorF(0, 0, 0), 1.5f);
			}
		}
		ui.text->DrawTextCentered(L"クリックで進む", cx, p.y + p.h - 64.0f, 240.0f, 17.0f,
			D2D1::ColorF(0.9f, 0.9f, 0.85f), 40.0f);
	}
	ui.text->End();
	// 受け取った物の説明（ホバー）：素材・レリックはゲーム中と同じ説明窓、カードは拡大＋キーワード
	if (result)
		for (int i = 0; i < (int)m_gains.size(); i++)
		{
			const auto& g = m_gains[i];
			URect r = GainRect(i, p);
			if (!r.has(mp)) continue;
			if (g.kind == "material" || g.kind == "relic")
				ItemTooltip::Draw(ui.sprite, ui.text, white, g.id, r.x + r.w + 12.0f, r.y, m_sw, m_sh);
			else if (g.kind == "card")
				if (const CardData* d = CardDataBase::Get(g.id))
				{
					const float s = 1.4f;
					float w = CardVisual::CARD_W * s, h = CardVisual::CARD_H * s;
					float tx = r.x + r.w + 16.0f, ty = r.y + r.h / 2.0f - h / 2.0f;
					if (tx + w > m_sw - 6.0f) tx = r.x - 16.0f - w;   // 右に入らなければ左
					if (ty < 46.0f) ty = 46.0f;
					float bx = tx - CardVisual::CARD_W * (1 - s) / 2.0f, by = ty - CardVisual::CARD_H * (1 - s) / 2.0f;
					ui.sprite->Begin();  ui.DrawCard(d, bx, by, s, false); ui.sprite->End();
					ui.text->Begin(); ui.DrawCard(d, bx, by, s, true); ui.text->End();
					CardTooltip::Draw(ui.sprite, ui.text, white, d, tx + w / 2.0f, ty, w, h, m_sw, m_sh);
				}
			break;
		}

	// 選択肢で手に入るカードを、窓の右に実物で見せる（隣の文字より手前に出すので最後に描く）
	if (m_result < 0)
		for (int i = 0; i < n; i++)
		{
			float x, y, w, h; ChoiceRect(i, x, y, w, h);
			if (!ChoiceEnabled(e->choices[i]) || mp.x < x || mp.x > x + w || mp.y < y || mp.y > y + h) continue;

			std::vector<const CardData*> cards;
			for (auto& o : e->choices[i].outcomes)
				if (o.type == "addCard")
					if (const CardData* d = CardDataBase::Get(o.param)) cards.push_back(d);
			if (cards.empty()) break;

			const float s = 1.4f, cw = CardVisual::CARD_W * s, ch = CardVisual::CARD_H * s, gap = 12.0f;
			float tx = p.x + p.w + 16.0f, ty = y + h / 2.0f - ch / 2.0f;
			if (ty < 46.0f) ty = 46.0f;
			bool curse = false;
			for (int k = 0; k < (int)cards.size(); k++)
			{
				float bx = tx + k * (cw + gap) - CardVisual::CARD_W * (1 - s) / 2.0f;
				float by = ty - CardVisual::CARD_H * (1 - s) / 2.0f;
				ui.sprite->Begin();  ui.DrawCard(cards[k], bx, by, s, false); ui.sprite->End();
				ui.text->Begin(); ui.DrawCard(cards[k], bx, by, s, true); ui.text->End();
				if (cards[k]->type == CardType::Curse) curse = true;
			}
			ui.text->Begin();
			ui.text->DrawOutlinedTextCentered(curse ? L"呪いが加わる" : L"デッキに加わる",
				tx + cw / 2.0f, ty - 30.0f, cw + 40.0f, 24.0f, 16.0f,
				curse ? D2D1::ColorF(1.0f, 0.45f, 0.45f) : D2D1::ColorF(1.0f, 0.9f, 0.6f), D2D1::ColorF(0, 0, 0), 1.5f);
			ui.text->End();
			break;
		}
}

void EventPanel::CardSlot(int i, float& x, float& y) const
{
	const float s = 0.85f;
	float cw = CardVisual::CARD_W * s, ch = CardVisual::CARD_H * s;
	int perRow = 8; float gapX = 12.0f, gapY = 16.0f;
	float startX = (m_sw - (perRow * cw + (perRow - 1) * gapX)) / 2.0f;
	x = startX + (i % perRow) * (cw + gapX);
	y = 150.0f + (i / perRow) * (ch + gapY) - m_pickScroll.pos;   // スクロール分ずらす
}

URect EventPanel::CardPickerPanel() const
{
	const float s = 0.85f, cw = CardVisual::CARD_W * s, ch = CardVisual::CARD_H * s;
	int n = (int)PlayerDataManager::GetData().deck.size();
	int rows = (n + 7) / 8; if (rows < 1) rows = 1;     // 1行8枚（GetEventCardSlotと同じ）
	float gridW = 8 * cw + 7 * 12.0f;
	float h = rows * (ch + 16.0f) + 32.0f, maxH = m_sh - 126.0f - 14.0f;   // 画面に収まらない分はスクロール
	return { (m_sw - gridW) / 2.0f - 24.0f, 126.0f, gridW + 48.0f, h < maxH ? h : maxH };
}

int EventPanel::CardAt(POINT p) const
{
	auto& deck = PlayerDataManager::GetData().deck;
	for (int i = 0; i < (int)deck.size(); i++)
	{
		if (!Pickable(m_pick, deck[i])) continue;   // 強化できない／外せないカードは選べない
		float bx, by; CardSlot(i, bx, by);
		float x, y, w, h; CardVisual::GetRect(bx, by, 0.85f, x, y, w, h);
		if (m_pickScroll.AlphaOfRect(y, h) < 0.5f) continue;   // 端で薄くなっているカードは押せない
		if (p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h) return i;
	}
	return -1;
}
void EventPanel::DrawCardPicker(UiContext& ui)
{
	auto& deck = PlayerDataManager::GetData().deck;
	auto pickAlpha = [&](float bx, float by) {   // スクロールの端で薄くする濃さ
		float x, y, w, h; CardVisual::GetRect(bx, by, 0.85f, x, y, w, h);
		return m_pickScroll.AlphaOfRect(y, h); };
	bool anim = (m_animIdx >= 0);
	int hov = anim ? -1 : CardAt(ui.input->GetMousePos());
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float t = anim ? (1.0f - m_animTimer / ANIM_DUR) : 0.0f;
	bool morph = anim && !m_animTo.empty();

	const CardData* animCard = nullptr;
	float animScale = 0.85f, animAlpha = 1.0f, animYoff = 0.0f, animBX = 0, animBY = 0;
	float fxT = 0.0f; bool presenting = false;   // fxT=エフェクト段階の進行 / presenting=前に見せてる段階
	if (anim)
	{
		float sbx, sby; CardSlot(m_animIdx, sbx, sby);
		float pbx = m_sw / 2.0f - CardVisual::CARD_W / 2.0f;
		float pby = m_sh / 2.0f - CardVisual::CARD_H / 2.0f;
		const float P1 = 0.18f, P2 = 0.40f;   // 旧カードを見せる時間を短く
		if (t < P1)
		{
			presenting = true;
			float u = t / P1, e = u * u * (3.0f - 2.0f * u);
			animBX = sbx + (pbx - sbx) * e; animBY = sby + (pby - sby) * e;
			animScale = 0.85f + (1.3f - 0.85f) * e; animAlpha = 1.0f;
			if (m_animIdx < (int)deck.size()) animCard = CardDataBase::Get(deck[m_animIdx]);
		}
		else if (t < P2)   // ← ホールド（中央で拡大したまま見せる）
		{
			presenting = true;
			animBX = pbx; animBY = pby; animScale = 1.3f; animAlpha = 1.0f;
			if (m_animIdx < (int)deck.size()) animCard = CardDataBase::Get(deck[m_animIdx]);
		}
		else
		{
			fxT = (t - P2) / (1.0f - P2);
			animBX = pbx; animBY = pby; animScale = 1.3f;
			if (!morph)
			{
				if (m_animIdx < (int)deck.size()) animCard = CardDataBase::Get(deck[m_animIdx]);
				animScale = 1.3f * (1.0f - fxT * 0.4f); animAlpha = 1.0f - fxT; animYoff = -fxT * 40.0f;
			}
			else if (fxT < 0.2f)   // 旧カードが消える
			{
				if (m_animIdx < (int)deck.size()) animCard = CardDataBase::Get(deck[m_animIdx]);
				animAlpha = 1.0f - fxT / 0.2f;
			}
			else if (fxT < 0.32f)   // 新カードが現れる
			{
				animCard = CardDataBase::Get(m_animTo);
				animAlpha = (fxT - 0.2f) / 0.12f;
			}
			else                    // 変化後
			{
				animCard = CardDataBase::Get(m_animTo);
				animAlpha = 1.0f;
			}
		}
	}

	ui.sprite->Begin();
	ui.sprite->DrawSprite(white, 0, UiContext::BAR_H, (float)m_sw, (float)m_sh - UiContext::BAR_H, 0.0f, XMFLOAT4(0.05f, 0.03f, 0.09f, 0.96f));
	URect pn = CardPickerPanel();
	UiWindow::Draw(ui.sprite, white, pn.x, pn.y, pn.w, pn.h);   // 一覧を囲む窓
	if (!anim)                                                     // 見出しの布バナー
		ui.sprite->DrawSprite(TextureManager::Get("ui_banner"), m_sw / 2.0f - 240.0f, pn.y - 86.0f,
			480.0f, 84.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));
	for (int i = 0; i < (int)deck.size(); i++)
	{
		if (i == m_animIdx) continue;
		const CardData* d = CardDataBase::Get(deck[i]);
		if (!d) continue;
		float bx, by; CardSlot(i, bx, by);
		float a = pickAlpha(bx, by); if (a <= 0.0f) continue;   // 見せる範囲の外
		by -= HoverLift::Get(bx, by, i == hov, 12.0f);
		if (i == hov)   // ホバー：色は変えず、浮かせて金の縁
		{
			float rx, ry, rw, rh; CardVisual::GetRect(bx, by, 0.85f, rx, ry, rw, rh);
			UiWindow::Frame(ui.sprite, white, rx, ry, rw, rh, XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f));
		}
		XMFLOAT4 col = CardVisual::GetCardColor(d->type); col.w = a;
		CardVisual::DrawBase(ui.sprite, white, bx, by, 0.85f, 0.0f, col, d, ui.time);
		if (!Pickable(m_pick, deck[i]))   // 選べないカードは暗く
		{
			float rx, ry, rw, rh; CardVisual::GetRect(bx, by, 0.85f, rx, ry, rw, rh);
			ui.sprite->DrawSprite(white, rx, ry, rw, rh, 0.0f, XMFLOAT4(0, 0, 0, 0.55f * a));
		}
	}
	ui.sprite->End();

	ui.text->Begin();
	if (!anim)
	{
		std::wstring prompt = L"カードを選択";
		if (m_pick == Pick::RemoveCard) prompt = L"削除するカードを選択";
		else if (m_pick == Pick::UpgradeCard) prompt = L"強化するカードを選択";
		else if (m_pick == Pick::TransformCard) prompt = L"変化させるカードを選択";
		ui.text->DrawOutlinedTextCentered(prompt.c_str(), m_sw / 2.0f, pn.y - 86.0f, 480.0f, 84.0f, 26.0f,
			D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);
	}
	for (int i = 0; i < (int)deck.size(); i++)
	{
		if (i == m_animIdx) continue;
		const CardData* d = CardDataBase::Get(deck[i]);
		if (!d) continue;
		float bx, by; CardSlot(i, bx, by);
		float a = pickAlpha(bx, by); if (a <= 0.0f) continue;
		by -= HoverLift::Get(bx, by, i == hov, 12.0f);
		CardVisual::DrawTexts(ui.text, d, nullptr, bx, by, 0.85f, 0.0f, a);
	}
	ui.text->End();

	// 提示カードは最後に「本体→文字」まとめて描く（背後の文字より後＝被った所だけ隠れる）
	if (anim && animCard)
	{
		ui.sprite->Begin();
		XMFLOAT4 col = CardVisual::GetCardColor(animCard->type); col.w = animAlpha;
		CardVisual::DrawBase(ui.sprite, white, animBX, animBY + animYoff, animScale, 0.0f, col, animCard, ui.time);
		if (!presenting && morph)
		{
			float flash = 1.0f - fabsf(fxT - 0.5f) * 4.0f;
			if (flash > 0.0f) { float x, y, w, h; CardVisual::GetRect(animBX, animBY, animScale, x, y, w, h); ui.sprite->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(1, 1, 1, flash * 0.8f)); }
		}
		else if (!presenting)
		{
			float x, y, w, h; CardVisual::GetRect(animBX, animBY + animYoff, animScale, x, y, w, h);
			ui.sprite->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(0.95f, 0.2f, 0.2f, animAlpha * 0.6f));
		}
		ui.sprite->End();

		ui.text->Begin();
		CardVisual::DrawTexts(ui.text, animCard, nullptr, animBX, animBY + animYoff, animScale, 0.0f, animAlpha);
		ui.text->End();
	}

	if (!anim && hov >= 0 && hov < (int)deck.size())
	{
		const CardData* d = CardDataBase::Get(deck[hov]);
		float bx, by; CardSlot(hov, bx, by);
		float rx, ry, rw, rh; CardVisual::GetRect(bx, by - 12.0f, 0.85f, rx, ry, rw, rh);
		CardTooltip::Draw(ui.sprite, ui.text, white, d,
			rx + rw / 2.0f, ry, rw, rh, m_sw, m_sh, pn.y);   // 見出しのバナーより上には出さない

		// 強化：強化後の姿を横に並べて見せる（右に入らなければ左）
		if (m_pick == Pick::UpgradeCard)
			if (const CardData* up = CardDataBase::Get(deck[hov] + "+"))
			{
				bool right = (rx + rw + 34.0f + CardVisual::CARD_W <= m_sw - 6.0f);
				float px = right ? rx + rw + 34.0f : rx - 34.0f - CardVisual::CARD_W;
				float py = ry - 8.0f;
				ui.sprite->Begin(); ui.DrawCard(up, px, py, 1.0f, false); ui.sprite->End();
				ui.text->Begin();
				ui.DrawCard(up, px, py, 1.0f, true);
				ui.text->DrawOutlinedTextCentered(right ? L"▶" : L"◀", right ? rx + rw + 17.0f : rx - 17.0f,
					ry + rh / 2.0f - 14.0f, 30.0f, 28.0f, 22.0f,
					D2D1::ColorF(1.0f, 0.85f, 0.3f), D2D1::ColorF(0, 0, 0), 2.0f);
				ui.text->End();
			}
	}
}

bool EventPanel::ChoiceEnabled(const EventChoice& c) const
{
	int need = 0;
	for (auto& o : c.outcomes)
		if (o.type == "gold" && o.value < 0) need += -o.value;
	for (auto& o : c.outcomes)
		if (o.type == "transformRelic" && PlayerDataManager::GetData().relics.empty()) return false;
	return PlayerDataManager::GetData().gold >= need;
}

URect EventPanel::RelicPanel() const
{
	int n = (int)PlayerDataManager::GetData().relics.size();
	int rows = (n + 5) / 6; if (rows < 1) rows = 1;   // 1行6個（EventRelicRectと同じ）
	URect r0 = RelicRect(0);
	float gridW = 6 * r0.w + 5 * 14.0f;
	return { r0.x - 24.0f, r0.y - 24.0f, gridW + 48.0f, rows * (r0.h + 14.0f) - 14.0f + 48.0f };
}

int EventPanel::RelicAt(POINT p) const
{
	int n = (int)PlayerDataManager::GetData().relics.size();
	for (int i = 0; i < n; i++)
		if (RelicRect(i).has(p)) return i;
	return -1;
}

void EventPanel::DrawRelicPicker(UiContext& ui)
{
	auto& relics = PlayerDataManager::GetData().relics;
	int n = (int)relics.size();
	bool anim = (m_animIdx >= 0);
	int hov = anim ? -1 : RelicAt(ui.input->GetMousePos());
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float t = anim ? (1.0f - m_animTimer / ANIM_DUR) : 0.0f;
	URect pn = RelicPanel();
	const float bnW = 480.0f, bnH = 84.0f, bnX = m_sw / 2.0f - bnW / 2.0f, bnY = pn.y - 86.0f;

	// 入れ替え中の枠：前半＝元のレリックが消える／後半＝新しいレリックが現れる
	auto slotInfo = [&](int i, std::string& id, float& a) {
		id = relics[i]; a = 1.0f;
		if (i == m_animIdx)
		{
			if (t < 0.5f) a = 1.0f - t * 2.0f;
			else { a = (t - 0.5f) * 2.0f; id = m_animTo; }
		}
		};

	// ---- スプライト ----
	ui.sprite->Begin();
	ui.sprite->DrawSprite(white, 0, UiContext::BAR_H, (float)m_sw, (float)m_sh - UiContext::BAR_H, 0.0f, XMFLOAT4(0.05f, 0.03f, 0.09f, 0.96f));
	UiWindow::Draw(ui.sprite, white, pn.x, pn.y, pn.w, pn.h);
	if (!anim)
		ui.sprite->DrawSprite(TextureManager::Get("ui_banner"), bnX, bnY, bnW, bnH, 0.0f, XMFLOAT4(1, 1, 1, 1));
	for (int i = 0; i < n; i++)
	{
		URect r = RelicRect(i);
		std::string id; float a; slotInfo(i, id, a);
		float y = (i == hov) ? r.y - 6.0f : r.y;
		UiWindow::Draw(ui.sprite, white, r.x, y, r.w, r.h);
		if (i == hov) UiWindow::Frame(ui.sprite, white, r.x, y, r.w, r.h, XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f));

		const RelicDef* d = RelicManager::Get(id);
		if (!d) continue;
		auto tex = TextureManager::Get(d->icon);
		if (!tex) continue;
		const float is = 64.0f;
		float ix = r.x + (r.w - is) / 2.0f, iy = y + 14.0f;
		XMFLOAT4 c = RelicManager::RarityColor(d->rarity); c.w = a;
		ui.sprite->DrawSprite(tex, ix + 2.0f, iy + 2.0f, is, is, 0.0f, XMFLOAT4(0, 0, 0, 0.65f * a));   // 影
		ui.sprite->DrawSprite(tex, ix, iy, is, is, 0.0f, c);
		if (i == m_animIdx)   // 入れ替わる瞬間に白く光って膨らむ
		{
			float flash = 1.0f - fabsf(t - 0.5f) * 4.0f;
			if (flash > 0.0f)
			{
				float g = is * (1.0f + 0.4f * flash);
				ui.sprite->DrawSprite(tex, ix + (is - g) / 2.0f, iy + (is - g) / 2.0f, g, g, 0.0f, XMFLOAT4(1, 1, 1, flash));
			}
		}
	}
	ui.sprite->End();

	// ---- 文字 ----
	ui.text->Begin();
	if (!anim)
		ui.text->DrawOutlinedTextCentered(L"入れ替えるレリックを選ぶ", bnX + bnW / 2.0f, bnY, bnW, bnH, 26.0f,
			D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);
	for (int i = 0; i < n; i++)
	{
		URect r = RelicRect(i);
		std::string id; float a; slotInfo(i, id, a);
		const RelicDef* d = RelicManager::Get(id);
		float y = (i == hov) ? r.y - 6.0f : r.y;
		ui.text->DrawTextCentered((d ? ToWString(d->name) : L"?").c_str(), r.x + r.w / 2.0f, y + 86.0f,
			r.w - 12.0f, 15.0f, D2D1::ColorF(1, 1, 1, a), 24.0f);
	}
	ui.text->End();

	// ---- ホバー中の説明（タイルの下に、ゲーム中と同じ説明窓） ----
	if (hov >= 0)
	{
		URect r = RelicRect(hov);
		ItemTooltip::Draw(ui.sprite, ui.text, white, relics[hov], r.x, r.y + r.h + 8.0f, m_sw, m_sh);
	}
}

