# Baseline 2026-07-14 — Mobile Matrix (X3, initial)

**축**: X (저사양·모바일 실측) · **단계**: X3 (실 기기 실측) · **상태**: **draft** —
두 티어 (T-high · T-mobile-high) 최초 캡처. 나머지 티어 (Android Chrome ·
Intel iGPU 노트북 · Safari.app iOS) 는 append 예정.

## Harness

- 커밋 `fda77f5` 의 in-browser 하네스 (`?bench=1&dwell=<n>`)
- 셸: `tests/volume_viewer_shell.html` (X2 additions + 폰용 collapse toggle)
- 배포 경로: Cloudflare Quick Tunnel (`cloudflared tunnel --url http://localhost:8000`)
  로 임시 HTTPS URL — WebGPU 는 secure context 필요, LAN IP `http://` 로는
  `navigator.gpu === undefined`
- Volume: 번들된 fMRI 시리즈 `sample_dicom_mr` (64×64×10, 0.08 MB dense,
  page 1×1×1, atlas 1×1×1)
- 4 모드 순환: **lambert (SPP=8)** · **pt_spp1** · **pt_spp4** ·
  **pt_spp8+denoise**, 각 1.5 s 워밍 + `dwell` 초 샘플링 (~100 ms 폴)

---

## 커버된 티어

| Tier | 기기 | 브라우저 | UA (요약) |
| --- | --- | --- | --- |
| **T-high** | Windows 데스크톱 dGPU | Chrome 150 | Windows 10, NVIDIA RTX 4070 (Lovelace) |
| **T-mobile-high (a)** | iPhone (A17 급 추정) | KakaoTalk **in-app WKWebView** | iOS 18.7, WebKit 605, KAKAOTALK/26.5.6 (INAPP) |
| **T-mobile-high (b)** | 같은 iPhone | **Safari.app 26.5** (정식) | iOS 18.7, WebKit 605, `Version/26.5 Mobile/15E148 Safari/604.1` |

**Append 예정**:

- T-mid-desktop — Intel iGPU (Iris Xe · UHD)
- T-mobile-mid — Android Chrome (Adreno 6xx / Mali-G7x)

---

## 프레임 타임 크로스 티어 (dwell=10 s)

| Mode | Cfg | T-high mean/max (ms) | mobile-high(a) KakaoTalk WKWebView | mobile-high(b) Safari.app 26.5 |
| --- | --- | ---:| ---:| ---:|
| lambert   | SPP=8 (참고, no PT)    | 3.84 / 4.60 | **1.43 / 2.24** | 1.57 / 3.33 |
| pt_spp1   | path-trace, no denoise | 2.19 / 3.02 | 1.57 / 1.95     | 1.67 / 1.96 |
| pt_spp4   | path-trace, no denoise | 3.42 / 4.39 | 4.01 / **19.27** | **12.84 / 24.60** (3× WKWebView) |
| pt_spp8   | path-trace + A-trous   | 5.12 / 6.50 | **42.4 / 53.3**  | **38.16 / 42.69** |

**iPhone 샘플 수**:
- KakaoTalk WKWebView — lambert 89 · pt_spp1 89 · pt_spp4 66 · pt_spp8 14
- Safari.app — lambert 92 · pt_spp1 85 · pt_spp4 43 · pt_spp8 11

pt_spp8 은 iPhone 두 브라우저 공히 프레임당 ~700 ms → 폴 간격 100 ms 이 프레임을
못 따라잡음. mean 자체는 정확, 통계 신뢰도만 낮음.

## 관측 · 결정 근거

### 관측 1 — iOS 18.7 에서 WebGPU **기본 활성** (Safari.app + WKWebView 양쪽)

X1 매트릭스에는 "Safari 26 (iOS 26)+ 부터 완전 지원" 이라고 적었으나, 실측 결과:

- **iOS 18.7 + Safari.app 26.5** (`Version/26.5 Mobile/15E148 Safari/604.1`) —
  별도 flag 조작 없이 즉시 동작. Adapter info, 15 features, limits 모두 정상.
