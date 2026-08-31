# Work Order — R8: CMake Test-Target Organization

Status: implementation work order  
Prepared from `main` at `ff5102afe28462017f5fbe941dec4aba1df2be24` after R7 / PR #194  
Scope: optional R8 of `docs/APP_REFACTOR_PLAN.md`

---

## 1. Decision after reviewing the current build files

R8 is worth doing now, but it must remain a **build-file organization-only** change.

The runtime architecture is already complete at R7. Do not reopen runtime refactoring.

The current root `CMakeLists.txt` contains:

- top-level project / platform validation;
- version generation;
- Velopack setup;
- production `ClawHUD` target;
- `ClawHUD.EcHelper` target;
- production post-build staging;
- then a very large `BUILD_TESTING` block containing the current 46 explicit test targets.

The test declarations dominate the lower part of the root build file. They also contain many legitimate target-specific differences in:

```text
source lists
compile definitions
include directories
link libraries
```

Therefore the correct R8 cleanup is:

> Move the existing test declarations out of the root `CMakeLists.txt` into one dedicated CMake include file, preserving every test target declaration as-is.

Do **not** introduce helper functions/macros in the same PR.

The value of this PR is file organization and a smaller production-focused root build file, not deduplication.

---

## 2. Goal

Create:

```text
cmake/ClawHUDTests.cmake
```

The root `CMakeLists.txt` should retain all production build configuration and only delegate the test declarations:

```cmake
include(CTest)
if(BUILD_TESTING)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ClawHUDTests.cmake")
endif()
```

`cmake/ClawHUDTests.cmake` should contain the current contents of the existing `if(BUILD_TESTING)` body, with no behavior changes.

The resulting structure should be approximately:

```text
CMakeLists.txt
    project/platform validation
    version configuration
    Velopack imported target
    ClawHUD production executable
    ClawHUD.EcHelper
    production post-build copy/staging
    include(CTest)
    if(BUILD_TESTING)
        include(cmake/ClawHUDTests.cmake)
    endif()

cmake/ClawHUDTests.cmake
    all current ClawHUD.*Tests target declarations
```

---

## 3. Files to read before editing

Read current `main`, not an older refactor branch:

```text
CMakeLists.txt
.github/workflows/Build-Test.yml
.github/workflows/Build-Release.yml
docs/APP_REFACTOR_PLAN.md
```

The active CI configuration currently configures with:

```text
-DBUILD_TESTING=ON
```

and both Build Test and Build Release run the full CTest suite after a Release build.

R8 must preserve that behavior exactly.

---

## 4. Hard scope boundary

Allowed implementation files:

```text
CMakeLists.txt
cmake/ClawHUDTests.cmake
docs/APP_REFACTOR_PLAN.md
```

A small work-order/progress documentation update is fine.

Do **not** modify:

```text
src/**
tests/**
.github/workflows/**
third_party/**
```

unless a build failure proves the pure relocation itself cannot work. If that happens, stop and report the issue rather than silently broadening R8.

There should be **zero C++ source/header diff** in R8.

There should be **zero test source diff** in R8.

There should be **zero workflow behavior diff** in R8.

---

## 5. Pure-relocation rule

Move the current test declarations from the root `if(BUILD_TESTING)` body into:

```text
cmake/ClawHUDTests.cmake
```

Preserve all existing declarations and order.

That includes every current:

```text
add_executable(...Tests ...)
target_compile_features(...)
target_compile_definitions(...)
target_include_directories(...)
target_link_libraries(...)
set_target_properties(...)
add_test(...)
```

Do not intentionally change whitespace-sensitive strings, compile-definition values, library lists, source paths, target names, or test names.

The active suite baseline at the start of R8 is:

```text
46 tests
```

The generated CTest inventory after relocation must still contain the same 46 test names.

---

## 6. Keep `include(CTest)` in the root file

Keep:

```cmake
include(CTest)
```

in the root `CMakeLists.txt`.

Reason:

`include(CTest)` establishes the normal CMake `BUILD_TESTING` option and enables CTest integration for the project.

Recommended final root tail:

```cmake
include(CTest)
if(BUILD_TESTING)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ClawHUDTests.cmake")
endif()
```

Do not move `include(CTest)` into the test file merely to make the root two lines shorter.

The root file should make the project's testing boundary obvious.

---

## 7. Do not wrap the included file in a second `if(BUILD_TESTING)`

Preferred shape:

```text
CMakeLists.txt owns the BUILD_TESTING condition.
ClawHUDTests.cmake contains declarations only.
```

Do not duplicate the condition in both files without a concrete reason.

This keeps ownership simple:

```text
root decides whether tests exist
included file declares the tests
```

---

## 8. Preserve path semantics

The current test targets use paths such as:

```text
tests/FooTests.cpp
src/ClawHUD/Foo.cpp
src/shared
${CMAKE_SOURCE_DIR}/third_party/...
```

When loaded with `include()` from the root source directory, these current relative source paths should continue to resolve against the same CMake source directory context.

Do not mechanically rewrite every path to `${CMAKE_SOURCE_DIR}/...` or `${CMAKE_CURRENT_LIST_DIR}/../...` unless configuration proves it is necessary.

The preferred implementation is a literal relocation with the existing paths unchanged.

---

## 9. Do not create a generic test-target helper in R8

Do not introduce helpers such as:

```cmake
function(add_clawhud_test ...)
function(clawhud_test ...)
macro(add_test_target ...)
```

in this PR.

Many of the existing targets intentionally differ in compile definitions and linked Windows libraries. Examples include combinations of:

```text
CLAWHUD_RUNTIME_LOGGER_TESTS
CLAWHUD_TEST_UNISPACE_PATH
user32
shell32
ole32
advapi32
propsys
wbemuuid
powrprof
d2d1
dwrite
d3d11
dxgi
```

A helper abstraction would turn R8 from a move into a semantic build-system refactor and make review harder.

If repeated boilerplate later becomes a demonstrated maintenance problem, consider a separate post-R8 CMake cleanup.

It is not part of this work order.

---

## 10. Do not split tests into multiple CMake files yet

Use one file:

```text
cmake/ClawHUDTests.cmake
```

Do not create a hierarchy such as:

```text
cmake/tests/HudTests.cmake
cmake/tests/GameDetectionTests.cmake
cmake/tests/TelemetryTests.cmake
cmake/tests/PresentMonTests.cmake
```

The current scale does not require that complexity.

One dedicated test file solves the actual problem while keeping navigation simple.

---

## 11. Production build definitions must stay in root `CMakeLists.txt`

Do not move these production areas into R8's test include:

```text
cmake_minimum_required / project
WIN32/MSVC guards
CLAWHUD_VERSION validation and Version.h generation
Velopack download/import
PresentMon path/version setup
ClawHUD target and its compile/link settings
ClawHUD.EcHelper target
ClawHUD POST_BUILD commands
runtime/font/Velopack/PresentMon copy rules
```

Root `CMakeLists.txt` should remain the obvious source of truth for what the shipping application builds.

---

## 12. Preserve production target behavior with BUILD_TESTING both ON and OFF

Verify both configurations.

### Test-enabled configuration

Equivalent to CI:

```powershell
cmake -S . -B build-test `
  -G "Visual Studio 18 2026" `
  -A x64 `
  -T v145 `
  -DBUILD_TESTING=ON
```

Required:

```text
ClawHUD target exists
ClawHUD.EcHelper target exists
all 46 test targets exist
CTest lists all 46 tests
```

### Test-disabled configuration

Use a separate clean build directory:

```powershell
cmake -S . -B build-no-tests `
  -G "Visual Studio 18 2026" `
  -A x64 `
  -T v145 `
  -DBUILD_TESTING=OFF
```

Required:

```text
ClawHUD target exists
ClawHUD.EcHelper target exists
test targets are not generated
configuration succeeds
```

Do not rely only on `BUILD_TESTING=ON`; the include boundary must also be correct when tests are disabled.

---

## 13. Build and test verification

Mandatory before PR completion:

```text
1. Configure clean Release x64 with BUILD_TESTING=ON.
2. Build full solution/target set.
3. Run full CTest.
4. Confirm 46/46 pass, or the actual unchanged current baseline if main legitimately changed before implementation.
5. Configure a separate clean directory with BUILD_TESTING=OFF.
6. Build ClawHUD and ClawHUD.EcHelper successfully in the no-tests configuration.
```

Use the actual current test count if another merged PR changes the baseline before R8 implementation; do not hard-code acceptance to 46 if main has legitimately changed.

The key invariant is **same test inventory before and after R8**.

---

## 14. Verify test inventory, not only pass count

A `46/46 passed` result alone is not enough if a target accidentally disappeared from CTest and the denominator also dropped.

Before editing, capture the current test list from a clean test-enabled configuration, for example:

```powershell
ctest --test-dir build -C Release -N
```

After relocation, capture it again.

Compare:

```text
number of tests
test names
```

Required result:

```text
identical inventory
```

If ordering changes but names/count and target semantics are identical, that is acceptable only if caused naturally by the same declaration order. Prefer preserving declaration order so even the listing stays stable.

---

## 15. Diff-audit requirement

This is intended to be a mechanical move.

Review the final diff specifically for:

```text
all old test declarations removed from root
all declarations present exactly once in cmake/ClawHUDTests.cmake
no test target duplicated
no test target omitted
no target name changed
no source list changed
no compile definition changed
no include path changed
no linked library changed
no CTest name changed
no production target change
```

