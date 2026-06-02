# 변경 이력 — 2026-06-02

> 작업 범위: M3-3 v0 (bricked volume storage) + M4 v1 (progressive 누적)을
> 양 백엔드에서 닫는 검증 라운드. 그 과정에서 표면화된 **두 개의 latent 버그**
> 진단 및 수정 — (1) WASM 빌드 환경에서 EMSDK env 전파 누락, (2) M3-3 v0의
> OccUBO 확장 시 bind group entry 크기 미동기화.

---

## 1. 개요

전략 체크포인트 후 **"Step A 네이티브 시각 검증 → Step B WASM 빌드+브라우저
검증"** 두 단계를 한 세션에 묶어서 닫기로 결정. 양 단계 모두 통과시키는 과정에서
이미 커밋된 두 변경이 **양 백엔드 중 한쪽에서만** 잘못 동작하고 있음을 발견.
숨어 있던 latent 버그라 빠르게 잡고, 두 진단 여정을 그대로 남긴다 — 같은
패턴(WASM-only 환경 트랩, Vulkan-permissive vs Dawn-strict skew)을 재현하지 않게.

오늘 끝낸 항목:

- **Step A** — 네이티브 `volume_viewer.exe`에서 절차적 + NIfTI(합성 96³) +
  DICOM 시리즈(합성 50 slices) + 큰 NIfTI(33MB) 4종 시각 검증. 모든 회전·창값·
  TF 프리셋·렌더 모드 전환 정상.
- **Step B** — WASM `volume_viewer_wasm.html` 브라우저 검증. 첫 시도에서 두
  버그 표면화 → 둘 다 수정 → 콘솔 깨끗 (4 셰이더 WGSL OK + validation 0).
- **부수 수정** — `scripts/wasm.ps1` EMSDK env 전파 + VolumeRenderer OccUBO
  bind group entry 크기. 둘을 한 커밋(`876b44e`)으로 묶음.

---

## 2. EMSDK 환경변수 전파 트랩 — 진단 여정

### 2.1 증상

깨끗한 환경에서 `scripts/wasm.ps1 build` 실행 → CMake configure 실패:

```text
emar qc libcmTC_4071f.a CMakeFiles/cmTC_4071f.dir/testCXXCompiler.cxx.obj
Error running link command: no such file or directory
NMAKE : fatal error U1077: ... : '0x2' 반환 코드입니다.
CMake will not be able to correctly generate this project.
-- Configuring incomplete, errors occurred!
emcmake: error: ... failed (returned 1)
```

표면적 의미: "emar라는 명령어를 찾을 수 없다". 그러나 `where emar`는 정상적으로
`C:\Users\dwm95\emsdk\upstream\emscripten\emar.bat`을 반환. emar.bat 단독 호출도
정상 작동(`/tmp/hello.cpp` → `em++ -c` → `emar qc`까지 성공).

함정 1: **exit code 0**으로 빌드 스크립트가 종료. CLAUDE.md §7 트랩(cmd /c 출력이
PowerShell에서 NativeCommandError로 래핑되어 실제 실패해도 exit code가 0이 됨).
산출물 부재(`ls build_wasm/volume_viewer_wasm.*` → no such file)로 비로소 실패
인지.

### 2.2 1차 가설들 — 모두 폐기

- "일시적 emsdk 초기화 실패" → 클린 빌드 두 번 모두 동일하게 재현. 환경적
  flakiness 아님.
- "emsdk 손상" → 단독 emar 호출 정상. 도구체인 자체는 정상.
- "PATH 누락" → 빌드 로그 `where emar`가 정상적으로 emar.bat을 반환. PATH 정상.

### 2.3 진짜 단서 — CreateProcess와 .bat 확장자

CMake가 `cmake -E cmake_link_script link.txt --verbose=1`로 link.txt 라인을
실행. link.txt 내용은 단순 `emar qc libxxx.a yyy.o`. 이 라인을 실행하는 주체는
CMake의 cmsys 라이브러리 → 결국 Windows `CreateProcess` API.

