# PresentMon API2 기반 VRR Diagnostic 재설계 검토

> Status: **DESIGN / RESEARCH RECORD — implementation threshold validation still required**  
> Date: 2026-08-31  
> Target: `ClawHUD.Diag.exe` standalone console diagnostic  
> Repository: `onehoon/ClawHUD`

## 1. 문서 목적

이 문서는 기존 ClawHUD 앱 내부의 legacy **VRR / Presentation Test**를 제거하고 archive에 보관한 이후, **PresentMon API2를 기준으로 VRR 관련 진단을 새로 설계하기 위한 기술 검토 결과**를 기록한다.

목표는 기존 `PresentMon.exe -> CSV -> HUD OFF / DYNAMIC phase 비교` 방식을 단순히 API2로 포팅하는 것이 아니다.

PresentMon API2에서는 기존 CLI CSV보다 훨씬 많은 per-frame / display-facing metric을 직접 조회할 수 있으므로, 새 진단은 다음 세 가지를 명확히 분리해야 한다.

1. **Presentation Path**  
   ClawHUD가 게임의 Independent Flip / tearing-capable presentation path를 유지하는가.
2. **Frame / Display Pacing**  
   ClawHUD가 실제 displayed-frame cadence, dropped-frame behavior, display latency 등에 유의미한 회귀를 만드는가.
3. **Physical Cadence Evidence**  
   D3DKMT와 같은 별도 보조 신호에서 HUD OFF 대비 HUD ON 시 variable-like cadence가 fixed-like cadence로 바뀌는가.

가장 중요한 설계 원칙은 다음이다.

> **PresentMon API2만으로 physical panel의 VRR active 상태를 직접 증명한다고 주장하지 않는다.**

새 Diag의 주 목적은 **ClawHUD가 기존 VRR-safe presentation behavior를 훼손하는지 검증하는 것**이다.

이 문서는 work order가 아니다. 구현 전에 raw-frame controlled capture를 추가로 수집해 pacing threshold를 확정하는 단계가 필요하다.

---

## 2. 관련 현재 구조와 archive 상태

### 2.1 Production 앱

현재 production `ClawHUD.exe`에는 Diagnostics 탭이나 developer diagnostic UI가 없다.

PresentMon 관련 production path는 다음과 같이 API2 기반으로 정리되어 있다.

- `PresentMonApi2Client`
- `PresentMonTelemetryProvider`
- `PresentMonProcessTelemetry`
- `PresentMonSystemTelemetry`
- `PresentMonFrameTelemetry`
- `PresentMonDebugFrameTelemetry`
- `PresentMonRuntimeBootstrap`

Production telemetry / game detection / debug observation은 shared PresentMon API2 runtime을 사용하며, developer diagnostic은 production `App` lifecycle에 다시 결합하지 않는 방향이 현재 구조적 결정이다.

참조:

- `docs/DIAGNOSTICS.md`
- `docs/APP_REFACTOR_PLAN.md`

### 2.2 기존 legacy VRR diagnostic

기존 앱 내부 VRR diagnostic은 다음 위치에 reference-only 상태로 보관되어 있다.

```text
archive/diagnostics/legacy-vrr-presentmon/
```

주요 파일:

```text
VrrDiagnostic.*
VrrDiagnosticAnalysis.*
D3dkmtVblankProbe.*
IntelVrrDiagnosticProbe.*
vrrpoc/main.cpp
```

기존 구조의 핵심은:

```text
PresentMon.exe
  -> CSV capture
  -> dominant swapchain 선택
  -> PresentMode / MsUntilDisplayed / MsBetweenPresents /
     MsBetweenDisplayChange 분석
  -> HUD OFF vs DYNAMIC 비교
```

이었다.

기존 verdict에서 가장 중요한 자동 판정은 **Independent Flip retention**이었다.

기존 상수:

```cpp
kMinimumVrrComparisonSamples = 20
kBaselineIndependentFlipMinimumPercent = 80.0
kFailureIndependentFlipMaximumPercent = 50.0
kFailureIndependentFlipDropPoints = 30.0
```

즉 baseline에서 Independent Flip이 충분히 성립하지 않으면 `INCONCLUSIVE`, HUD phase에서 Independent Flip 비율이 크게 붕괴하면 `FAIL`로 처리했다.

이 개념은 새 API2 diagnostic에서도 유지할 가치가 있지만, 의미를 정확히 다음과 같이 제한해야 한다.

```text
Independent Flip retained
    != physical VRR proven active

Independent Flip retained
    == VRR-capable / VRR-safe presentation path evidence retained
```

### 2.3 기존 API2 diagnostic archive

PresentMon API2 자체를 검증했던 과거 developer diagnostic은 다음 위치에 보관되어 있다.

```text
archive/diagnostics/presentmon-api2/
```

기존 기능:

- API2 runtime / session validation
- API version query
- introspection dump
- static/dynamic metric capability survey
- PID tracking
- dynamic telemetry query
- `pmRegisterFrameQuery`
- `pmConsumeFrames`
- frame CSV / log output

해당 기능이 archive된 이유는 API2가 실패해서가 아니라, **API2 검증이 완료되었고 developer diagnostics를 production App / Settings / HUD lifecycle에서 분리하기 위해서**다.

따라서 새 VRR diagnostic은 이 archive를 그대로 복원하는 것이 아니라, **standalone `ClawHUD.Diag.exe`의 독립 command/module로 필요한 API2 code만 재사용 또는 재구성**하는 것이 적절하다.

---

## 3. PresentMon API2 실측에서 확인된 현재 capability

Google Drive의 2026-08-30 API2 probe 자료를 기준으로 다음이 실측 확인되어 있다.

주요 근거:

```text
INTEL_GRAPHICS_SOFTWARE_PRESENTMON_API2_RE_REPORT.md
api2-20260830-200431.log
api2-20260830-200431-introspection.json
api2-20260830-200431-metrics.csv
api2-20260830-200431-frames.csv
```

실측 runtime:

