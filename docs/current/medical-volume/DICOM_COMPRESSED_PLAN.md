# DICOM Compressed Transfer Syntax 지원 계획

**작성일**: 2026-06-07
**목표**: JPEG 2000 + RLE Lossless 압축 transfer syntax 지원 추가. 임상 PACS의
일부 + 대용량 시리즈 압축 저장 unlock.

**로드맵 정렬**: §5 line 329 "DICOM 압축 transfer syntax(JPEG/JPEG2000/RLE) —
libjpeg 등 외부 의존 필요" 후보. Implicit VR LE 완료 후 자연스러운 다음 단계.

---

## 1. DICOM 압축 Transfer Syntax 개요

DICOM 파일의 pixel data는 압축 방식에 따라 다른 UID로 식별. 임상 사용 빈도:

| UID | 형식 | 외부 의존 | 임상 빈도 | 본 계획 |
| --- | --- | --- | --- | --- |
| `1.2.840.10008.1.2.5` | RLE Lossless | 없음 (직접 구현) | 보조적 | **Step 1** |
| `1.2.840.10008.1.2.4.90` | JPEG 2000 Lossless | openjpeg | 임상 표준 | **Step 2** |
| `1.2.840.10008.1.2.4.91` | JPEG 2000 (lossy) | openjpeg | 보조 | Step 2 동시 |
| `1.2.840.10008.1.2.4.70` | JPEG Lossless SV1 | libjpeg | 레거시 | 후속 |
| `1.2.840.10008.1.2.4.50` | JPEG Baseline 8-bit | libjpeg | 8비트만 (의료 부적합) | 미지원 |
| `1.2.840.10008.1.2.4.80` | JPEG-LS Lossless | charls | 일부 PACS | 후속 |

**본 계획 범위**: RLE Lossless + JPEG 2000 (Lossless + Lossy)만. 나머지는 후속.

---

## 2. 압축 pixel data 구조 (encapsulated)

비압축 (현재 지원): `(7FE0,0010) OW len=N value=원시바이트` — 그냥 단일 블록.

압축 (encapsulated):

```text
(7FE0,0010) OB len=0xFFFFFFFF
  (FFFE,E000) len=0  -- Basic Offset Table (BOT, 선택)
  (FFFE,E000) len=N1 value=frame_0 압축 바이트
  (FFFE,E000) len=N2 value=frame_1 압축 바이트
  ...
  (FFFE,E0DD) len=0  -- Sequence Delimitation
```

각 item은 한 frame의 압축 데이터. 다중 frame DICOM (NumberOfFrames > 1)은 frame 수만큼 item.

---

## 3. Atomic 단계

### Step 1 — Encapsulated pixel data 구조 파서 (외부 의존 없음)

- 압축 TS UID 인식 (`1.2.840.10008.1.2.4.90`, `91`, `1.2.840.10008.1.2.5`)
- `(7FE0,0010)` len=0xFFFFFFFF 만나면 item walk로 frame 바이트 수집
- 디코딩 없이 raw 압축 바이트만 `EncapsulatedFrame[]` 벡터에 저장
- 디코더 미구현 → "TS X supported but decoder for X missing" 로그 후 reject

**작업**: ~2-3시간. RLE/JPEG2000 모두 같은 walk 로직.

### Step 2 — RLE Lossless 디코더 (직접 구현)

DICOM Annex G.5 RLE: PackBits 변종.
- Header: 16-byte. Number of segments (uint32) + 15 segment offset (uint32).
- Each segment: PackBits stream — control byte n:
  - 0..127 (n+1) literal bytes
  - 129..255 (257-n) replication of next byte
  - 128 NOP

16비트 픽셀의 경우 segment 0 = high byte, segment 1 = low byte. 디코드 후 결합.

**작업**: ~3-4시간. 합성 RLE 생성 (Python으로) + 디코드 검증.

### Step 3 — openjpeg 외부 의존 설정

- vcpkg.json에 `openjpeg` 추가 (BSD 라이선스, ~500KB)
- CMakeLists.txt: `find_package(OpenJPEG)` + 링크
- WASM: emscripten 빌드도 시도 (openjpeg는 Emscripten 포팅 존재)
- 일단 native만 진행, WASM은 분리 작업으로

**작업**: ~2-3시간 (특히 WASM 빌드 검증).

### Step 4 — JPEG 2000 Lossless + Lossy 디코더

- `opj_codec_create_decoder(OPJ_CODEC_J2K)` + 메모리 stream
- 각 frame 압축 바이트 → openjpeg → 16비트 평면 픽셀
- pydicom `693_J2KI.dcm` (J2K Lossy) + 사전에 확보한 합성 J2K Lossless로 검증

**작업**: ~5-6시간.

### Step 5 — 검증

- pydicom `MR_small_RLE.dcm` 로드 → MR 데이터 정상 표시
- pydicom `693_J2KI.dcm` 로드 → CT 데이터 정상 표시
- 회귀: 기존 Explicit/Implicit VR LE 4 코퍼스 무회귀
- (선택) 합성 J2K Lossless 생성 + 비트 동일성 확인 (lossy는 부분 검증만)

**작업**: ~2-3시간.

### 합계: ~15-20시간

---

## 4. 영향 받는 파일

| 단계 | 파일 |
| --- | --- |
| Step 1 | `src/assets/DicomFile.cpp` — encapsulated walk, TS UID 인식 |
| Step 2 | `src/assets/DicomFile.cpp` — RLE 디코더 (스택 로컬, ~100줄) |
| Step 3 | `vcpkg.json`, `CMakeLists.txt`, `scripts/wasm.ps1` (WASM 빌드) |
| Step 4 | `src/assets/DicomFile.cpp` — openjpeg integration |
| Step 5 | `tests/dicom_compressed_test.cpp` (선택, 단위 테스트) |

엔진 코어 변경 없음.

---

## 5. 위험과 대응

| 위험 | 대응 |
| --- | --- |
| openjpeg vcpkg 빌드 실패 (Windows) | vcpkg manifest 모드로 명시. 실패 시 conan/직접 빌드 대안 |
| WASM openjpeg 포팅 | 일단 native만. WASM은 차후 작업 분리 (사용자 결정 필요) |
| Pixel padding 처리 (16비트 → uint16 align) | RLE 디코더에서 segment 결합 시 endianness 주의 |
| Multi-frame (NumberOfFrames > 1) | Step 1 walker는 frame 수만큼 item 수집. 기존 multi-frame 경로 그대로 |

---

## 6. 후속 (이 계획 밖)

- JPEG Lossless (`1.2.840.10008.1.2.4.70`) — libjpeg-turbo 의존, 레거시 PACS
- JPEG-LS (`1.2.840.10008.1.2.4.80`) — charls 의존, 일부 PACS
- WASM openjpeg 빌드 — Emscripten 포팅 검증 + WASM 뷰어에서 압축 DICOM 로드

---

## 7. 다음 진입점

Step 1 (encapsulated walk + TS 인식) 부터 진행. 각 단계 끝에 커밋. 외부 의존
추가(Step 3)는 별도 커밋으로 분리해 vcpkg.json/CMakeLists 변경을 명확히.