**`CreateProcess`는 PATHEXT를 따르지 않는다**. cmd.exe가 `emar`를 입력받으면
PATHEXT(`.com;.exe;.bat;.cmd;...`)로 확장해서 `emar.bat`을 찾지만, `CreateProcess`
는 정확히 `emar`라는 파일만 찾는다. 그런 파일은 없으니 ENOENT → "no such file
or directory" 에러로 변환되어 CMake가 출력.

`cmake/EmscriptenToolchain.cmake`은 이 함정을 이미 알고 있음 (헤더 주석에 명시):

```cmake
# On Windows, emar/emranlib are .bat wrappers — cmake_link_script
# (CreateProcess) cannot run extension-less script names directly, but CMake
# wraps .bat paths in "cmd /c" automatically. Test CMAKE_HOST_WIN32, NOT
# WIN32 ... → wrongly fall to the plain emar, which Windows cannot execute.
if(CMAKE_HOST_WIN32 AND DEFINED ENV{EMSDK})
    set(CMAKE_AR "${_em}/emar.bat" CACHE FILEPATH "Emscripten ar" FORCE)
else()
    set(CMAKE_AR emar CACHE FILEPATH "Emscripten ar")
endif()
```

조건이 `CMAKE_HOST_WIN32 AND DEFINED ENV{EMSDK}` 이므로 EMSDK 환경변수가 비어
있으면 else 가지 → `CMAKE_AR=emar` (확장자 없음) → CreateProcess 실패.

진단 배치 파일을 만들어 cmd 컨텍스트에서 EMSDK 값을 직접 출력:

```text
=== Before emcmake ===
EMSDK=
```

비어 있다.

### 2.4 진짜 원인 — emsdk_env.bat의 bash-style export

`emsdk_env.bat`는 emsdk를 활성화하는 표준 스크립트. 그러나 출력은:

```text
export PATH="/c/Users/dwm95/emsdk:...";
export EMSDK="C:/Users/dwm95/emsdk";
export EMSDK_NODE="...";
unset EMSDK_PY;
```

**POSIX 스타일 `export`**. bash/zsh에서만 동작. cmd.exe는 `export`를 미지의
명령어로 처리하고 silently 무시 → 환경변수 전혀 설정 안 됨.

`scripts/wasm.ps1`은 이 함정을 이미 PATH 한정으로 인지하고 있었음 — 활성화
후 `set PATH=%USERPROFILE%\emsdk\upstream\emscripten;%PATH%`를 명시적으로
실행. 하지만 EMSDK env 변수는 명시 set이 누락. PATH만 살아남고 EMSDK는 죽음.

### 2.5 수정

`scripts/wasm.ps1`의 wrapper bat에 한 줄 추가:

```powershell
$lines.Add("set PATH=$emBin;%PATH%")
# emsdk_env.bat outputs bash-style `export` statements that cmd ignores,
# so EMSDK ends up unset. Set EMSDK manually so the toolchain branch fires.
$lines.Add("set EMSDK=$EmsdkDir")
```

재빌드 후 configure 정상 통과 → 컴파일 → 링킹 → 산출물 4종(`html`, `js`, `wasm`,
`data`) 정상 생성.

### 2.6 교훈

- **WASM exit code 0 ≠ 빌드 성공** (CLAUDE.md §7 트랩). 항상 산출물 직접 확인.
- **CreateProcess는 PATHEXT 무시**. emsdk 같은 .bat 래퍼 도구체인은 명시적
  `.bat` 경로 또는 `cmd /c` 래핑 필수.
- **bash-style `export`는 cmd에서 silently 무시**. 외부 활성화 스크립트가
  POSIX 출력이면 wrapper에서 PATH·그 외 환경변수까지 모두 명시 `set` 필요.

memory에 [`project_wasm_emsdk_env.md`](../../../C:/Users/dwm95/.claude/projects/c--Users-dwm95-Desktop-Mini-Engine/memory/project_wasm_emsdk_env.md) 노트로 기록.

