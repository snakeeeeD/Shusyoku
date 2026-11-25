#include "CardEffect.h"
#include "BattleScene.h"
#include "CardTooltip.h"
#include "TextureLoader.h"
#include "EffectManager.h"
#include "RelicManager.h"
#include "CardExecutor.h"
#include "TerrainDataBase.h"
#include "MaterialDataBase.h"
#include "HighlightPalette.h"
#include "FloatingText.h"
#include "BuffPopup.h"
#include "PileFx.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "TurnBanner.h"
#include "DamageFeedback.h"
#include "RangeShape.h"
#include "UiNotice.h"
#include "Cursor.h"
#include "Audio.h"
#include "Hint.h"
#include "Telemetry.h"
#include "EnemyIntentVisual.h"
#include "Faith.h"

#include <algorithm>
#include <random>
#include <cstdio>
#include <queue>
#include <map>
#include <set>

#ifdef _DEBUG
#include "External/imgui/imgui.h"
#endif

// シーン側のカメラ（隠れた狙い先・マス→画面・自由に見る・操作のデモ）
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

// カメラ操作の自動デモ（初回の2ターン目に1回）を始めるか。手順・動きは BattleCamera
void BattleScene::MaybeStartCamDemo()
{
    auto& pd = PlayerDataManager::GetData();
    if (pd.tutorialCamera) { Hint::Hold(false); return; }   // 見終わった：ヒントを出してよい
    Hint::Hold(true);                                        // まだ見ていない：デモを先に見せるため、新しいヒントは待たせる
    if (m_camera.DemoActive() || m_camera.Homing()) return;
    // 始める条件：自分の番（1ターン目から）、ターン表示が消えて、カードを持っていない、出ているヒントが閉じた
    if (m_battleResult != BattleResult::None || !m_turnManager.IsPlayerTurn()
        || TurnBanner::IsActive() || m_selectedCardIndex >= 0 || Hint::Current()) return;
    pd.tutorialCamera = true;
    pd.seenHints.insert("camera");   // 同じことをヒントでもう一度言わない
    PlayerDataManager::Save();
    m_camera.StartDemo();
}

bool BattleScene::CamDemoCaption(const wchar_t*& text, const char*& icon, float& alpha) const
{
    return m_camera.DemoCaption(text, icon, alpha);
}

float BattleScene::HiddenTargetsPanZ(const CardData& d) const
{
    XMMATRIX vp = m_renderer3D->GetViewMatrix() * m_renderer3D->GetProjectionMatrix();
    float handTop = m_screenHeight - CardVisual::PLAY_LINE_OFFSET;   // 手札の上端（使用ラインと同じ線）
    int rng = (d.type == CardType::Move) ? m_player->GetMoveRange(d.range) : d.range;

    // ワールド座標 → 画面のy（下ほど大きい）
    auto screenY = [&](float wx, float wz) {
        XMFLOAT4 clip;
        XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(wx, 0.0f, wz, 1.0f), vp));
        return (clip.w > 0.0f) ? (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight : -1.0f;
        };

    // 狙えるマスのうち、画面で一番下に映っているもの
    float lowest = 0.0f, lowX = 0.0f, lowZ = 0.0f;
    for (auto& [c, r] : BattleHighlighter::GetCandidates(m_playerCol, m_playerRow, d.rangeType, rng, 0, -1))
    {
        if (c < 0 || c >= m_gridMap->GetCols() || r < 0 || r >= m_gridMap->GetRows()) continue;   // 盤面外も返るので除く
        float wx = (c - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float wz = (r - m_gridMap->GetRows() / 2.0f) * 1.1f;
        float sy = screenY(wx, wz);
        if (sy > lowest) { lowest = sy; lowX = wx; lowZ = wz; }
    }
    const float MARGIN = 30.0f;              // 手札の上端より、これだけ上に出す
    float limit = handTop - MARGIN;
    if (lowest <= limit) return 0.0f;        // 余白込みで隠れていない → 動かさない

    // そのマスの近くで「カメラを奥へ1ずらすと、画面で何px上がるか」を測って、必要な量に直す
    float pxPerUnit = screenY(lowX, lowZ) - screenY(lowX, lowZ - 1.0f);
    if (pxPerUnit <= 1.0f) return 0.0f;
    return (lowest - limit) / pxPerUnit;
}

bool BattleScene::CellToScreen(int col, int row, float& sx, float& sy) const
{
    float wx = (col - m_gridMap->GetCols() / 2.0f) * 1.1f;
    float wz = (row - m_gridMap->GetRows() / 2.0f) * 1.1f;
    XMFLOAT4 clip;
    XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(wx, 0.0f, wz, 1.0f),
        m_renderer3D->GetViewMatrix() * m_renderer3D->GetProjectionMatrix()));
    if (clip.w <= 0.0f) return false;
    sx = (1.0f + clip.x / clip.w) * 0.5f * m_screenWidth;
    sy = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
    return true;
}

