# 빌드·실행 가이드

저장소 루트에서 명령을 실행합니다. 빠른 검증은 [CPU 테스트](../../tests/README.md)부터 시작할 수 있습니다. 이 경로에는 GPU SDK가 필요하지 않습니다.

## Native Vulkan 준비

| 도구 | 요구 사항 |
| --- | --- |
| CMake | 3.28 이상 |
| 컴파일러 | C++20과 CMake의 C++ 모듈 빌드를 지원하는 도구 체인. 이 저장소는 `vulkan.cppm`을 컴파일함 |
| 빌드 도구 | Ninja 권장 |
| Vulkan SDK | Vulkan 1.3 기반 코드, `vulkan.cppm`, `glslc` 필요. 로컬 검증 SDK: 1.4.321.1 |
| vcpkg | [manifest](../../vcpkg.json)의 라이브러리 설치에 사용 |
| Python | Python 3, 에셋 생성·다운로드·서빙 스크립트에 사용 |

Windows에서는 Visual Studio C++ 도구와 Windows SDK가 설정된 개발자 터미널을 사용합니다. 로컬 검증 컴파일러는 Clang 21.1.1입니다. `VCPKG_ROOT`와 `VULKAN_SDK`를 본인의 설치 경로로 설정하고 CMake, Ninja, 컴파일러를 PATH에 추가하세요.

### Windows / PowerShell

```powershell
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build-native --target MiniEngine volume_viewer
.\build-native\MiniEngine.exe
.\build-native\volume_viewer.exe
```

최초 configure는 vcpkg 의존성을 다운로드·빌드합니다. 이미 구성한 디렉토리에서는 `cmake --build <디렉토리> --target <타깃>`으로 증분 빌드할 수 있습니다.

### Linux / macOS

아래는 같은 CMake 구성을 사용하는 경로입니다. 최신 코드의 플랫폼별 실행 검증 여부는 [검증 기록](../VALIDATION.md)을 참고하세요. macOS는 MoltenVK와 C++ 모듈 지원 컴파일러 구성이 별도로 필요합니다.

```sh
cmake -S . -B build-native -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build-native --target MiniEngine volume_viewer
./build-native/MiniEngine
./build-native/volume_viewer
```

### 기존 presets와 Makefile

[CMakePresets.json](../../CMakePresets.json)과 [Makefile](../../Makefile)은 기존 개발 환경의 경로를 포함합니다. 새 환경에서는 위의 직접 configure 명령을 우선 사용하세요. preset을 재사용하려면 컴파일러·vcpkg·SDK·Ninja 경로를 확인하고 필요한 cache 변수를 덮어써야 합니다.

Ninja는 단일 구성 generator입니다. `--config Release`만 붙여도 Debug 캐시가 Release로 바뀌지는 않습니다. 최적화 빌드는 configure 시 `-DCMAKE_BUILD_TYPE=Release`를 지정합니다.

## 타깃과 옵션

| 타깃 | 설명 |
| --- | --- |
| `MiniEngine` | PBR/deferred 장면 쇼케이스 |
| `volume_viewer` | Native CT/MRI 뷰어 (`BUILD_TESTS=ON`) |
| `volume_viewer_wasm` | 브라우저 CT/MRI 뷰어 (Emscripten 구성) |
| `building_shaders` | Native GLSL→SPIR-V 셰이더 빌드 |
| `rhi_smoke_test`, `instancing_test`, `pbr_demo`, `dual_light_demo` | 창/브라우저를 여는 수동 데모 |
| `showcase_demo` | Native 수동 쇼케이스 |
| `threadpool_test`, `volume_loader_test`, `dicom_loader_test` | Native CPU 회귀 테스트 |

| 옵션 | 의미 |
| --- | --- |
| `BUILD_TESTS=ON` | 기존 이름 유지: 대화형 데모와 native 뷰어를 생성 |
| `BUILD_TESTING=ON` | Native 자동 테스트를 생성하고 CTest에 등록 |
| `MINIENGINE_TEST_DICOM=ON` | DICOM 회귀 테스트. 루트 빌드는 기본 ON, 독립 tests 빌드는 기본 OFF |
| `RHI_BACKEND_VULKAN` / `RHI_BACKEND_WEBGPU` | Native 기본 Vulkan. Emscripten은 WebGPU ON / Vulkan OFF로 설정 |