```text
PresentMon API version: 3.3.0
pmOpenSession: SUCCESS
pmGetIntrospectionRoot: SUCCESS
Devices: 3
Metrics: 92
Enums: 13
Frame query elements registered: 47
```

장치:

```text
Device 0      : Device-independent
Device 1      : Intel(R) Arc(TM) B390 GPU
Device 65536  : System
```

이 중 VRR / presentation analysis와 직접 관련 있는 device-independent metric은 충분히 제공되고 있다.

---

## 4. 새 VRR diagnostic에서 중요한 API2 metric

아래는 현재 introspection 실측 metric 중 VRR diagnostic에서 의미가 큰 항목을 분류한 것이다.

### 4.1 Identity / segmentation — 필수

| Metric | ID | 역할 |
|---|---:|---|
| `PROCESS_ID` | 89 | frame ownership 검증 |
| `SWAP_CHAIN_ADDRESS` | 1 | dominant game swapchain 구분 |
| `SESSION_START_QPC` | 90 | session time anchor |
| `PRESENT_START_QPC` | 77 | frame ordering / phase segmentation |
| `PRESENT_START_TIME` | 76 | human-readable / relative timing support |

이 그룹은 verdict 자체의 VRR signal이라기보다 **분석 대상이 동일한 게임 / 동일한 swapchain / 올바른 phase인지 증명하는 integrity data**다.

Numeric PID만으로 target identity를 유지해서는 안 된다. Windows는 PID를 재사용할 수 있으므로, standalone Diag의 현재 process identity 모델처럼 PID + process creation time을 함께 보존하는 것이 안전하다.

### 4.2 Presentation path — 최우선

| Metric | ID | 역할 |
|---|---:|---|
| `PRESENT_MODE` | 20 | Independent Flip / composed path 구분 |
| `ALLOWS_TEARING` | 22 | tearing-capable presentation state |
| `SYNC_INTERVAL` | 18 | VSync / Present synchronization behavior 변화 탐지 |
| `PRESENT_FLAGS` | 19 | Present policy/context 변화 보조 |
| `PRESENT_RUNTIME` | 21 | runtime context 보조 |

이 중 자동 path verdict의 중심은 `PRESENT_MODE`다.

`ALLOWS_TEARING`은 중요하지만 단독 verdict source가 아니다.

### 4.3 App-side frame pacing

| Metric | ID | 역할 |
|---|---:|---|
| `CPU_FRAME_TIME` | 8 | application-side frame interval context |
| `BETWEEN_PRESENTS` | 78 | Present call 간격 |
| `PRESENTED_FRAME_TIME` | 87 | presented-frame pacing |
| `PRESENTED_FPS` | 12 | summary/reference |
| `APPLICATION_FPS` | 62 | app-facing summary/reference |

이 그룹은 게임이 실제로 어떤 속도로 frame을 제출하고 있는지 보여준다.

그러나 **app present cadence를 physical refresh cadence로 해석하면 안 된다.**

### 4.4 Display-facing frame pacing — API2 전환의 핵심

| Metric | ID | 역할 |
|---|---:|---|
| `DISPLAYED_TIME` | 17 | frame display duration / display-facing timing |
| `BETWEEN_DISPLAY_CHANGE` | 80 | 실제 displayed content change 사이 간격 |
| `DISPLAYED_FRAME_TIME` | 85 | displayed-frame pacing |
| `DISPLAYED_FPS` | 11 | display-facing summary/reference |
| `UNTIL_DISPLAYED` | 81 | Present -> displayed 지연 |
| `DISPLAY_LATENCY` | 24 | display path latency |
| `RENDER_PRESENT_LATENCY` | 82 | render -> present latency context |
| `FLIP_DELAY` | 88 | flip scheduling delay context |

이 그룹이 기존 PresentMon CLI CSV 방식보다 새 API2 diagnostic을 크게 강화하는 부분이다.

특히 초기 분석의 중심은 다음 두 metric을 권장한다.

```text
DISPLAYED_FRAME_TIME
BETWEEN_DISPLAY_CHANGE
```

다만 이 둘 역시 **physical scanout period 자체가 아니다.**

예를 들어 게임이 30 FPS이면 displayed content가 약 33.33 ms마다 바뀔 수 있지만, 실제 panel scanout은 VRR/LFC 정책에 따라 훨씬 높은 frequency로 동작할 수 있다.

### 4.5 Drop / display integrity

| Metric | ID | 역할 |
|---|---:|---|
| `DROPPED_FRAMES` | 16 | HUD ON 시 display loss / delivery regression 탐지 |
| `UNTIL_DISPLAYED` | 81 | display delivery delay |
| `DISPLAY_LATENCY` | 24 | latency regression 보조 |

`DROPPED_FRAMES`는 HUD OFF -> HUD ON 비교에서 특별히 가치가 높다.

단순 average FPS가 동일해도 dropped frame 증가 또는 latency tail 증가가 있으면 회귀일 수 있다.

### 4.6 Frame Generation / frame classification

| Metric | ID | 역할 |
|---|---:|---|
| `FRAME_TYPE` | 63 | application/generated frame type 분리 |
| `ANIMATION_ERROR` | 64 | generated/pacing context 보조 |
| `ANIMATION_TIME` | 67 | generated animation timing context |

XeFG를 포함한 frame-generation 환경에서는 `FRAME_TYPE`을 반드시 raw output에 보존해야 한다.

PresentMon이 모든 Intel UMD-generated output frame을 완전히 관측한다고 가정하면 안 된다.

따라서 FG active/suspected session에서:

- `PRESENT_MODE` 비교는 계속 유효한 presentation-path evidence로 사용할 수 있다.
- PresentMon의 displayed/presented FPS를 physical final output FPS라고 단정하지 않는다.
- generated frame visibility가 제한될 수 있음을 summary에 명시한다.

### 4.7 VRR verdict에서 제외할 telemetry

다음 API2 telemetry는 capture context로 저장할 수는 있지만 VRR verdict에 직접 넣지 않는 것이 좋다.

