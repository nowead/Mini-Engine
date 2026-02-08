# Network Integration Points

**Document Version**: 1.0
**Last Updated**: 2026-01-21
**Author**: Rendering Team
**Target Audience**: Network/Backend Team

---

## Overview

이 문서는 Mini-Engine 렌더링 시스템과 네트워크 레이어를 연결하기 위한 통합 포인트를 정의합니다.

**현재 상태**:
- Rendering Engine: Phase 3 완료 (Skybox, Lighting, Particles)
- Network Integration: Phase 4 대기 중

---

## 1. 데이터 흐름 아키텍처

```
┌─────────────────────────────────────────────────────────────────┐
│                     Network Layer (Your Part)                   │
├─────────────────────────────────────────────────────────────────┤
│  WebSocket Client  ──>  FlatBuffers Decoder  ──>  PriceUpdate   │
└───────────────────────────────┬─────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                   Integration Interface                         │
├─────────────────────────────────────────────────────────────────┤
│  WorldManager::applyPriceUpdates(PriceUpdateBatch& updates)     │
└───────────────────────────────┬─────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                  Rendering Engine (My Part)                     │
├─────────────────────────────────────────────────────────────────┤
│  BuildingManager  ──>  Height Animation  ──>  GPU Instance      │
│  ParticleSystem   ──>  Effect Spawning   ──>  Billboard Render  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 핵심 통합 인터페이스

### 2.1 PriceUpdate 구조체

**파일**: `src/game/sync/PriceUpdate.hpp`

```cpp
struct PriceUpdate {
    std::string buildingId;     // 빌딩 식별자 (예: "AAPL", "BTC")
    float newPrice;             // 새로운 가격
    float priceChangePercent;   // 변동률 (%) - 파티클 이펙트 트리거용
    uint64_t timestamp;         // 서버 타임스탬프
};

struct PriceUpdateBatch {
    std::vector<PriceUpdate> updates;
    uint64_t serverTimestamp;
};
```

### 2.2 WorldManager 인터페이스

**파일**: `src/game/managers/WorldManager.hpp`

```cpp
class WorldManager {
public:
    /**
     * @brief 가격 업데이트 배치 적용
     * @param updates 네트워크에서 수신한 가격 업데이트 배치
     *
     * 이 메서드를 호출하면:
     * 1. 각 빌딩의 높이가 새 가격에 맞게 애니메이션됨
     * 2. 급등/급락 시 파티클 이펙트가 자동 생성됨
     * 3. 빌딩 색상이 변동에 따라 변경됨 (상승=초록, 하락=빨강)
     */
    void applyPriceUpdates(const PriceUpdateBatch& updates);

    /**
     * @brief 특정 빌딩의 가격 업데이트
     * @param buildingId 빌딩 ID
     * @param newPrice 새 가격
     */
    void updateBuildingPrice(const std::string& buildingId, float newPrice);

    /**
     * @brief 빌딩 매니저 접근
     */
    BuildingManager* getBuildingManager();
};
```

---

## 3. 빌딩 ID 매핑

### 3.1 현재 섹터 구조

| 섹터 | 빌딩 ID 패턴 | 슬롯 수 | 위치 |
|------|-------------|--------|------|
| NASDAQ | `NASDAQ_{row}_{col}` | 400 (20x20) | 중앙 |
| KOSDAQ | `KOSDAQ_{row}_{col}` | 256 (16x16) | 좌측 |
| CRYPTO | `CRYPTO_{row}_{col}` | 100 (10x10) | 우측 |

### 3.2 빌딩 등록

네트워크 팀에서 실제 종목 ID를 사용하려면:

```cpp
// 옵션 1: 종목 코드를 빌딩 ID로 사용
worldManager->registerBuilding("AAPL", sectorName, gridPosition);

// 옵션 2: 매핑 테이블 사용
std::unordered_map<std::string, std::string> tickerToBuilding = {
    {"AAPL", "NASDAQ_0_0"},
    {"GOOGL", "NASDAQ_0_1"},
    {"BTC", "CRYPTO_0_0"},
    // ...
};
```

---

## 4. 파티클 이펙트 트리거

### 4.1 자동 트리거 (권장)

`applyPriceUpdates()` 호출 시 내부적으로 처리됨:

```cpp
// WorldManager 내부 로직
void WorldManager::applyPriceUpdates(const PriceUpdateBatch& updates) {
    for (const auto& update : updates.updates) {
        auto* building = buildingManager->getBuilding(update.buildingId);
        if (!building) continue;

        // 높이 업데이트
        building->setTargetPrice(update.newPrice);

        // 급등/급락 시 파티클 자동 생성
        if (update.priceChangePercent > 5.0f) {
            // 5% 이상 상승: 로켓 이펙트
            particleSystem->spawnEffect(
                ParticleEffectType::RocketLaunch,
                building->getPosition() + glm::vec3(0, building->getHeight(), 0),
                3.0f  // duration
            );
        } else if (update.priceChangePercent < -5.0f) {
            // 5% 이상 하락: 연기 이펙트
            particleSystem->spawnEffect(
                ParticleEffectType::SmokeFall,
                building->getPosition(),
                2.0f
            );
        }
    }
}
```

### 4.2 수동 트리거

특별한 이벤트에 파티클을 직접 생성하려면:

```cpp
// Application.cpp 또는 네트워크 핸들러에서
particleSystem->spawnEffect(
    effects::ParticleEffectType::Confetti,  // 이펙트 타입
    glm::vec3(0.0f, 50.0f, 0.0f),           // 위치
    5.0f                                     // 지속 시간 (초)
);
```

**사용 가능한 이펙트 타입**:
- `RocketLaunch` - 상승 시 (초록 파티클, 위로 분출)
- `Confetti` - 축하 이벤트 (다색 버스트)
- `SmokeFall` - 하락 시 (회색, 아래로)
- `Sparks` - 변동성 (주황, 빠른 속도)
- `Glow` - 주목 표시 (청록, 느린 움직임)
- `Rain` - 배경 효과 (파란-회색, 스트리밍)

---

## 5. 메인 루프 통합

### 5.1 현재 Application.cpp 구조

```cpp
void Application::run() {
    while (!glfwWindowShouldClose(window)) {
        float deltaTime = calculateDeltaTime();

        glfwPollEvents();
        processInput();

        // ============================================
        // 여기에 네트워크 업데이트 추가
        // ============================================
        // if (networkClient->hasUpdates()) {
        //     auto updates = networkClient->getUpdates();
        //     worldManager->applyPriceUpdates(updates);
        // }
        // ============================================

        renderer->updateCamera(camera->getViewMatrix(),
                               camera->getProjectionMatrix(),
                               camera->getPosition());

        // 월드 업데이트 (애니메이션, 파티클)
        if (worldManager) {
            worldManager->update(deltaTime);

            // 렌더링 데이터 제출
            auto* buildingManager = worldManager->getBuildingManager();
            if (buildingManager) {
                rendering::InstancedRenderData renderData;
                renderData.mesh = buildingManager->getBuildingMesh();
                renderData.instanceBuffer = buildingManager->getInstanceBuffer();
                renderData.instanceCount = buildingManager->getBuildingCount();
                renderer->submitInstancedRenderData(renderData);
            }
        }

        // 파티클 시스템 업데이트
        if (particleSystem) {
            particleSystem->update(deltaTime);
            renderer->submitParticleSystem(particleSystem.get());
        }

        renderer->drawFrame();
    }
}
```

### 5.2 네트워크 클라이언트 인터페이스 제안

```cpp
// 네트워크 팀에서 구현할 인터페이스 (제안)
class INetworkClient {
public:
    virtual ~INetworkClient() = default;

