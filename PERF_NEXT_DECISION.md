結論（Ubuntu + Xvfb, llvmpipe）

- 実行コマンド:
```
timeout 15s xvfb-run -a -s "-screen 0 1920x1080x24" ./build/maplibre-slint-example 2>&1 | tee perf_breakdown_xvfb.log
```

- 得られたログ: `perf_breakdown_xvfb.log`（リポジトリ直下）
  - 取得できた分解サンプル（例、1回目の BREAKDOWN_SEC）:
    - render (avg) = 約 0.34 ms
    - readback (avg) = 約 1.33 ms
    - unpremultiply (avg) = 約 1.58 ms
    - memcpy (avg) = 約 0.049 ms
  - 備考: 実行環境の GPU は `llvmpipe`（ソフトウェアレンダラ）でした。

判断（2択）
- 選択: Windows に切り替えて実GPUで再計測する。

理由（短く）
- `llvmpipe` はソフトウェアレンダリングのため絶対性能は不正確。今回の目的は“支配的区間の分解”だが、最終的な最適化判断／絶対性能は実GPUでの計測が必須。

次のアクション提案
1) Windows環境で同じ計測コード（現在の `render_map()` 計測）を実行して、readback/unpremultiply/memcpy の実GPUでの比率を確認する。
2) 代替案として、Ubuntu 上で物理GPUに切り替えられる場合は 15 分以内に `GPU Identifier` が `llvmpipe` 以外になるか確認する（失敗した場合は Windows に移行）。

簡潔な結論: llvmpipe のため Ubuntu 継続での“絶対性能”評価は不適切。Windows（実GPU）へ移行します。