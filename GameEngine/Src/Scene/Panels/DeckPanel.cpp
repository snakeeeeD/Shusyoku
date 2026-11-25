#include "DeckPanel.h"
#include "PlayerDataManager.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "CardTooltip.h"
#include "CardConfirm.h"
#include "TextureManager.h"
#include "UiWindow.h"
#include "Audio.h"
#include "Se.h"

void DeckPanel::Open(Mode mode)
{
	m_open = true;
	m_mode = mode;
	m_scroll = 0.0f; m_scrollTarget = 0.0f;
}

void DeckPanel::Close()
{
	m_open = false;
	m_mode = Mode::View;
	m_previewIdx = -1;
}

std::string DeckPanel::TakeUpgraded()
{
	std::string s;
	s.swap(m_upgraded);
	return s;
}

// 「強化する」：拡大した2枚の下（描画とクリックで同じ矩形）
URect DeckPanel::UpgradeBtnRect() const
{
	const float chh = CardVisual::CARD_H * 2.0f;   // 拡大表示の倍率 2.0
	float y0 = m_sh / 2.0f - chh / 2.0f;
	const float bW = 200.0f, bH = 44.0f;
	return { m_sw / 2.0f - bW / 2.0f, y0 + chh - 30.0f, bW, bH };
}

// クリック・ホイール。戻る（backHit）で閉じたら true
bool DeckPanel::HandleInput(UiContext& ui, POINT m, bool clickRel, bool backHit)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	m_scrollTarget -= ui.input->GetMouseWheelDelta() * 0.5f;
	const float OVER = 60.0f;
	float maxS = DeckMaxScroll();
	if (m_scrollTarget < 0.0f)        m_scrollTarget = 0.0f;
	if (m_scrollTarget > maxS + OVER) m_scrollTarget = maxS + OVER;
	if (clickRel)
	{
		// 強化後表示トグル（デッキ閲覧のみ・拡大中でない時）
		float ubX = 20.0f, ubY = m_sh - 46.0f, ubW = 190.0f, ubH = 34.0f;
		if (m_previewIdx < 0 &&
			m.x >= ubX && m.x <= ubX + ubW && m.y >= ubY && m.y <= ubY + ubH)
		{
			m_showUpgrade = !m_showUpgrade; Audio::PlaySE("Assets/Sound/se/click.mp3"); return false;
		}

		if (backHit)
		{
			Audio::PlaySE("Assets/Sound/se/click.mp3");
			Close();
			return true;   // 休憩から来ていたら休憩へ戻すのは外
		}

		if (IsUpgrade())
		{
			if (m_previewIdx >= 0)   // 拡大中：強化ボタン or 閉じる
			{
				std::string baseId = PlayerDataManager::GetData().deck[m_previewIdx];
				if (CardDataBase::CanUpgrade(baseId))
				{
					if (UpgradeBtnRect().has(m))
					{
						PlayerDataManager::UpgradeCard(m_previewIdx);
						const std::string& after = PlayerDataManager::GetData().deck[m_previewIdx];
						if (after != baseId) { m_upgraded = after; Se::Play(Se::Upgrade); }   // 出現演出は外（TakeUpgraded）
						m_previewIdx = -1;
						return false;
					}
				}
				m_previewIdx = -1; return false;   // ボタン外で閉じる
			}
			int idx = GetDeckCardAt(m);
			if (idx >= 0)
			{
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				std::string baseId = PlayerDataManager::GetData().deck[idx];
				if (!CardDataBase::CanUpgrade(baseId)) { m_previewIdx = idx; return false; }   // 強化できないカードは今まで通り拡大
				CardConfirm::Open(ConfirmKind::Upgrade, baseId, [this, idx, baseId]() {   // 強化前と強化後を見せて確認
					auto& deck = PlayerDataManager::GetData().deck;
					if (idx >= (int)deck.size() || deck[idx] != baseId) return;
					PlayerDataManager::UpgradeCard(idx);
					const std::string& after = PlayerDataManager::GetData().deck[idx];
					if (after != baseId) { m_upgraded = after; Se::Play(Se::Upgrade); }   // 出現演出は外（TakeUpgraded）
					});
				return false;
			}
			return false;
		}

		// 通常デッキ：拡大中はクリックで閉じる（ボタンなし）
		if (m_previewIdx >= 0) { m_previewIdx = -1; return false; }
		int idx = GetDeckCardAt(m);
		if (idx >= 0) { m_previewIdx = idx; m_previewShowUpgrade = m_showUpgrade; Audio::PlaySE("Assets/Sound/se/click.mp3"); return false; }
	}
	return false;
}

