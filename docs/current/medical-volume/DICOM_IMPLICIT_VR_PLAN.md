# DICOM Implicit VR LE 지원 계획

**작성일**: 2026-06-07
**목표**: 현재 Explicit VR LE만 지원하는 DICOM 로더에 **Implicit VR Little Endian
(`1.2.840.10008.1.2`)** transfer syntax 지원을 추가. 임상 PACS의 기본 출력
형식이며 TCIA 등 대형 코퍼스의 다수를 unlock.

**로드맵 정렬**: §0 "대용량 실데이터" 차별화 + §3 "1GB 60fps" 헤드라인 측정의
선결 조건. v1-β LOD 완료 후 자연스러운 다음 단계 (§5 line 305).

---

## 1. Implicit VR LE 구조 차이

| | Explicit VR LE (현재) | Implicit VR LE (추가) |
| --- | --- | --- |
| Transfer syntax UID | `1.2.840.10008.1.2.1` | `1.2.840.10008.1.2` |
| Element 헤더 | `tag(4B) + VR(2B) + len(2B 또는 4B)` | `tag(4B) + len(4B)` |
| VR 확인 | 헤더에서 직접 | **tag dictionary 룩업** |
| Sequence (SQ) 표식 | VR == "SQ" | Dictionary에서 SQ로 등록 |
| Undefined length | `len == 0xFFFFFFFF` (같음) | `len == 0xFFFFFFFF` (같음) |
| File meta (group 0002) | Explicit VR | **항상 Explicit VR** (DICOM 표준) |

**핵심 관찰**: file meta (group 0002)는 transfer syntax 무관하게 항상 Explicit
VR LE. 즉 transfer syntax UID는 Explicit VR로 읽고, 그 이후 dataset만 분기.

---

## 2. Atomic 단계 (5 step)

### Step 1 — Implicit VR LE 합성 생성기

`scripts/make_synthetic_dicom.py`에 `--vr {explicit,implicit}` 옵션 추가. 같은
합성 데이터를 두 인코딩으로 출력 → 시각 결과 비트 동일 검증.

**작업**: ~30분.

### Step 2 — 파서 리팩토링 (file meta vs dataset 분리)

`parseSlice`를 둘로 분할:
- `parseFileMeta(buf, size, off)` → TransferSyntaxUID 읽고 dataset 시작 offset 반환
- `parseExplicitDataset(...)` → 현재 본문 그대로
- `parseImplicitDataset(...)` → 신규 (Step 4)

분기 위치: file meta 끝난 직후, TransferSyntaxUID에 따라.

**작업**: ~1시간 (정확성 유지가 핵심, 시각 무회귀).

### Step 3 — Tag → VR Dictionary

현재 parseSlice가 사용하는 12개 tag에 대해 VR 정의:

| Tag | VR | 의미 |
| --- | --- | --- |
| (0008,xxx) PatientID 등 | LO/UI/DA | (현재 미사용, 무시) |
| (0018,0050) | DS | SliceThickness |
| (0020,000E) | UI | SeriesInstanceUID |
| (0020,0013) | IS | InstanceNumber |
| (0020,0032) | DS | ImagePositionPatient |
| (0028,0008) | IS | NumberOfFrames |
| (0028,0010) | US | Rows |
| (0028,0011) | US | Cols |
| (0028,0030) | DS | PixelSpacing |
| (0028,0100) | US | BitsAllocated |
| (0028,0103) | US | PixelRepresentation |
| (0028,1052) | DS | RescaleIntercept |
| (0028,1053) | DS | RescaleSlope |
| (7FE0,0010) | OW | PixelData |

알려지지 않은 tag는 기본값 (예: UN — 4-byte length로 그냥 skip) 처리.

**중요**: undefined-length sequence는 VR 정보 없이도 `len == 0xFFFFFFFF`로
감지 가능. `skipUndefSeq` / `walkItemUntilEnd`는 거의 같은 로직.

**작업**: ~30분.

### Step 4 — Implicit VR Dataset Parser

`parseImplicitDataset`:
- 8-byte element header (tag 4B + len 4B)
- Tag dictionary에서 VR 룩업
- VR 기반 파싱 (`US` → 2B int, `DS` → string parse 등)
- Sequence (SQ) 또는 undefined-length → skip

`parseExplicitDataset`의 파싱 로직 (특히 `parseDsAsDouble` 등)을 재사용.

**작업**: ~1.5시간.

### Step 5 — 검증

1. **합성**: 같은 데이터로 Explicit + Implicit 생성 → 두 결과 binary identical.
2. **공개 코퍼스**: pydicom datasets 중 Implicit VR LE 파일 다운로드 시도 (TCIA
   소규모 샘플).
3. **회귀**: 기존 Explicit VR LE 4 코퍼스 모두 다시 로드 (무회귀).

**작업**: ~1시간.

---

## 3. 영향 받는 파일

| 파일 | 변경 종류 |
| --- | --- |
| `scripts/make_synthetic_dicom.py` | `--vr` 옵션 추가, Implicit VR 인코딩 |
| `src/assets/DicomFile.cpp` | parser 분기 + tag dictionary + implicit path |
| `src/assets/DicomFile.hpp` | 주석 갱신 (지원 transfer syntax 확장 명시) |
| (선택) `scripts/fetch_sample_ct.py` 또는 신규 fetch | Implicit VR 샘플 URL |

엔진 코어 변경 없음 — 파서 출력 `Volume3D`는 동일.

---

## 4. 검증 기준

- ✅ 합성 Implicit VR 파일 로드 → 같은 합성 Explicit VR와 voxel 비트 동일
- ✅ 기존 4종 Explicit VR 공개 코퍼스 무회귀
- ✅ 메모리/성능 영향 없음 (파싱은 로드 1회)
- (선택) 실 Implicit VR 임상 데이터 1종 이상 로드 성공

---

## 5. 위험과 대응

| 위험 | 대응 |
| --- | --- |
| Tag dictionary 누락된 unknown VR | 기본 4-byte length skip — 안전한 fallback |
| Sequence (SQ) 식별 실패 | 보유 dictionary에 SQ tag 명시. 못 찾으면 len이 알려진 값이면 skip. |
| Group 0002 (file meta) 누락 시 | DICM magic + group 0002 첫 element가 있는지 검증 (현재 코드도 검증) |
| 압축 transfer syntax 혼동 | Implicit VR LE는 비압축. TransferSyntaxUID로 명확 분기. 압축은 후속 작업으로 분리. |

---

## 6. 후속 (이 계획 밖)

- DICOM 압축 transfer syntax (JPEG/JPEG2000/RLE) — libjpeg/openjpeg 의존
- Big Endian variants (`1.2.840.10008.1.2.2` 등) — 거의 쓰이지 않음, 후순위
- 멀티 사이트 PACS의 conformance 차이 - 코너 케이스 발견 시 추가

---

## 7. 다음 진입점

Step 1부터 atomic 진행. 각 단계 끝에 커밋. 최종 통합 커밋 1회 또는
단계별 ~5 커밋 (사용자 선호).
