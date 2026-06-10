# WASM OpenJPEG 빌드 계획

**작성일**: 2026-06-10
**목표**: 브라우저 `volume_viewer_wasm`에서 **JPEG 2000 압축 DICOM** 로드. 로드맵
§0 "브라우저에서, 서버 GPU 없이, 시네마틱 화질로, **대용량 실데이터를**"
차별화의 마지막 데이터 입력 갭 해소.

**로드맵 정렬**: 네이티브는 commit `5a2f8ce`에서 OpenJPEG vcpkg 의존으로 JPEG
2000 디코드 완료. WASM 브라우저 빌드는 OpenJPEG가 없어 미지원 상태 — 본 작업이
브라우저 동등 기능 달성.

---

## 1. 현재 상태

| 백엔드 | OpenJPEG | DICOM | JPEG 2000 |
| --- | --- | --- | --- |
| 네이티브 (Vulkan) | vcpkg `openjpeg` | `DicomFile.cpp` 연결 | ✅ |
| WASM (WebGPU) | 없음 | DicomFile.cpp 미연결 | ❌ |

`volume_viewer_wasm`은 현재 NIfTI 1개만 preload로 받는 데모. DICOM 지원이
처음부터 없음 (네이티브 viewer가 디렉토리를 받지만 WASM은 파일 입력 모델이 다름).

---

## 2. 설계 — Emscripten + OpenJPEG

### 의존 추가 방식

Emscripten의 빌트인 ports에 OpenJPEG는 없음. FetchContent + add_subdirectory로
소스에서 빌드:

```cmake
FetchContent_Declare(
    openjpeg
    GIT_REPOSITORY https://github.com/uclouvain/openjpeg.git
    GIT_TAG v2.5.0
    GIT_SHALLOW TRUE
)
# Configure: 시각화 / CLI 도구 / 테스트 다 끄기
set(BUILD_CODEC OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_LUTS_GENERATOR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)   # WASM은 static only
FetchContent_MakeAvailable(openjpeg)
```

OpenJPEG 빌드는 `openjp2` static target을 노출. WASM 빌드 자동.

### 브라우저 DICOM 파일 입력

WASM은 디렉토리 트리를 직접 못 받음. 두 가지 접근:

1. **단일 .dcm 파일 preload** (가장 단순): 빌드 시 `--preload-file` 하나 추가.
   합성 또는 다운로드된 .dcm 한 개를 데모로 묶어 보임. 테스트용으로 충분.
2. **`<input type="file" multiple>` 업로드**: 런타임에 JS가 사용자 선택
   .dcm 파일들을 가상 파일시스템에 쓰고 wasm 측 로더가 읽음. 임상 워크플로에
   가깝지만 작업 분량 큼.

**본 계획**: 1번 — 단일 파일 preload로 JPEG 2000 디코드 동작 검증. 2번은 후속
트랙으로 분리.

### 작업 분량 분석

WASM OpenJPEG 빌드 자체: ~1-2시간 (FetchContent + 빌드 옵션 조정).
DicomFile.cpp WASM 연결: ~1시간 (이미 포터블 C++, std::filesystem WASM 동작 확인 필요).
volume_viewer_wasm DICOM 디스패치: ~2시간.
빌드 + 테스트: ~1-2시간.

합계: ~5-7시간. (원래 추정 10-15h보다 작음 — DicomFile.cpp가 이미 포터블이라).

---

## 3. Atomic 단계

### Step W1 — OpenJPEG FetchContent + WASM 링크 확인 (~1-2h)

- `CMakeLists.txt`의 WASM 분기에 OpenJPEG FetchContent
- 빌드 옵션 정리 (codec/testing/shared OFF)
- 빈 호출 (e.g., `opj_version()` 한 줄)로 링크 검증
- 실제 사용은 없음 (회귀 0)

### Step W2 — DicomFile.cpp WASM 연결 (~1-2h)

- `volume_viewer_wasm` 소스 리스트에 `DicomFile.cpp/hpp` 추가
- `openjp2` 링크
- `std::filesystem::directory_iterator` Emscripten 동작 확인 (MEMFS에서 동작해야 함)
- 컴파일 + 링크 통과 확인 (런타임 동작은 다음 단계)

### Step W3 — 뷰어 dispatch + HTML 진입 (~2-3h)

- `volume_viewer_wasm.cpp`: 명령행 인자 (또는 컴파일 시 매크로) 로 NIfTI vs DICOM 디렉토리 분기
- HTML 셸: DICOM preload 옵션 (단일 .dcm 또는 디렉토리)
- 합성 JPEG 2000 .dcm 생성 (또는 pydicom 샘플 다운로드 → preload)

### Step W4 — 브라우저 검증 + 문서 갱신 (~1h)

- 브라우저에서 합성 / pydicom 693_J2KI.dcm 로드 확인
- 시각 결과 네이티브와 동일 (range, 시각 표현)
- VIEWERS.md / README 갱신
- BASELINE 또는 변경 로그 (필요 시)

### 합계: ~5-7시간 (2-3 세션 예상)

---

## 4. 영향 받는 파일

| 단계 | 파일 |
| --- | --- |
| W1 | `CMakeLists.txt` (WASM 분기) |
| W2 | `CMakeLists.txt` (volume_viewer_wasm 소스) |
| W3 | `tests/volume_viewer_wasm.cpp`, `tests/volume_viewer_shell.html` (또는 별도) |
| W4 | 문서들 + 합성 데이터 생성기 (선택) |

엔진 코어 (RHI, 셰이더, BrickedVolume) 변경 없음.

---

## 5. 위험과 대응

| 위험 | 대응 |
| --- | --- |
| OpenJPEG의 SIMD 코드가 WASM에서 빌드 실패 | OPJ_HAVE_SSE 등 OFF로 강제 (CMake 옵션) |
| OpenJPEG 의존성 (PNG/TIFF 등 — codec 도구용) | BUILD_CODEC OFF로 해소 |
| WASM 바이너리 크기 증가 | OpenJPEG static lib ~200-400 KB 예상. 측정 후 정직 보고 |
| MEMFS에서 std::filesystem::directory_iterator 비동작 | Emscripten 빌드 시 `-s FORCE_FILESYSTEM=1` 또는 대안 (preload된 파일 경로 직접 사용) |
| ASYNCIFY와 OpenJPEG 콜백 충돌 | OpenJPEG는 동기 API. ASYNCIFY 가드 가능 |

---

## 6. 후속 (이 계획 밖)

- `<input type="file">` 런타임 업로드 지원 (사용자 DICOM 파일 선택)
- 다중 슬라이스 DICOM 시리즈 (현재 WASM은 단일 파일 모델)
- WASM RLE / JPEG Baseline 등 추가 transfer syntax

---

## 7. 다음 진입점

Step W1 (OpenJPEG FetchContent + 링크 검증) 부터 진행.
