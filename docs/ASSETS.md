# 의존성·에셋 안내

이 문서는 저장소에서 확인할 수 있는 사용처와 출처 기록을 모읍니다. 프로젝트 자체의 배포 라이선스는 아직 별도 파일로 정해져 있지 않습니다. 외부 자산의 라이선스를 프로젝트 전체에 적용하지 않습니다.

## 라이브러리

| 용도 | 라이브러리 | 구성의 기준 |
| --- | --- | --- |
| 윈도우·수학·UI | GLFW, GLM, Dear ImGui | [vcpkg.json](../vcpkg.json) |
| GPU 메모리·Vulkan 로딩 | Vulkan Memory Allocator, volk, Vulkan SDK | [RHI 빌드](../src/rhi/CMakeLists.txt), [루트 CMake](../CMakeLists.txt) |
| 장면·이미지 로딩 | cgltf, tinyobjloader, stb | [루트 CMake](../CMakeLists.txt) |
| DICOM 압축 해제 | OpenJPEG, libjpeg-turbo | Native vcpkg / WASM FetchContent |
| 브라우저 GPU 연결 | Emscripten, emdawnwebgpu | [번들 remote port](../third_party/emdawnwebgpu/) |

각 라이브러리의 라이선스 원문은 설치·다운로드된 패키지에 포함된 파일을 기준으로 확인합니다. WASM 라이브러리 버전은 CMake에서 관리하며 stb는 현재 `master`를 참조합니다. 완전한 의존성 버전 고정은 아직 되어 있지 않습니다.

## 모델·환경맵

| 자산 | 저장소에 남아 있는 출처 정보 |
| --- | --- |
| `models/DamagedHelmet.glb` | Khronos glTF 샘플에서 도입. [도입 기록](archive/changelogs/CHANGELOG_2026-05-21.md) |
| `models/BoxAnimated.glb` | Khronos glTF 계층·다중 메시 검증용 샘플. [도입 기록](archive/changelogs/CHANGELOG_2026-05-24.md) |
| `models/viking_room.obj`, `textures/viking_room.png`, 기타 모델·HDR/EXR·텍스처 | 이 정리에서 자산별 원저작자·라이선스 원문까지 확인하지 못함. 별도 출처 명세 보완 필요 |

## 볼륨 데이터

- [NIfTI 생성기](../scripts/make_synthetic_nii.py)는 Python 표준 라이브러리로 합성 CT/MR phantom을 만듭니다.
- [DICOM 생성기](../scripts/make_synthetic_dicom.py)는 `numpy`와 `pydicom`을 사용합니다. 대용량 실험 수치는 실제 환자 데이터와 구분해 기록합니다.
- 공개 DICOM 샘플의 다운로드 URL은 [fetch_test_dicom.py](../scripts/fetch_test_dicom.py), [fetch_wasm_dicom_sample.py](../scripts/fetch_wasm_dicom_sample.py), [CMakeLists.txt](../CMakeLists.txt)에 있습니다. 공개 접근 가능 여부와 재배포 조건은 별개이므로 해당 코퍼스의 조건을 확인해야 합니다.
- `.nii`, `.nii.gz`, `.dcm`, `.raw`와 `test_data/`는 Git에서 제외합니다. 자동 회귀 테스트는 외부 다운로드 없이 임시 디렉토리에 작은 합성 fixture를 생성합니다.

WASM 뷰어 빌드는 샘플 DICOM을 `.data`에 포함합니다. 사용자 파일 선택 경로는 브라우저의 가상 파일시스템에서 디코드하며, 원격 분석 서비스는 구현되어 있지 않습니다.
