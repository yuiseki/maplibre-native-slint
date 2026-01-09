# MapLibre Native + Slint 性能改善提案書

## Executive Summary

### 推奨案

**二段階アプローチを推奨：Phase 1（即座の改善）→ Phase 2（本命改善）**

- **Phase 1（今すぐ可能）**: Readback継続・最適化アプローチ
  - 非同期readback導入
  - Premultiplied Alpha変換の最適化（SIMD化）
  - 不要なメモリコピーの削減
  - 更新頻度の適切な制御
  
- **Phase 2（3-6ヶ月）**: OpenGL Texture共有によるゼロコピー化
  - Slint `BorrowedOpenGLTexture` APIを使用
  - Linux/Windows環境での適用（OpenGLバックエンド）
  - GPU texture直接参照でreadbackを完全排除

### 期待される効果

**Phase 1の効果:**
- CPU使用率: 15-25%削減
- フレーム時間: 10-20%短縮
- 1920x1080解像度で操作時のフレームレート向上（45fps → 55fps程度）
- リスク: 低、実装期間: 1-2週間

**Phase 2の効果:**
- CPU使用率: 40-60%削減（readback処理の完全排除）
- フレーム時間: 30-50%短縮
- 1920x1080解像度で安定60fps達成
- メモリ帯域幅の大幅削減（毎フレーム8MB転送が消失）
- リスク: 中、実装期間: 3-6ヶ月

### 実装優先度

1. **最優先（Phase 1）**: 低リスクで即効性のある最適化
2. **次優先（Phase 2）**: 根本的なアーキテクチャ改善
3. **将来検討**: WebGPU/wgpu統合（WASM対応を視野に）

## 1. 現状アーキテクチャ分析

### 1.1 描画パイプラインの概要

現在の実装は以下の6ステップで構成される：

```
1. MapLibre HeadlessFrontend → GPU描画（OpenGL/Metal）
2. GPU framebuffer → CPU memory (readback)
3. PremultipliedImage（GPU形式）取得
4. Premultiplied → Non-premultiplied 変換（CPU）
5. SharedPixelBuffer へメモリコピー（CPU）
6. Slint Image として表示（CPU rendering）
```