- **iOS 18.7 + KakaoTalk in-app WKWebView** — 동일 (WebKit 공통 기반).

Safari 앱 버전 넘버 (`26.5`) 는 iOS 버전 (`18.7`) 과 어긋나 있음 — Apple 이
Safari 를 26 브랜드로 앞선 릴리즈. WebKit 자체는 605 시리즈. 즉 **iOS 18 사용자
대다수가 이미 WebGPU 접근 가능**. X1 매트릭스의 "iOS 26+" 는 과보수적, 실사용
커버리지는 훨씬 넓음. → X1 iOS 행 정정 (본 커밋에 포함).

### 관측 2 — 저부하에선 iPhone > RTX 4070

Lambert 1.43 ms vs 3.84 ms, pt_spp1 1.57 vs 2.19. fMRI 크기 (40k voxels) 에선
프레임 시간이 **커맨드 오버헤드 + waitIdle 지연** 이 지배. Apple Silicon 의
unified memory + Metal 저지연 파이프라인이 dGPU 의 PCIe 왕복보다 유리.

**함의**: 원 목표 (저사양·모바일에서 실 CT/MRI) 의 정직한 증거 — **인프라만 잘
두면 모바일이 실용 영역에서 이김**. dGPU 는 대용량·고 SPP path-trace 에서만
차별화. 접근성 축은 정합적 방향.

### 관측 3 — pt_spp8+denoise 는 iPhone 에서 24 fps

42 ms mean = 24 fps, max 53 ms = 19 fps. 카메라 회전 시 더 나빠질 것. dGPU 는
같은 조건에서 5 ms (200 fps). 8 배 격차.

**함의 (X4 auto policy 초안 검증)**:

- mobile-high 티어 기본값에서 **path-trace off**, **denoise off** 유지가 옳음.
- 사용자가 명시적으로 켜는 옵션으로 남기되, SPP 상한 (mobile 티어 4 등) 을
  강제하는 UI 게이팅 검토 필요.

### 관측 4 — pt_spp4 iPhone: WKWebView 4 ms vs Safari.app 12.8 ms (3× 격차)

카톡 WKWebView 는 mean 4.01 ms (p95 15 ms 스파이크는 있지만 baseline 은 낮음),
Safari.app 26.5 는 mean 12.84 ms (p50 12 ms, 즉 상시). 같은 iPhone / 같은 iOS /
같은 WebKit 605 임에도 3× 차이. pt_spp1 · pt_spp8 은 두 브라우저 거의 동일하고
pt_spp4 만 격차.

원인 후보 (미확정):

- **Path-trace 파이프라인 shader compile 캐시 차이** — 두 브라우저가 서로 다른
  cache 사용, 재컴파일 트리거 상황이 다름. 카톡은 pt_spp1 을 앞서 컴파일 → pt_spp4
  는 SPP loop 이 다른 파이프라인이나 재사용 성공, Safari 는 재컴파일 발생 등.
- **Foreground/background scheduling 차이** — Safari.app 의 URL bar / 시스템
  UI overhead 가 WKWebView 보다 큼.
- **Sample selection artifact** — Safari.app 은 43 샘플로 KakaoTalk 66 대비
  적음. Warm-up 부족 가능성. `?dwell=20` 재실행으로 확인 필요.

**함의**: 동일 하드웨어에서도 브라우저별 성능 프로파일이 다를 수 있음 → X4
policy 는 UA 뿐 아니라 실측 pilot burst 로 tier fine-tuning 필요할 수도 있음
(future work).

### 관측 5 — Z1 (adaptive SPP by motion) 데스크톱 검증 완료

같은 세션에서 Z1 (adaptive SPP by motion, 커밋 `8ce5a9b`) 데스크톱 검증:

- Path-traced SPP=8 + denoise, RTX 4070: 정지 ~5 ms → 드래그 중 ~2 ms → 릴리즈
  250 ms 후 ~5-6.5 ms (accum reset 오차 안).
- adaptive off 토글: 드래그 중에도 ~5 ms 유지 (control).