```text
GPU_POWER
GPU_FREQUENCY
GPU_UTILIZATION
GPU_RENDER_COMPUTE_UTILIZATION
GPU_MEDIA_UTILIZATION
GPU_POWER_LIMITED
GPU_TEMPERATURE_LIMITED
GPU_VOLTAGE_LIMITED
GPU_UTILIZATION_LIMITED
GPU_MEM_USED
GPU_MEM_UTILIZATION
CPU_UTILIZATION
CPU_FREQUENCY
```

이 값은 test reproducibility / load context에는 유용하지만, VRR-safe presentation verdict의 직접 조건이 아니다.

---

## 5. `pmConsumeFrames()` batch behavior에 대한 중요한 주의

API2 실측 로그에서 frame consume은 대략 다음과 같은 형태로 관찰됐다.

```text
0
0
0
...
64
11
0
...
64
32
...
64
37
```

즉 `pmConsumeFrames()`를 약 100 ms 간격으로 호출해도 frame이 호출마다 균등하게 반환되는 것이 아니라 shared frame storage / flushing behavior에 따라 batch로 전달될 수 있다.

따라서 다음은 금지한다.

```text
X: pmConsumeFrames() 호출 간격 = 게임 frame cadence
X: consume batch arrival interval = display refresh cadence
X: batch size 변화 = VRR frequency 변화
```

분석은 반드시 **각 frame 안에 들어 있는 timestamp/QPC/timing metric**으로 수행해야 한다.

권장:

```text
pmConsumeFrames()
  -> returned frames decode
  -> sort/validate by PRESENT_START_QPC
  -> assign to phase
  -> group by swapchain
  -> analyze per-frame timing fields
```

---

## 6. 가장 중요한 해석 경계 — PresentMon만으로 VRR Active를 증명하지 않는다

이 프로젝트에는 이미 매우 중요한 hardware control result가 있다.

참조:

```text
docs/VRR_HARDWARE_VALIDATION_RESULTS.md
```

Death Stranding Director's Cut, 약 30 FPS, Frame Generation OFF, VSync OFF 조건에서 다음이 확인됐다.

### VRR ON

```text
PresentMon:
  Independent Flip = 100%
  AllowsTearing = 1
  MsBetweenDisplayChange ~= 33.33 ms

Manual Special K:
  Variable Rate below 120 Hz
  LFC approximately x3/x4

D3DKMT:
  variable sub-120 cadence
```

### VRR OFF / fixed 120 Hz control

```text
PresentMon:
  Independent Flip = 100%
  AllowsTearing = 1
  MsBetweenDisplayChange ~= 33.33 ms

Manual Special K:
  Constant Rate 120 Hz

D3DKMT:
  stable ~119.9 Hz
```

즉 두 조건 모두 PresentMon presentation data는 매우 비슷했지만 physical display behavior는 명확히 달랐다.

이 결과가 의미하는 것은 다음과 같다.

### 6.1 Independent Flip

```text
Independent Flip
  = VRR을 허용할 수 있는 중요한 presentation-path evidence
  != physical panel VRR active proof
```

### 6.2 AllowsTearing

```text
AllowsTearing = 1
  = tearing-capable / compatible present configuration evidence
  != VRR active flag
```

### 6.3 BetweenDisplayChange / DisplayedFrameTime

30 FPS 게임이 33.33 ms마다 새로운 content를 display에 넘기는 것은:

```text
content update cadence
```

에 가깝다.

그것만으로 panel이 30 Hz, 90 Hz, 120 Hz, LFC x4 중 무엇으로 scanout 중인지 증명할 수 없다.

### 6.4 새 diagnostic 표현 규칙

따라서 새 Diag는 다음 문구를 사용해야 한다.

권장:

```text
HUD preserved the VRR-safe presentation path.
```

금지:

```text
VRR is active because PresentMode is Independent Flip.
VRR is active because AllowsTearing = 1.
VRR frequency = 1 / DisplayedFrameTime.
```

---

## 7. 새 진단의 전체 판정 모델

새 VRR diagnostic은 한 개의 bool verdict로 모든 의미를 압축하지 않는다.

권장 3축 구조:

```text
1. Presentation Path Verdict       [authoritative for HUD path regression]
2. Frame / Display Pacing Verdict  [API2 comparative evidence]
3. Physical Cadence Evidence       [D3DKMT supporting evidence]
```

그리고 마지막에 이들을 요약한 overall result를 제공한다.

---

# 8. Axis A — Presentation Path Verdict

이 축은 새 diagnostic의 가장 강한 자동 verdict다.

## 8.1 분석 대상 swapchain 선택

기존 legacy diagnostic은 row count가 가장 많은 swapchain을 dominant swapchain으로 선택했다.

API2에서는 가능하면 단순 row count보다 다음 순서를 권장한다.

1. target PID와 process generation이 일치하는 frame만 사용
2. valid `PRESENT_MODE`가 있는 frame만 고려
3. 가능하면 실제 displayed frame count / display-facing sample이 충분한 swapchain 우선
4. phase 전체에서 가장 지속적으로 존재하는 swapchain 선택
5. HUD OFF와 HUD ON의 swapchain identity 연속성을 기록

최소한 summary에는 다음을 반드시 남긴다.

```text
baseline dominant swapchain
HUD ON dominant swapchain
sample count
present-mode sample count
displayed sample count
swapchain changed/recreated 여부
```

## 8.2 Independent Flip family

기존 helper는 문자열에 `Independent Flip`이 포함되면 Independent Flip family로 분류했다.

이 개념은 유지할 수 있다.

예:

```text
Hardware: Independent Flip
Hardware Composed: Independent Flip
```

둘 다 Independent Flip family로 계산한다.

## 8.3 기존 threshold 재사용 가능 영역

기존 threshold는 presentation-path regression detection에 여전히 합리적인 초기값이다.

```text
Minimum comparable PresentMode samples: 20
Baseline Independent Flip minimum: 80%
Failure absolute Independent Flip maximum: < 50%
Failure relative drop: >= 30 percentage points
```

예:

