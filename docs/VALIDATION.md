# 검증 기록 — 2026-09-28

문서·테스트 정리 작업 중 로컬에서 수행한 검증입니다. 작업 기준은 `eebb1e5`와 이번 미커밋 변경이며, 과거 성능 baseline을 새로 측정한 결과가 아닙니다.

## 환경

- Windows x64, Clang 21.1.1, CMake 3.31.6, Ninja
- Native GPU 타깃: 기존 `build-windows`의 **Debug** 구성, Vulkan SDK 1.4.321.1, 설치된 vcpkg 의존성 사용
- 독립 CPU 테스트: 새 `build-tests`의 **Release** 구성
- WebGPU 쇼케이스·볼륨 뷰어: 기존 `build_wasm`의 **Release** 구성, 설치된 Emscripten/NMake 및 캐시된 에셋 사용

## 실행 결과

| 검증 | 결과 |
| --- | --- |
| 독립 `cmake -S tests` configure/build | 성공. Vulkan·vcpkg 없이 구성 |
| `ctest --test-dir build-tests -C Release --output-on-failure` | **2/2 통과** — ThreadPool, NIfTI/raw |
| Native CTest 통합 | **3/3 통과** — 위 2개 + DICOM |
| Native `MiniEngine`, `volume_viewer` | 컴파일·링크 성공 |
| `scripts/wasm.ps1 build -Target volume_viewer_wasm` | 빌드 성공, `.html/.js/.wasm/.data` 생성 |
| `scripts/wasm.ps1 build -Target MiniEngine` | 빌드 성공. `.html/.js/.wasm/.data`가 `build_wasm` 바로 아래에 생성됨 |
| WASM 스크립트에 존재하지 않는 타깃 전달 | 예상대로 nonzero 종료. 실패를 성공으로 숨기지 않음 |
| 합성 NIfTI 생성 명령 | 96×96×48 int16 CT 파일 생성 성공 |
| `python scripts/check_docs.py` | 현재 공개 문서 11개의 로컬 링크 158개 통과 |
| `git diff --check` | 통과 |

Native configure에서는 `-DVCPKG_MANIFEST_INSTALL=OFF`를 사용해 이미 설치된 의존성만 재사용했습니다. 새 checkout의 의존성 다운로드·설치를 재검증한 결과는 아닙니다.

## 테스트가 발견한 문제

- **NIfTI 음수 slope**: mmap 경로가 뒤집힌 min/max를 반환. 변환 후 두 끝점을 정렬하고 int16/uint16 양쪽에서 회귀 검증.
- **쇼케이스 링크 실패**: `BrickedVolume`이 사용하는 `MmappedFile.cpp`가 `MiniEngine` source 목록에서 빠져 있었음. 타깃에 추가.
- **Windows WASM 래퍼**: PowerShell의 `ErrorActionPreference=Stop`이 CMake 경고 stderr를 예외로 처리. cmd 내부에서 스트림을 합치고 실제 종료 코드로 판정하도록 수정.
- **WASM 산출물 위치**: OpenJPEG의 전역 출력 설정으로 쇼케이스가 의존성 하위 폴더에 생성됐음. 메인과 데모의 출력 위치를 랜딩 페이지가 있는 빌드 루트로 고정.

[진단·수정 과정](archive/changelogs/CHANGELOG_2026-09-28.md)

## 이번에 확인하지 않은 범위

GPU 화면의 시각적 정확성, 브라우저 실행·업로드·터치 조작, iPhone/iPad의 현재 성능, Linux/macOS 전체 엔진 빌드, 원격 GitHub Actions 실행은 이번 로컬 검증에 포함하지 않았습니다. GPU 및 장치별 수치는 [날짜별 baseline](current/medical-volume/README.md)을 참고하세요.

Native 빌드에는 기존 `VulkanRHIDevice`의 mutex 때문에 기본 move가 삭제된다는 경고 등이 남아 있습니다. 빌드 성공을 warning-free나 모든 런타임 동작의 보장으로 해석하지 않습니다.

[재실행 방법](../tests/README.md) · [빌드 가이드](guides/BUILD_GUIDE.md)
