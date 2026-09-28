# 2026-09-28 — 공개 문서와 자동 회귀 검증 정리

## 문서와 테스트

README를 공유 RHI → 장면 렌더링 → CT/MRI 볼륨 시각화의 발전 과정으로 정리했다.
빌드·뷰어 가이드를 현재 타깃과 입력 경로에 맞추고, 측정값은 날짜별 baseline으로
연결했다. 현재 소개와 과거 계획의 역할을 구분했다.

`BUILD_TESTING`/CTest에 ThreadPool·NIfTI/raw·DICOM CPU 테스트를 등록했다.
`cmake -S tests`는 Vulkan 없이 구성할 수 있고, 기본적으로 외부 의존성이 없는
두 테스트만 빌드한다. DICOM은 native 의존성을 이용해 별도 검사한다.

## NIfTI mmap에서 음수 기울기의 물리 범위가 뒤집힘

**증상**: 새 회귀 테스트에서 같은 int16 fixture를 일반 decode와 mmap으로 열었을 때
일반 경로의 range는 `[-30, -6]`, mmap은 `[-6, -30]`으로 나왔다.

**진단**: 첫 양수 기울기 케이스는 일치했다. mmap 데이터·헤더 파싱은 두 경로가
같았고, 차이는 raw min/max에 slope를 적용한 뒤 순서를 정규화하지 않은 계산이었다.
바이트 순서나 mmap 수명 문제를 가정한 수정은 하지 않았다.

**원인·수정**: 음수 slope는 raw 순서를 뒤집는다. 두 끝점을 변환한 뒤
`std::min`/`std::max`로 물리 range를 정한다. raw 범위는 유지한다.
int16/uint16 × 양수/음수 slope의 decode/mmap 비교로 검증한다.

## MiniEngine의 MmappedFile 링크 누락

**증상**: native 뷰어와 CPU 테스트는 빌드됐지만 메인 쇼케이스가
`MmappedFile` 소멸자·move assignment의 undefined symbol로 링크에 실패했다.

**원인**: `BrickedVolume`에 mmap 소유권이 추가된 뒤 메인 타깃의 source 목록에는
`MmappedFile.cpp`가 추가되지 않았다. 뷰어 타깃은 이미 이를 포함하고 있었다.
컴파일러·라이브러리를 바꾸는 대신 링크 로그와 타깃별 source 목록을 대조했다.

**수정**: Native/WASM 공통 `MiniEngine` 타깃에 해당 구현 파일을 연결했다.
Native `MiniEngine`과 `volume_viewer` 링크 성공을 확인했다.

실행한 명령과 이번 검증의 한계는 [검증 기록](../../VALIDATION.md)에 남긴다.

## WASM 빌드 래퍼와 산출물 위치

PowerShell에서 CMake의 deprecation warning이 `NativeCommandError`로 승격되어
스크립트가 실패로 종료됐다. cmd 내부에서 stderr를 stdout과 합친 뒤 실제
종료 코드를 보존한다. 정상 타깃의 성공과 존재하지 않는 타깃의 nonzero 종료를
각각 확인했다. 임시 batch 파일 생성 시 남던 빈 `.tmp` 파일도 없앴다.

추가로 WASM 쇼케이스는 링크에 성공해도 `_deps/openjpeg-build/bin` 아래로 출력돼
랜딩 페이지의 `MiniEngine.html` 링크가 맞지 않았다. OpenJPEG의 전역 runtime
출력 설정이 원인이었다. 뷰어에 이미 있던 명시적 출력 디렉토리 설정을 메인
쇼케이스와 WASM 데모에도 적용해 `build_wasm` 바로 아래에 출력하도록 했다.