// スクロールの補間（シーンがホイールを使う前に読む）
void DeckPanel::Update(UiContext& ui, float deltaTime)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	if (m_open)
	{
		m_scrollTarget -= Input::GetPendingWheel() * 0.5f;   // シーンが消費する前に読む（1回だけ）
		Input::ClearWheel();
		float maxS = DeckMaxScroll();
		if (m_scrollTarget < 0.0f)      m_scrollTarget = 0.0f;
		else if (m_scrollTarget > maxS) m_scrollTarget = maxS;
		m_scroll += (m_scrollTarget - m_scroll) * min(1.0f, 15.0f * deltaTime);
	}
}

// 暗幕・一覧・「強化後表示」。帯の描画のまとまり（スプライト）の中から呼ばれる
void DeckPanel::DrawGrid(UiContext& ui)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	ui.sprite->DrawSprite(white, 0.0f, UiContext::BAR_H, (float)m_sw, (float)m_sh - UiContext::BAR_H,
		0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.8f));          // 暗幕
	DrawCards(ui, false);

	// 強化後表示トグル
	float ubX = 20.0f, ubY = m_sh - 46.0f, ubW = 190.0f, ubH = 34.0f;
	UiWindow::Button(ui.sprite, white, ubX, ubY, ubW, ubH,
		m_showUpgrade ? XMFLOAT4(0.30f, 0.55f, 0.30f, 1.0f)
		: XMFLOAT4(0.30f, 0.30f, 0.35f, 1.0f));

	ui.text->Begin();

		DrawCards(ui, true);
		ui.text->DrawText(m_showUpgrade ? L"強化後表示: ON" : L"強化後表示: OFF",
			36.0f, m_sh - 38.0f, 18.0f, D2D1::ColorF(1, 1, 1));

	ui.text->End();
}

// 拡大表示・ホバーの説明（一番手前）
void DeckPanel::DrawTop(UiContext& ui, bool showTooltip)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	if (!m_open) return;
	DrawPreview(ui);   // 全グリッド描画の後＝最前面

	// デッキのキーワード解説（パス外で描く）
	if (m_previewIdx < 0 && showTooltip)   // 強化の演出中は出さない
	{
		int hk = DeckHoverIndex(ui);
		auto vis = VisibleDeckIndices();
		for (int slot = 0; hk >= 0 && slot < (int)vis.size(); slot++)
			if (vis[slot] == hk)
			{
				const CardData* d = CardDataBase::Get(PlayerDataManager::GetData().deck[hk]);
				float bx, by; GetDeckCardBase(slot, bx, by);
				float rx, ry, rw, rh; CardVisual::GetRect(bx, by - 10.0f, DECK_SCALE, rx, ry, rw, rh);
				CardTooltip::Draw(ui.sprite, ui.text, TextureManager::Get("white"), d,
					rx + rw / 2.0f, ry, rw, rh, m_sw, m_sh);
				break;
			}
	}
}

// デッキ確認のホバー中のカード。説明のウィンドウの上にいる間は、隣のカードに切り替えない
int DeckPanel::DeckHoverIndex(UiContext& ui)
{
	if (CardTooltip::MouseInPanel()) return m_tipIdx;
	m_tipIdx = GetDeckCardAt(ui.input->GetMousePos());
	return m_tipIdx;
}

