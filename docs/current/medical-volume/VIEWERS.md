# Mini-Engine — 의료 볼륨 뷰어 사용 가이드

**최신화**: 2026-06-02 (M3-3 v0 + M4 v1 반영)

의료 볼륨 트랙의 산출물 두 실행파일에 대한 조작·기능·기술 레퍼런스. 코드는
[`tests/volume_viewer.cpp`](../../../tests/volume_viewer.cpp) (네이티브),
[`tests/volume_viewer_wasm.cpp`](../../../tests/volume_viewer_wasm.cpp) (브라우저).

---

## 1. 두 실행파일

| 실행파일 | 백엔드 | 위치 | UI |
| --- | --- | --- | --- |
| `volume_viewer.exe` | Vulkan (네이티브) | `build-windows\volume_viewer.exe` | ImGui 패널 |
| `volume_viewer_wasm.html` | WebGPU (브라우저) | `build_wasm\volume_viewer_wasm.html` | HTML 컨트롤 패널 |

두 실행파일은 같은 엔진 코어(`VolumeRenderer` + 같은 셰이더 세트의 GLSL/WGSL
쌍)를 공유한다. 입력 데이터 · 렌더 결과 · 컨트롤이 1:1 대응되도록 설계했다.

참고: 메인 `MiniEngine.exe` / `MiniEngine.html`은 deferred + PBR + IBL 쇼케이스로
볼륨은 부수적으로만 합성한다. **임상 볼륨 작업은 이 두 뷰어가 진입점**이다.

---

## 2. 빌드 · 실행

### 네이티브 (Windows / Vulkan)

```powershell
cmake --build build-windows --target volume_viewer --config Release
# 산출물: build-windows\volume_viewer.exe
```

실행 예:

```powershell
# 인자 없음 → 절차적 합성 볼륨 128³로 부팅
.\build-windows\volume_viewer.exe

# NIfTI 한 파일 (헤더에 dims + spacing 포함)
.\build-windows\volume_viewer.exe path\to\study.nii

# DICOM 시리즈 (디렉토리 = 한 시리즈, 슬라이스당 .dcm 하나)
.\build-windows\volume_viewer.exe path\to\dicom_dir

# raw headerless (W H D bytesPerVoxel)
.\build-windows\volume_viewer.exe vol.raw 256 256 128 2
```

### WASM (브라우저 / WebGPU)

```powershell
$env:EMSDK_QUIET="1"
.\scripts\wasm.ps1 build
# 산출물: build_wasm\volume_viewer_wasm.{html,js,wasm,data} + index.html (랜딩)

# CMakeLists.txt를 건드렸다면 캐시 무효화 필요 (CLAUDE.md §7 함정)
Remove-Item -Recurse -Force build_wasm
.\scripts\wasm.ps1 build
```

서빙:

```powershell
make serve-wasm   # scripts\serve_nocache.py 사용 (브라우저 캐시 우회)
```

브라우저에서 `index.html` 또는 직접 `volume_viewer_wasm.html`로 접근. 데이터는
빌드 시 `--preload-file`로 패키징된 합성 NIfTI(`/synthetic_ct.nii`)가 자동 로드.

---

## 3. 입력 데이터

| 포맷 | 네이티브 | WASM | 비고 |
| --- | --- | --- | --- |
| **NIfTI (.nii)** | ✅ | ✅ (preload 1개) | 헤더에 dims·spacing·intensity 단위. 우선 권장 포맷 |
| **DICOM 시리즈 (디렉토리)** | ✅ | ❌ | Explicit VR LE 단일 시리즈, int16 CT/MR. 실 임상 코퍼스 4종 검증됨 (HU CT, 고해상도 MR, multi-frame MR, enhanced CT) |
| **raw headerless** | ✅ | ❌ | dims·bpv를 CLI로 명시 |
| **절차적 합성** | ✅ (인자 없음) | ❌ | 128³ 노이즈 볼륨, 첫 부팅용 |

### 합성 데이터 생성

테스트용 NIfTI / DICOM 생성기 (CT/MR 둘 다, sphere/brain phantom):

