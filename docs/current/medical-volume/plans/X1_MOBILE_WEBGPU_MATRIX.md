# X1 — Mobile WebGPU Support Matrix & Engine Compatibility

**축**: X (저사양/모바일 실측) · **단계**: X1 (조사) · **작성**: 2026-07-10

축 X 는 원 목표 (저사양 PC · 모바일 브라우저에서 대용량 CT/MRI 렌더링) 의
정면 갭인 "데스크톱 dGPU 에만 실측" 을 좁힌다. X1 은 후속 X2 (실측 하네스)
· X3 (실 디바이스 실측) · X4 (사양 티어별 자동 policy) 의 근거를 만드는 조사
단계.

---

## 1. 브라우저 지원 매트릭스 (2026-07 현재)

### 데스크톱 (참조 기준)

| 브라우저 | 상태 | 비고 |
| --- | --- | --- |
| Chrome / Edge (Chromium+Dawn) | ✅ 113~ 안정 | macOS · Windows · ChromeOS 전면; Linux Intel Gen12+ 144~, NVIDIA 147~ |
| Safari (WebKit) | ✅ 26.0 안정 | macOS Tahoe 26 (2025 fall) |
| Firefox (wgpu) | ✅ 141 (Windows) · 145 (macOS Apple Silicon 26+) · 147 (macOS 전 버전) | Linux/mac other 는 Nightly only |

### 모바일 — X 축의 실제 타깃

| 플랫폼 | 상태 | 하드웨어 요구 | 커버리지 (2026 Q1 기준) |
| --- | --- | --- | --- |
| **Chrome for Android** | ✅ 121~ 안정 | Android 12+, Qualcomm Adreno 6xx+ 또는 ARM Mali-G78+; Imagination GPU 139~ | ~78% |
| **Safari iOS/iPadOS** | ✅ 26.0 정식 · **⚠ iOS 18+ 도 experimental 로 이미 활성** (실측 확인) | iOS/iPadOS 26 정식; iOS 18 은 WKWebView + experimental flag 조건부 | OS 26 롤아웃 진행 중; 18+ 는 앱별 |
| **Safari visionOS** | ✅ 26 | visionOS 26+ | — |
| **Samsung Internet** | ✅ 24~ | Adreno 6xx+ | Galaxy 기기 상당수 |
| **Opera Mobile** | ✅ 80~ | Chrome for Android 와 유사 | — |
| **Firefox Android** | ❌ flag 뒤 | Mozilla 는 2026 년 내 작업 예정 | 0% (기본 off) |
| **Chromium on iOS** | ❌ 불가 | WebKit 강제 → Safari 26 통과할 뿐 | Safari 규칙과 동일 |

**전체 브라우저 (데스크톱+모바일) 글로벌 커버리지**: ~83% (caniuse
2026-07 기준).

### 우리 축 X 의 커버리지 결론

- **Android Chrome (Adreno 6xx+ / Mali-G78+)** — 최대 실사용 타깃, 커버리지
  넓음. X3 실측의 primary device.
- **iOS Safari (iPhone/iPad iOS 26+)** — 정식 완전 지원. **iOS 18+ WKWebView
  경로 도 실측 상 동작 확인** (2026-07-14 카카오톡 in-app 브라우저에서 iPhone
  iOS 18.7 성공, [BASELINE_2026-07-14_MOBILE_MATRIX.md](../baselines/BASELINE_2026-07-14_MOBILE_MATRIX.md)).
  즉 iOS 커버리지가 예상보다 넓음 — WebGPU 활성 웹뷰를 쓰는 앱을 통하면 iOS
  18+ 사용자도 접근 가능.
- **Chromebook (Android Chrome 기반)** — 저사양 데스크톱 대체재. Adreno/Mali
  없이 Intel iGPU 인 모델도 있음 (Chromium 데스크톱 Intel Gen12+ 경로).
- **Firefox Android** — flag off 이므로 실측 대상에서 제외.
- **iOS < 26 · Android 11 이하 · Adreno 5xx 이하** — 하드웨어/OS 미충족.
  Fallback UX (지원 안 됨 안내) 로 처리, 렌더 성공 목표는 없음.

---