iPhone Safari.app 벤치에선 adaptive on 상태로 순수 벤치 (motion 없음) 는
pt_spp8+denoise 38 ms 그대로 (adaptive 트리거 안 됨 = 정확한 baseline 캡처).
Ring-buffer 통계는 사용자 상호작용 중 mean 9-10 ms 로 낮춰지는 것이 스크린샷에
잡힘 — 간접적으로 Z1 mobile 임팩트 확인. 정식 mobile Z1 baseline (드래그 중 프레임
타임 히스토그램) 은 후속 세션에서 별도 캡처 가능.

---

## Raw device profiles

### T-high (Chrome 150 · Windows · RTX 4070)

```json
{
  "ua": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 ... Chrome/150",
  "screen": { "w": 2560, "h": 1440, "dpr": 1 },
  "gpu": {
    "info": { "vendor": "nvidia", "architecture": "lovelace",
              "device": "0x2786", "description": "NVIDIA GeForce RTX 4070" },
    "features": ["depth32float-stencil8", "rg11b10ufloat-renderable",
                 "texture-formats-tier1", "bgra8unorm-storage",
                 "texture-compression-bc", "dual-source-blending",
                 "core-features-and-limits", "float32-filterable",
                 "indirect-first-instance", "float32-blendable",
                 "depth-clip-control", "texture-compression-bc-sliced-3d",
                 "texture-formats-tier2", "shader-f16", "timestamp-query",
                 "clip-distances", "primitive-index",
                 "texture-component-swizzle", "subgroups"],
    "limits": {
      "maxTextureDimension2D": 16384, "maxTextureDimension3D": 2048,
      "maxTextureArrayLayers": 2048, "maxStorageBufferBindingSize": 2147483644,
      "maxUniformBufferBindingSize": 65536, "maxBufferSize": 2147483648,
      "maxComputeInvocationsPerWorkgroup": 1024, "maxComputeWorkgroupSizeX": 1024,
      "maxComputeWorkgroupsPerDimension": 65535, "maxSamplersPerShaderStage": 16,
      "maxSampledTexturesPerShaderStage": 48
    }, "isFallback": false
  }, "tier": "high"
}
```

### T-mobile-high (a) — KakaoTalk in-app WKWebView · iPhone iOS 18.7 · Apple GPU

```json
{
  "ua": "Mozilla/5.0 (iPhone; CPU iPhone OS 18_7 like Mac OS X) AppleWebKit/605.1.15 ... KAKAOTALK/26.5.6 (INAPP)",
  "screen": { "w": 393, "h": 852, "dpr": 3 },
  "gpu": {
    "info": { "vendor": "apple", "architecture": "apple",
              "device": "apple", "description": "apple" },
    "features": ["depth-clip-control", "depth32float-stencil8",
                 "timestamp-query", "texture-compression-etc2",
                 "texture-compression-astc", "texture-compression-astc-sliced-3d",
                 "indirect-first-instance", "shader-f16",
                 "rg11b10ufloat-renderable", "bgra8unorm-storage",
                 "float32-blendable", "float16-renderable", "float32-renderable",
                 "core-features-and-limits", "texture-formats-tier1"],
    "limits": {
      "maxTextureDimension2D": 16384, "maxTextureDimension3D": 2048,
      "maxTextureArrayLayers": 2048, "maxStorageBufferBindingSize": 1073741824,
      "maxUniformBufferBindingSize": 1073741824, "maxBufferSize": 1073741824,
      "maxComputeInvocationsPerWorkgroup": 1024, "maxComputeWorkgroupSizeX": 1024,
      "maxComputeWorkgroupsPerDimension": 65535, "maxSamplersPerShaderStage": 22,
      "maxSampledTexturesPerShaderStage": 44
    }, "isFallback": false
  }, "tier": "mobile-high"
}
```

### T-mobile-high (b) — Safari.app 26.5 · 같은 iPhone iOS 18.7 · Apple GPU

Adapter info · features · limits 는 T-mobile-high (a) 와 **완전히 동일** (동일한
WebKit / Metal 스택). UA 만 다름:

```text
Mozilla/5.0 (iPhone; CPU iPhone OS 18_7 like Mac OS X) AppleWebKit/605.1.15
(KHTML, like Gecko) Version/26.5 Mobile/15E148 Safari/604.1
```