void DeckPanel::DrawCards(UiContext& ui, bool textPass)
{
	auto& deck = PlayerDataManager::GetData().deck;
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	auto vis = VisibleDeckIndices();

	int hovIdx = (m_previewIdx >= 0) ? -1 : DeckHoverIndex(ui); // ホバー中のデッキindex

	if (!textPass && hovIdx != m_hoveredPrev)           // SEは1回だけ（spriteパス）
	{
		if (hovIdx >= 0) Audio::PlaySE("Assets/Sound/se/hover.mp3");
		m_hoveredPrev = hovIdx;
	}

	for (int slot = 0; slot < (int)vis.size(); slot++)
	{
		std::string cid = deck[vis[slot]];
		if (m_showUpgrade && CardDataBase::CanUpgrade(cid)) cid += "+";   // ← トグルONなら+版
		const CardData* d = CardDataBase::Get(cid);
		if (!d) continue;
		float bx, by; GetDeckCardBase(slot, bx, by);
		by -= HoverLift::Get(bx, by, vis[slot] == hovIdx, 10.0f);   // ← ホバーで浮く（滑らかに）

		if (!textPass)
		{
			XMFLOAT4 col = CardVisual::GetCardColor(d->type);
			CardVisual::DrawBase(ui.sprite, white, bx, by, DECK_SCALE, 0.0f, col, d, ui.time);
		}
		else
			CardVisual::DrawTexts(ui.text, d, nullptr, bx, by, DECK_SCALE, 0.0f, 1.0f);
	}
}

void DeckPanel::DrawPreview(UiContext& ui)
{
	auto& deck = PlayerDataManager::GetData().deck;
	if (m_previewIdx < 0 || m_previewIdx >= (int)deck.size()) return;

	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	const float PS = 2.0f;
	float cw = CardVisual::CARD_W * PS, chh = CardVisual::CARD_H * PS;
	std::string baseId = deck[m_previewIdx];
	bool canUp = CardDataBase::CanUpgrade(baseId);

	// 休憩：クリックで 現在→強化後（横並び）＋「強化する」
	if (IsUpgrade() && canUp)
	{
		const CardData* cur = CardDataBase::Get(baseId);
		const CardData* upd = CardDataBase::Get(baseId + "+");
		float gap = 150.0f, x0 = m_sw / 2.0f - (cw * 2 + gap) / 2.0f, y0 = m_sh / 2.0f - chh / 2.0f;
		URect ub = UpgradeBtnRect();   // クリックと同じ矩形
		float bW = ub.w, bH = ub.h, bX = ub.x, bY = ub.y;
		ui.sprite->Begin();
		ui.sprite->DrawSprite(white, 0, 0, (float)m_sw, (float)m_sh, 0.0f, XMFLOAT4(0, 0, 0, 0.7f));
		ui.DrawCard(cur, x0, y0, PS, false); ui.DrawCard(upd, x0 + cw + gap, y0, PS, false);
		// 強化する ボタン（カード下寄り、ホバーで浮く＋SE）
		POINT mp = ui.input->GetMousePos();
		bool bHov = (mp.x >= bX && mp.x <= bX + bW && mp.y >= bY && mp.y <= bY + bH);
		if (bHov && !m_upgradeBtnHov) Audio::PlaySE("Assets/Sound/se/hover.mp3");
		m_upgradeBtnHov = bHov;
		float bDy = -HoverLift::Get(bX, bY, bHov, 4.0f);
		UiWindow::Button(ui.sprite, white, bX, bY + bDy, bW, bH,
			bHov ? XMFLOAT4(0.70f, 0.58f, 0.28f, 1.0f) : XMFLOAT4(0.55f, 0.45f, 0.20f, 1.0f));
		ui.sprite->End();

		ui.text->Begin();
		ui.DrawCard(cur, x0, y0, PS, true); ui.DrawCard(upd, x0 + cw + gap, y0, PS, true);
		ui.text->DrawOutlinedText(L"→",
			x0 + cw + gap / 2.0f - 70.0f, y0 + chh / 2.0f - 100.0f, 56.0f,
			D2D1::ColorF(1.0f, 0.9f, 0.4f), D2D1::ColorF(0, 0, 0), 3.0f);
		ui.text->DrawOutlinedText(L"現在", x0, y0 - 100.0f, 30.0f,
			D2D1::ColorF(0.85f, 0.85f, 0.85f), D2D1::ColorF(0, 0, 0), 2.5f);
		ui.text->DrawOutlinedText(L"強化後", x0 + cw + gap, y0 - 100.0f, 30.0f,
			D2D1::ColorF(1.0f, 0.9f, 0.4f), D2D1::ColorF(0, 0, 0), 2.5f);
		ui.text->DrawTextInRect(L"強化する", bX, bY + bDy, bW, bH, 22.0f, D2D1::ColorF(1, 0.95f, 0.7f));
		ui.text->End();
		return;
	}

	// 通常デッキ：単体拡大（ボタンなし）
	std::string showId = (m_previewShowUpgrade && canUp) ? baseId + "+" : baseId;
	const CardData* d = CardDataBase::Get(showId);
	if (!d) return;
	float x0 = m_sw / 2.0f - cw / 2.0f, y0 = m_sh / 2.0f - chh / 2.0f;
	ui.sprite->Begin();
	ui.sprite->DrawSprite(white, 0, (float)UiContext::BAR_H, (float)m_sw, (float)m_sh - (float)UiContext::BAR_H, 0.0f, XMFLOAT4(0, 0, 0, 0.7f));
	ui.DrawCard(d, x0, y0, PS, false);
	ui.sprite->End();
	ui.text->Begin();
	ui.DrawCard(d, x0, y0, PS, true);
	ui.text->End();
}