```text
BASELINE: 100% Independent Flip
HUD ON:    22% Independent Flip
=> PATH FAIL
```

또는:

```text
BASELINE: 42% Independent Flip
HUD ON:   40% Independent Flip
=> INCONCLUSIVE
```

baseline 자체가 VRR-safe path를 확립하지 못했기 때문에 ClawHUD 영향이라고 판정할 수 없다.

## 8.4 추가 path consistency checks

`PRESENT_MODE` 외에 다음을 summary evidence로 함께 기록한다.

```text
AllowsTearing distribution
SyncInterval distribution
PresentFlags distribution
PresentRuntime distribution
DroppedFrames
```

단, `AllowsTearing`만 바뀌었다고 즉시 FAIL을 내릴지는 controlled capture 이후 별도 확정하는 것이 좋다.

## 8.5 Path verdict 제안

```text
PASS
FAIL
INCONCLUSIVE
```

의미:

### PASS

- baseline이 충분한 Independent Flip path를 확립함
- HUD ON에서도 dominant Independent Flip path 유지
- major path regression 없음

### FAIL

- HUD ON에서 Independent Flip이 명확하게 붕괴
- 또는 안정된 baseline 대비 일반 composed path로 큰 전환 발생

### INCONCLUSIVE

- baseline 자체가 Independent Flip을 확립하지 못함
- frame count 부족
- target / swapchain continuity 불확실
- game swapchain recreation 때문에 fair A/B comparison이 어려움

---

# 9. Axis B — Frame / Display Pacing Verdict

API2 전환에서 가장 새롭게 강화되는 영역이다.

## 9.1 phase별 기본 distribution

다음 metric은 average만 저장하지 말고 distribution을 계산한다.

```text
DISPLAYED_FRAME_TIME
PRESENTED_FRAME_TIME
BETWEEN_DISPLAY_CHANGE
BETWEEN_PRESENTS
UNTIL_DISPLAYED
DISPLAY_LATENCY
RENDER_PRESENT_LATENCY
FLIP_DELAY
```

권장 summary:

```text
sample count
min
P50 / median
P90
P95
P99
max
mean
standard deviation 또는 MAD(선택)
```

초기 버전에서는 P50 / P95 / P99만으로도 충분하다.

## 9.2 왜 tail을 봐야 하는가

평균 FPS가 같아도 HUD ON에서 다음과 같은 회귀가 발생할 수 있다.

```text
median ~33 ms -> unchanged
P95 35 ms -> 55 ms
P99 40 ms -> 90 ms
DroppedFrames 0 -> multiple
```

이 경우 average FPS만 보면 정상처럼 보이지만 display delivery는 명확히 나빠진 것이다.

따라서 pacing diagnostic은 `average FPS` 중심이 아니라 **frame-time distribution + dropped-frame comparison** 중심이어야 한다.

## 9.3 초기 verdict 이름

초기 구현에서 pacing은 다음처럼 독립 classification을 권장한다.

```text
STABLE
REGRESSED
UNSTABLE
INCONCLUSIVE
```

그러나 **현재 문서 단계에서 정확한 numeric regression threshold를 고정하지 않는다.**

이유는 현재 실측 control matrix가 Independent Flip / D3DKMT validation에는 충분하지만, API2의 `DISPLAYED_FRAME_TIME`, `UNTIL_DISPLAYED`, `FLIP_DELAY`, `DISPLAY_LATENCY` distribution에 대해 VRR ON/OFF 및 HUD OFF/ON control sample이 아직 충분히 누적되지 않았기 때문이다.

## 9.4 구현 전에 필요한 threshold calibration

최소 controlled capture matrix:

```text
VRR ON / VRR OFF
30 FPS
60 FPS
90 FPS or uncapped where practical
FG OFF
XeFG 2x/3x where supported
HUD OFF
Production HUD ON
```

가능하면 같은 게임 / 같은 scene / 같은 power profile에서 반복한다.

수집 후 다음을 검토한다.

```text
DisplayedFrameTime P50/P95/P99 delta
BetweenDisplayChange P50/P95/P99 delta
UntilDisplayed P95/P99 delta
DisplayLatency P95/P99 delta
DroppedFrames rate delta
```

그 후 threshold를 code constant로 승격한다.

---

# 10. Axis C — D3DKMT Physical Cadence Evidence

D3DKMT는 API2 metric이 아니다.

그러나 기존 hardware validation에서 VRR ON/OFF control을 구분하는 매우 유용한 보조 evidence를 제공했다.

기존 archive:

```text
archive/diagnostics/legacy-vrr-presentmon/D3dkmtVblankProbe.*
```

## 10.1 기존 실기 결과

ClawHUD-only / VRR ON 30 FPS no-FG 조건에서 1-second cadence windows는 대략 variable한 sub-120 값을 보였다.

예:

```text
~97, 98, 99, 100, 102, 104, 106, 108 Hz
```

다른 phase/run에서는:

```text
~101-116 Hz
```

반면 VRR OFF / fixed 120 Hz control에서는:

```text
~119.9 Hz average
complete 1-second windows 대부분 119-120 events/s
```

이 방향은 manual Special K observation과 일치했다.

## 10.2 중요 한계

`D3DKMTWaitForVerticalBlankEvent` event cadence를 공식 physical panel scanout API라고 선언할 근거는 없다.

따라서 automatic output에는 다음과 같이 표시한다.

```text
Physical Cadence Evidence
```

또는:

```text
D3DKMT Cadence Evidence
```

권장하지 않는 표현:

```text
Measured physical refresh rate
Actual VRR frequency
```

## 10.3 비교 방식

absolute Hz 하나보다 **HUD OFF -> HUD ON 변화**를 보는 것이 중요하다.

예:

```text
BASELINE
  variable-like
  1s windows: 98-110 Hz

HUD ON
  variable-like
  1s windows: 101-115 Hz

=> PRESERVED
```

반면:

```text
BASELINE
  variable-like
  98-110 Hz

HUD ON
  fixed-like
  119-120 Hz nearly every complete window

=> STRONG REGRESSION EVIDENCE
```