`Version/26.5` 가 Safari 앱 브랜딩 버전 (iOS 18 위에 Safari 26 이 나온 것 —
Apple 이 Safari 를 OS 와 분리된 릴리즈 사이클로 앞선 것). 그러나 프레임 타임은
pt_spp4 에서 3× 격차 발생 — 관측 4 참조.

**주목할 limit 차이** (RTX vs iPhone; iPhone 두 브라우저는 동일):

- `maxStorageBufferBindingSize`: RTX 2 GiB vs iPhone 1 GiB — 우리 page table 은
  1024³ 볼륨에서도 64 KiB 이라 둘 다 여유 매우 큼.
- `maxBufferSize`: RTX 2 GiB vs iPhone 1 GiB — 큰 staging 버퍼 (예: 512 MB 볼륨
  통업로드) 시 iPhone 에서 걸릴 가능성. 우리 brick 단위 업로드는 66³×2 = 574 KB
  로 무관.
- `maxTextureDimension3D`: 둘 다 2048 (WebGPU 스펙 하한). 임상 CT 1024³ 볼륨의
  atlas (~1056³) 는 안전 마진 안.
- `maxSamplersPerShaderStage`: iPhone 22 (RTX 16). 유리하나 우리는 sampler 1 개.
- iPhone 은 `subgroups` · `float32-filterable` · `texture-compression-bc` **미지원**
  — 후자는 무관 (우리는 R16Float), subgroups 는 향후 최적화 옵션에서 mobile 은
  fallback 필요.

---

## X4 policy 초안 검증

X1 이 제안한 초안:

| Tier | atlas | LOD | PT default | Denoise default | SPP | K |
|---|---|---|---|---|---|---|
| high | 무제한 | L3 | on | on | 8 | 64 |
| mobile-high | 256 MB | L2 | **off** | **off** | 4 | 16 |

이번 실측이 뒷받침:

- ✅ **mobile-high PT default off** — 42 ms/frame 에서 사용자 경험 붕괴. off 가 옳음.
- ✅ **mobile-high Denoise default off** — path-trace 자체가 꺼져있으면 denoise 도 무의미.
- ⚠ **mobile-high SPP 상한 4** — pt_spp4 는 mean 4 ms 로 실용 (240 fps), 다만
  jitter 큼. 상한을 4 로 두되 UI 에 "고성능 GPU 권장" 힌트 필요.
- ❓ **atlas 256 MB · LOD L2** — 이번 볼륨 (0.08 MB) 로는 검증 불가. 대용량
  볼륨 append 필요 (축 Y 이후).
- ❓ **K budget 16** — Streaming 이 이번 볼륨에선 트리거 안 됨 (Static). Y 축
  이후 검증.

## Follow-ups

1. ~~**X1 매트릭스 정정**: iOS 18 실측 결과 반영~~ ✅ 완료 (본 커밋 포함,
   Safari.app · WKWebView 양쪽 확인).
2. ~~**Safari.app control test**~~ ✅ 완료 (본 append). 예상 밖 결과 —
   pt_spp4 만 3× 격차, 관측 4 로 별도 조사 항목 유지.
3. **pt_spp4 브라우저 격차 조사**: `?dwell=20` 재실행 후 sample warm-up
   충분 상태에서 재측정, 여전히 3× 이면 pipeline compile cache 차이로
   결론 짓고 X4 policy 는 UA (WKWebView vs Safari.app) 도 참고 시그널로.
4. **Z1 mobile 명시적 baseline**: 정지 pt_spp8 42 ms → 드래그 중 프레임
   타임 분포 캡처. 벤치 하네스에 "auto-orbit" 모드 추가하면 자동화 가능
   (지금 harness 는 정지 벤치만).
5. **Android · Intel iGPU** 실측 후 본 문서 append (준비된 기기 없음).
6. **대용량 볼륨 실측**: 축 Y 완료 후 T-mobile-high 에서 512×512×N 실 CT
   재실측. 지금은 fMRI 만이라 atlas·LOD·streaming policy 검증 불가.