    // 연결 관리
    virtual bool connect(const std::string& serverUrl) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // 데이터 수신
    virtual bool hasUpdates() const = 0;
    virtual PriceUpdateBatch getUpdates() = 0;

    // 구독 관리
    virtual void subscribe(const std::vector<std::string>& symbols) = 0;
    virtual void unsubscribe(const std::vector<std::string>& symbols) = 0;
};
```

---

## 6. 성능 고려사항

### 6.1 업데이트 빈도

| 시나리오 | 권장 빈도 | 비고 |
|----------|----------|------|
| 실시간 시세 | 100ms ~ 500ms | 프레임 레이트에 영향 없음 |
| 배치 업데이트 | 1초 | 많은 종목 동시 업데이트 시 |
| 급등/급락 알림 | 즉시 | 파티클 이펙트 트리거 |

### 6.2 버퍼 업데이트 최적화

```cpp
// BuildingManager 내부 - Dirty Flag 패턴 사용
void BuildingManager::updateInstanceBuffer() {
    if (!isDirty) return;  // 변경 없으면 스킵

    // GPU 버퍼 업데이트
    instanceBuffer->update(instanceData.data(), instanceData.size() * sizeof(InstanceData));
    isDirty = false;
}
```

### 6.3 현재 성능 지표

- **빌딩 렌더링**: 50,000 인스턴스 @ 60 FPS
- **파티클 시스템**: 10,000 파티클 @ 60 FPS
- **업데이트 오버헤드**: < 1ms per 1000 buildings

---

## 7. 테스트용 목업 데이터 생성기

현재 `MockDataGenerator`가 테스트용으로 구현되어 있습니다:

**파일**: `src/game/sync/MockDataGenerator.hpp`

```cpp
class MockDataGenerator {
public:
    MockDataGenerator(size_t buildingCount);

    // 랜덤 가격 업데이트 생성
    PriceUpdateBatch generateUpdates();

    // 급등/급락 이벤트 시뮬레이션
    PriceUpdateBatch generateSurgeEvent(const std::string& buildingId, float changePercent);
};
```

네트워크 연결 전 테스트에 활용 가능합니다.

---

## 8. 추가 구현 필요 항목 (네트워크 팀)

| 항목 | 우선순위 | 설명 |
|------|----------|------|
| WebSocket 클라이언트 | Critical | Emscripten WebSocket API 사용 |
| FlatBuffers 스키마 | Critical | 가격 업데이트 메시지 정의 |
| 재연결 로직 | High | 연결 끊김 시 자동 재연결 |
| 구독 관리 | Medium | 섹터별 구독/해제 |
| 히스토리 요청 | Low | 초기 로드 시 과거 데이터 |

---

## 9. 연락처 및 질문

렌더링 관련 질문이나 인터페이스 변경 요청은 이 문서를 업데이트하거나 이슈를 생성해주세요.

**주요 파일 위치**:
- `src/game/managers/WorldManager.hpp/cpp`
- `src/game/managers/BuildingManager.hpp/cpp`
- `src/game/sync/PriceUpdate.hpp`
- `src/effects/ParticleSystem.hpp/cpp`
- `src/Application.cpp` (메인 루프)

---

## Appendix: Quick Start 코드 예제

```cpp
// 1. 네트워크 클라이언트 생성 (네트워크 팀 구현)
auto networkClient = std::make_unique<WebSocketClient>();
networkClient->connect("wss://market-data-server.example.com");

// 2. 메인 루프에서 업데이트 적용
while (running) {
    // 네트워크 업데이트 확인
    if (networkClient->hasUpdates()) {
        PriceUpdateBatch updates = networkClient->getUpdates();
        worldManager->applyPriceUpdates(updates);
    }

    // 나머지 렌더링 로직...
    worldManager->update(deltaTime);
    renderer->drawFrame();
}
```