---

## 3. OccUBO bind group entry 크기 트랩 — 진단 여정

### 3.1 증상

WASM 빌드 성공 후 브라우저 콘솔 (Chrome DevTools):

```text
[WebGPU Error] Validation: [Buffer "VolumeOccUBO"] bound with size 32 at
group 0, binding 0 is too small. The pipeline ([ComputePipeline
"VolumeOccPipeline"]) requires a buffer binding which is at least 64 bytes.
This binding is a uniform buffer binding. It is padded to a multiple of 16
bytes, and as a result may be larger than the associated data in the shader
source.
 - While encoding [ComputePassEncoder "VolumeOccupancy"].DispatchWorkgroups(4, 4, 4).
 - While finishing [CommandEncoder (unlabeled)].

[WebGPU Error] Validation: [Invalid CommandBuffer] is invalid due to a
previous error.
 - While calling [Queue].Submit([[Invalid CommandBuffer]])
```

매 프레임 occupancy compute dispatch가 무효한 command buffer를 만들어 큐
submit 실패. 그러나 **시각적으로는 멀쩡** — 화면에 볼륨이 정상 표시되고
사용자 조작도 다 작동.

**네이티브(Vulkan)에서는 어떤 validation 에러도 안 뜨고 시각적으로도 정상.**

### 3.2 1차 가설들

- "WGSL 셰이더의 UBO 선언이 잘못됐다" → 셰이더는 16 uint32 = 64바이트로 선언
  (volDim + gridDim + pageGrid + atlasGrid 각 vec4<u32>). 셰이더 코드 정상.
- "C++ UBO 버퍼 생성 크기가 작다" → `ud.size = sizeof(uint32_t) * 16` 으로
  64바이트 정상 할당, write도 16 uint32 다 채움 (`u[16]`). 버퍼 자체 OK.

### 3.3 진짜 원인 — bind group entry binding size

`BindGroupEntry::Buffer(binding, buffer, offset, size)`에서 마지막 `size`
인자가 **이 binding으로 셰이더가 읽을 수 있는 영역의 크기**. 실제 버퍼 할당
크기와 별개로, descriptor에서 "이 binding은 size 바이트만 보인다"고 선언하는
값.

M3-3 v0 커밋(`45448cd`)에서 OccUBO를 8 → 16 uint32(32 → 64바이트)로 확장하면서:

- ✅ 버퍼 생성 크기 (`ud.size = sizeof(uint32_t) * 16`)
- ✅ 쓰기 (`m_occUBO->write(u, sizeof(u))` where `u[16]`)
- ❌ bind group entry size — **여전히 `sizeof(uint32_t) * 8 = 32 바이트`**

```cpp
// 변경 누락 — Vulkan 분기와 WebGPU 분기 모두
rhi::BindGroupEntry::Buffer(0, m_occUBO.get(), 0, sizeof(uint32_t) * 8),
```

셰이더가 binding 0의 offset 32+에서 pageGrid·atlasGrid를 읽으려고 하면 Dawn은
"binding은 32바이트만 약속됐는데 셰이더는 64바이트 요구한다" 검출 → 매 dispatch
무효화 → 매 submit 실패.

### 3.4 왜 Vulkan에선 안 보였나

Vulkan validation은 이 케이스를 strict하게 안 잡는다 (또는 robust buffer access
extension이 활성화되어 OOB read를 0으로 silently 처리). 셰이더는 garbage를
읽지만 그 결과가 흐름상 무해한 패스(occupancy의 min/max 계산):

- 잘못된 pageGrid → 잘못된 brick index → 셰이더 내부 OOB read → atlas의 0
  영역 샘플 → cell의 min/max가 잘못된 값
- 그러나 **empty-space skipping은 잘못된 셀을 더 많이/적게 건너뛰는 수준의
  성능 차이만 만들고 시각적 결과를 깨지 않는다**

