# CT/MRI 볼륨 뷰어 사용법

두 뷰어는 `VolumeRenderer`와 `BrickedVolume`, 대응하는 GLSL/WGSL 셰이더를 공유합니다. Native는 ImGui, 브라우저는 HTML 패널을 사용합니다. 장면 쇼케이스인 `MiniEngine`과 별도 실행 타깃입니다.

## 빌드와 첫 실행

도구 설치·최초 configure는 [빌드 가이드](../../guides/BUILD_GUIDE.md)를 참고하세요. 다음 native 예시는 Release로 구성한 `build-native`를 사용합니다.

```powershell
cmake --build build-native --target volume_viewer
.\build-native\volume_viewer.exe
```

인자 없이 실행하면 절차적 볼륨을 보여줍니다. 합성 CT, NIfTI 파일, DICOM 시리즈 또는 raw 파일을 열 수 있습니다.

```powershell
python scripts/make_synthetic_nii.py build-native/synthetic_ct.nii 96 96 48
.\build-native\volume_viewer.exe build-native/synthetic_ct.nii
.\build-native\volume_viewer.exe path\to\series
.\build-native\volume_viewer.exe path\to\volume.raw 256 256 128 2
```

브라우저 버전은 타깃을 명시합니다.

```powershell
.\scripts\wasm.ps1 serve -Target volume_viewer_wasm
```

`http://localhost:8000/volume_viewer_wasm.html`로 접속합니다. 현재 빌드에는 공개 MR, JPEG 2000 CT, JPEG Lossless 샘플과 합성 NIfTI가 포함됩니다. 기본 로드는 공개 MR 샘플을 우선 시도합니다.

## 입력 데이터

| 입력 | Native | 브라우저 |
| --- | --- | --- |
| NIfTI-1 `.nii` | CLI 파일 경로. int16/uint16 mmap streaming 경로와 일반 decode fallback | 빌드에 포함한 합성 샘플 |
| DICOM | 한 시리즈의 `.dcm` 파일이 든 디렉토리 | `Pick DICOM files` / `Pick DICOM folder`, 번들 샘플, 같은 origin의 시리즈 fetch |
| raw | CLI에서 너비·높이·깊이·bytesPerVoxel(1/2) 지정 | 별도 업로드 UI 없음 |
| 절차적 볼륨 | 인자 없는 기본 화면 | 번들 샘플 중심 |

DICOM은 단일 시리즈의 16-bit grayscale CT/MR 중심입니다. Explicit/Implicit VR LE, RLE, JPEG 2000, JPEG Baseline/Extended/Lossless P14·SV1 디코드 경로가 있습니다. JPEG 디코더의 8-bit 처리 능력과 로더의 `BitsAllocated == 16` 입력 제한은 별개입니다. JPEG-LS와 big-endian은 지원하지 않습니다.

NIfTI는 little-endian 단일 파일의 uint8/int8/int16/uint16/int32/uint32/float32를 읽습니다. `.nii.gz`, `.hdr/.img`는 직접 지원하지 않습니다. 현재 spacing과 제한된 슬라이스 정렬을 사용하므로 임의의 oblique orientation·환자 좌표를 완전히 재구성하는 뷰어로 간주하면 안 됩니다.

## 조작과 화면 확인

| 입력/패널 | 동작 |
| --- | --- |
| 왼쪽 드래그 / 모바일 한 손가락 드래그 | 카메라 회전 |
| 휠 / 모바일 pinch | 확대·축소 |
| Transfer function | Custom, Cloud, Fire, CT Bone/Soft Tissue, MR T1/T2 |
| Window / level | 표시할 강도 구간 조절. CT의 HU와 MR의 신호 강도는 구분 |
| Gradient shading / soft shadows | 볼륨 음영 설정. 추가 샘플링 비용 발생 |
| Lambert / Path-traced | 빠른 ray march와 산란 기반 path trace 전환 |
| SPP / anisotropy / bounces | Path-trace 품질·산란 설정 |
| A-trous / accumulation cap / reset | 공간 필터와 누적 제어 |
| Stats / Copy stats (브라우저) | 차원, atlas 메모리, 모드, CPU frame time 등의 기록 |

정지한 카메라에서 PT 결과가 누적됩니다. 카메라·window·preset·렌더 모드 등의 변경은 누적을 초기화합니다. 모바일은 장치/브라우저 분류에 따라 기본 품질 설정이 낮아질 수 있습니다.

## 재현용 데이터

NIfTI 생성기는 Python 표준 라이브러리만 사용합니다.

```powershell
python scripts/make_synthetic_nii.py build-native/mr_brain.nii 128 128 128 --modality mr --shape brain
```

DICOM 생성기는 `numpy`, `pydicom`이 설치된 환경에서 사용합니다. 작은 합성 시리즈로 먼저 확인하세요.

```powershell
python scripts/make_synthetic_dicom.py build_wasm/synth_ct 96 96 48
```

서버 fetch를 이용하려면 `build_wasm`을 서버의 루트로 서빙하고 패널의 경로·파일 수를 생성한 시리즈와 맞춥니다. 모바일 원격 접속은 HTTPS 환경이 필요합니다. 공개 샘플 모음은 `python scripts/fetch_test_dicom.py`로 준비할 수 있으며 이 명령은 네트워크를 사용합니다.

## 메모리 모드와 한계

- **Static**: atlas에 전체 데이터를 배치합니다. Progressive DICOM은 이 경로에서 슬라이스 ring buffer와 staging slab을 사용해 전체 중간 버퍼를 줄입니다.
- **Streaming**: atlas 예산과 LRU/LOD에 따라 일부 브릭을 업로드합니다. Native의 `--atlas-cap N`은 작은 예산으로 동작을 관찰하는 옵션입니다. DICOM progressive Static과 같은 메모리 특성을 기대하면 안 됩니다.
- LOD seam·stale LOD·missing brick은 남아 있는 과제입니다. PT 화면이 보인다는 사실만으로 임의 데이터에 대한 진단 정확성이 검증되지는 않습니다.
- HUD의 atlas 값은 GPU 저장소 지표이고 CPU frame time은 end-to-end GPU 시간이나 FPS와 같지 않습니다. WASM heap·브라우저 프로세스 메모리도 따로 측정해야 합니다.

[아키텍처](../../ARCHITECTURE.md) · [테스트 절차](../../../tests/README.md) · [측정 기록 모음](README.md) · [의존성·데이터 출처](../../ASSETS.md)
