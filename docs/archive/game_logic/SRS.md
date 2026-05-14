# Software Requirements Specification (SRS)

## 1. Project Overview

This project aims to build a web-based platform that visualizes real-time stock and cryptocurrency market data in a 3D open-world (metaverse) environment, providing users with an intuitive and immersive investment data exploration experience.

---

## 2. Functional Requirements

### 2.1 Client & Rendering Engine

| ID | Requirement | Description | Priority |
|----|-------------|-------------|----------|
| **FR-1.1** | RHI-based Rendering | Hardware-accelerated graphics processing through RHI abstraction layer including WebGPU backend | Critical |
| **FR-1.2** | Data Visualization | Buildings (stocks) dynamically change height based on real-time price fluctuations | Critical |
| **FR-1.3** | Special Visual Effects | Rocket launch/particle effects on surge; building underground burial animation on crash | Medium |
| **FR-1.4** | World Exploration | Sector-based zoning (KOSDAQ, NASDAQ, etc.) with WASD keyboard free-camera navigation | Critical |
| **FR-1.5** | User Mode Management | Non-login (spectator) mode vs logged-in user mode (watchlist management) | Medium |

### 2.2 Backend & Data Pipeline

| ID | Requirement | Description | Priority |
|----|-------------|-------------|----------|
| **FR-2.1** | Real-time Collection Engine | Stock/crypto real-time tick data collection via Korea Investment & Securities (KIS) API integration | Critical |
| **FR-2.2** | Message Broker | Redis Pub/Sub to broadcast collected data to WebSocket clients | Critical |
| **FR-2.3** | Data Synchronization | Real-time synchronization of user positions (absolute coordinates) and chat bubbles across connected users | Critical |
| **FR-2.4** | Authentication & Security | JWT-based login processing and per-user portfolio (watchlist) storage | Critical |

### 2.3 Interaction & Content

| ID | Requirement | Description | Priority |
|----|-------------|-------------|----------|
| **FR-3.1** | Social Communication | Real-time chat bubble system for inter-user communication in multiplayer environment | Medium |
| **FR-3.2** | Mock Investment Content | Casino-themed in-game mini-game with "price betting" mechanics | Low |

---

## 3. Non-Functional Requirements

### 3.1 Performance

| Requirement | Description |
|-------------|-------------|
| **GPU Instancing** | Essential for rendering thousands of 3D building objects without frame drops |
| **Compute Shaders** | Utilized for complex animations and physics calculations |
| **Memory Optimization** | Ring buffers and FlatBuffers to minimize GC overhead and network latency |

### 3.2 Scalability

| Requirement | Description |
|-------------|-------------|
| **MSA Design** | AWS ECS-based microservices architecture for flexible scaling |
| **RHI Extensibility** | Low-coupling design enabling easy extension to next-gen graphics APIs (Vulkan, DX12, etc.) |

### 3.3 Compatibility & Environment

| Aspect | Specification |
|--------|---------------|
| **Browser Support** | Desktop browsers with WebGPU support only (Chrome, Edge, etc.) |
| **Mobile Support** | Not supported in initial phase due to performance constraints |
| **Core Logic** | C++20 native code running in web environment via Emscripten (WASM) |

### 3.4 Aesthetics

| Aspect | Description |
|--------|-------------|
| **Design Tone** | Casual and playful tone-and-manner similar to SpongeBob's "Bikini Bottom" |

---

## 4. Technical Stack

| Category | Technologies |
|----------|--------------|
| **Core** | C++20, WebAssembly (WASM), Emscripten |
| **Graphics** | WebGPU, GLSL/WGSL |
| **Backend** | Redis (Pub/Sub), WebSocket, JWT, KIS API |
| **Infrastructure** | AWS ECS (Microservices Architecture) |
| **Serialization** | FlatBuffers (Binary Data Transfer) |

---

## 5. Deliverables

| Document | Description |
|----------|-------------|
| **Software Requirements Specification (SRS)** | This document specifying system functionalities and constraints |
| **Architecture Design Document** | RHI structure UML diagrams, network topology, and data flow diagrams |
| **API Specification** | Client-backend JSON/Binary communication protocol definitions |
| **Prototype Build** | Test build implementing WebGPU rendering and real-time data communication |

---

**Last Updated**: 2025-05-22