## 2. Mini-Engine 스택 mobile 호환성 진단

우리 엔진이 실제로 쓰는 기능 각각을 모바일 WebGPU 스펙 · 알려진 문제에 대비
검사.

### 2.1 텍스처 · 포맷

| 항목 | 우리 사용 | 스펙 · 모바일 | 판정 |
| --- | --- | --- | --- |
| R16Float 3D 텍스처 | brick atlas (66³ 표준, per-axis shrink) | WebGPU 코어 필터러블. **shader-f16 기능은 별개** — Qualcomm 은 uniform/storage 에 f16 노출 불가. 우리는 텍스처로만 씀, WGSL 에서 f32 로 샘플. | ✅ 안전 |
| RGBA16Float 2D | path-trace history · denoise ping-pong | 코어 렌더 어태처블. | ✅ 안전 |
| RGBA8UnormSrgb / RGBA8Unorm | 머티리얼 · TF LUT | 코어. | ✅ 안전 |
| Depth32Float | occlusion depth (있으면) | 코어. | ✅ 안전 |
| maxTextureDimension3D | 우리 최대 volumetric atlas 축 (예: 8×66=528) | 스펙 하한 2048. 실 임상 CT 1024³ pageGrid 16×16×16 × 66 = 1056³ — 안전. | ✅ 안전 (2048 하한 안) |

### 2.2 버퍼 · 컴퓨트

| 항목 | 우리 사용 | 스펙 · 모바일 | 판정 |
| --- | --- | --- | --- |
| Storage buffer (page table) | pageGrid × 4 B — 예: 32³ = 128 KB, 1024³ = 16 KB | 스펙 하한 `maxStorageBufferBindingSize` 128 MiB. 여유 큼. | ✅ 안전 |
| Compute shader | occupancy grid build (4×4×4 workgroup) | WebGPU 코어. Adreno/Mali 모두 지원. | ✅ 안전 |
| Indirect draw | GPU frustum cull (엔진 코어) — 현재 볼륨 뷰어에선 미사용 | 코어. | N/A (볼륨 뷰어) |
| timestamp-query | GPU 프로파일러 | 옵션 기능. 이미 `wgpuAdapterHasFeature` 로 조건부 활성 + CPU 타이밍 fallback 구현됨. | ✅ 안전 |

### 2.3 WASM · Emscripten

| 항목 | 우리 설정 | 모바일 위험 | 판정 |
| --- | --- | --- | --- |
| `MAXIMUM_MEMORY=4 GB` (32-bit WASM) | CMakeLists.txt L918/1026/1082 | 모바일 총 RAM 3~8 GB 흔함. 큰 DICOM 시리즈 (수백 MB) 를 memfs 로 통째 로드하면 heap 폭증 → 축 Y (chunk 페이징) 로 정면 대응 예정. | ⚠️ 축 Y 로 완화 |
| `INITIAL_MEMORY=128 MB` | 동일 | 128 MB 초기는 저사양에서도 안전. `ALLOW_MEMORY_GROWTH=1` 로 필요 시 grow. | ✅ 안전 |
| `ASYNCIFY=1` + `STACK_SIZE=16 KB` | fence wait `emscripten_sleep` | 이미 데스크톱 Chrome 검증 완료. 모바일에서도 동일 emscripten runtime — 실행 자체 안전. `Module._wasmBusy` 재진입 가드 이미 구축. | ✅ 안전 (동작 확인은 실측 필요) |
| OpenJPEG + libjpeg-turbo 정적 링크 | +670 KB WASM | 모바일 다운로드는 wasm 1.4 MB — 3G 에선 느리나 WiFi/LTE 는 즉시. | ✅ 안전 |

### 2.4 사양별 예상 위험

- **Android Chrome / Adreno 6xx (Snapdragon 8xx 이하)**: shader compile 시간
  ↑, path-trace SPP=8 이 프레임 예산 초과 예상 → 축 Z (adaptive SPP · path-trace
  기본 off tier) 로 대응.
- **iOS Safari (iPhone A15+)**: Metal 기반, GPU 성능은 상위. 실제 병목은
  ASYNCIFY WASM runtime — 모바일 Safari 의 background tab throttling 이
  fence wait 을 얼마나 견디는지 실측 필요.