std::vector<int> DeckPanel::VisibleDeckIndices() const
{
	std::vector<int> v;
	auto& deck = PlayerDataManager::GetData().deck;
	for (int i = 0; i < (int)deck.size(); i++)
	{
		if (IsUpgrade() && !CardDataBase::CanUpgrade(deck[i])) continue;  // 強化画面では強化不可を隠す
		v.push_back(i);
	}
	return v;
}

bool DeckPanel::GetDeckCardBase(int i, float& baseX, float& baseY) const
{
	int n = (int)PlayerDataManager::GetData().deck.size();
	if (i < 0 || i >= n) return false;

	float cw = CardVisual::CARD_W * DECK_SCALE;
	float ch = CardVisual::CARD_H * DECK_SCALE;
	int perRow = 6;
	float gapX = 15.0f, gapY = 20.0f;
	float totalW = perRow * cw + (perRow - 1) * gapX;
	float startX = (m_sw - totalW) / 2.0f;
	float startY = 70.0f;

	baseX = startX + (i % perRow) * (cw + gapX);
	baseY = startY + (i / perRow) * (ch + gapY) - m_scroll;
	return true;
}

float DeckPanel::DeckMaxScroll() const
{
	int n = (int)PlayerDataManager::GetData().deck.size();
	int perRow = 6;
	int rows = (n + perRow - 1) / perRow;
	if (rows <= 0) return 0.0f;

	float ch = CardVisual::CARD_H * DECK_SCALE;
	float gapY = 20.0f;
	float startY = 70.0f;

	// scroll=0 のときの「最下段カードの実描画下端」(GetRectの拡大分込み)
	float lastBaseY = startY + (rows - 1) * (ch + gapY);
	float lastBottom = lastBaseY + (ch + CardVisual::CARD_H) / 2.0f;

	float wantBottom = (float)m_sh - 70.0f;   // ここまで見せたい(下UIの余白)
	float m = lastBottom - wantBottom;
	return m > 0.0f ? m : 0.0f;                          // 収まるデッキは0(スクロール無し)
}

int DeckPanel::GetDeckCardAt(POINT p) const
{
	auto vis = VisibleDeckIndices();
	for (int slot = 0; slot < (int)vis.size(); slot++)
	{
		float bx, by; GetDeckCardBase(slot, bx, by);
		float x, y, w, h; CardVisual::GetRect(bx, by, DECK_SCALE, x, y, w, h);
		if (p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h)
			return vis[slot];
	}
	return -1;
}

