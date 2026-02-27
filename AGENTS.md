# Repository Guidelines

## Project Structure & Module Organization
`src/` contains application code:
- `src/core/` for config and utility logic.
- `src/ui/` for main window, session tree, terminals, and command tools.
- `src/scriptengine/` for Lua integration.
- `src/resources/` for icons, desktop entries, plist templates, and bundled scripts.

Top-level `CMakeLists.txt` configures the build; `src/CMakeLists.txt` defines the `qshell` target. `scripts/` holds platform build/format helpers. `docs/` stores feature and packaging docs. `third_party/` vendors dependencies (treat as external; avoid edits unless required).

## Build, Test, and Development Commands
- `cmake -B build -S .` configures the project.
- `cmake --build build -j8` builds `qshell`.
- `cmake --install build` installs binaries/resources.
- `cmake --build build --target package` creates packages (DEB on Linux, DMG on macOS via CPack config).
- `./scripts/linux-build.sh [version]` Linux build + package helper.
- `./scripts/macos-build.sh` macOS build flow.
- `pwsh ./scripts/windows-build.ps1 [-BuildOnly]` Windows build/package flow.
- `./scripts/format.sh` runs `dos2unix`, `clang-format`, and `cmake-format`.

## Coding Style & Naming Conventions
Use C++17 and Qt6 patterns already in the codebase. Formatting is enforced by `.clang-format`:
- 4-space indentation, no tabs.
- `PointerAlignment: Right`.
- Class names `CamelCase`, functions/variables `camelBack`.
- Private/protected members use trailing underscore (see `.clang-tidy` naming rules).

Run `./scripts/format.sh` before committing. On Linux, CMake enables `clang-tidy` by default (`QSLOG_CLANG_TIDY_ENABLE=ON`).

## Testing Guidelines
There is no first-class unit-test target in the top-level CMake yet. Validate changes with:
- A clean build on your target OS.
- Feature smoke tests in-app (SSH, serial, local-shell flows as relevant).
- Lua script checks using examples in `scripts/lua/` (for example, `timer_test.lua`, `reboot_test.lua`) when touching script engine behavior.

## Commit & Pull Request Guidelines
Recent commits are short, imperative, and focused (Chinese or English), e.g. `fix windows compile error` / `添加 macos dmg 发布逻辑`. Follow that style:
- Keep subject concise and action-oriented.
- One logical change per commit.

For PRs, include:
- What changed and why.
- Target platform(s) tested and exact commands run.
- Screenshots/GIFs for UI changes.
- Linked issue/reference when applicable.
