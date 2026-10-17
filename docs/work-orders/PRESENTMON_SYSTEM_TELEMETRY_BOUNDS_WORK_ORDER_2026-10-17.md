# Work Order — Harden PresentMon System Telemetry Query Bounds and Memory-Byte Conversion

> **Repository:** \`onehoon/ClawHUD\`  
> **Date:** 2026-10-17  
> **Status:** Implementation work order only; no production code modified by this document  
> **Required targets:** \`main\` AND \`integration/steamaddon\`  
> **Read-only reviewed baselines:** \`main@3a2dc12bd5ef0692cc4a917aa789fddf7517c00c\`; \`integration/steamaddon@29511330d02e921d346ed0d06d1f98a334dc934a\`  
> **Dependency contract:** Bundled PresentMon v2.6.0 / API 3.4 remains unchanged  
> **Delivery:** One focused *logical* change, applied to both branches. Prefer a main implementation PR, squash-merge after review/CI, then cherry-pick that squash commit to an integration/steamaddon PR. Each PR must remain below ~500 changed LOC.  
> **Priority:** Small defensive correctness hardening, not a demonstrated normal-hardware crash or urgent production regression.

## 1. Goal and exact boundaries

Fix **only** these two findings in the production system-telemetry decoder:

1. A registered PresentMon dynamic system query returns \`PM_QUERY_ELEMENT::dataOffset\` and \`dataSize\`, but \`PresentMonSystemTelemetry::Initialize()\` sums them without checking overflow and \`Number()\` dereferences using a raw blob pointer without its length.
2. \`DecodePresentMonMemoryBytes()\` compares against \`static_cast<double>(UINT64_MAX)\` using \`>\`. That conversion rounds to \`2^64\`, so a decoded value equal to \`2^64\` passes and is converted to an out-of-range \`uint64_t\`.

Keep six existing system metrics, query planning, all normal unit conversions, polling cadence, diagnostics, fallback semantics, and every other runtime feature unchanged.

**Explicitly out of scope:** the debug-only \`WindowsGameIdentitySource\` foreground queue; API header deduplication; process/frame query behavior; Diag implementation; PresentMon version/MSI changes; game detection; HUD rendering and presentation; broad telemetry refactoring.

## 2. Source-level findings verified in BOTH target branches

The following blobs were confirmed **identical across both branches** at the reviewed baselines:

| File | Git blob SHA |
| --- | --- |
| \`src/ClawHUD/PresentMonSystemTelemetry.cpp\` | \`fbec63970d933e8209f9db65b898fa22ad362244\` |
| \`src/ClawHUD/PresentMonSystemTelemetry.h\` | \`efb68d3e7f92c654fe4fe05aee2bf909adbdb800\` |
| \`tests/PresentMonTelemetryProviderTests.cpp\` | \`908db6a608364f58a290ece60f404ff47addd654\` |

\`cmake/ClawHUDTests.cmake\` differs between branches; **do not replace one branch's CMake file with the other**. The existing \`ClawHUD.PresentMonTelemetryProviderTests\` target already compiles the affected implementation and should be reused.

### Confirmed call path

\`\`\`text
PresentMonTelemetryProvider::Initialize()
  -> PresentMonSystemTelemetry::Initialize()
     -> BuildPresentMonSystemQueryPlan(capabilities)
     -> RegisterDynamicQuery(&query_, elements_.data(), ...)
        [API fills each element's dataOffset and dataSize]
     -> end = max(end, dataOffset + dataSize)  // unchecked
     -> blob_.resize(max(end, 4096))

PresentMonTelemetryProvider::ReadSystem()
  -> PresentMonSystemTelemetry::Read()
     -> client.PollDynamicQuery(query_, pid=0, blob_.data(), ...)
     -> DecodePresentMonSystemSnapshot(status, count, blob_.data(), ...)
        -> DecodePresentMonPercentage / FrequencyMHz / PowerWatts / MemoryBytes
           -> Number(blob, element, type)
              -> memcpy(blob + dataOffset, ...) // no known buffer extent
\`\`\`

\`PM_QUERY_ELEMENT::dataOffset\` and \`dataSize\` are \`uint64_t\` in \`PresentMonApi2Api.h\`. The current process telemetry decoder already uses a \`std::span\` and subtractive size checks in \`PresentMonProcessTelemetry.cpp\`; follow that local pattern.

The production client \`PollDynamicQuery(..., uint8_t* pBlob, ...)\` does **not** take a buffer length. Therefore checking only at decode time is insufficient: query layout must be checked **before the first poll** so invalid metadata cannot cause the API to write into a too-small allocation.

## 3. Required implementation

### 3.1 Validate the complete registered system-query layout before polling

In \`PresentMonSystemTelemetry::Initialize()\`, after successful \`RegisterDynamicQuery\` and before \`blob_.resize\`:

- For every returned element, reject \`dataOffset > UINT64_MAX - dataSize\` **before** addition.
- Determine the maximum end offset using the checked sum; preserve the existing minimum allocation of **4096 bytes** for successful, normal registrations.
- Verify the resulting allocation length fits \`size_t\` and \`blob_.max_size()\`. Avoid attempting unreasonable allocations for this fixed six-scalar query (a documented conservative ceiling, e.g. 64 KiB, is acceptable).
- Reject a malformed registration as **system telemetry unavailable**, log a concrete \`[PresentMonSystem]\` warning, call \`Shutdown(client)\` to free an already-registered query, and return \`false\`. **Do not poll** that query.
- Do not let system-query invalidity tear down otherwise working PresentMon process FPS or frame telemetry; the provider already has independently ready subsystems.
- Handle a success status with null \`query_\` as unsuccessful registration if encountered while editing this narrow path.
- Retain the existing 250 ms telemetry polling period, 1000 ms query window and 1064 ms metric offset.

**Illustrative core validation** (integrate cleanly with current error handling and logging):

\`\`\`cpp
constexpr std::uint64_t kMaxSystemQueryBlobBytes = 64 * 1024;
std::uint64_t end = 0;
for (const auto& element : elements_)
{
    if (element.dataOffset >
        std::numeric_limits<std::uint64_t>::max() - element.dataSize)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"[PresentMonSystem] initialized ready=0 stage=query-layout reason=overflow");
        Shutdown(client);  // frees successful registration and clears buffers
        return false;
    }

    end = std::max(end, element.dataOffset + element.dataSize);
}

const std::uint64_t required = std::max<std::uint64_t>(end, 4096);
if (required > kMaxSystemQueryBlobBytes || required > blob_.max_size())
{
    RuntimeLogger::Log(RuntimeLogLevel::Warn,
        L"[PresentMonSystem] initialized ready=0 stage=query-layout reason=size");
    Shutdown(client);
    return false;
}
blob_.resize(static_cast<std::size_t>(required));
\`\`\`

**Note:** The example ceiling is a conservative *registration-integrity limit*, not a new timeout, polling policy, or cap on legitimate telemetry values. If implementation selects a different ceiling, justify it using the fixed set of six scalar metrics. A nonzero \`dataSize\` per required decoded element is sensible but do not accidentally reject a valid optional metric merely because its statistic is unavailable; keep metadata selection as today. The registration-wide layout must be safe for **all** elements returned by the API, even if individual values are unavailable.

### 3.2 Carry the real blob extent into every system decoder

Prefer updating the existing system-only decoder APIs in \`PresentMonSystemTelemetry.h/.cpp\` from \`const std::uint8_t* blob\` to \`std::span<const std::uint8_t> blob\`, including:

- \`DecodePresentMonPercentage\`
- \`DecodePresentMonFrequencyMHz\`
- \`DecodePresentMonPowerWatts\`
- \`DecodePresentMonMemoryBytes\`
- \`DecodePresentMonSystemSnapshot\`
- their private \`Number\` helper

Add \`<span>\` where necessary. In \`Read()\`, pass a view of the **actual** \`blob_\` allocation, rather than \`blob_.data()\` alone. Update existing test call sites to use arrays/vectors as spans.

Before \`memcpy\`, check that:

1. The element type is supported.
2. \`dataSize\` contains at least the number of bytes needed by that type (\`double\`/\`uint64_t\` = 8; \`uint32_t\`/\`int32_t\` = 4).
3. \`dataOffset <= blob.size()\`.
4. \`dataSize <= blob.size() - dataOffset\`, using subtraction to avoid overflow.

Only convert \`dataOffset\` to \`size_t\` after the bounds check, and only then compute \`blob.data() + offset\`. For an invalid span or element, return \`std::nullopt\` for that **individual metric**, retaining other valid fields within a successful snapshot. Do not widen this patch into process/frame decoders.

**Illustrative core check:**

\`\`\`cpp
std::optional<double> Number(std::span<const std::uint8_t> blob,
    const PM_QUERY_ELEMENT& e, PM_DATA_TYPE type)
{
    if (!SupportedType(type)) return std::nullopt;
    const std::size_t width =
        (type == PM_DATA_TYPE_DOUBLE || type == PM_DATA_TYPE_UINT64)
            ? sizeof(std::uint64_t) : sizeof(std::uint32_t);

    if (e.dataSize < width || e.dataOffset > blob.size())
        return std::nullopt;
    const auto offset = static_cast<std::size_t>(e.dataOffset);
    if (e.dataSize > blob.size() - offset)
        return std::nullopt;

    // Keep the existing per-type memcpy / conversion branches,
    // but read from blob.data() + offset only after validation.
    // ...
}
\`\`\`

Do not retain an unsafe public pointer-only decoder overload that can be called without a size; the test-facing system decoder signatures should provide their actual extent.

### 3.3 Fix the exclusive upper bound for uint64_t conversion

\`static_cast<double>(std::numeric_limits<std::uint64_t>::max())\` is **not** a safe inclusive upper bound: it rounds upward to exactly \`2^64\` in binary64.

Retain the finite and nonnegative checks, but reject values \`>= 2^64\` before any floating-to-\`uint64_t\` cast:

\`\`\`cpp
constexpr double kUint64ExclusiveUpperBound = 0x1.0p64; // exactly 2^64
const double result = *value * UnitScale(unit);
if (!std::isfinite(result) || result >= kUint64ExclusiveUpperBound)
    return std::nullopt;
return static_cast<std::uint64_t>(result);
\`\`\`

Do **not** change \`UnitScale()\` semantics or replace it with a new generic numeric-conversion framework. Valid common VRAM readings must decode exactly as before; \`uint64_t\` values near the theoretical maximum may legitimately be rounded by the existing \`double\` conversion and must not be cast when rounded to the exclusive bound.

## 4. Mandatory targeted tests

Extend **existing** \`tests/PresentMonTelemetryProviderTests.cpp\`, especially \`CheckSystemTelemetry()\`. A minimal fake client with controlled \`RegisterDynamicQuery\` offsets may be added there only if needed to test cleanup. Do not add a new test target or change \`cmake/ClawHUDTests.cmake\` unless strictly required.

1. **Normal layout:** six unchanged metric slots, normal registered offsets/sizes, \`SystemReady()\` true and ordinary CPU/GPU values still decoded.
2. **Offset arithmetic overflow:** a synthetic returned element with \`dataOffset = UINT64_MAX - 3\` and \`dataSize = 8\` is rejected before allocation/poll; registered query is freed once; system telemetry does not become ready.
3. **Excessive but non-overflowing layout:** a synthetic offset/size yielding an implausible query blob is rejected without attempting allocation or polling, with the registered handle freed.
4. **Decoder span overrun:** an 8-byte value starting at offset 7 in an 8-byte blob returns unavailable, rather than reading outside the blob.
5. **Decoder oversized declared span:** \`dataOffset\` inside the blob but \`dataSize\` extending beyond it returns unavailable.
6. **Decoder offset near UINT64_MAX:** returns unavailable without overflow, pointer wrap, or exception.
7. **Mixed snapshot:** one malformed binding does not suppress valid values from other bindings.
8. **uint64 conversion boundary:** \`2^64\`, \`+infinity\`, and overflow after unit scaling return unavailable; a representable value immediately below \`2^64\` (e.g. \`std::nextafter(0x1.0p64, 0.0)\`) remains accepted; normal byte/kilobyte/megabyte/gigabyte values remain unchanged.
9. **Existing guards:** empty \`resultCount\` and failed poll still yield no snapshot; system-query failure does not affect independent provider readiness.
10. **Clean shutdown:** no leaked dynamic-query registration after rejection; repeating \`Shutdown\` is safe.

**Regression notes:** The existing tests currently pass \`blob.data()\` into system decode helpers. These callers need mechanical conversion to a span/array/vector view. Process telemetry decoder tests are out of scope.

## 5. Excluded changes and non-negotiable safety contract

Absolutely no changes to:

- \`windowExStyle\`, \`WS_EX_TRANSPARENT\`, \`WS_EX_NOACTIVATE\`, \`WS_EX_TOPMOST\`, \`WS_EX_LAYERED\`;
- \`WM_NCHITTEST -> HTTRANSPARENT\`, \`WM_MOUSEACTIVATE -> MA_NOACTIVATE\`;
- \`ProductionHudPresentationContract()\`, independent flip, Presentation API/DirectComposition production path, premultiplied alpha;
- background-opacity semantics or HUD text/foreground opacity;
- foreground/game-session decisions, production process FPS polling, frame-render verification;
- PresentMon API public enum/header/version/MSI/runtime bootstrap;
- \`ClawHUD.Diag\` and its independent copied API header;
- the debug-only \`WindowsGameIdentitySource\` queue.

Existing click-through/no-activation/topmost/hit-test/independent-flip/premultiplied-alpha/presentation-contract tests must remain intact.

## 6. Execution sequence, branch parity and acceptance

1. Implement the focused patch against current \`main\`, touching only:
   - \`src/ClawHUD/PresentMonSystemTelemetry.h\`
   - \`src/ClawHUD/PresentMonSystemTelemetry.cpp\`
   - \`tests/PresentMonTelemetryProviderTests.cpp\`
2. Run an MSVC/Windows build and the \`ClawHUD.PresentMonTelemetryProviderTests\` CTest target. Run the existing presentation-contract and other relevant test targets; verify the full suite when CI permits.
3. Open **main PR** (<500 changed LOC). Verify cleanup and malformed-metadata tests, as well as unchanged production behavior. Resolve actual blocking review findings, then squash-merge after required CI succeeds.
4. Cherry-pick the **resulting implementation squash commit** onto \`integration/steamaddon\`, resolving only actual integration differences. Open an **integration PR** (<500 changed LOC), run its own build/tests/CI, and squash-merge after review. Never merge the full \`main\` branch into the integration branch just for this fix.
5. Verify both branch heads contain equivalent production code and tests. At reviewed baselines, the three target source files are blob-identical; however, other build files differ, so compare the *final result*, not branch-level commit counts.
6. The instruction document itself is mirrored to both branches for local Codex visibility; its commits are **documentation-only** and do not mean the fix has been implemented.

**Acceptance:** Both branches reject unsafe system-query layouts before polling, read only inside the actual system blob, reject \`2^64\` before conversion to \`uint64_t\`, retain valid sensor telemetry, free rejected query registrations, pass tests, and preserve the complete production HUD/VRR safety contract. No extra cleanup/refactor work is part of this request.