이 두 번째 패턴은 PresentMon이 계속 100% Independent Flip을 보고하더라도 매우 중요한 supporting evidence다.

실제로 과거 Center M diagnostic-coexistence experiment에서:

- PresentMon: 100% Independent Flip
- AllowsTearing: 1
- Special K: fixed 120 Hz
- D3DKMT: ~119.9 Hz stable

이 동시에 관찰되었다.

즉 D3DKMT는 PresentMon presentation path와 다른 층의 evidence를 제공한다.

---

## 11. Production HUD 자체를 시험해야 한다

기존 synthetic diagnostic HUD에서 중요한 실패 경험이 있다.

과거 DYNAMIC diagnostic은 mock telemetry를 **100 ms**마다 갱신하고 HUD를 render했다.

실기에서:

```text
Normal production HUD -> Variable Rate 유지
HUD OFF               -> Variable Rate 유지
Old DYNAMIC @ 100 ms  -> fixed 120 Hz
```

가 관찰되었다.

즉 diagnostic 자체가 production과 다른 workload를 만들면서 VRR behavior를 바꾸는 artifact를 유발했다.

후에 500 ms DYNAMIC으로 완화했을 때 이 artifact가 사라졌다.

새 standalone diagnostic은 이 경험을 기준으로 **자체 synthetic HUD renderer를 만들지 않는 것**이 가장 안전하다.

권장:

```text
ClawHUD.Diag.exe
  = observer / capture / analyzer only

Actual ClawHUD.exe
  = production HUD under test
```

즉 새 Diag가 production HUD presentation contract를 흉내 내거나 대체하지 않는다.

---

## 12. 권장 A/B 테스트 흐름

가장 단순하고 강한 기본 sequence:

```text
PREPARE
  -> game running
  -> target process identified
  -> player returns to game
  -> hotkey/F8 arms diagnostic
  -> short settle

PHASE A — BASELINE
  -> actual ClawHUD HUD OFF
  -> capture N seconds

PHASE B — HUD ON
  -> actual production ClawHUD HUD ON
  -> capture N seconds

ANALYZE
  -> same process generation?
  -> same/reasonably continuous dominant swapchain?
  -> presentation path comparison
  -> pacing distribution comparison
  -> D3DKMT cadence comparison
  -> report
```

### 12.1 권장 capture 길이

초기 권장:

```text
settle: 3-5 seconds
phase capture: 10-15 seconds each
```

너무 짧으면 P95/P99와 1-second D3DKMT window에 충분한 sample이 없다.

너무 길면 scene change / user interaction / game load 변화가 A/B 비교를 오염시킬 수 있다.

최종 시간은 hardware capture로 조정한다.

### 12.2 phase 전환 방식

가능하면 Diag가 직접 production presentation internals를 조작하지 않는다.

허용 가능한 방향:

- 이미 존재하는 안전한 public/runtime HUD enable mechanism을 명확하게 호출
- 또는 사용자가 phase instruction에 따라 HUD를 OFF/ON

금지:

- alternate diagnostic window 생성
- test-only presentation contract 생성
- `HudPresentation` VRR-critical internals 수정
- window style / independent flip requirement / alpha contract 우회

---

## 13. HUD Presentation / VRR Safety Contract는 진단 때문에 수정하지 않는다

새 diagnostic 구현은 production HUD contract 변경의 근거가 되어서는 안 된다.

다음은 그대로 보존한다.

```text
windowExStyle
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
existing WS_EX_LAYERED behavior
WM_NCHITTEST -> HTTRANSPARENT
WM_MOUSEACTIVATE -> MA_NOACTIVATE
ProductionHudPresentationContract()
independent-flip requirement
Presentation API / DirectComposition production path
premultiplied-alpha presentation contract
```

Diagnostic은 이 contract의 외부 관찰자여야 한다.

관련 문서:

- `docs/HUD_PRESENTATION_VRR_DECISION_HISTORY.md`
- `docs/HUD_PRESENTATION_REFACTOR_GUARDRAIL.md`
- `docs/VRR_HARDWARE_VALIDATION_RESULTS.md`

---

## 14. Swapchain continuity / process integrity 규칙

A/B verdict가 신뢰 가능하려면 두 phase가 같은 실제 renderer를 비교해야 한다.

### 14.1 process generation

권장 key:

```text
PID + process creation FILETIME
```

현재 standalone diagnostic의 game-detection evidence도 같은 방식으로 PID reuse를 방지한다.

### 14.2 swapchain recreation

게임은 resolution/fullscreen change, alt-tab, renderer reset 등으로 swapchain을 재생성할 수 있다.

다음 상황을 구분해야 한다.

#### Case A — same swapchain

가장 좋은 A/B 비교.

#### Case B — one controlled recreation, same target renderer

기록은 가능하지만 confidence를 낮춘다.

#### Case C — phase 사이 renderer/swapchain이 크게 바뀜

```text
INCONCLUSIVE — target swapchain changed during comparison
```

가 적절하다.

HUD ON 때문에 swapchain recreation이 반복 발생한다면 그것 자체가 별도 failure evidence가 될 수 있지만, 먼저 일반 게임 behavior와 구분해야 한다.

---

## 15. Frame Generation / XeFG 처리

기존 hardware validation에서 Intel UMD-generated frame이 PresentMon에 전부 관측되지 않을 수 있다는 한계가 이미 확인되었다.

따라서 새 Diag는 FG session을 별도 표기한다.

권장 summary:

```text
Frame Generation: detected / suspected / not detected
FrameType distribution: ...
PresentMon generated-frame visibility: limited / unknown
Displayed FPS interpretation: limited when FG active
```

### 15.1 FG에서도 유지 가능한 verdict

다음은 계속 유효하다.

```text
PresentMode distribution
Independent Flip retention
swapchain continuity
AllowsTearing state
major dropped-frame anomalies visible to API2
```

### 15.2 FG에서 단정하면 안 되는 것

```text
PresentMon DisplayedFPS = physical final output FPS
all generated frames are represented
1 / DisplayedFrameTime = panel refresh Hz
```

