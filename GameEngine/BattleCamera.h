#pragma once
// 戦闘のカメラ：位置・ズーム・手での操作・狙いカメラ・操作デモ
// 盤面やカードの事情は知らない（知っているのは盤面の大きさだけ）。BattleScene が「どこへ寄せたいか」を渡す
#include <Windows.h>

class Input;
class Renderer3D;

class BattleCamera
{
public:
    static constexpr float ZOOM_MIN = 0.5f;
    static constexpr float ZOOM_MAX = 0.8f;
    static constexpr float ZOOM_SPEED = 0.1f;

    // ---- 基本
    void  Home(float px, float pz);                   // 初期位置（プレイヤーの少し奥・ズーム最大）へすぐ
    void  LookAt(float x, float z) { m_x = x; m_z = z; }
    float Zoom() const { return m_zoom; }
    void  Apply(Renderer3D* r) const { ApplyAt(r, m_x, m_z, m_zoom); }   // 画面揺れを足してカメラを設定
    static void ApplyAt(Renderer3D* r, float x, float z, float zoom);    // 覚醒演出など、好きな位置で

    // ---- 手での操作（ホイール：ズーム／右ドラッグ：パン／ホイールクリック：初期位置）
    // 戻り値：右クリックをドラッグせずに離した（＝カード選択を解除する合図）
    bool  HandleInput(Input& in, int cols, int rows, float px, float pz);
    bool  Dragging() const { return m_dragging; }
    bool  UserPanning() const { return m_dragging && m_dragged; }

    // ---- 狙いカメラ（カードでマスを選んでいる間だけ寄せる）
    void  BeginAimFrame() { m_autoMoving = false; }      // 毎フレーム、狙いの処理の前に
    bool  AimPan(float dX, float dZ, float speed, float dt);   // 寄せる（最初に元の位置を覚える）。動いたら true
    bool  AimFollow(float goalX, float goalZ, float dt);        // ゴーストへ（遊びつき）。動いたら true
    void  AimEnd();                                     // 選択が終わった → 移動していなければ元の位置へ戻す
    void  AimCancel() { m_aimPanned = false; m_aimReturning = false; }   // 手でパンした → 寄せた記録を捨てる
    void  SetAimKeep(bool keep) { m_aimKeep = keep; }   // 移動カードで動いた → 戻さない
    bool  AutoMoving() const { return m_autoMoving; }   // 今フレーム、狙いカメラが自動で動いたか

    // ---- 操作デモ（初回の2ターン目に1回）。始める条件はシーンの事情なので BattleScene が決める
    void  StartDemo();
    void  EndDemo() { m_demoStep = -1; m_homing = true; }   // 最後まで見た／クリックで飛ばした → 初期位置へ滑らかに戻す
    bool  DemoActive() const { return m_demoStep >= 0; }
    bool  Homing() const { return m_homing; }
    bool  DemoCaption(const wchar_t*& text, const char*& icon, float& alpha) const;

    // 毎フレーム：狙いから元に戻る動き・デモの動き
    void  Update(float dt, int cols, int rows, float px, float pz);

private:
    void  Toward(float gx, float gz, float gzoom, float dt);   // 目標へなめらかに（デモ・戻りで共通の指数補間）
    void  ClampToBoard(int cols, int rows);                    // 盤面の外へ行きすぎない

    static constexpr float AIM_FOLLOW_SPEED = 4.0f;   // ゴーストへ寄る速さ（大きいほど速い。手札に隠れるマスを出す時は 8）
    static constexpr float AIM_FOLLOW_START = 1.6f;   // ゴーストがこれ以上離れたら寄り始める（1マス＝1.1）

    float m_x = 0.0f, m_z = 0.0f;        // 注視点
    float m_zoom = ZOOM_MAX;

    bool  m_dragging = false;            // 右ボタンを押している
    bool  m_dragged = false;             // 押している間に動かした（単押しと区別）
    POINT m_dragStart = { 0, 0 };

    bool  m_aimPanned = false;           // この選択でカメラを寄せたか
    bool  m_aimKeep = false;             // 移動カードを使った → 戻さない
    bool  m_aimReturning = false;        // 元の位置へ戻している途中
    float m_aimStartX = 0.0f, m_aimStartZ = 0.0f;   // 寄せる前の位置
    bool  m_autoMoving = false;
    bool  m_ghostFollowing = false;      // 今、ゴーストへ寄っている途中か

    int   m_demoStep = -1;               // 表示中の手順（-1=していない）
    float m_demoTime = 0.0f;             // 今の手順の経過時間
    bool  m_homing = false;              // デモの後、初期の位置へ戻している途中
};