```powershell
# CT 합성 sphere (기본)
python scripts\make_synthetic_nii.py    out\synthetic_ct.nii  96 96 48
python scripts\make_synthetic_dicom.py  out\dicom_series\     96 96 48

# MR 합성 sphere (T1 강도값)
python scripts\make_synthetic_nii.py    out\synthetic_mr.nii  96 96 48 --modality mr

# MR 뇌 phantom (4-shell: 외부 CSF / GM / WM / 중심 CSF) — T1 preset으로 보기 좋음
python scripts\make_synthetic_nii.py    out\mr_brain.nii      128 128 128 --modality mr --shape brain
```

공개 임상 코퍼스 다운로드:

```powershell
python scripts\fetch_sample_ct.py out\dicom_ct_corpus   # pydicom CT 슬라이스
python scripts\fetch_sample_mr.py out\dicom_mr_corpus   # pydicom MR_small.dcm
```

### Transfer function preset

| Preset | 적합 데이터 | 효과 |
| --- | --- | --- |
| Custom | — | 2색 그래디언트, UI 슬라이더 |
| Cloud | 안개 / 절차적 노이즈 | 부드러운 흰색 |
| Fire | 폭발 / 에너지 효과 | 검정→빨강→흰 |
| CT - Bone | CT (뼈 윈도우) | 뼈 강조 |
| CT - Soft Tissue | CT (연조직 윈도우) | 장기·근육 |
| **MR - T1** | MR T1-weighted | WM 밝게, CSF 어둡게 (전형 brain T1) |
| **MR - T2** | MR T2-weighted | 물(CSF) 밝게, WM 어둡게 |

MR 데이터 로드 시 `MR - T1` 또는 `MR - T2` 전환으로 즉시 임상 콘트라스트 표시.
Window center/width는 데이터 범위 자동 감지(데이터 min/max).

---

## 4. 조작

### 마우스

| 입력 | 동작 |
| --- | --- |
| 좌클릭 드래그 | 카메라 궤도 회전 (orbit) |
| 휠 스크롤 | 줌 (camera distance) |

ImGui / HTML 패널 위에서는 호버 시 자동으로 카메라 입력이 차단된다.

### 컨트롤 패널 (양 뷰어 공통)

| 그룹 | 컨트롤 | 단위 / 범위 | 의미 |
| --- | --- | --- | --- |
| **데이터** | TF preset 콤보 | 5종: Custom / Cloud / Fire / CT-Bone / CT-Soft Tissue | 밀도→(rgb,α) LUT. CT-Bone이 기본 |
| **Window / Level** | Full / Bone / Soft / Lung 버튼 | 임상 프리셋 | HU 윈도우 즉시 설정 (center/width) |
| | Win center | HU (데이터 범위) | 윈도우 중심값 |
| | Win width | HU | 윈도우 폭. 좁을수록 콘트라스트↑ |
| **Transfer function** | Density | 0~5 | 밀도 스케일 (불투명도에 곱) |
| | Extinction | 0.1~30 | Beer-Lambert 감쇠 계수 |
| | Threshold | 0~0.5 | 이 밀도 미만은 완전 투명 (배경 컷오프) |
| | Color mix (Custom 전용) | 0~5 | 저↔고밀도 색 보간 곡선 |
| | Low / High color (Custom 전용) | RGB | Custom TF 양 끝 색 |
| **Shading** | Gradient shading 토글 | on/off | 밀도 기울기 = 법선, Lambert 음영 (M2-1) |
| | Ambient / Diffuse | 0~1 / 0~1.5 | 음영 광량 |
| | Soft shadows 토글 | on/off | 광원 방향 보조 레이 (M2-2). 비용 큼 |
| | Shadow strength | 0~4 | 그림자 감쇠 강도 |
| **Render mode (M4)** | Mode 콤보 | Lambert / Path-traced | 라이팅 방식 |
| | SPP (PT 전용) | 1~32 | 프레임당 sample-per-pixel |
| | Aniso g (PT 전용) | -0.9~0.9 | Henyey-Greenstein 위상함수 (>0=전방산란) |
| | Bounces (PT 전용) | 0~4 | 최대 산란 횟수 |
| **Performance** | Empty-space skip 토글 | on/off | 컴퓨트 occupancy 그리드 활용 (M3-1) |
| | FPS 표시 (네이티브만) | — | 1초 평균 |