뷰어와 데모를 제외하려면 `BUILD_TESTS=OFF`, 자동 테스트도 제외하려면 `BUILD_TESTING=OFF`를 함께 지정합니다.

## WebGPU / WASM

프로젝트 스크립트의 기준 Emscripten 버전은 **4.0.10**입니다. WASM은 vcpkg 대신 CMake FetchContent와 번들 emdawnwebgpu remote port를 사용합니다. 최초 빌드에는 네트워크가 필요합니다. 볼륨 뷰어는 공개 DICOM 샘플도 다운로드해 `.data`에 패키징합니다.

### Windows

Visual Studio 2022의 NMake와 Emscripten이 필요합니다. SDK가 없다면 설치할 위치를 정한 뒤 다음 setup 명령을 한 번 실행합니다.

```powershell
.\scripts\wasm.ps1 setup
.\scripts\wasm.ps1 build -Target MiniEngine
.\scripts\wasm.ps1 build -Target volume_viewer_wasm
.\scripts\wasm.ps1 serve -Target volume_viewer_wasm
```

기본 SDK 위치는 `$env:USERPROFILE\emsdk`입니다. 다른 위치는 `-EmsdkDir <경로>`로 지정합니다. `build`의 기본 타깃은 `MiniEngine`이므로 **볼륨 뷰어는 `-Target volume_viewer_wasm`을 명시**합니다.

### Linux / macOS

Emscripten 환경을 활성화한 셸에서:

```sh
source "$HOME/emsdk/emsdk_env.sh"
emcmake cmake -S . -B build_wasm -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=cmake/EmscriptenToolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build_wasm --target MiniEngine volume_viewer_wasm
cd build_wasm
python3 ../scripts/serve_nocache.py 8000
```

### 산출물과 접속

- `build_wasm/MiniEngine.{html,js,wasm,data}` — 장면 쇼케이스
- `build_wasm/volume_viewer_wasm.{html,js,wasm,data}` — 볼륨 뷰어
- `build_wasm/index.html` — 데모 목록. 각 링크의 타깃을 빌드해야 열 수 있음

`http://localhost:8000/volume_viewer_wasm.html` 또는 `/MiniEngine.html`로 접속합니다. WebGPU 사용 가능 여부는 장치·GPU·브라우저에 따라 다릅니다. 원격 장치는 HTTPS에서 실행하세요. 특정 브라우저의 최소 버전을 전체 지원 보장으로 사용하지 않습니다.

## 자주 막히는 지점

| 증상 | 확인할 점 |
| --- | --- |
| `vulkan.cppm`/모듈 검사 실패 | Vulkan SDK 헤더와 컴파일러의 모듈 지원, Ninja 버전 확인 |
| 라이브러리 또는 shader 파일 없음 | vcpkg configure 완료 여부, 실행 타깃의 shader 의존성 확인. 저장소 루트에서 실행 |
| CMake 변경이 WASM에 반영되지 않음 | `wasm.ps1`은 캐시가 있으면 configure를 생략. `-BuildDir build_wasm_fresh`로 새 디렉토리에 구성 가능 |
| WASM 빌드 실패 | 스크립트는 컴파일러의 실제 종료 코드를 반환. 로그의 첫 error를 확인하고, 이전 바이너리의 존재만으로 성공 판정하지 않기 |
| 브라우저에서 예전 화면이 보임 | `serve_nocache.py`로 빌드 출력 디렉토리를 서빙 |
| `.html`은 있지만 볼륨을 못 읽음 | `.js`, `.wasm`, `.data`도 같은 디렉토리에서 서빙되는지 확인 |

[뷰어 사용법](../current/medical-volume/VIEWERS.md) · [테스트](../../tests/README.md) · [검증 기록](../VALIDATION.md)
