実行コマンド:
```
timeout 10s xvfb-run -a -s "-screen 0 1920x1080x24" ./build/maplibre-slint-example 2>&1 | tee perf_run_xvfb_10s.log
```

ログ抜粋（先頭〜重要箇所）:

[main] Starting application
Initial Window Size: 640x480
[main] Entering UI event loop
Map Area Size Changed: 782x530
[SlintMapLibre] initialize(782,530)
[SlintMapLibre] Creating mbgl::util::RunLoop
Setting solid background color style...
Loading remote MapLibre style...
[MapObserver] Will start loading map
[SlintMapLibre] Map initialization completed
Rendering map...
render_map() called
Style not loaded yet, returning empty image
Rendering map...
render_map() called
Style not loaded yet, returning empty image
[MapObserver] Did finish loading style
[INFO] {maplibre-slint-}[General]: GPU Identifier: llvmpipe (LLVM 20.1.2, 256 bits)
Rendering map...
render_map() called
Style loaded, proceeding with rendering...
Using frontend.render(map) like mbgl-render...
Rendered one frame, reading still image...
Image size: 782x530
Image data pointer: valid
Converting from premultiplied to unpremultiplied...
Creating Slint pixel buffer...
Copying unpremultiplied pixel data...
Non-transparent pixels: 414460 / 414460
Image created successfully
...（中略、連続してフレーム出力）...
Terminated

判定と簡単な所見:
- 起動は成功しました（プロセスは 10 秒間生存し、標準出力に `Initial Window Size` および `[MapObserver] Did finish loading style` が出力されました）。
- エラー文字列 `Failed to open X display` や `Could not load the Qt platform plugin "xcb"` は出力されていません。

どの区間が支配的か（1段落）:
ログからはレンダリング→読み取り（`Rendered one frame, reading still image...` → `frontend->readStillImage()`）の繰り返しが中心で、既知の実装上は GPU→CPU の同期を伴う readback（`glReadPixels`）がボトルネックになりやすいです。今回のヘッドレス実行でも "reading still image" が繰り返し確認できるため、最初に着目すべきは readback です（unpremultiply と memcpy も存在しますが、まずは readback の影響を評価してください）。
