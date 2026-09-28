# 현재 구현 요약

Mini-Engine은 C++20 기반 렌더링 프로젝트입니다. Vulkan 데스크톱과 WebGPU 브라우저 백엔드를 RHI로 추상화하고, 같은 코어로 PBR 장면과 CT/MRI 볼륨을 그립니다.

| 타깃 | 용도 | 플랫폼 |
| --- | --- | --- |
| `MiniEngine` | Deferred/PBR/IBL 장면 쇼케이스 | Native Vulkan / WASM WebGPU |
| `volume_viewer` | NIfTI·DICOM·raw·절차적 볼륨 시각화 | Native Vulkan |
| `volume_viewer_wasm` | 샘플 데이터·사용자 DICOM 업로드 시각화 | WASM WebGPU |

## 읽어볼 구현

- **RHI와 렌더링 파이프라인**: 두 API 사이의 공통 인터페이스, Render Graph, GPU culling, indirect draw, 후처리.
- **자산에서 화면까지**: glTF 머티리얼 처리, NIfTI/DICOM 스칼라 강도와 spacing 해석, transfer function.
- **메모리 병목 해소**: brick atlas, LRU/LOD, native mmap, slab staging, DICOM slice callback을 통한 progressive build.
- **검증과 측정**: CPU 회귀 테스트, 공개 MR 로드 기록, 합성 대용량 데이터와 모바일 장치별 실험.

구현 세부와 백엔드 차이는 [아키텍처](ARCHITECTURE.md), 사용법은 [빌드 가이드](guides/BUILD_GUIDE.md)와 [뷰어 가이드](current/medical-volume/VIEWERS.md), 테스트 범위는 [tests/README.md](../tests/README.md)를 참고하세요.

최신 개발 기록은 2026-07-22의 DICOM progressive build와 iPhone 512³ 합성 CT 로드 실험입니다. 이는 임의의 임상 데이터나 모든 모바일 장치를 검증했다는 의미는 아닙니다. [현재 상태](current/README.md) · [근거와 제한](current/medical-volume/baselines/BASELINE_2026-07-22_Y1C_FULL.md)
