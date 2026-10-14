# Skill Contract v1

- 配置: `skills/unreal-<topic>/SKILL.md`(正本)。バイナリに同梱する
- frontmatter の必須項目: `name`(ディレクトリ名と一致)、`description`(「Use when ...」で発動条件を書く。300 文字以内)、`version`
- 本文の必須見出し: `## When to use` / `## Steps` / `## Verify` / `## On failure`
- 参照するコマンドやツール名は、実在することを lint で検証する
- 本文は 500 行以内。詳細は `references/*.md` に分ける
- lint ルール(初期は 12 個): frontmatter の必須項目 / name の一致 / description の長さ / 必須見出し / 未知のツール / 未知のコマンド / 壊れた相対リンク / 行数の上限 / 重複した name / 空のセクション / 絶対パスの使用 / 他製品名の混入(unity など)
- 初期スキル: unrealengine-cli-usage, unreal-project-bootstrap, unreal-development-loop, unreal-blueprint-edit, unreal-level-build, unreal-asset-management, unreal-cpp-edit, unreal-pie-testing, unreal-animation, unreal-umg, unreal-material, unreal-niagara, unreal-landscape, unreal-enhanced-input, unreal-data-assets, unreal-sequencer-audio, unreal-packaging, unreal-python-scripting, unreal-engine-reference