- **Intel iGPU 노트북 (UHD 620/630 급)**: 5+ 세대 오래된 iGPU → WebGPU
  hardware acceleration 존재. shader-f16 없음 (spec 상관 없음), path-trace
  는 무리, Lambert+shadow 는 가능 예상.
- **Chromebook (ARM)**: Adreno/Mali 축과 동일.
- **iOS < 26 · Android < 12 · Adreno 5xx**: WebGPU 미지원 → `navigator.gpu`
  falsy 로 감지 후 안내 UX. 실측 목표 아님.

### 2.5 결론

원 목표 lens 로 우리 엔진 스택은 **mobile WebGPU 스펙 자체에는 근본 위험이
없음**. 진짜 리스크는 세 가지:

1. **성능 예산 초과** — path-trace + denoise + shadow 조합이 모바일 GPU 에
   너무 무거울 것. → **축 Z (adaptive SPP, tier auto policy)** 로 대응.
2. **RAM/heap 폭증** — 큰 DICOM 을 memfs 로 통째 로드 → 모바일 kill 위험.
   → **축 Y (chunk paging + IndexedDB cache)** 로 대응.
3. **미검증 재진입 시나리오** — 모바일 브라우저의 background throttle,
   tab visibility, iOS Safari WebKit 특유 이슈. → **X3 실측** 으로 발견.

---

## 3. X3 실측 테스트 방법론

X3 는 이 매트릭스 위에서 실제 기기 fps/메모리 baseline 을 잡는다. X2 (실측
하네스 코드) 가 완료된 뒤 실행. 이 섹션은 X3 실행 시 지침.

### 3.1 기기 매트릭스

| 티어 | 대표 기기 (예시) | 우선순위 | 획득 방법 |
| --- | --- | --- | --- |
| **T-high** (baseline) | Windows 데스크톱 dGPU (RTX/Radeon) | ✅ 있음 | 개발 PC |
| **T-mid-desktop** | Intel iGPU 노트북 (Iris Xe · UHD 620~730) | ⭐ 최우선 | 저사양 노트북 실측 필요 |
| **T-mobile-high** | iPhone 15+/iPad Pro (iOS 26+) | ⭐ 필수 | 개인/구독 기기 |
| **T-mobile-mid** | Android Chrome / Adreno 6xx~7xx (Snapdragon 8xx 대) | ⭐ 필수 | 개인/타 기기 |
| **T-mobile-low** | Android Adreno 6xx 초기 / Mali-G78 (엔트리 폰) | 선택 | 여러 기기 필요 |
| **T-chromebook** | ARM Chromebook (Mali) | 선택 | 기기 확보 시 |

**최소 실측 집합**: T-high (baseline 재확인) + T-mid-desktop + T-mobile-high
+ T-mobile-mid (4 티어 4 기기).

### 3.2 관측 지표 (기기 × 시리즈 매트릭스)

각 셀에 대해 기록:

**정적 정보** (device profile):
- `navigator.gpu.requestAdapter().info` — vendor · device · architecture
- Adapter limits — `maxTextureDimension3D` · `maxStorageBufferBindingSize` ·
  `maxComputeWorkgroupSize*` · `maxComputeInvocationsPerWorkgroup`
- Adapter features — `shader-f16` · `timestamp-query` · `float32-filterable`
- 브라우저 UA · OS 버전
- 화면 해상도 · devicePixelRatio

**로드 시간**:
- Shader compile total (startup 부터 첫 프레임까지)
- 시리즈 로드 시간 (파일 read · decode · brick pack · GPU upload)

**렌더 성능** (Lambert+shadow / Path-trace SPP=1/4/8 각 조합):
- Frame time mean/max (30 s 정지 + 30 s 카메라 orbit)
- Frame time p99
- 30 fps 유지 여부 · 60 fps 유지 여부

**메모리**:
- `performance.memory.usedJSHeapSize` (Chrome 만)
- HUD 의 Memory `X.X / Y.Y MB` (atlas 할당)
- WASM heap peak (`HEAPU8.length`)
- (GPU 메모리는 브라우저 API 없음 → adapter limits 대비 우리 알로케이션
  량으로 추정)