### Path-trace 모드의 누적 (M4 v1)

PT 모드 진입 시 자동으로 progressive 누적이 시작된다. **카메라 정지** 상태에서
프레임이 쌓일수록 노이즈가 사라진다. 다음 입력이 들어오면 누적이 자동 reset
된다:

- 카메라 회전 / 줌 (matrix 비교)
- Window center · width · TF preset · 렌더 모드 변경
- SPP · anisotropy · bounces 변경
- 화면 리사이즈 (재시작)

---

## 5. 구현된 기능 ↔ 마일스톤 대응

| 기능 | 마일스톤 | 출처 |
| --- | --- | --- |
| 16비트 강도 저장(R16Float) + Hounsfield 보존 | M1 | `loadFromFloatData` |
| Window / level 슬라이더 + 임상 4 프리셋 | M1 | UBO `window` |
| NIfTI 로더 + 물리 spacing 비율 | M1 | `assets::loadNifti` |
| DICOM Explicit VR LE 로더(NumberOfFrames + 중첩 SQ skip) | M1 | `assets::loadDicomSeries` |
| Gradient 음영 (Lambert) | M2-1 | `volume_march`의 `ubo.light.w` |
| 볼류메트릭 소프트 섀도우 | M2-2 | `volume_march`의 `ubo.shadow` |
| 컴퓨트 occupancy 그리드 + empty-space skip | M3-1 | `volume_occupancy.comp` |
| **Brick atlas + page table 간접 참조** | **M3-3 v0** | `BrickedVolume` |
| Path-traced 볼륨 산란 (Woodcock + HG + NEE) | M4 v0 | `volume_pathtrace` |
| **Progressive 누적 (ping-pong, 자동 reset)** | **M4 v1** | `volume_pathtrace_display` + ping-pong 텍스처 |

---

## 6. 기술 스택 요약

### 엔진 측

- **언어**: C++20 (Concepts · Modules 부분 활용 · smart pointer 소유권)
- **RHI 추상화**: `src/rhi/include/rhi/` — 한 인터페이스, 두 백엔드 (Vulkan +
  WebGPU). 모든 그래픽 호출이 RHI를 통과.
- **데이터 경로**:
  - NIfTI/DICOM → `Volume3D{intensity, w, h, d, spacing*}` 공용 구조체
  - → `VolumeRenderer::loadFromFloatData()` → R16Float 변환
  - → `BrickedVolume::build()` — 빈 brick 제거 + atlas + page table 업로드
  - → 셰이더가 `sampleVolume(uvw)` 헬퍼로 page-lookup → linear atlas 샘플

### 셰이더 세트 (양 백엔드 쌍)

| 셰이더 | GLSL (Vulkan) | WGSL (WebGPU) |
| --- | --- | --- |
| 레이마칭 | `volume_march.frag.glsl` | `volume_march.wgsl` |
| Path tracer | `volume_pathtrace.frag.glsl` | `volume_pathtrace.wgsl` |
| PT display | `volume_pathtrace_display.frag.glsl` | `volume_pathtrace_display.wgsl` |
| Occupancy 컴퓨트 | `volume_occupancy.comp.glsl` | `volume_occupancy.comp.wgsl` |

각 쌍은 같은 UBO 레이아웃과 의미를 공유 (CLAUDE.md §8: 두 사본 항상 동기화).

### Brick atlas 핵심 수치 (M3-3 v0 + v1-α)

- Brick size: 64³ interior + 1-voxel halo 양면 = 66³ stored
- **atlas 크기 자동 산정 (v1-α)**: `axisGuess = ceil(cbrt(nonEmpty))` 시작점,
  pageGrid axis별 clamp, `kAutoAtlasBudgetBytes = 512MB` 안에 맞도록 longest
  axis shrink. 사용자가 `--atlas-cap N`으로 명시 override 가능
- Page table: `u32` per virtual brick, `0xFFFFFFFF` = "air" (sentinel)
- 압축률 예: 1024³ default (304 non-empty) → atlas (7,7,7) = 343 slots = 188MB
  (1 GB → 81.6% 절감)

### 모드: Static vs Streaming (v1-α)

