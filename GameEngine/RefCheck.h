#pragma once
#include <string>
#include <vector>

// 参照切れチェッカー（Debugビルド専用）
// JSON 同士は「名前(id)」で参照し合っているが、名前を間違えてもビルドも起動もエラーにならない。
// その「コンパイラの代わり」として、全 JSON をまとめて読み、存在しない名前・重複 id を一覧にする。
//
// 【拡張のしかた】RefCheck.cpp の表に1行足すだけ
// - DEFS  : 「このファイルのこの項目が id の定義」（例：enemies.json の enemies[].id ＝ "enemy"）
// - RULES : 「このファイルのこの項目は、この一覧の名前を指す」（例：encounters[].enemies[].id → "enemy"）
// 新しいエディタや JSON を足した時も、ここに行を足せば同じチェックが効く。
namespace RefCheck
{
    enum class Severity { Error, Warning };

    struct Problem
    {
        Severity severity;
        std::string file;      // どのファイルか
        std::string where;     // ファイルの中のどこか（例：enemies[slime].actions[2].effects[0].summonId）
        std::string message;   // 何がおかしいか
    };

    std::vector<Problem> Run();   // 全ルールを実行して問題を返す（エディタの保存前などからも呼べる）

    void Toggle();
    bool IsOpen();
    void Draw();
}