**Failure modes**:
- Tab crash · adapter lost 이벤트
- iOS Safari background throttle → resume 시나리오
- Shader compile 실패 (limit 초과 시)

### 3.3 실행 절차 (기기 1 대당)

1. **환경 준비**
   - 로컬 서버를 LAN 에 노출 (`python -m http.server 8000` on dev PC + 방화벽
     허용) — 모바일이 `http://<PC-IP>:8000/volume_viewer_wasm.html` 접근
   - 또는 GitHub Pages / Cloudflare Pages 로 임시 배포
2. **접속 → device profile 자동 캡처** (X2 하네스가 콘솔에 JSON 로그)
3. **4 시리즈 순환 로드** (bundled fMRI + user picker 로 다른 3 개)
4. **각 시리즈 × 각 모드 × (정지 / orbit) 조합 실측**
5. **콘솔 로그 · 스크린샷 저장 → PC 로 전송** (AirDrop · 이메일 · WeChat 등)

### 3.4 산출물

- `baselines/BASELINE_YYYY-MM-DD_MOBILE_MATRIX.md` — 기기 × 시리즈 × 모드
  표 + 관측된 사양별 병목
- 발견된 이슈는 축 Y/Z 의 우선순위 재조정 재료

---

## 4. X2 실측 하네스 코드 요구사항

X1 조사 결과 X2 는 다음을 구현해야 함:

1. **Device profile 자동 캡처** (JS 측)
   - `navigator.gpu.requestAdapter().info` · `adapter.features` · `adapter.limits`
   - UA · screen · devicePixelRatio
   - JSON 으로 콘솔에 dump (복사 붙여넣기 가능하게)

2. **Baseline 자동 시나리오 러너** (URL 파라미터로 진입)
   - `?bench=1&series=fmri,siemens,ct,mammo&modes=lambert,pt1,pt4,pt8`
   - 각 조합 순환 · 30 s 정지 + 30 s orbit 자동 · 프레임 타임 링 버퍼 dump
   - 완료 시 결과 JSON 콘솔 출력 · localStorage 저장

3. **간이 tier heuristic** (X4 근거)
   - adapter info · limits 로부터 `tier: "high"|"mid"|"low"|"mobile"` 판정
   - 지금은 감지만 · 로그로 표시 · 자동 policy 는 X4

## 5. X4 tier 별 자동 policy (X3 결과 후 확정)

X3 실측 결과에 따라 다음 policy 를 확정:

| Tier | atlas 사이즈 상한 | LOD 상한 | Path-trace 기본 | Denoise 기본 | SPP 기본 | K budget |
| --- | --- | --- | --- | --- | --- | --- |
| high | (제한 없음) | L3 | on | on | 8 | 64 |
| mid-desktop | 512 MB | L3 | off | off | 4 | 32 |
| mobile-high | 256 MB | L2 | off | off | 4 | 16 |
| mobile-mid | 128 MB | L1 | off | off | 1 | 8 |
| mobile-low | 64 MB | L1 | off | off | 1 | 8 |

*(위 표는 초안 · X3 실측 후 조정)*

---

## 6. Sources (2026-07)

- WebGPU 구현 상태: <https://github.com/gpuweb/gpuweb/wiki/Implementation-Status>
- 브라우저 지원 percentage: <https://caniuse.com/webgpu>
- iOS 26 WebGPU 릴리즈: <https://webkit.org/blog/16993/news-from-wwdc25-web-technology-coming-this-fall-in-safari-26-beta/>
- Chrome Android 21 shipping intent:
  <https://groups.google.com/a/chromium.org/g/blink-dev/c/YFWuDlCKTP4>
- Qualcomm shader-f16 이슈: <https://github.com/gpuweb/gpuweb/issues/5006>

---

## 7. 다음 스텝

- **X2 착수 조건**: 이 문서 검토 완료 후 사용자 승인
- **X3 착수 조건**: X2 하네스 데스크톱 검증 + 실측 대상 기기 준비 (특히
  Intel iGPU 노트북 · Android 폰)
- **X4 착수 조건**: X3 baseline 완료 (실측 데이터가 있어야 policy 수치가
  근거 있음)
