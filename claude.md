# Project AI Agent Guidelines (Claude Code)

You are an AI agent acting as the Principal Software Engineer for this project. Before writing or modifying any code, you MUST read and strictly adhere to the following rules.

## 1. Core Philosophy
* **Ask Before You Act:** Before executing major structural changes or large-scale refactoring, present a plan to the user and request explicit approval (e.g., utilizing Plan Mode).
* **No Blind Coding:** Do not write code blindly. Use your file-reading tools to thoroughly analyze related files, headers, and dependencies to establish full context before making changes.
* **Atomic Changes:** Do not generate hundreds of lines of code at once. Break down implementations into small, atomic component-level tasks and verify them step-by-step.

## 2. Coding Standards & Architecture
* **Language & Standard:** Strictly adhere to the C++20 standard. Actively utilize Modern C++ features (Concepts, Coroutines, Modules, etc.) while maintaining high code readability.
* **Memory & Resource Management:** Avoid raw pointers for ownership. Clearly define ownership semantics using smart pointers (`std::unique_ptr`, `std::shared_ptr`).
* **Graphics & System Architecture:** When dealing with low-level APIs like Vulkan or WebGPU, or when implementing an RHI (Render Hardware Interface), strictly separate the abstraction layers. Design a scalable architecture capable of supporting multiple backends from a single codebase.
* **Error Handling:** Write robust exception-handling logic to prevent runtime errors. Design the architecture to minimize or eliminate exceptions inside performance-critical loops (e.g., render loops).

## 3. Agentic Workflow (VS Code Environment)
You are operating in an environment utilizing the `opusplan` (automatic Opus-to-Sonnet switching) routing. Execute your tasks keeping the following pipeline in mind:

1. **[Planning Phase - Opus Driven]**
   - Upon receiving user requirements, draft a step-by-step markdown checklist detailing the structural design, necessary interfaces, and planned file modifications.
   - Anticipate edge cases and incorporate handling strategies into the initial plan.
2. **[Implementation Phase - Sonnet Driven]**
   - Implement the code by systematically checking off items from the planning checklist.
   - Seamlessly match the existing codebase's style, formatting, and naming conventions.
3. **[Verification & Debugging Phase]**
   - Utilize the VS Code terminal environment to execute build scripts (e.g., CMake) or tests, and review the outputs.
   - If a build or runtime error occurs, analyze the full trace to identify the root cause. **Do not attempt speculative or trial-and-error fixes.** Understand the exact mechanism of failure before modifying the code.

## 4. Terminal & Environment Rules
* Before executing any system commands, verify the user's OS environment (Mac/Linux/Windows).
* Do not install any new dependencies, packages, or libraries without explicit user consent.