void BattleScene::FreeLookStep(Input& in, float deltaTime)
{
    // ズーム・パン・初期位置（本番の戦闘と同じ操作）
    m_camera.HandleInput(in, m_gridMap->GetCols(), m_gridMap->GetRows(), m_player->worldX, m_player->worldZ);

    // 敵クリック（グリッド）→ 範囲選択
    if (in.GetMouseButtonTrigger(0)
        && in.GetMousePos().x < m_screenWidth - 250.0f
        && in.GetMousePos().y < m_screenHeight - 150.0f) {
        auto rc = m_gridMap->GetClickedCell3D(in.GetMousePos(),
            m_renderer3D->GetViewMatrix(), m_renderer3D->GetProjectionMatrix(), m_screenWidth, m_screenHeight);
        int found = -1;
        if (rc.cell)
            for (int i = 0; i < (int)m_enemies.size(); i++)
                for (auto& [dc, dr] : m_enemies[i]->GetGridShape())
                    if (m_enemies[i]->gridCol + dc == rc.col && m_enemies[i]->gridRow + dr == rc.row) found = i;
        m_selectedEnemyRange = (found >= 0 && found != m_selectedEnemyRange) ? found : -1;
    }

    // 敵パネルクリック → 範囲選択＋その敵へカメラ
    if (in.GetMouseButtonTrigger(0)) {
        int i = BattleUI::EnemyEntryAt(in.GetMousePos(), m_enemies, m_screenWidth);
        if (i >= 0) {
            m_selectedEnemyRange = (m_selectedEnemyRange == i) ? -1 : i;
            m_camera.LookAt(m_enemies[i]->worldX, m_enemies[i]->worldZ);
        }
    }

    m_camera.Apply(m_renderer3D);   // カメラ適用

    // ハイライト（脅威範囲＋当たりマーカー）
    m_highlighter.SetSelectedEnemy(FocusEnemy());
    m_highlighter.ClearPlayerHighlight(m_gridMap);
    m_highlightTimer += deltaTime * 0.5f;
    m_highlighter.UpdateEnemyHighlight(m_enemies, m_gridMap, m_player,
        m_playerCol, m_playerRow, m_highlightTimer, m_decoyCol, m_decoyRow);
    if (m_forceHitmark && !m_enemies.empty()) m_enemies[0]->hitsPlayer = true;   // 学習用に強制表示
}

// カメラの手での操作（ホイール・右ドラッグ・ホイールクリック）。操作のデモ中は動かせない
void BattleScene::HandleCameraInput()
{
    if (!m_camera.DemoActive()
        && m_camera.HandleInput(m_input, m_gridMap->GetCols(), m_gridMap->GetRows(), m_player->worldX, m_player->worldZ))
    {
        // ドラッグせずに離した＝単押し→カード選択解除（Moveカードは除く）
        if (m_selectedCardIndex >= 0
            && m_selectedCardIndex < (int)m_hand.GetCards().size()
            && m_hand.GetCards()[m_selectedCardIndex]->GetData()->type != CardType::Move)
            m_selectedCardIndex = -1;
    }
}

// 狙いカメラ・操作のデモ・カメラの更新（ゴーストを中央へ寄せている間は、カーソルも一緒に動かす）
void BattleScene::UpdateAimCamera(float deltaTime)
{
    // ===== 狙いカメラ =====
    // ・マスを選ぶカードを選んでいる間、狙えるマスが手札に隠れていたら、隠れなくなるまで奥へ寄せる
    // ・移動カードで経路を引いている間は、移動予定のマス（ゴースト）を画面の中央へ寄せる（カーソルも一緒に動かす）
    // ・選択が終わったら：移動カードで移動した → そのまま／キャンセルなど → 寄せる前の位置へ戻す
    // シーンは「どこへ寄せたいか」を決めるだけ。寄せ方・戻し方は BattleCamera
    if (m_camera.UserPanning())
    {
        m_camera.AimCancel();
        Cursor::Request(CursorKind::Camera);   // 右ドラッグ中＝カメラ操作のカーソル
    }

    bool carryCursor = false;                 // このフレーム、カーソルをゴーストと一緒に動かすか
    std::pair<int, int> carryCell = { -1, -1 };
    float carryBX = 0.0f, carryBY = 0.0f;     // カメラを動かす前のゴーストの画面位置

    m_camera.BeginAimFrame();
    if (SelectedNeedsTarget() && Settings::Get().aimCamera)
    {
        if (!m_camera.Dragging())
        {
            const CardData* sd = m_hand.GetCards()[m_selectedCardIndex]->GetData();
            bool moved;
            if (sd->type == CardType::Move && !m_movePath.empty())
            {
                // 移動予定のマス（ゴースト）を中央へ。カメラの注視点を少し奥に置くと中央に映る（初期カメラと同じ -0.7）
                std::pair<int, int> tip = m_movePath.back();
                float goalX = (tip.first - m_gridMap->GetCols() / 2.0f) * 1.1f;
                float goalZ = (tip.second - m_gridMap->GetRows() / 2.0f) * 1.1f - 0.7f;

                // カーソルは「カーソルの下のマス」と一緒に動かす（ゴーストではない）
                if (m_hoveredCell.first >= 0)
                {
                    carryCell = m_hoveredCell;
                    carryCursor = CellToScreen(carryCell.first, carryCell.second, carryBX, carryBY);
                }
                moved = m_camera.AimFollow(goalX, goalZ, deltaTime);
            }
            else
                moved = m_camera.AimPan(0.0f, HiddenTargetsPanZ(*sd), 8.0f, deltaTime);   // 経路がまだ無い：手札に隠れるマスを出す
            if (!moved) carryCursor = false;
        }
    }
    else m_camera.AimEnd();

    MaybeStartCamDemo();   // カメラ操作の自動デモ（初回の1ターン目から。どのヒントよりも先）
    m_camera.Update(deltaTime, m_gridMap->GetCols(), m_gridMap->GetRows(), m_player->worldX, m_player->worldZ);

    m_camera.Apply(m_renderer3D);

    // ゴーストを中央へ寄せている間は、カーソルの下のマスがずれた分だけカーソルもずらす（指している場所を変えない）
    if (carryCursor)
    {
        float ax, ay;
        if (CellToScreen(carryCell.first, carryCell.second, ax, ay))   // 新しいカメラでの位置
            m_input.MoveMouseBy(ax - carryBX, ay - carryBY);
    }
}