---

## 16. 권장 raw capture artifact

새 Diag는 console summary만 출력하지 말고 raw evidence를 보존해야 한다.

권장 output directory:

```text
vrr-YYYYMMDD-HHMMSS/
```

예:

```text
session.json
frames.csv
api2-introspection.json        optional / first-run or --verbose
phase-baseline-summary.json
phase-hud-summary.json
d3dkmt-baseline.csv
d3dkmt-hud.csv
report.txt
```

### 16.1 `session.json`

권장 내용:

```text
schemaVersion
Diag version / commit
OS version
PresentMon API version
PresentMon service/middleware version if available
GPU name
GPU LUID
process PID
process creation time
exe path/name
swapchain IDs
phase timestamps
FG state / FrameType summary
capture duration
HUD state transition records
```

### 16.2 `frames.csv`

최소 권장 columns:

```text
Phase
ProcessId
ProcessStartIdentity
SwapChainAddress
PresentStartQpc
PresentMode
PresentRuntime
AllowsTearing
SyncInterval
PresentFlags
FrameType
DroppedFrames
BetweenPresents
PresentedFrameTime
DisplayedTime
BetweenDisplayChange
DisplayedFrameTime
UntilDisplayed
DisplayLatency
RenderPresentLatency
FlipDelay
PresentedFps
DisplayedFps
ApplicationFps
```

실제 API2 frame-query availability/type에 따라 unavailable field는 blank/NA로 남기고 전체 capture를 실패시키지 않는다.

### 16.3 `report.txt`

사람이 바로 읽을 수 있는 최종 요약.

---

## 17. 권장 console / report 형식

예:

```text
=== ClawHUD VRR Diagnostic ===

Target
  EXE                         deathstranding.exe
  PID                         12345
  Process generation          stable
  Dominant swapchain          0x000001ABCDEF0000
  Frame Generation            OFF

Presentation Path
  Baseline samples            840
  HUD ON samples              839
  Baseline Independent Flip   100.0%
  HUD ON Independent Flip     100.0%
  AllowsTearing               100% -> 100%
  SyncInterval                stable
  Swapchain                   stable
  Dropped Frames              0 -> 0
  Result                      PASS

Display Pacing
  DisplayedFrameTime P50      33.31 -> 33.32 ms
  DisplayedFrameTime P95      34.02 -> 34.11 ms
  DisplayedFrameTime P99      35.10 -> 35.24 ms
  BetweenDisplayChange P95    34.05 -> 34.14 ms
  UntilDisplayed P95          ...
  DisplayLatency P95          ...
  Result                      STABLE

D3DKMT Cadence Evidence
  Baseline 1s range           98-109 Hz
  HUD ON 1s range             102-115 Hz
  Baseline classification     VARIABLE-LIKE
  HUD ON classification       VARIABLE-LIKE
  Result                      PRESERVED

OVERALL
  PASS
  HUD preserved the VRR-safe presentation path.
  No material API2 pacing regression was observed.
  D3DKMT supporting cadence evidence remained variable-like.

NOTE
  PresentMon API2 does not directly prove that physical panel VRR is active.
```

---

## 18. Overall verdict 정책 제안

최종 result는 최소 다음 상태가 필요하다.

```text
PASS
FAIL
INCONCLUSIVE
```

단, report에는 하위 축 결과를 항상 함께 출력한다.

### 18.1 FAIL로 볼 수 있는 강한 조건

초기부터 비교적 안전하게 FAIL로 사용할 수 있는 것은 presentation-path failure다.

예:

```text
baseline Independent Flip >= 80%
AND
HUD Independent Flip < 50%
```

또는:

```text
baseline -> HUD Independent Flip drop >= 30 percentage points
```

이 기존 기준은 이미 legacy diagnostic에서 사용되었다.

추후 controlled API2 calibration이 끝나면 다음도 FAIL candidate가 될 수 있다.

```text
large reproducible displayed-frame pacing regression
material dropped-frame increase
variable-like -> fixed-like D3DKMT regression correlated with HUD ON
```

그러나 D3DKMT 하나만으로 physical VRR failure라고 단정하는 정책은 별도 validation 전에는 피한다.

### 18.2 INCONCLUSIVE 조건

```text
not enough frame samples
baseline does not establish Independent Flip
process generation changed
renderer/swapchain changed too much
API2 frame query unavailable
HUD state could not be verified
capture interrupted by alt-tab / suspend / process exit
FG visibility makes requested metric interpretation invalid
```

`INCONCLUSIVE`를 실패로 억지 변환하지 않는다.

---

## 19. D3DKMT classification threshold는 기존 control을 seed로만 사용

현재 hardware validation에서:

```text
VRR OFF fixed control -> ~119.9 Hz, complete windows mostly 119-120
VRR ON                -> clearly variable sub-120 windows
```

라는 매우 강한 empirical separation이 있었다.

하지만 이것을 바로 generic code로:

```text
>=119 = fixed
<119 = VRR
```

처럼 만들면 안 된다.

이유:

- display nominal refresh가 항상 120 Hz라는 보장이 없는 future device
- incomplete 1-second windows
- scheduler / measurement jitter
- LFC / game load interaction
- D3DKMT event semantics 자체가 physical scanout API로 공식 확정되지 않음

따라서 첫 구현에서는:

```text
window list
range
mean
median
near-nominal concentration ratio
```

를 출력하고, classification threshold는 MSI Claw supported scope에 대해 control capture를 더 축적한 뒤 고정하는 것이 안전하다.

---

## 20. PresentMon API2 service와 production ClawHUD 동시 사용

PresentMon API2 service는 session 단위 tracking을 지원하며, live non-backpressured tracking은 broadcaster의 target segment를 공유할 수 있는 구조다.

Upstream 2.5.1 `StartTracking` implementation에서도:

- 동일 session에서 같은 PID를 중복 tracking하면 reject
- 별도 session은 broadcaster target을 register/find
- backpressured playback의 single-owner case만 별도로 제한