| 케이스 | 모드 결정 | 동작 |
| --- | --- | --- |
| nonEmpty ≤ atlas slots | **Static** | 로드 시 atlas 통째 packing, 매 프레임 무비용 |
| nonEmpty > atlas slots | **Streaming** | atlas 비어있게 시작 + WARN 로그, 매 프레임 visible brick K=8개씩 paging |

Streaming 모드 진입 시 콘솔에 권장 atlasGrid + 메모리 추정 WARN 출력 — visible
set이 atlas 초과하면 시각 hole 발생함을 안내.

### Streaming 동작 (v1-α)

매 프레임 viewer가 `updateBrickStreaming(view, proj, frameIdx)` 호출:

1. CPU frustum culling (page grid 단위) → visible brick list
2. visible 중 resident인 slot 의 `lastFrameUsed` bump
3. visible 중 missing 최대 K=8개 → empty slot 또는 LRU evict로 자리 확보 →
   CPU에서 brick 추출 후 `copyBufferToTexture`
4. page table buffer push (전체 mirror)

**핵심 정책**: 같은 프레임에 bumped된 slot (현재 보고 있는 brick)은 **절대
evict하지 않음**. 가시 brick 보호로 churn 방지.

### CLI 플래그: `--atlas-cap N`

`(N, N, N)` 으로 atlasGrid 강제. 주 용도:

- Streaming 동작 stress test (default volume에서 streaming 트리거)
- 메모리 한계 환경에서 explicit 사이즈 설정
- 권장 — 일반 사용에서는 auto-size 신뢰

예:

```powershell
.\volume_viewer.exe test_1024.nii --atlas-cap 2   # 8 slots = 4.4 MB, Streaming
.\volume_viewer.exe test_1024.nii --atlas-cap 14  # 2744 slots, dense 1024³를 Static으로
```

### Path tracer (M4)

- Woodcock(delta) free-flight 트래킹
- Henyey-Greenstein 위상함수 (anisotropy g 슬라이더)
- 단일 광원 next-event estimation (NEE)
- v1 누적: RGBA16Float ping-pong 텍스처, running mean `(prev*N + cur)/(N+1)`,
  Reinhard tonemap은 두 번째 패스 (평균 이후, **non-linear는 반드시 평균 뒤**)

### 외부 의존

- `glm` (수학)
- `glfw` (창 + 입력, 네이티브 + Emscripten 양쪽)
- `vk-bootstrap` + `vulkan-hpp` (Vulkan 네이티브)
- `emdawnwebgpu` (WebGPU, Emscripten)
- `Dear ImGui` (네이티브 패널만)
- `stb_image` (텍스처 디코딩)
- `cgltf` (glTF, 메인 쇼케이스용)

---

## 7. 자주 만나는 함정 (CLAUDE.md §9 발췌)

| 증상 | 원인 | 해결 |
| --- | --- | --- |
| WASM 빌드가 몇 초 만에 끝나고 변경이 반영 안 됨 | `wasm.ps1`이 CMake 캐시 재사용 | `Remove-Item -Recurse -Force build_wasm` 후 재빌드 |
| WASM 콘솔 `Cannot have multiple async operations in flight` | ASYNCIFY 재진입 (JS→wasm 호출이 emscripten_sleep 중 들어옴) | `Module._wasmBusy` 게이트 필요. 기존 path들은 이미 처리됨 |
| 1×1 텍스처 업로드가 WebGPU validation 실패 | bytesPerRow 256정렬 미준수 | 모든 row를 256B 배수로 패딩 (Dawn은 1×1에도 강제) |
| `BufferTextureCopyInfo::bytesPerRow` 값 혼란 | Vulkan은 텍셀, WebGPU는 바이트로 해석 | `#ifdef __EMSCRIPTEN__`로 분기 (CLAUDE.md §9) |

---

## 8. 관련 문서

- [MEDICAL_VOLUME_ROADMAP.md](MEDICAL_VOLUME_ROADMAP.md) — 마일스톤 전체 로드맵
- [ENGINE_ROADMAP.md](../engine-roadmap/ENGINE_ROADMAP.md) — 별도 엔진 성숙도 트랙
- [BUILD_GUIDE.md](../../guides/BUILD_GUIDE.md) — 전체 빌드 가이드
- [CLAUDE.md](../../../CLAUDE.md) — 프로젝트 규약·함정 모음