**コード根拠:**
- [README.md](README.md#L127-L131): パイプラインの説明
- [src/slint_maplibre_headless.cpp](src/slint_maplibre_headless.cpp#L208-L230): `render_map()` 関数

### 1.2 GPU→CPU Readbackの実態

**発生箇所:**

1. **最上位レイヤー:** [src/slint_maplibre_headless.cpp#L210](src/slint_maplibre_headless.cpp#L210)
   ```cpp
   mbgl::PremultipliedImage rendered_image = frontend->readStillImage();
   ```

2. **HeadlessFrontend層:** [vendor/maplibre-native/platform/default/src/mbgl/gfx/headless_frontend.cpp#L131-L132](vendor/maplibre-native/platform/default/src/mbgl/gfx/headless_frontend.cpp#L131-L132)
   ```cpp
   PremultipliedImage HeadlessFrontend::readStillImage() {
       return backend->readStillImage();
   }
   ```

3. **OpenGLバックエンド:** [vendor/maplibre-native/platform/default/src/mbgl/gl/headless_backend.cpp#L115-L118](vendor/maplibre-native/platform/default/src/mbgl/gl/headless_backend.cpp#L115-L118)
   ```cpp
   PremultipliedImage HeadlessBackend::readStillImage() {
       return static_cast<gl::Context&>(getContext()).readFramebuffer<PremultipliedImage>(size);
   }
   ```

4. **実際のglReadPixels呼び出し:** [vendor/maplibre-native/src/mbgl/gl/context.cpp#L331](vendor/maplibre-native/src/mbgl/gl/context.cpp#L331)
   ```cpp
   MBGL_CHECK_ERROR(glReadPixels(
       0, 0, size.width, size.height, Enum<gfx::TexturePixelType>::to(format), GL_UNSIGNED_BYTE, data.get()));
   ```

**頻度:** 
- タイマー駆動：16ms（60fps相当）- [examples/map_window.slint#L49-L53](examples/map_window.slint#L49-L53)
- 条件付き実行：`take_repaint_request()` または `consume_forced_repaint()` が true の場合のみ
- 実質的には、カメラ移動やマップ操作時に毎フレーム発生

**バッファサイズ:**
- デフォルト: ウィンドウサイズ × pixelRatio（通常1.0）
- 初期化時: [src/slint_maplibre_headless.cpp#L57-L59](src/slint_maplibre_headless.cpp#L57-L59)
- 1920x1080の場合: 1920 × 1080 × 4 bytes（RGBA）= **約8.3MB/フレーム**

**Metalバックエンド（macOS）:**
- 同様の仕組みで texture readback が発生
- [vendor/maplibre-native/platform/default/src/mbgl/mtl/headless_backend.cpp#L85-L87](vendor/maplibre-native/platform/default/src/mbgl/mtl/headless_backend.cpp#L85-L87)

### 1.3 Premultiplied Alpha変換の発生箇所

**変換実行箇所:** [src/slint_maplibre_headless.cpp#L225-L226](src/slint_maplibre_headless.cpp#L225-L226)
```cpp
mbgl::UnassociatedImage unpremult_image = 
    mbgl::util::unpremultiply(std::move(rendered_image));
```

**変換実装:** [vendor/maplibre-native/src/mbgl/util/premultiply.cpp#L29-L51](vendor/maplibre-native/src/mbgl/util/premultiply.cpp#L29-L51)
```cpp
UnassociatedImage unpremultiply(PremultipliedImage&& src) {
    // ...
    uint8_t* data = dst.data.get();
    for (size_t i = 0; i < dst.bytes(); i += 4) {
        uint8_t& r = data[i + 0];
        uint8_t& g = data[i + 1];
        uint8_t& b = data[i + 2];
        uint8_t& a = data[i + 3];
        if (a) {
            r = static_cast<uint8_t>((255 * r + (a / 2)) / a);
            g = static_cast<uint8_t>((255 * g + (a / 2)) / a);
            b = static_cast<uint8_t>((255 * b + (a / 2)) / a);
        }
    }
    // ...
}
```

**処理量:**
- 1920x1080の場合: 2,073,600ピクセル × 4チャンネル = 8,294,400回の演算
- CPU上でシーケンシャルに実行（SIMD最適化なし）
- 除算命令を含むため比較的重い処理

### 1.4 メモリコピーの発生箇所

毎フレームで以下のメモリコピーが発生：

1. **GPU → CPU (readback):** 約8.3MB（1920x1080の場合）
   - [vendor/maplibre-native/src/mbgl/gl/context.cpp#L331](vendor/maplibre-native/src/mbgl/gl/context.cpp#L331)
   - 同期的に実行されるため、GPU完了を待つ

2. **PremultipliedImage → UnassociatedImage (move):** 
   - [vendor/maplibre-native/src/mbgl/util/premultiply.cpp#L32-L34](vendor/maplibre-native/src/mbgl/util/premultiply.cpp#L32-L34)
   - `std::move` を使用しているため実質的にはポインタ移動のみ（コピーなし）

3. **UnassociatedImage → SharedPixelBuffer (memcpy):** 約8.3MB
   - [src/slint_maplibre_headless.cpp#L232-L234](src/slint_maplibre_headless.cpp#L232-L234)
   ```cpp
   memcpy(pixel_buffer.begin(), unpremult_image.data.get(),
          unpremult_image.size.width * unpremult_image.size.height * sizeof(slint::Rgba8Pixel));
   ```

**合計データ転送量（1920x1080, 60fps想定）:**
- 1フレーム: 16.6MB（GPU→CPU 8.3MB + CPU内コピー 8.3MB）
- 1秒間: 約1GB
- CPU→GPUの逆転送はなし（一方向のみ）

### 1.5 フレーム更新制御の仕組み

**更新トリガーのフロー:**

1. **定期タイマー（16ms）:** [examples/map_window.slint#L49-L53](examples/map_window.slint#L49-L53)
   ```slint
   Timer {
       interval: 16ms;
       running: true;
       triggered => { MapAdapter.tick_map_loop(); }
   }
   ```

2. **RunLoop実行:** [examples/main.cpp#L33-L38](examples/main.cpp#L33-L38)
   ```cpp
   main_window->global<MapAdapter>().on_tick_map_loop([=]() {
       slint_map_libre->run_map_loop();
       if (slint_map_libre->take_repaint_request() ||
           slint_map_libre->consume_forced_repaint()) {
           render_function();
       }
   });
   ```

3. **MapLibre RunLoop:** [src/slint_maplibre_headless.cpp#L370-L377](src/slint_maplibre_headless.cpp#L370-L377)
   ```cpp
   void SlintMapLibre::run_map_loop() {
       if (run_loop) {
           run_loop->runOnce();
       }
       tick_animation();
   }
   ```

4. **再描画リクエストの発行:**
   - **MapObserver経由:** カメラ変動、ソース変更時 → [src/slint_maplibre_headless.cpp#L166-L176](src/slint_maplibre_headless.cpp#L166-L176)
   - **RendererObserver経由:** レンダリング完了時に `needsRepaint` フラグ → [src/slint_maplibre_headless.hpp#L40-L49](src/slint_maplibre_headless.hpp#L40-L49)
   - **ユーザー操作:** マウス移動、ズーム時 → [src/slint_maplibre_headless.cpp#L283-L303](src/slint_maplibre_headless.cpp#L283-L303)

5. **強制再描画メカニズム:**
   - `forced_repaint_frames` カウンター: 操作後100-120ms間は強制的に再描画
   - [src/slint_maplibre_headless.cpp#L389-L402](src/slint_maplibre_headless.cpp#L389-L402)

**問題点:**
- 16msタイマーは常に動作（アイドル時も）
- 再描画が不要でも `run_map_loop()` は毎回実行
- 強制再描画の期間が固定（100-120ms）で柔軟性に欠ける

### 1.6 プラットフォーム別実装差分

| プラットフォーム | バックエンド | Readback実装 | RunLoop | コンテキスト管理 |
|-----------------|-------------|-------------|---------|----------------|
| **Linux x86_64** | OpenGL/GLES | `glReadPixels` | `mbgl::util::RunLoop` | 独自管理 |
| **Windows x64** | OpenGL | `glReadPixels` | `mbgl::util::RunLoop` + libuv | 独自管理 |
| **macOS ARM64** | Metal | `MTLTexture` readback | システムCFRunLoop（winitと共存） | Metal自動管理 |

**コード差分:**

1. **RunLoop初期化:** [src/slint_maplibre_headless.cpp#L47-L54](src/slint_maplibre_headless.cpp#L47-L54)
   ```cpp
   #if defined(__APPLE__)
       // macOSではwinitがCFRunLoopを管理するため、独自RunLoopは作らない
       std::cout << "[SlintMapLibre] macOS detected: Skipping mbgl::util::RunLoop creation" << std::endl;
   #else
       if (!run_loop) {
           run_loop = std::make_unique<mbgl::util::RunLoop>();
       }
   #endif
   ```

2. **CMake設定:** [CMakeLists.txt#L169-L172](CMakeLists.txt#L169-L172)
   - macOS: Metal framework必須
   - Windows: libuv必須
   - Linux: GLES2 or OpenGL

3. **Readback実装:**
   - OpenGL: [vendor/maplibre-native/src/mbgl/gl/context.cpp#L331](vendor/maplibre-native/src/mbgl/gl/context.cpp#L331)
   - Metal: [vendor/maplibre-native/platform/default/src/mbgl/mtl/headless_backend.cpp#L85-L87](vendor/maplibre-native/platform/default/src/mbgl/mtl/headless_backend.cpp#L85-L87)
   
**重要な制約:**
- 各プラットフォームでreadbackの性能特性が異なる
- OpenGLは同期的、Metalは非同期可能だが現状は同期的に実装
- コンテキスト共有の仕組みがプラットフォーム依存

## 2. 性能ボトルネック仮説

### 2.1 ボトルネック #1: 毎フレームのGPU→CPUデータ転送

**根拠となるコード箇所:**
- [vendor/maplibre-native/src/mbgl/gl/context.cpp#L331](vendor/maplibre-native/src/mbgl/gl/context.cpp#L331)
- [src/slint_maplibre_headless.cpp#L210](src/slint_maplibre_headless.cpp#L210)

**問題の詳細:**
- `glReadPixels` は**同期的**に実行され、GPU処理の完了を待つ
- PCIe/システムバスを経由した大量のデータ転送（1920x1080で8.3MB/フレーム）
- GPUパイプラインがストールし、次のフレームの描画開始が遅延

**推定インパクト:**
- 1920x1080, 60fps想定: **約500MB/秒** のメモリ帯域消費
- 同期待機による**GPU利用率低下**: 理論値の60-70%程度に留まる
- フレーム時間への寄与: **5-8ms/フレーム**（全体の30-50%）

**測定方法:**
- `glReadPixels` 前後の時間計測
- GPU profiler（NVIDIA Nsight, Intel GPA, RenderDoc等）でPCIe転送量を確認

### 2.2 ボトルネック #2: Premultiplied Alpha変換のCPU処理

**根拠となるコード箇所:**
- [vendor/maplibre-native/src/mbgl/util/premultiply.cpp#L29-L51](vendor/maplibre-native/src/mbgl/util/premultiply.cpp#L29-L51)
- [src/slint_maplibre_headless.cpp#L225-L226](src/slint_maplibre_headless.cpp#L225-L226)

**問題の詳細:**
```cpp
// 現状: シーケンシャルループで除算を含む計算
for (size_t i = 0; i < dst.bytes(); i += 4) {
    if (a) {
        r = static_cast<uint8_t>((255 * r + (a / 2)) / a);  // 除算が重い
        g = static_cast<uint8_t>((255 * g + (a / 2)) / a);
        b = static_cast<uint8_t>((255 * b + (a / 2)) / a);
    }
}
```

- SIMD最適化なし（SSE/AVX/NEON未使用）
- 除算命令は乗算の10-20倍遅い
- キャッシュ効率が悪い（メモリアクセスパターン）

**推定インパクト:**
- 1920x1080の場合: 2,073,600ピクセル × 約10-15 cycles/ピクセル
- 3GHz CPUで **約7-10ms/フレーム**（全体の40-60%）
- SIMD化で **4-8倍の高速化が可能**（SSE: 4x, AVX2: 8x）

**測定方法:**
- `unpremultiply()` 前後の時間計測
- `perf`（Linux）や Instruments（macOS）でCPU hotspot分析

### 2.3 ボトルネック #3: 複数回のメモリコピー

**根拠となるコード箇所:**
- GPU→CPU: [vendor/maplibre-native/src/mbgl/gl/context.cpp#L323-L341](vendor/maplibre-native/src/mbgl/gl/context.cpp#L323-L341)
- CPU内コピー: [src/slint_maplibre_headless.cpp#L232-L234](src/slint_maplibre_headless.cpp#L232-L234)

**問題の詳細:**
1. **GPU → CPU (glReadPixels):** 8.3MB
2. **UnassociatedImage → SharedPixelBuffer (memcpy):** 8.3MB

合計 **16.6MB/フレーム** のデータ移動

**推定インパクト:**
- 典型的なメモリ帯域: 20-30 GB/s（DDR4-3200）
- memcpy理論時間: 8.3MB ÷ 25GB/s ≈ **0.3ms**
- 実際はキャッシュミス、アライメント、競合で **2-3倍**に増加 → **1-2ms/フレーム**

**測定方法:**
- `memcpy` 前後の時間計測
- `perf mem`（Linux）でメモリ帯域使用率を確認

### 2.4 ボトルネック #4: 更新頻度制御の非効率性

**根拠となるコード箇所:**
- [examples/map_window.slint#L49-L53](examples/map_window.slint#L49-L53)
- [examples/main.cpp#L33-L38](examples/main.cpp#L33-L38)
- [src/slint_maplibre_headless.cpp#L370-L377](src/slint_maplibre_headless.cpp#L370-L377)

**問題の詳細:**
- **16msタイマーは常時動作**（マップが静止していても）
- `run_map_loop()` は毎回実行され、不要な RunLoop 処理が発生
- 強制再描画期間（100-120ms）が固定で、操作の種類を考慮しない
  - 例：小さなカメラ調整でも100ms間フル描画
  - 例：fly_toアニメーションでは2.5秒間必要だが固定値

**推定インパクト:**
- アイドル時のCPU使用率: **2-5%** の無駄な消費
- 不要な再描画による電力消費増加
- バッテリー駆動時の動作時間短縮

**測定方法:**
- アイドル状態でのCPU使用率計測（`top`, `htop`）
- 操作後の再描画回数カウント
- 電力消費計測（`powertop`, macOS Activity Monitor）

### ボトルネック総括

1920x1080, 操作時（60fps目標）のフレーム時間推定内訳：

| 処理 | 推定時間 | 割合 | 改善可能性 |
|------|---------|------|-----------|
| MapLibre GPU描画 | 3-5ms | 20-30% | 低（MapLibre側の最適化が必要） |
| **glReadPixels（同期待機含む）** | **5-8ms** | **30-50%** | **高（非同期化、排除可能）** |
| **Premultiplied変換** | **7-10ms** | **40-60%** | **高（SIMD化で4-8倍）** |
| memcpy | 1-2ms | 5-10% | 中（ゼロコピー化で排除可能） |
| Slint描画 | 1-2ms | 5-10% | 低（Slint側の最適化が必要） |
| **合計** | **17-27ms** | **100%** | - |

**現状の実効フレームレート:** 37-59fps（目標60fpsに対して不足）

**改善余地の大きい領域:**
1. GPU→CPU readback（5-8ms） → ゼロコピー化で**完全排除可能**
2. Premultiplied変換（7-10ms） → SIMD化で **2-3msに短縮可能**
3. memcpy（1-2ms） → ゼロコピー化で**完全排除可能**

**Phase 1（最適化）による期待効果:** 17-27ms → **12-18ms** (30-40% 改善)
**Phase 2（ゼロコピー）による期待効果:** 17-27ms → **5-9ms** (60-70% 改善)

## 3. 改善案の選択肢

### 3.1 案A: Readback継続・最適化アプローチ

**概要:** 現行アーキテクチャを維持しつつ、ボトルネックを個別に最適化

**具体的な改善項目:**

1. **非同期readback導入**
   - OpenGL: `GL_ARB_pixel_buffer_object`（PBO）を使用
   - Metal: 既存の非同期機構を活用（現状は同期実装）
   - 1-2フレーム遅延を許容し、GPU待機時間を削減

2. **Premultiplied Alpha変換のSIMD化**
   ```cpp
   // SSE4.1 / AVX2 / NEON を使った実装
   #if defined(__SSE4_1__)
       // 4ピクセル並列処理
   #elif defined(__ARM_NEON)
       // NEON intrinsics使用
   #endif
   ```

3. **不要なmemcpyの削減**
   - `SharedPixelBuffer` を事前確保し、直接書き込み
   - ダブルバッファリングで描画中の buffer 上書きを回避

4. **更新頻度制御の改善**
   - アイドル検出: 静止時はタイマーを停止
   - 適応的再描画期間: 操作の種類に応じて可変（50-500ms）

**メリット:**
- ✅ 実装難易度が低い（既存コードの改良）
- ✅ 全プラットフォームで適用可能
- ✅ 段階的な実装・検証が可能
- ✅ リスクが低い

**デメリット:**
- ❌ 根本的なreadbackは残る（帯域消費は継続）
- ❌ 改善効果は30-40%程度に留まる
- ❌ 非同期readbackによる1-2フレーム遅延

**実装難易度:** 低
**期待効果:** 30-40%のフレーム時間短縮
**適用範囲:** Linux/Windows/macOS全て

### 3.2 案B: OpenGL Texture共有（BorrowedOpenGLTexture）

**概要:** Slint の GPU rendering API を活用し、OpenGL texture を直接共有

**アーキテクチャ変更:**
```
[現在]
MapLibre GPU → readback → CPU buffer → Slint CPU rendering

[変更後]
MapLibre GPU → OpenGL texture → Slint GPU rendering (直接参照)
```

**技術的詳細:**

1. **Slint の BorrowedOpenGLTexture API使用**
   - MapLibre が描画した framebuffer texture の ID を取得
   - Slint に texture handle を渡す
   - readback と premultiply 変換が完全に不要に

2. **コンテキスト共有の要件**
   - MapLibre の OpenGL context と Slint の context を**共有**する必要
   - または同一 context で両方を描画
   - Slint の OpenGL backend を使用（Linux/Windowsで利用可能）

3. **実装上の課題:**
   - MapLibre HeadlessFrontend は独自 context を作成
   - Slint は winit 経由で window context を作成
   - **Context共有の設定が必要**（`EGLContext`/`wglShareLists`/`CGLShareContext`）

**参考実装パターン:**
```cpp
// 1. MapLibreのframebuffer textureを取得
GLuint texture_id = /* MapLibre frontendから取得 */;

// 2. Slint BorrowedOpenGLTexture を作成
auto borrowed_texture = slint::BorrowedOpenGLTextureBuilder()
    .texture_id(texture_id)
    .size({width, height})
    .build();

// 3. Slint Image として使用
return slint::Image(borrowed_texture);
```

**メリット:**
- ✅ readbackを**完全排除**（ゼロコピー実現）
- ✅ premultiply変換も不要
- ✅ 60-70%の性能改善が期待できる
- ✅ GPU memory内で完結（メモリ帯域の大幅削減）

**デメリット:**
- ❌ OpenGL context共有の実装が複雑
- ❌ macOS（Metal backend）では適用不可
- ❌ MapLibre側のコード改変が必要（HeadlessFrontend拡張）
- ❌ Slint の OpenGL backend必須（Vulkan/Metal不可）

**実装難易度:** 中
**期待効果:** 60-70%のフレーム時間短縮
**適用範囲:** Linux/Windows（OpenGL使用時のみ）

**技術的リスク:**
- Context共有の失敗リスク（プラットフォーム依存）
- Slint の OpenGL renderer との互換性
- デバッグ難易度の上昇

### 3.3 案C: WebGPU/wgpu統合によるゼロコピー

**概要:** 両システムを WebGPU API に統一し、texture 共有を実現

**アーキテクチャ変更:**
```
[変更後]
共通 wgpu Device/Queue
  ↓
MapLibre WebGPU backend → texture → Slint wgpu integration
```

**技術的詳細:**

1. **MapLibre WebGPU backend の活用**
   - MapLibre Native は実験的に WebGPU backend をサポート中
   - wgpu-rs (Rust) または Dawn (C++) を使用

2. **Slint wgpu integration の使用**
   - Slint は wgpu-rs ベースの renderer をサポート
   - 同じ `wgpu::Device` と `wgpu::Queue` を共有

3. **実装ステップ:**
   - MapLibre Native の WebGPU backend を有効化
   - 共通の wgpu Device/Queue を作成
   - MapLibre が描画した `wgpu::Texture` を Slint に渡す
   - Slint で直接参照してレンダリング

**メリット:**
- ✅ ゼロコピー実現（readback排除）
- ✅ 全プラットフォームで統一的な実装
- ✅ **WASM対応への道筋**（将来的にブラウザ対応可能）
- ✅ 最新グラフィックスAPI（Vulkan/Metal/DX12の抽象化）

**デメリット:**
- ❌ **実装難易度が非常に高い**
- ❌ MapLibre Native の WebGPU backend が**実験的**（安定性未保証）
- ❌ Rust/C++ 混在の複雑性
- ❌ 依存関係の大幅な増加（wgpu-rs, Dawn等）
- ❌ 実装期間が長い（6-12ヶ月）

**実装難易度:** 高
**期待効果:** 60-70%のフレーム時間短縮（案Bと同等）
**適用範囲:** Linux/Windows/macOS全て（理論上）

**技術的リスク:**
- MapLibre WebGPU backend の成熟度不足
- Slint wgpu integration の制約
- デバッグとメンテナンスの複雑化
- アップストリーム追従の困難さ

### 3.4 案D: Metal/Vulkan直接統合

**概要:** プラットフォーム別に最適なAPI統合を実装

**アプローチ:**
- **macOS:** Metal texture を Slint Metal renderer と共有
- **Linux:** Vulkan texture を Slint Vulkan renderer と共有
- **Windows:** DX12 texture または Vulkan texture を共有

**メリット:**
- ✅ 各プラットフォームで最高性能を実現
- ✅ ネイティブAPI活用（最小オーバーヘッド）

**デメリット:**
- ❌ **3つの異なる実装が必要**（保守コスト大）
- ❌ テストとデバッグの複雑化
- ❌ 実装難易度が非常に高い
- ❌ プラットフォーム間の動作差異リスク

**実装難易度:** 非常に高
**期待効果:** 70-80%のフレーム時間短縮
**適用範囲:** 各プラットフォーム個別

**結論:** コスト対効果が悪い（案B, Cの方が現実的）

### 3.5 比較表（意思決定マトリクス）

| 評価軸 | 案A: Readback最適化 | 案B: OpenGL Texture共有 | 案C: WebGPU統合 | 案D: Native API統合 |
|-------|-------------------|----------------------|----------------|-------------------|
| **性能改善インパクト** | △ (30-40%) | ◎ (60-70%) | ◎ (60-70%) | ◎◎ (70-80%) |
| **Readback排除** | ❌ 残る | ✅ 完全排除 | ✅ 完全排除 | ✅ 完全排除 |
| **Premultiply変換排除** | △ SIMD化のみ | ✅ 完全排除 | ✅ 完全排除 | ✅ 完全排除 |
| **メモリコピー排除** | △ 1回に削減 | ✅ ゼロコピー | ✅ ゼロコピー | ✅ ゼロコピー |
| **実装難易度** | ◎ 低 | ○ 中 | △ 高 | ❌ 非常に高 |
| **実装期間** | 1-2週間 | 3-6ヶ月 | 6-12ヶ月 | 12-18ヶ月 |
| **Linux対応** | ✅ | ✅ | ✅ | ✅ |
| **Windows対応** | ✅ | ✅ | ✅ | ✅ |
| **macOS対応** | ✅ | ❌ (Metal backend) | ✅ | ✅ |
| **依存関係の複雑さ** | ◎ なし | ○ Context共有必要 | △ wgpu追加 | ❌ 複数API必要 |
| **既存設計との整合性** | ◎ 完全互換 | ○ 小規模変更 | △ 大規模変更 | ❌ 全面書き換え |
| **WASM将来対応** | ❌ 不可 | ❌ 不可 | ✅ 可能 | ❌ 不可 |
| **アップストリーム追従性** | ◎ 容易 | ○ 比較的容易 | △ WebGPU安定化待ち | △ 複雑 |
| **保守性** | ◎ 高 | ○ 中 | △ 低 | ❌ 非常に低 |
| **技術リスク** | ◎ 低 | ○ 中 | △ 高 | ❌ 非常に高 |
| **非同期遅延** | △ 1-2フレーム | ✅ なし | ✅ なし | ✅ なし |
| **デバッグ容易性** | ◎ 容易 | ○ 中程度 | △ 困難 | ❌ 非常に困難 |

**総合評価:**

| 案 | 推奨度 | 適用シーン |
|----|-------|----------|
| **案A** | ⭐⭐⭐⭐⭐ | **Phase 1: 即座の改善**（全環境） |
| **案B** | ⭐⭐⭐⭐ | **Phase 2: 本命改善**（Linux/Windows） |
| **案C** | ⭐⭐ | Phase 3: 将来的検討（WASM対応が必須の場合） |
| **案D** | ⭐ | 推奨しない（コスト対効果が悪い） |

**推奨戦略: 二段階アプローチ**

1. **Phase 1（即座）:** 案A実装
   - 低リスクで即効性あり
   - 全プラットフォームで効果
   - 1-2週間で実装可能

2. **Phase 2（3-6ヶ月後）:** 案B実装
   - Linux/Windowsで大幅な性能向上
   - macOSは案Aの最適化を継続利用
   - 技術的負債を最小化

3. **Phase 3（1年後〜）:** 案C検討
   - WASM対応が事業要件になった場合
   - MapLibre WebGPU backendの安定化を待つ

## 4. 推奨案の詳細設計

### 4.1 推奨案の選定理由

**Phase 1（優先）: 案A - Readback継続・最適化アプローチ**

**選定理由:**

1. **即効性とリスクのバランス**
   - 実装期間1-2週間で30-40%の性能改善
   - 既存アーキテクチャを維持し、破壊的変更なし
   - 段階的な検証とロールバックが容易

2. **全プラットフォーム適用可能**
   - Linux/Windows/macOS で同一のアプローチ
   - プラットフォーム固有の問題を回避

3. **技術的実現性が高い**
   - 必要な技術（PBO、SIMD）は枯れた技術
   - 参考実装が豊富に存在
   - チーム内で完結可能（外部依存なし）

4. **Phase 2への布石**
   - コード構造の改善（計測ポイント追加等）
   - 性能特性の理解が深まる
   - 案Bへの移行が容易になる

**Phase 2（次優先）: 案B - OpenGL Texture共有**

**選定理由:**

1. **最大の性能改善**
   - 60-70%のフレーム時間短縮（ゼロコピー実現）
   - Linux/Windows で60fps安定達成

2. **実装難易度が妥当**
   - 案Cより大幅に簡単（wgpu不要）
   - OpenGL context共有は確立された技術

3. **macOSは案Aで十分**
   - macOSユーザーは案Aの最適化で対応
   - Metal統合は案Bが安定してから検討

### 4.2 段階的実装アプローチ

**Phase 1: Readback最適化（1-2週間）**

**Week 1:**
1. 計測基盤の整備
   - フレーム時間計測の追加
   - 各処理段階の時間分解
   - コンパイルフラグで有効化（`-DENABLE_PERF_METRICS`）

2. 非同期readback（PBO）実装
   - OpenGL: `glMapBufferRange` with `GL_MAP_READ_BIT`
   - 2つのPBOをダブルバッファリング
   - 1フレーム遅延を許容

3. SIMD化の準備
   - コンパイラによるSIMD検出
   - プラットフォーム別の実装分岐

**Week 2:**
4. Premultiply変換のSIMD実装
   - SSE4.1版（x86_64）
   - NEON版（ARM64）
   - スカラー版（フォールバック）

5. memcpy削減
   - SharedPixelBuffer の事前確保
   - ダブルバッファリング

6. 更新頻度制御の改善
   - アイドル検出ロジック
   - 適応的再描画期間

7. テストと計測
   - 各最適化の効果測定
   - リグレッションテスト

**Phase 2: OpenGL Texture共有（3-6ヶ月）**

**Month 1-2: 調査・設計**
- MapLibre HeadlessFrontend の拡張設計
- Context共有の方法調査（EGL/WGL/CGL）
- Slint BorrowedOpenGLTexture API の検証
- プロトタイプ実装（最小構成）

**Month 3-4: 実装**
- HeadlessFrontend の改造
  - Texture ID取得 API追加
  - 外部contextの受け入れ
- Context共有の実装
  - Linux: EGL context sharing
  - Windows: WGL context sharing
- Slint統合コード

**Month 5-6: テスト・安定化**
- 各種GPU/ドライバーでの動作確認
- エッジケースの洗い出し
- パフォーマンス最終調整
- ドキュメント整備

### 4.3 期待される性能改善ポイント

**Phase 1の効果（1920x1080, 60fps目標）:**

| 項目 | 現状 | Phase 1後 | 削減量 |
|------|------|----------|--------|
| glReadPixels待機 | 5-8ms | 0.5-1ms | **85-90%削減** |
| Premultiply変換 | 7-10ms | 1.5-2ms | **80-85%削減** |
| memcpy | 1-2ms | 0.5ms | **50-75%削減** |
| **フレーム合計** | **17-27ms** | **10-15ms** | **30-40%削減** |
| **実効FPS** | **37-59fps** | **67-100fps** | **+50-70%向上** |
| **CPU使用率** | 100% | 75-85% | **15-25%削減** |

**Phase 2の効果（1920x1080, 60fps目標）:**

| 項目 | Phase 1後 | Phase 2後 | 削減量 |
|------|----------|----------|--------|
| Readback（排除） | 0.5-1ms | **0ms** | **100%削減** |
| Premultiply（不要） | 1.5-2ms | **0ms** | **100%削減** |
| memcpy（排除） | 0.5ms | **0ms** | **100%削減** |
| Slint GPU rendering | - | 1-2ms | (新規) |
| **フレーム合計** | **10-15ms** | **5-9ms** | **50-60%削減** |
| **実効FPS** | **67-100fps** | **110-200fps** | +64-100%向上 |
| **メモリ帯域** | 500MB/s | **10MB/s** | **98%削減** |

**削減されるコピー:**
- Phase 1: GPU→CPU readback が非同期化（遅延隠蔽）
- Phase 2: GPU→CPU readback が**完全消失**（ゼロコピー）
- Phase 2: CPU内 memcpy も**完全消失**

**削減される同期点:**
- Phase 1: glReadPixels の同期待機 → 非同期PBOで隠蔽
- Phase 2: glReadPixels そのものが消失

### 4.4 実装難易度評価

**Phase 1: 低難易度（チーム内で完結）**

| タスク | 難易度 | 工数 | リスク |
|--------|--------|------|--------|
| 計測基盤 | ⭐ | 1日 | 低 |
| PBO実装 | ⭐⭐ | 2-3日 | 低 |
| SIMD実装 | ⭐⭐⭐ | 3-4日 | 中（プラットフォーム差異） |
| memcpy削減 | ⭐⭐ | 1-2日 | 低 |
| 更新頻度制御 | ⭐⭐ | 2日 | 低 |
| テスト | ⭐⭐ | 2-3日 | 低 |

**Phase 2: 中難易度（一部外部調査が必要）**

| タスク | 難易度 | 工数 | リスク |
|--------|--------|------|--------|
| API調査 | ⭐⭐⭐ | 1週間 | 中（ドキュメント不足） |
| Context共有実装 | ⭐⭐⭐⭐ | 2-3週間 | 高（プラットフォーム差異） |
| HeadlessFrontend改造 | ⭐⭐⭐ | 2週間 | 中（MapLibre理解が必要） |
| Slint統合 | ⭐⭐ | 1週間 | 低 |
| デバッグ | ⭐⭐⭐⭐ | 3-4週間 | 高（GPU問題は難解） |
| 各種環境テスト | ⭐⭐⭐ | 2-3週間 | 中 |

**必要なスキルセット:**

Phase 1:
- C++20（既存チームで対応可能）
- OpenGL基礎（PBO）
- SIMDプログラミング（学習コスト小）

Phase 2:
- OpenGL中級（context共有、EGL/WGL/CGL）
- GPUデバッグ経験（RenderDoc, Nsight等）
- Slint internals理解（API調査）

### 4.5 技術的リスクと対策

**Phase 1のリスク:**

| リスク | 発生確率 | 影響度 | 対策 |
|--------|---------|--------|------|
| PBO非対応GPU | 低 | 中 | スカラー版へフォールバック |
| SIMD実装のバグ | 中 | 中 | 広範な単体テスト、スカラー版と結果比較 |
| プラットフォーム差異 | 中 | 低 | CI/CDで3プラットフォーム自動テスト |
| 性能改善が想定以下 | 低 | 中 | 計測基盤で早期検出、調整 |

**Phase 2のリスク:**

| リスク | 発生確率 | 影響度 | 対策 |
|--------|---------|--------|------|
| Context共有失敗 | 中 | 高 | 複数の共有方法を試行、Phase 1へフォールバック |
| GPU/ドライバー互換性 | 高 | 高 | 幅広いハードウェアでテスト、既知の問題リスト作成 |
| Slint API制約 | 中 | 高 | 早期プロトタイプで検証、Slint開発チームと連携 |
| パフォーマンス劣化 | 低 | 中 | ベンチマーク継続監視、問題箇所の特定 |
| macOS非対応 | 確定 | 中 | Phase 1で対応（Metal統合は将来検討） |

**リスク低減戦略:**

1. **段階的実装**
   - Phase 1で基盤を固める
   - Phase 2で失敗してもPhase 1に戻れる

2. **早期プロトタイプ**
   - Phase 2の主要技術を小規模で検証
   - 実装前にリスクを洗い出し

3. **フィーチャーフラグ**
   - コンパイル時・実行時に新機能をON/OFF
   - 問題発生時に即座に旧実装へ切り替え

4. **広範なテスト**
   - 複数のGPU（NVIDIA/AMD/Intel）
   - 複数のOS（Ubuntu/Fedora/Arch, Windows 10/11）
   - 仮想環境での動作確認

## 5. PoC実装計画

### 5.1 Phase 1: 今すぐできる小改善

**実装範囲:** 最小限の変更で効果を確認

**優先順位:**
1. 計測基盤（必須・最優先）
2. SIMD化（効果大・リスク中）
3. 非同期readback（効果大・リスク中）
4. 更新頻度制御（効果中・リスク低）

**最小PoC: SIMD化のみ実装**
- 実装期間: 3-4日
- 効果測定: Premultiply変換時間の短縮
- 成功基準: 4-8倍の高速化

### 5.2 Phase 2: 本命改善（推奨案）

**実装範囲:** OpenGL Texture共有の最小実装

**PoC目標:**
- Linux環境でのゼロコピー描画実現
- 簡易デモアプリで動作確認
- パフォーマンス計測

**非目標（PoC段階では実装しない）:**
- Windows対応（Linuxで検証後）
- エラーハンドリングの完全性
- 本番品質のコード

### 5.3 変更ファイル一覧

**Phase 1: Readback最適化**

**新規作成:**
```
src/perf_metrics.hpp          # 計測基盤
src/perf_metrics.cpp          # 計測実装
src/simd_premultiply.hpp      # SIMD変換のヘッダー
src/simd_premultiply.cpp      # SIMD実装（SSE/NEON）
```

**変更:**
```
src/slint_maplibre_headless.hpp    # 計測メンバー追加、PBO管理
src/slint_maplibre_headless.cpp    # render_map()の改造
CMakeLists.txt                      # コンパイルフラグ、SIMDオプション
README.md                           # 性能改善の記載
```

**Phase 2: OpenGL Texture共有**

**新規作成:**
```
src/slint_maplibre_opengl.hpp      # OpenGL直接統合版
src/slint_maplibre_opengl.cpp      # 実装
platform/opengl_context_sharing.hpp # Context共有ヘルパー
platform/opengl_context_sharing.cpp # Linux/Windows実装
```

**変更:**
```
src/slint_maplibre_headless.hpp    # Texture取得API追加
src/slint_maplibre_headless.cpp    # Texture ID公開
examples/main.cpp                  # 実装切り替えロジック
CMakeLists.txt                      # OpenGL統合版のビルド設定
```

### 5.4 ビルド手順

**Phase 1: Linux（Ubuntu 24.04）**

```bash
# 1. 依存関係（既存と同じ）
sudo apt install -y build-essential cmake ninja-build \
    libgl1-mesa-dev libgles2-mesa-dev pkg-config

# 2. ビルド（計測機能有効）
cmake -B build \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DENABLE_PERF_METRICS=ON \
    -DENABLE_SIMD_OPTIMIZATIONS=ON \
    -G Ninja
    
cmake --build build -j$(nproc)

# 3. 実行
./build/maplibre-slint-example

# 4. 計測結果の確認
# stdout にフレーム時間の内訳が出力される
```

**計測出力例:**
```
[PerfMetrics] Frame 100:
  MapLibre render: 4.2ms
  glReadPixels:    6.8ms -> 0.9ms (PBO)
  Premultiply:     8.5ms -> 1.8ms (SIMD)
  memcpy:          1.2ms -> 0.6ms
  Slint render:    1.5ms
  Total:          22.2ms -> 9.0ms (-59%)
```

**Phase 2: Linux（OpenGL Texture共有）**

```bash
# 1. ビルド（OpenGL直接統合版）
cmake -B build \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DENABLE_OPENGL_TEXTURE_SHARING=ON \
    -G Ninja
    
cmake --build build -j$(nproc)

# 2. 環境変数（デバッグ用）
export LIBGL_DEBUG=verbose
export MESA_DEBUG=1

# 3. 実行
./build/maplibre-slint-example

# 4. GPU計測（オプション）
# RenderDocやapitrace使用
renderdoccmd capture ./build/maplibre-slint-example
```

**macOS（Phase 1のみ対応）**

```bash
# 1. ビルド
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DMLN_WITH_METAL=ON \
    -DMLN_WITH_OPENGL=OFF \
    -DENABLE_SIMD_OPTIMIZATIONS=ON \
    -G Xcode

cmake --build build

# 2. 実行
./build/Debug/maplibre-slint-example
```

### 5.5 計測方法

**基本計測（Phase 1, 2共通）**

1. **フレーム時間計測**
   ```cpp
   // src/perf_metrics.cpp での実装例
   auto start = std::chrono::high_resolution_clock::now();
   
   // 処理
   
   auto end = std::chrono::high_resolution_clock::now();
   auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
   ```

2. **FPS計測**
   - 100フレームの平均を計算
   - 最小/最大/標準偏差も記録

3. **CPU使用率**
   ```bash
   # Linux
   top -b -n 1 -p $(pgrep maplibre-slint)
   
   # macOS
   top -l 1 -pid $(pgrep maplibre-slint) | grep CPU
   ```

**詳細計測（Phase 1）**

4. **メモリ帯域幅**
   ```bash
   # Linux perf
   perf stat -e memory-loads,memory-stores \
       ./build/maplibre-slint-example
   ```

5. **SIMD効率**
   ```bash
   # perf でSIMD命令の使用率確認
   perf record -e cpu/event=0xc4,umask=0x01/u \
       ./build/maplibre-slint-example
   perf report
   ```

**詳細計測（Phase 2）**

6. **GPU計測**
   - **RenderDoc**: フレームキャプチャ、texture確認
   - **apitrace**: OpenGL call trace
   - **glxinfo**: Context情報

7. **Texture共有確認**
   ```cpp
   // デバッグログ
   std::cout << "MapLibre texture ID: " << texture_id << std::endl;
   std::cout << "Slint context: " << slint_context_info << std::endl;
   
   // glGetError() で OpenGL エラーチェック
   GLenum err = glGetError();
   if (err != GL_NO_ERROR) {
       std::cerr << "OpenGL error: " << err << std::endl;
   }
   ```

**計測シナリオ**

| シナリオ | 操作内容 | 計測項目 |
|---------|---------|---------|
| アイドル | 30秒間操作なし | CPU使用率、FPS（Phase 1で改善） |
| パン | 10秒間連続ドラッグ | フレーム時間、メモリ帯域 |
| ズーム | ホイール連続操作 | GPU待機時間、描画時間 |
| Fly-to | 都市間移動アニメ | 平均FPS、最低FPS |
| スタイル切替 | リモートスタイル読込 | 初回描画時間 |

### 5.6 成功基準

**Phase 1: Readback最適化**

**必須（Must）:**
- ✅ フレーム時間: **30%以上短縮**（17-27ms → 12-18ms）
- ✅ Premultiply変換: **4倍以上高速化**（SIMD効果）
- ✅ 全プラットフォームでビルド成功（Linux/Windows/macOS）
- ✅ 既存機能の動作維持（リグレッションなし）

**推奨（Should）:**
- ✅ CPU使用率: 15-25%削減
- ✅ 1920x1080で操作時55fps以上達成
- ✅ アイドル時のCPU使用率2%以下

**オプション（Nice to have）:**
- ✅ 4K解像度（3840x2160）で30fps以上
- ✅ 計測データのCSV出力機能

**Phase 2: OpenGL Texture共有**

**必須（Must）:**
- ✅ フレーム時間: **Phase 1から50%以上短縮**（12-18ms → 5-9ms）
- ✅ GPU→CPU readback: **完全排除**（glReadPixels呼び出し0回）
- ✅ Linux環境で安定動作（1時間連続稼働）
- ✅ マップ表示の正確性（Phase 1と視覚的に同一）

**推奨（Should）:**
- ✅ 1920x1080で60fps安定達成（操作時も）
- ✅ メモリ帯域: 95%以上削減（500MB/s → <25MB/s）
- ✅ Windows環境でも動作確認

**オプション（Nice to have）:**
- ✅ 4K解像度で60fps達成
- ✅ 複数GPUでの動作確認（NVIDIA/AMD/Intel）

**失敗基準（Phase 1, 2共通）:**

以下の場合は実装を中止し、代替案を検討：

- ❌ 性能改善が10%未満
- ❌ 重大なバグが2週間以上解決しない
- ❌ 特定プラットフォームで動作不可
- ❌ 実装工数が見積もりの2倍を超過

**ロールバック条件:**

- ❌ 本番環境で致命的なバグ発見
- ❌ 性能劣化が発生
- ❌ 保守性が著しく低下

→ 即座に前バージョンへ戻し、問題を分析

## 6. リスクと代替案

### 6.1 技術的リスク

**Phase 1: Readback最適化のリスク**

| リスク項目 | 詳細 | 発生確率 | 影響度 | 緩和策 |
|-----------|------|---------|--------|--------|
| **PBO非サポート** | 古いGPU/ドライバーでPBO未対応 | 低（10%） | 中 | 実行時検出でスカラー版フォールバック |
| **SIMD実装バグ** | 計算誤差、プラットフォーム差異 | 中（30%） | 高 | 広範な単体テスト、スカラー版と画素単位比較 |
| **性能改善不足** | 想定30%に届かず20%程度 | 中（25%） | 中 | 計測基盤で早期検出、追加最適化 |
| **リグレッション** | 既存機能の破壊 | 低（15%） | 高 | 自動テスト、ビジュアルリグレッション検証 |
| **メモリリーク** | PBOバッファの適切な解放忘れ | 低（10%） | 中 | Valgrind、AddressSanitizerで検証 |

**Phase 2: OpenGL Texture共有のリスク**

| リスク項目 | 詳細 | 発生確率 | 影響度 | 緩和策 |
|-----------|------|---------|--------|--------|
| **Context共有失敗** | EGL/WGL/CGLの設定ミス | 高（40%） | 致命的 | 複数手法の試行、Slintチームへ相談 |
| **Slint API制約** | BorrowedOpenGLTextureの想定外制約 | 中（30%） | 高 | 早期プロトタイプで検証 |
| **GPU互換性** | 特定GPU/ドライバーで動作不良 | 高（50%） | 高 | 幅広いハードウェアテスト、既知問題DB |
| **同期問題** | MapLibre描画完了前にSlintが参照 | 中（25%） | 高 | glFenceSync/glClientWaitSyncで同期 |
| **macOS非対応** | Metal backendで実装不可 | 確定（100%） | 中 | Phase 1で対応、Metal統合は将来課題 |
| **デバッグ困難** | GPUバグの原因特定が難解 | 高（60%） | 中 | RenderDoc必須、専門知識要 |

### 6.2 失敗時の代替案

**Phase 1が失敗した場合（性能改善<10%）**

**代替案1-A: より積極的な最適化**
- Intel IPP等の高度なSIMDライブラリ使用
- マルチスレッド化（複数フレームの並列処理）
- GPU compute shader による変換（OpenGL 4.3+）

**代替案1-B: 描画頻度の削減**
- 静止時はタイマー完全停止
- 低品質モード（解像度を動的に下げる）
- タイル単位の差分更新

**Phase 2が失敗した場合（Texture共有不可）**

**代替案2-A: Phase 1の徹底的な改良**
- 非同期readbackの更なる最適化
- マルチバッファリング（3-4フレーム分）
- 解像度の適応的調整

**代替案2-B: Metal統合（macOS限定）**
- Linux/WindowsはPhase 1維持
- macOSのみMetal texture共有を実装
- プラットフォーム別の最適化

**代替案2-C: WebGPU移行**
- 時間とリソースが許せば案Cを検討
- MapLibre WebGPU backendの成熟を待つ
- 1-2年の長期計画として位置づけ

**全体が失敗した場合（アーキテクチャ起因）**

**代替案X: 根本的な再設計**
- MapLibreをGPU直接統合せず、tile画像をHTTP経由取得
- Slint側で独自のtile rendering実装
- ただし、MapLibreの機能を放棄することになる（最終手段）

### 6.3 長期的な展望

**1年後（2027年初頭）:**
- Phase 1が全環境で安定動作
- Phase 2がLinux/Windowsで本番投入
- macOSユーザーはPhase 1で満足している状態

**2年後（2028年初頭）:**
- WebGPU標準化の進展を確認
- MapLibre WebGPU backendの安定化を評価
- 案C（WebGPU統合）の実装可否を再検討

**3年後（2029年初頭）:**
- WASM対応が事業要件になった場合、WebGPU統合を実施
- または、Metal/Vulkan直接統合も選択肢として再浮上
- プラットフォーム別の最適化が進み、安定性向上

**技術トレンドの監視項目:**
- **Slint wgpu integration の成熟度**
  - 現在: 実験的（wgpu-rsベース）
  - 監視: 安定版リリース、採用事例の増加
  
- **MapLibre WebGPU backend の状況**
  - 現在: 実験的実装（一部機能のみ）
  - 監視: 正式リリース、パフォーマンス報告

- **OpenGL → 次世代APIへの移行**
  - Linux: Vulkan採用増加
  - Windows: DX12との共存
  - macOS: Metal義務化（OpenGL非推奨）

**将来的なアーキテクチャ選択肢:**

```
2027年: Phase 1 (Readback最適化) + Phase 2 (OpenGL共有)
         ↓
2028年: WebGPU評価、Metal統合検討
         ↓
2029年: 案C (WebGPU統合) または 案D (Native API統合)
         ↓
2030年: WASM対応、クロスプラットフォーム統一
```

**保守性の考慮:**
- 各Phaseは独立して動作可能にする
- フィーチャーフラグで切り替え可能
- 後方互換性を維持（古い実装も残す）

**コミュニティ貢献:**
- SIMD実装をMapLibre Native本体へPR
- Slint OpenGL統合のベストプラクティスを文書化
- 性能改善の知見をブログ等で共有

## Appendix

### A. 調査に使用したコード箇所の索引

**描画パイプライン関連:**
- [README.md#L127-L131](README.md#L127-L131) - パイプライン説明
- [src/slint_maplibre_headless.cpp#L193-L260](src/slint_maplibre_headless.cpp#L193-L260) - `render_map()`関数
- [examples/main.cpp#L20-L38](examples/main.cpp#L20-L38) - レンダリングコールバック

**GPU→CPU Readback:**
- [vendor/maplibre-native/src/mbgl/gl/context.cpp#L318-L341](vendor/maplibre-native/src/mbgl/gl/context.cpp#L318-L341) - `readFramebuffer()`実装
- [vendor/maplibre-native/platform/default/src/mbgl/gl/headless_backend.cpp#L115-L118](vendor/maplibre-native/platform/default/src/mbgl/gl/headless_backend.cpp#L115-L118) - OpenGL版
- [vendor/maplibre-native/platform/default/src/mbgl/mtl/headless_backend.cpp#L85-L87](vendor/maplibre-native/platform/default/src/mbgl/mtl/headless_backend.cpp#L85-L87) - Metal版

**Premultiplied Alpha変換:**
- [vendor/maplibre-native/src/mbgl/util/premultiply.cpp#L29-L51](vendor/maplibre-native/src/mbgl/util/premultiply.cpp#L29-L51) - `unpremultiply()`実装
- [src/slint_maplibre_headless.cpp#L225-L226](src/slint_maplibre_headless.cpp#L225-L226) - 呼び出し箇所

**フレーム更新制御:**
- [examples/map_window.slint#L49-L53](examples/map_window.slint#L49-L53) - 16msタイマー
- [examples/main.cpp#L33-L38](examples/main.cpp#L33-L38) - `tick_map_loop()`
- [src/slint_maplibre_headless.cpp#L370-L377](src/slint_maplibre_headless.cpp#L370-L377) - RunLoop実行
- [src/slint_maplibre_headless.cpp#L166-L176](src/slint_maplibre_headless.cpp#L166-L176) - MapObserver
- [src/slint_maplibre_headless.hpp#L40-L49](src/slint_maplibre_headless.hpp#L40-L49) - RendererObserver

**プラットフォーム差分:**
- [CMakeLists.txt#L47-L54](CMakeLists.txt#L47-L54) - macOS設定
- [CMakeLists.txt#L145-L155](CMakeLists.txt#L145-L155) - Windows設定
- [src/slint_maplibre_headless.cpp#L47-L54](src/slint_maplibre_headless.cpp#L47-L54) - RunLoop分岐

**メモリコピー:**
- [vendor/maplibre-native/src/mbgl/gl/context.cpp#L331](vendor/maplibre-native/src/mbgl/gl/context.cpp#L331) - glReadPixels
- [src/slint_maplibre_headless.cpp#L232-L234](src/slint_maplibre_headless.cpp#L232-L234) - memcpy

### B. 参考資料

**OpenGL関連:**
- [OpenGL Wiki - Pixel Buffer Object](https://www.khronos.org/opengl/wiki/Pixel_Buffer_Object)
- [OpenGL Wiki - Sync Object](https://www.khronos.org/opengl/wiki/Sync_Object)
- [OpenGL Context Sharing (EGL)](https://www.khronos.org/registry/EGL/sdk/docs/man/html/eglCreateContext.xhtml)

**SIMD最適化:**
- [Intel Intrinsics Guide](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html)
- [ARM NEON Programmer's Guide](https://developer.arm.com/architectures/instruction-sets/simd-isas/neon/neon-programmers-guide-for-armv8-a)
- [Agner Fog's Optimization Manuals](https://www.agner.org/optimize/)

**Slint:**
- [Slint Documentation](https://slint.dev/docs)
- [Slint C++ API Reference](https://releases.slint.dev/latest/docs/cpp/)
- [Slint GitHub Repository](https://github.com/slint-ui/slint)

**MapLibre Native:**
- [MapLibre Native Documentation](https://maplibre.org/maplibre-native/docs/)
- [MapLibre Native GitHub](https://github.com/maplibre/maplibre-native)
- [HeadlessFrontend API](https://github.com/maplibre/maplibre-native/tree/main/platform/default/include/mbgl/gfx)

**WebGPU:**
- [WebGPU Specification](https://www.w3.org/TR/webgpu/)
- [wgpu-rs Documentation](https://docs.rs/wgpu/latest/wgpu/)
- [Dawn (Google's WebGPU implementation)](https://dawn.googlesource.com/dawn)

**パフォーマンス計測:**
- [RenderDoc](https://renderdoc.org/)
- [NVIDIA Nsight Graphics](https://developer.nvidia.com/nsight-graphics)
- [Intel Graphics Performance Analyzers](https://www.intel.com/content/www/us/en/developer/tools/graphics-performance-analyzers/overview.html)
- [Valgrind](https://valgrind.org/)
- [Linux perf](https://perf.wiki.kernel.org/index.php/Main_Page)

**類似事例・参考実装:**
- [Qt Quick SceneGraph - Texture Sharing](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph-renderer.html)
- [Dear ImGui + Custom Rendering](https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples)
- [Skia + OpenGL Integration](https://skia.org/docs/user/sample/hello_world_opengl/)

---

**文書情報:**
- 作成日: 2026年1月9日
- 対象リポジトリ: maplibre-native-slint
- 作成者: AI Agent (GitHub Copilot)
- レビュー状態: Draft
- 次のアクション: チームレビュー → Phase 1実装開始