하는 구조가 확인된다.

따라서 설계상:

```text
ClawHUD.exe production API2 session
+
ClawHUD.Diag.exe diagnostic API2 session
```

이 같은 게임 PID를 별도로 관찰하는 것이 가능하다.

다만 실제 supported package/service version에서 multi-client live capture를 acceptance test로 한 번 더 검증하는 것이 좋다.

Diagnostic은 production ClawHUD의 internal telemetry provider에 IPC로 기생할 필요가 없다.

---

## 21. 권장 standalone architecture

새 기능을 `DiagnosticSession.cpp`에 다시 크게 몰아넣지 않는 것이 좋다.

현재 `ClawHUD.Diag`는 game-detection research를 위해 이미 많은 window/process/PDH/API2 evidence logic을 가진다.

VRR 기능은 작은 독립 command/module로 분리하는 편이 적절하다.

개념 구조:

```text
ClawHUD.Diag.exe
  |
  |-- existing game-detection diagnostic
  |
  `-- VRR diagnostic command
       |
       |-- VrrDiagnosticCommand
       |-- Api2FrameCapture
       |-- VrrPhaseAnalyzer
       |-- VrrComparisonEvaluator
       |-- D3dkmtCadenceProbe
       `-- VrrReportWriter
```

구현 이름은 예시이며 exact naming contract는 아니다.

### 21.1 `VrrDiagnosticCommand`

책임:

```text
target acquisition
phase orchestration
settle/capture timing
HUD state instruction/control
cancellation
artifact directory creation
final result
```

### 21.2 `Api2FrameCapture`

책임:

```text
session open
track PID
frame query register
consume/decode frames
metric availability handling
raw frame persistence
```

### 21.3 `VrrPhaseAnalyzer`

책임:

```text
dominant swapchain
PresentMode distribution
Independent Flip ratio
AllowsTearing distribution
DroppedFrames
pacing percentiles
FrameType distribution
```

### 21.4 `VrrComparisonEvaluator`

책임:

```text
baseline validity
path verdict
pacing comparison
confidence/inconclusive reasons
overall verdict composition
```

### 21.5 `D3dkmtCadenceProbe`

기존 archive POC의 검증된 capture 개념을 참고하되 diagnostic-only supporting signal로 유지한다.

Production HUD code와 link하지 않는다.

---

## 22. 오류 처리 원칙

Diagnostic은 research/validation tool이므로 unavailable metric 하나 때문에 전체 session을 버리지 않는다.

예:

```text
DISPLAY_LATENCY unavailable
```

이면:

```text
DisplayLatency: N/A
```

로 두고 `PRESENT_MODE`, `BETWEEN_DISPLAY_CHANGE` 등 사용 가능한 evidence로 계속 분석한다.

반면 다음은 comparison integrity에 치명적이므로 `INCONCLUSIVE`가 맞다.

```text
no usable target frames
no PresentMode samples
process exited
PID generation changed
phase HUD state unknown
no stable dominant target
```

API2 query error와 physical presentation failure를 혼동하지 않는다.

---

## 23. 테스트 / validation matrix

구현 전후로 최소 다음 matrix를 권장한다.

### A. Fixed refresh negative control

```text
Intel VRR OFF
120 Hz fixed
30 FPS game
FG OFF
```

기대:

```text
PresentMode may still be 100% Independent Flip
AllowsTearing may remain 1
D3DKMT should reproduce fixed-like near-120 pattern on validated MSI Claw hardware
```

이 control은 `Independent Flip == VRR active` 같은 잘못된 logic을 즉시 잡아낸다.

### B. VRR ON positive control

```text
Intel VRR ON
30 FPS
FG OFF
```

기대:

```text
Independent Flip retained
D3DKMT variable-like sub-120 evidence on current validated hardware
```

### C. Production HUD A/B

```text
VRR ON
HUD OFF -> production HUD ON
```

기대:

```text
same presentation family
no major pacing regression
no material dropped-frame increase
supporting cadence behavior preserved
```

### D. 60 FPS

낮은 30 FPS/LFC case와 다른 영역에서 pacing metric 분포 확인.

### E. Higher FPS / near upper VRR range

가능하면 90 FPS 또는 VRR upper range에 가까운 게임 조건.

### F. XeFG 2x / 3x

```text
FrameType distribution
PresentMon visible/generated frame limitations
PresentMode retention
```

확인.

### G. Alt-tab contamination

capture 중 alt-tab 시:

```text
phase invalidation or explicit event marker
```

가 되는지 검증.

### H. Swapchain recreation

fullscreen/window mode change 또는 renderer recreation이 있을 때 잘못된 PASS/FAIL 대신 `INCONCLUSIVE`가 나오는지 확인.

### I. ClawHUD production API2 concurrent session

production HUD가 이미 API2로 게임을 tracking 중일 때 standalone Diag가 동일 PID frame query를 정상 소비하는지 확인.

---

## 24. 구현 순서 권고

바로 final automatic verdict까지 만들지 않는다.

### Stage 1 — Raw API2 frame capture

먼저 standalone Diag에:

```text
target PID
frame query
raw frames.csv
phase markers
```

만 구현한다.

### Stage 2 — Presentation path analysis

기존 검증된 Independent Flip comparison logic을 API2 frame data에 적용한다.

이 단계의 verdict는 비교적 바로 안정화할 수 있다.

### Stage 3 — D3DKMT supporting capture

기존 archive logic을 diagnostic-only module로 복원하고 1-second windows를 report한다.

### Stage 4 — Controlled hardware matrix

VRR ON/OFF, FPS, FG matrix를 실제 MSI Claw에서 캡처한다.

### Stage 5 — Pacing threshold calibration

수집 결과를 기준으로:

```text
DisplayedFrameTime
BetweenDisplayChange
UntilDisplayed
DisplayLatency
DroppedFrames
```

의 regression threshold를 결정한다.

### Stage 6 — Final combined report/verdict

마지막에 overall PASS/FAIL/INCONCLUSIVE를 안정화한다.