→ Step A 시각 검증에서 통과. 정확성 버그가 *기능적*으로 숨겨진 채 commit됨.

### 3.5 수정

`replace_all`로 두 군데 동시:

```cpp
rhi::BindGroupEntry::Buffer(0, m_occUBO.get(), 0, sizeof(uint32_t) * 16),
```

WASM 재빌드 → 하드 리프레시 → 콘솔 깨끗 (4 셰이더 WGSL OK + validation 0 +
4 path-trace accumulation 정상).

### 3.6 교훈

- **버퍼 확장 시 4지점 동기 필수**:
  1. 버퍼 `BufferDesc::size`
  2. `buffer->write(...)`의 size
  3. **`BindGroupEntry::Buffer(binding, buf, offset, size)`의 size**
  4. 셰이더의 struct 선언
  넷 중 하나만 누락돼도 함정. 특히 (3)이 가장 잘 까먹힘 (생성 시점 ≠ 바인딩
  시점).
- **Vulkan validation은 Dawn보다 permissive**. 같은 코드가 Vulkan에서 통과해도
  Dawn은 잡는다. **양 백엔드 시각 검증 = 양쪽 모두 콘솔까지 깨끗** 으로 기준
  강화.
- M3-3 v0 같은 UBO 확장 작업에서는 grep으로 `m_xxxUBO.get(), 0,` 패턴 전수
  점검을 PR 전에 의식적으로 한 번 더.

---

## 4. 검증 절차 — 향후 비슷한 작업의 템플릿

### Step A 네이티브 시각 검증 — 데이터 매트릭스

| 데이터 | 명령 | 확인 |
| --- | --- | --- |
| 절차적 (no args) | `volume_viewer.exe` | atlas 인덱싱 sanity, 가장 작은 page grid |
| 합성 NIfTI 소형 | `volume_viewer.exe synthetic_ct.nii` | 명확한 구조 시각화 |
| 합성 DICOM 시리즈 | `volume_viewer.exe synthetic_ct_dcm` | NIfTI와 동일 결과 (DICOM 파서 무회귀) |
| 큰 합성 NIfTI 33MB | `volume_viewer.exe large_ct.nii` | atlas 슬롯 더 많이 사용 → brick 경계 노출 |

각 데이터에서 확인: 카메라 회전 부드러움, window slider 부드러움, TF preset
즉시 적용, Lambert ↔ Path-trace 전환 + PT 누적 reset, empty-space skip 토글이
시각 결과 무영향 (성능만 차이).

### Step B WASM 브라우저 검증

```powershell
.\scripts\wasm.ps1 build -Target volume_viewer_wasm
cd build_wasm; python ..\scripts\serve_nocache.py 8000
# → http://localhost:8000/volume_viewer_wasm.html
```

DevTools 콘솔(F12) **에러 0** 확인이 통과 기준 — WGSL OK 4종(VolumeMarchVS/FS,
VolumePathtraceFS, VolumePathDisplayFS, VolumeOccupancy) + validation 메시지
없음. **시각 정상은 통과 조건 부족** (이번 케이스로 입증).

---

## 5. 잔여 작업

M3-3 v0 + M4 v1이 양 백엔드에서 닫혔으므로 다음은 **Step C/D/E**:

- **Step C** — atlas 용량 파라미터화 + 로드 타임 자동 산정. 현재 기본 (4,4,4)
  = 64 슬롯 = 37MB로 1GB-class CT를 못 받음.
- **Step D** — TCIA류 공개 대용량 임상 CT 수급 + 측정 인프라(이미 보유한
  `WebGPUTimer`로 1024³ march/PT 패스 타이밍).
- **Step E** — M3-3 v1 streaming (camera-frustum 가시성 + LRU eviction +
  on-disk 페이징). "1GB+ × 60fps" 헤드라인의 본체.

부수 가지: DICOM Implicit VR LE (임상 PACS 커버리지 확장, 엔진 변경 없음).
