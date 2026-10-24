# 테스트와 데모

이 폴더에는 자동 회귀 테스트와 창을 여는 데모·뷰어가 함께 있습니다. **CTest에는 종료 코드로 판정할 수 있는 CPU 테스트만 등록**합니다. 렌더링 데모를 자동 테스트 통과 수에 포함하지 않습니다.

## GPU 없이 실행

CMake 3.28+와 C++20 컴파일러가 필요합니다. 기본 구성은 표준 C++/OS API만 사용하고 fixture를 임시 디렉토리에 생성합니다. 네트워크, 실제 의료 파일, Vulkan SDK가 필요하지 않습니다.

```sh
cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

Windows에서 특정 컴파일러를 선택하려면 configure에 `-G Ninja -DCMAKE_CXX_COMPILER=clang++`를 추가할 수 있습니다. Visual Studio generator를 쓰면 `--config Release` / `-C Release`를 유지하세요.

## Native 전체 테스트

루트 프로젝트는 `BUILD_TESTING=ON`일 때 아래 4개를 등록합니다. `make test-native`가 같은 일을 합니다. 기존 native configure가 없다면 [빌드 가이드](../docs/guides/BUILD_GUIDE.md)부터 진행합니다.

```sh
cmake -S . -B build-native -DBUILD_TESTING=ON
cmake --build build-native --target threadpool_test volume_loader_test dicom_loader_test scene_culling_test --config Release
ctest --test-dir build-native -C Release --output-on-failure
```

| 타깃 | 검사하는 동작 |
| --- | --- |
| [threadpool_test](threadpool_test.cpp) | future 반환, 작업 병렬 실행, 예외 전달, wait barrier, 소멸 시 큐 drain |
| [volume_loader_test](volume_loader_test.cpp) | NIfTI int16/uint16, 양수·음수 rescale, anisotropic spacing, mmap/float 범위 일치, 잘못된 헤더·잘린 데이터 거부; raw 8/16-bit·상수 볼륨·잘못된 입력 |
| [scene_culling_test](scene_culling_test.cpp) | AABB 포함·교차·병합, frustum의 앞·뒤·far·좌우·상하 평면 판정(Vulkan Y flip 유무 모두), 세 판정 함수의 일치, 이동·회전 카메라 |
| [dicom_loader_test](dicom_loader_test.cpp) | Explicit/Implicit VR LE, 공간 순서 정렬, signed rescale, row flip, multi-frame, float/half/progressive 결과 일치, callback 전 metadata, 전체 결과 버퍼 미보유, native 원본 보존, 혼합 시리즈·잘린 입력 거부 |

DICOM 테스트는 GLM, OpenJPEG, **12/16-bit API가 있는 libjpeg-turbo 3.x**가 필요합니다. 독립 `tests` configure에서도 `-DMINIENGINE_TEST_DICOM=ON`과 해당 라이브러리의 toolchain/prefix를 제공하면 실행할 수 있습니다. 씬 컬링 테스트는 GLM이 필요하며 `-DMINIENGINE_TEST_SCENE=ON`으로 켭니다. 기본 GPU 없는 경로에서는 둘 다 OFF이며, DICOM 테스트가 생략됐다는 점을 통과 결과와 함께 읽어야 합니다.

`BUILD_TESTS`는 대화형 데모를 제어하는 기존 옵션명입니다. 자동 테스트는 `BUILD_TESTING`으로 구분합니다. 모든 CTest 항목에 `cpu` label과 30초 timeout이 있고 Release에서도 검사가 생략되지 않습니다.

## 자동화 범위

[GitHub Actions](../.github/workflows/ci.yml)는 Windows/Linux에서 독립 CPU 테스트 2개와 [문서 링크 검사](../scripts/check_docs.py)를, Linux에서 [셰이더 검사](../scripts/check_shaders.py)를 실행하도록 구성되어 있습니다. 워크플로 파일 추가와 실제 원격 CI 실행 완료는 별개입니다.

```sh
make check     # make test + check-shaders + check-docs
make smoke     # MiniEngine 120프레임 렌더 후 종료, Vulkan validation 오류가 있으면 실패
```

셰이더 검사는 WGSL을 naga로, GLSL을 glslc로 확인합니다. naga는 `cargo install naga-cli --locked`로 설치합니다(rustc 1.86이면 `--version "~26"`). `make smoke`는 창을 열지만 사람의 조작 없이 종료합니다. 화면 내용은 판정하지 않습니다.

링크 검사는 현재 소개·가이드의 로컬 파일/이미지 경로를 확인합니다. 외부 URL의 가용성, Markdown heading anchor, 과거 archive 문서 전체는 검사하지 않습니다.

현재 자동화하지 않은 범위: 압축 DICOM 코덱별 실제 샘플, GPU 이미지 비교, WASM 파일 삭제·ASYNCIFY 수명, progressive GPU atlas의 내용, 모바일 성능/메모리 상한. 관련 과거 실험은 [볼륨 측정 인덱스](../docs/current/medical-volume/README.md)에 있습니다.

## 수동 데모

| 타깃/파일 | 용도 |
| --- | --- |
| `rhi_smoke_test` | 창·swapchain·기본 RHI 동작 |
| `instancing_test` | 인스턴싱 렌더링 |
| `pbr_demo`, `dual_light_demo` | 머티리얼과 광원 |
| `showcase_demo` | Native deferred 쇼케이스 |
| `volume_viewer`, `volume_viewer_wasm` | 볼륨 데이터와 렌더링 기능 |
| `wasm_shell.html`, `volume_viewer_shell.html` | WASM 실행 UI |
| `wasm_index.html` | 빌드된 데모 링크 목록 |

경로를 옮기면 CMake와 preload/shell 참조도 바뀌므로 기존 파일 위치는 유지합니다.

## 렌더링 변경 후 확인 순서

1. `MiniEngine`과 native `volume_viewer`를 빌드하고, 쇼케이스와 합성 NIfTI가 표시되는지 확인합니다.
2. DICOM 시리즈 하나를 native/브라우저에서 열어 차원·spacing·강도 범위가 예상과 같은지 확인합니다. MR과 CT를 각각 사용합니다.
3. Orbit/zoom, window/level, TF, Lambert/PT 전환, 카메라 이동 시 누적 초기화, denoise, resize를 확인합니다.
4. 브라우저에서 파일/폴더 업로드와 재로드, 모바일 터치, 오류 콘솔을 확인합니다.
5. 메모리 작업이라면 데이터 차원·모드·atlas 예산·렌더 설정을 고정하고 baseline과 비교합니다.

측정 기록에는 commit, 빌드 구성, OS/GPU/브라우저/장치, 데이터 출처·차원·형식, viewport, SPP·모드, 측정 도구와 표본 구간을 남깁니다. 합성/실데이터, CPU/GPU 시간, 할당량/관측 메모리/추정치를 구분합니다.

[최근 로컬 검증](../docs/VALIDATION.md) · [빌드](../docs/guides/BUILD_GUIDE.md) · [뷰어 사용법](../docs/current/medical-volume/VIEWERS.md)