이 순서의 장점은 arbitrary threshold를 먼저 코드에 박고 나중에 hardware behavior에 맞추는 실수를 피할 수 있다는 점이다.

---

## 25. 하지 말아야 할 것

다음은 새 VRR diagnostic에서 피한다.

### Presentation / HUD

- diagnostic 전용 alternate HUD renderer
- production `HudPresentation` contract 변경
- window style 변경
- independent flip requirement 약화
- premultiplied alpha contract 변경
- diagnostic을 opacity/click-through 변경의 이유로 사용

### PresentMon 해석

- `Independent Flip == VRR ACTIVE`
- `AllowsTearing == VRR ACTIVE`
- `DisplayedFrameTime reciprocal == physical refresh Hz`
- `pmConsumeFrames` arrival interval을 frame cadence로 사용
- FG active 상태에서 PresentMon FPS를 physical final output FPS로 단정

### Architecture

- developer diagnostic을 다시 `App.cpp`에 연결
- Settings Diagnostics tab 부활
- production telemetry lifecycle에 diagnostic phase state 삽입
- 별도 PresentMon.exe CLI dependency 재도입
- diagnostic 때문에 production API2 provider를 복잡하게 변경

### Verdict

- baseline이 invalid한데 PASS/FAIL 강제
- swapchain이 완전히 바뀌었는데 단순 비교
- D3DKMT 하나만으로 generic physical VRR truth 선언
- hardware calibration 없이 임의 P95/P99 regression threshold 확정

---

## 26. 현재 결론

### 26.1 API2는 새 VRR diagnostic에 충분히 가치가 있다

기존 `PresentMon.exe` CSV diagnostic보다 API2는 다음 면에서 명확히 우월하다.

```text
per-frame frame query
explicit metric introspection
DisplayedFrameTime
PresentedFrameTime
BetweenDisplayChange
UntilDisplayed
DisplayLatency
DroppedFrames
FrameType
QPC-based segmentation
```

즉 단순 PresentMode checker가 아니라 **presentation-path + frame/display pacing regression analyzer**를 만들 수 있다.

### 26.2 기존 Independent Flip verdict는 버리지 않는다

다만 이름과 의미를 정확히 바꾼다.

기존 의미가 암묵적으로 `VRR PASS`처럼 읽힐 수 있었다면, 새 설계에서는:

```text
Presentation Path PASS
```

로 명확히 정의한다.

### 26.3 Physical VRR active는 별도 층이다

API2 alone으로 physical panel active VRR을 증명하지 않는다.

D3DKMT는 현재 MSI Claw hardware에서 유용한 supporting cadence evidence를 제공했지만, 공식 physical refresh API처럼 과장하지 않는다.

### 26.4 최종 추천 구조

```text
PresentMon API2
  -> authoritative comparative evidence for game presentation path
  -> strong frame/display pacing evidence

D3DKMT
  -> supporting external cadence evidence

Controlled VRR ON/OFF hardware matrix
  -> threshold calibration / interpretation anchor
```

### 26.5 구현 판단

**DESIGN READY, THRESHOLDS NOT FULLY CALIBRATED**

구현 자체는 시작할 수 있다.

그러나 첫 PR에서 완성형 automatic physical-VRR verdict를 목표로 하지 않는 것이 좋다.

가장 안전한 순서는:

```text
raw API2 capture
-> presentation-path verdict
-> D3DKMT evidence
-> hardware matrix
-> pacing thresholds
-> combined final verdict
```

이다.

---

## 27. 최종 설계 요약

새 `ClawHUD.Diag.exe` VRR diagnostic의 정의는 다음 한 문장으로 정리할 수 있다.

> **ClawHUD production HUD가 게임의 VRR-safe presentation path와 display pacing을 보존하는지 PresentMon API2 frame data로 A/B 검증하고, D3DKMT cadence를 별도 supporting evidence로 기록하는 standalone diagnostic.**

그리고 가장 중요한 interpretation rule은 다음이다.

> **PresentMon API2가 Independent Flip을 보고한다고 해서 physical panel VRR이 현재 active라고 판정하지 않는다.**

이 경계를 지키면 새 diagnostic은 기존 legacy VRR test보다 훨씬 많은 정보를 제공하면서도, 현재 hardware evidence가 허용하는 범위를 넘어선 잘못된 VRR 판정을 피할 수 있다.

---

## 28. Reference material

Repository:

```text
docs/VRR_HARDWARE_VALIDATION_RESULTS.md
docs/HUD_PRESENTATION_VRR_DECISION_HISTORY.md
docs/HUD_PRESENTATION_REFACTOR_GUARDRAIL.md
docs/DIAGNOSTICS.md
docs/APP_REFACTOR_PLAN.md

archive/diagnostics/legacy-vrr-presentmon/README.md
archive/diagnostics/legacy-vrr-presentmon/VrrDiagnostic.cpp
archive/diagnostics/legacy-vrr-presentmon/VrrDiagnosticAnalysis.cpp
archive/diagnostics/legacy-vrr-presentmon/VrrDiagnosticAnalysis.h
archive/diagnostics/legacy-vrr-presentmon/D3dkmtVblankProbe.cpp
archive/diagnostics/legacy-vrr-presentmon/IntelVrrDiagnosticProbe.cpp

archive/diagnostics/presentmon-api2/README.md
archive/diagnostics/presentmon-api2/PresentMonApi2Diagnostic.cpp
archive/diagnostics/presentmon-api2/PresentMonApi2Diagnostic.h

src/ClawHUD.Diag/DiagnosticSession.cpp
src/ClawHUD.Diag/DiagnosticSession.h
```

External/local research evidence used for this design:

```text
INTEL_GRAPHICS_SOFTWARE_PRESENTMON_API2_RE_REPORT.md
api2-20260830-200431.log
api2-20260830-200431-introspection.json
api2-20260830-200431-metrics.csv
api2-20260830-200431-frames.csv
```

Upstream PresentMon reference:

```text
GameTechDev/PresentMon
PresentMon API2 / PresentMonService StartTracking live-session behavior
```