`git diff --color-moved=zebra` is useful here.

The ideal diff should read as a large moved block plus a very small root include stanza.

---

## 16. CI workflow compatibility

Do not edit `.github/workflows/Build-Test.yml` or `.github/workflows/Build-Release.yml`.

Both already configure the root project with `BUILD_TESTING=ON`, build Release x64, and run CTest.

The R8 CMake include must be transparent to those workflows.

After the PR is pushed, GitHub Build Test must pass on the PR head.

The Release workflow does not need to be manually published merely to validate R8; its configure/build/test contract is already covered by the same CMake entry point and local/PR verification.

---

## 17. HUD / VRR safety contract

R8 must not touch runtime source code at all, so the protected HUD presentation contract should have a literal zero diff.

Do not modify:

```text
HudPresentation.*
HudPresentationContract.*
HudPresentationLifecycle.*
HudRenderer.*
HudController.*
windowExStyle
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
WS_EX_LAYERED
HTTRANSPARENT
MA_NOACTIVATE
ProductionHudPresentationContract()
Presentation API / DirectComposition path
independent flip
premultiplied alpha
background opacity behavior
```

All existing HUD contract/lifecycle tests must remain present in the moved CMake declarations and remain green.

Hardware smoke is not required for this CMake-only PR.

---

## 18. Settings / startup / production behavior

R8 must have no C++ diff, so all runtime behavior remains unchanged, including:

```text
tray-only startup
lazy SettingsWindow creation/release
HUD enable/visibility/F8 behavior
telemetry sampling
game detection
PresentMon API2 single-provider ownership
suspend/resume
VRR tweak startup
single-instance gate
hardware gate
update flow
```

Do not use R8 as a reason to clean unrelated source code.

---

## 19. Documentation update after implementation

Update:

```text
docs/APP_REFACTOR_PLAN.md
```

with the R8 result:

```text
PR number
head commit
squash merge commit
new cmake/ClawHUDTests.cmake ownership
root CMake test include shape
before/after CTest inventory comparison
full Release build + CTest result
BUILD_TESTING=OFF configure/build result
no runtime source changes
```

After R8, mark the R0-R8 refactor series as fully complete.

Do not imply R8 was required for runtime correctness; record it as the final optional build-file organization pass.

---

## 20. Expected changed files

Ideal implementation PR:

```text
M  CMakeLists.txt
A  cmake/ClawHUDTests.cmake
M  docs/APP_REFACTOR_PLAN.md
```

No other file should normally need modification.

If the implementation diff starts touching runtime `.cpp/.h`, test `.cpp`, workflow YAML, or packaging behavior, stop and reassess scope.

---

## 21. PR acceptance checklist

### Structure

- [ ] `cmake/ClawHUDTests.cmake` exists.
- [ ] Root `CMakeLists.txt` retains production targets/configuration.
- [ ] Root retains `include(CTest)`.
- [ ] Root gates the test include with `if(BUILD_TESTING)`.
- [ ] Test declarations exist only in `cmake/ClawHUDTests.cmake`.
- [ ] No generic test helper/macro introduced.
- [ ] No multi-file test-CMake hierarchy introduced.

### Test-target equivalence

- [ ] Every pre-R8 test target still exists.
- [ ] Every pre-R8 CTest name still exists.
- [ ] No duplicate target/test declaration.
- [ ] Source lists unchanged.
- [ ] Compile features unchanged.
- [ ] Compile definitions unchanged.
- [ ] Include directories unchanged.
- [ ] Link libraries unchanged.
- [ ] Target properties unchanged.

### Verification

- [ ] Clean `BUILD_TESTING=ON` configure succeeds.
- [ ] Release build succeeds.
- [ ] Pre/post `ctest -N` inventory is identical.
- [ ] Full CTest passes with unchanged count.
- [ ] Clean `BUILD_TESTING=OFF` configure succeeds.
- [ ] `ClawHUD` and `ClawHUD.EcHelper` build with tests disabled.
- [ ] GitHub Build Test succeeds on the final PR head.

### Frozen runtime

- [ ] No `src/**` changes.
- [ ] No `tests/**` changes.
- [ ] No `.github/workflows/**` changes.
- [ ] No packaging/runtime copy-rule changes.
- [ ] HUD/VRR presentation contract has zero diff.
- [ ] One shared PresentMon provider architecture untouched.

---

## 22. Completion criterion

R8 is complete when the root CMake file is production-focused, the entire explicit test-target block lives in one dedicated include, and the generated build/test graph is demonstrably identical.

The intended final state is:

```text
R0-R7  runtime architecture refactor complete
R8     build-file organization complete
```

No R9 is implied by this work order.
