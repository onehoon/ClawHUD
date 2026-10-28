# ClawHUD Work Order — VRR Fix: 패널별 독립 처리, 정확한 IGCL 매핑, DRR·Intel VRR 전용 로그 통합

> **Repository:** `onehoon/ClawHUD`
> **Date:** 2026-10-28
> **Status:** 구현 작업지시서 (이 문서 자체는 동작 코드를 변경하지 않음)
> **1차 대상:** `integration/steamaddon`
> **2차 대상:** `main` — 애드온 검증·병합 후 커밋을 순서대로 체리픽하는 별도 PR
> **선행 변경:** Addon PR #274 및 Main PR #275 — Windows DRR OFF 통합 완료
> **납품:** 브랜치당 구현 PR 1개, 권장 각 2개 구현 커밋. 코드 줄 수 제한 없음.
> **핵심 불변식:** HUD production presentation / independent flip / premultiplied alpha 계약 변경 금지. UI 진행 상황 추가 금지.

## 1. 배경과 사용자 실측

사용자 환경에서 실제 사용 중인 외부 모니터는 LG UltraGear 27GS65F 하나였지만, Intel VRR 결과 파일에 아래와 같이 기록됨.

```ini
[IntelVrr]
Status=UnsupportedPanel
PanelName=PN8007QB1-2
RangeBefore=
RangeAfter=
Message=Display capability range does not match the expected panel class.
Timestamp=2026-10-28T07:47:20Z
```

이 로그로 **확인 가능한 사실**은 Claw 내장 패널 `PN8007QB1-2` 분기가 먼저 선택됐고, 선택된 IGCL 출력의 capability가 내장 패널용 `48–120Hz` 조건에 불일치하여 프로파일을 읽기 전에 중단했다는 것이다. 실제 IGCL 값이 반드시 `48–180Hz`였거나, WMI의 `Active`가 실제로 stale 상태였다고 단정하지 않는다. 이번 수정 후 진단 로그로 그 두 가능성을 구분할 수 있어야 한다.

사용자 요구:

1. 지원 패널을 하나만 우선 선택하지 말고 **발견된 내장 패널과 LG 패널 각각**에 맞는 Fix 정책을 실행한다.
2. WMI `Active` 값은 **모델 발견 여부를 결정하는 조건으로 사용하지 않는다.** 다만 정확히 매핑되지 않거나 제어할 수 없는 비활성 출력에 무조건 IGCL 쓰기를 허용하라는 뜻은 아니다.
3. Windows DRR OFF와 Intel VRR Fix의 세부 진단을 **VRR 전용 단일 로그**로 통합한다. 일반 `clawhud.log`는 VRR 진단의 저장소가 아니다.
4. 앱 시작 시 백그라운드에서 조용히 수행한다. UI 진행률/스피너/토스트/새 상태 카드/추가 옵션 없음.
5. 애드온 브랜치에서 구현·검증한 뒤 `main`에 해당 구현 커밋만 체리픽한다.

## 2. 2026-10-28 소스 코드 검토 결과 (현행 동작)

### 2.1 패널 판정의 구조적 우선순위 문제

- `src/ClawHUD/Tweaks/IntelVrr/IntelVrrRangeTweak.cpp`: `Run()`이 `std::find_if(..., IsAffectedPanel)`로 내장 패널을 **먼저 한 개 선택**한다. LG 검색 `IsLg27gs65fPanel` / `RunLg27gs65fProductionVrrFix()`은 **내장 패널이 하나도 없을 때만** 호출된다.
- 같은 함수의 `Native()`는 `48–120Hz ±0.1Hz`를 검사한다. 우선 선택된 내장 패널과 실제 IGCL 출력이 다르면 `UnsupportedPanel` / 위 메시지가 발생할 수 있다.
- `src/ClawHUD/Tweaks/IntelVrr/AffectedPanelDetector.cpp`: `IsAffectedPanel()`은 `Active && CSW && 0801 && PN8007QB1-2`, `IsLg27gs65fPanel()`은 `Active && GSM && 5C8C && LG ULTRAGEAR/27GS65F`를 요구한다. 모델 분류와 WMI 활성 정보가 불필요하게 결합돼 있다.
- `IntelArcSyncClient::EnumerateDisplayOutputs()` 결과의 `friendlyName`은 실제로 채워지지 않는다. 현행 `MatchesCorrelatingIdentity()`는 이름이 비었을 때 `true`를 반환한다. 단일 출력 또는 `Native()` 범위만으로 올바른 내장 대상이라고 간주하는 것은 다중 패널 환경에서 안전한 식별이 아니다.

### 2.2 LG 전용 경로에도 전역 단일 모니터 제한이 있음

- `src/ClawHUD/Tweaks/IntelVrr/Lg27gs65fVrrRangeTweak.cpp`: `FindExactLgTarget()`은 `EnumDisplayMonitors` 개수 `== 1`과 Windows `activePathCount == 1`을 요구한다. 따라서 내장+LG 동시 활성 환경에서는 LG 모델을 올바르게 식별해도 실행되지 않는다.
- 반면 LG 경로는 `GSM5C8C` 디바이스 경로와 이름을 확인하고, Windows target `adapter LUID + target ID`를 통해 IGCL을 조회한 후 setter 전에 타깃을 재검증한다. **이 안전장치는 보존**한다.
- 애드온의 `src/ClawHUD/Tweaks/IntelVrr/Probe/IntelVrrTargetStateProbe.{h,cpp}`에는 `ResolveIntelVrrTarget()` 및 `SetProfileForExactTarget()`가 구현돼 있다.
- **중요:** 이 setter는 `48–180Hz` 및 LG 전용 상태/프로파일을 하드코딩해 검증한다. **내장 패널 48–120Hz에 그대로 재사용하면 안 된다.** 정확한 타깃 매핑 부분은 재사용하되, 쓰기 정책은 알려진 모델별로 분리하거나 안전하게 명시적 모델 정책을 전달해야 한다.

### 2.3 로그가 흩어지거나 소실됨

- `IntelVrrRangeTweak::Run()`에서 WMI 목록·capability·IGCL 호출 로그를 `log_`에 기록하지만, 앱 시작 워커인 `TweakStartupCoordinator::Run()`은 `tweak.Run(enabled)` 결과만 사용하고 `LastLog()`를 파일로 내보내지 않는다.
- LG 전용 `outcome.log`도 `log_`로 수집된 이후 파일로 출력되지 않는다.
- `IntelVrrResultStore::Save()`가 만드는 `%LOCALAPPDATA%\ClawHUD\tweaks-intel-vrr-result.ini`는 `Status/PanelName/RangeBefore/RangeAfter/Message/Timestamp` **단일 요약 결과**이지 상세 진단 로그가 아니다.
- `src/ClawHUD/Tweaks/WindowsDrr/WindowsDrrDisable.cpp` 및 `TweakStartupCoordinator.cpp`의 DRR 진단은 현재 `RuntimeLogger`를 통해 `%LOCALAPPDATA%\ClawHUD\logs\clawhud.log`에 저장된다. 여기에는 before/after, BOOST, LUID/target, validate/apply/verify 상태가 이미 기록된다.
- `RuntimeLogger::SetDebugLogging()`이 INFO 이상을 기본 기록하므로 상세 진단을 DEBUG에만 넣거나 개발자 설정에만 의존시키면 재현 로그가 부족해진다.

### 2.4 브랜치 차이 — 체리픽 시 반드시 보존

공통 `IntelVrrRangeTweak.cpp`, `AffectedPanelDetector.cpp`, `TweakStartupCoordinator.cpp`의 핵심 구조는 양쪽 브랜치에서 동일하다. 그러나 LG 정확한 타깃 매핑 구현은 다음과 같이 다르다.

| 역할 | `integration/steamaddon` | `main` |
| --- | --- | --- |
| Windows 디스플레이 매핑 | `src/ClawHUD/Tweaks/IntelVrr/Probe/IntelDisplayPathProbe` | `src/ClawHUD.Diag/DisplayPathProbe` |
| IGCL 상태/출력 매핑 | `src/ClawHUD/Tweaks/IntelVrr/Probe/IntelVrrTargetStateProbe` | `src/ClawHUD.Diag/DiagIntelVrrStateProbe` |
| LG Fix 연계 | 위 애드온 타입 사용 | 위 Main Diag 타입 사용 |
| 빌드·테스트 목록 | 애드온 자체 CMake 구성 | Main 자체 CMake 구성 |

**Main PR에서는 애드온 Probe 파일을 통째로 복사하거나 Main의 Diag 경로를 갈아엎지 않는다.** 브랜치별 기존 구현을 살리고 같은 판정·로그 동작을 충족하도록 필요한 부분만 적응시킨다.

## 3. 최종 동작 계약

### 3.1 발견(Discovery)과 쓰기(Eligibility)를 분리

- WMI `ManufacturerName`, `ProductCodeID`, `UserFriendlyName`, `InstanceName`, `Active`를 수집한다.
- 지원 모델 분류는 `Active` 없이 시행한다. `Active` 값은 **진단 메타데이터**로만 기록한다.
- 지원 모델:
  - Claw 내장 패널: `CSW / 0801 / PN8007QB1-2` — capability `48–120Hz`, 기존 Claw 정책 유지.
  - LG 27GS65F: `GSM / 5C8C / LG ULTRAGEAR 또는 LG 27GS65F` — capability `48–180Hz`, 기존 LG 정책 유지.
- 이름만 같은 다른 모델, 제조사/제품 코드가 다른 모니터, 기타 제품의 48–120/180 범위는 지원 대상이 아니다.
- 동일 WMI 패널이 중복 열거되거나 Windows 경로가 복제되는 경우 같은 실제 타깃을 두 번 적용하지 않는다.

### 3.2 정확한 활성 Windows 타깃 + IGCL 매핑

1. Windows의 실제 활성 display path를 열거한다. 모니터별 target adapter LUID, target ID, source, monitor device path/이름, 가용성 등을 취득한다.
2. 지원 모델 후보와 Windows의 **정확한 장치 인스턴스/제품 ID 및 식별 근거**를 연결한다. WMI `InstanceName`와 Windows monitor device path의 정규화된 동등성도 고려한다. 단순 문자열 부분 일치, IGCL 출력 수 == 1, 주사율 범위 단독 선택은 쓰기 근거로 인정하지 않는다.
3. 확인된 Windows target (`adapter LUID + target ID`)을 IGCL 출력과 정확히 1:1로 매핑한다. IGCL 출력 열거가 불완전하거나 중복·불명확하면 해당 타깃은 **Skip**한다.
4. 타깃별 capability/profile을 읽고 **해당 모델의 정책**으로 판단한다. 서로 다른 모니터의 결과를 교차 적용하지 않는다.
5. 변경 직전 대상/현재 상태를 재검증하고, 변경 후 같은 타깃을 재조회해 검증한다. 다른 패널에 쓰는 것은 허용하지 않는다.

**해석상 중요:** 비활성 WMI 후보도 발견하여 로그에는 기록한다. 다만 **Windows 활성 display path에서 주소 지정이 불가능한 타깃은 setter를 호출하지 않고 `NoActiveTarget`/`Unmapped` 등 이유를 남긴다.** 비활성 패널을 억지로 재활성화하거나 표시 모드를 바꿔 쓰기 위한 복잡한 별도 기능을 만들지 않는다.

### 3.3 패널별 독립 실행

- 전역 단일 `find_if` 및 “Claw 없을 때만 LG” 분기를 없앤다.
- 현재의 LG `EnumDisplayMonitors == 1` / 전역 `activePathCount == 1` 제한은 제거한다. **전체 모니터 수와 무관하게 해당 LG 타깃이 유일하고 정확한지** 확인한다.
- 내장+LG가 둘 다 활성 상태라면 각각에 대해 독립적인 현재 상태 확인·정책 적용·사후 검증을 수행한다.
- 내장 패널이 WMI에 남아 있지만 Windows에서 비활성인 **LG 단독 활성** 환경이라면 내장은 `SkippedNotActive`로 기록하고 LG에 `48–180Hz` 정책을 적용해야 한다.
- Claw 단독 활성 상태에서는 기존 `48–120Hz` Fix 결과와 Custom/Off 보호 정책을 회귀 없이 유지한다.
- LG는 기존 `RECOMMENDED/COMPATIBLE 90–180Hz → EXCELLENT → 48–180Hz 확인` 및 미확인 사용자 설정 보호, 안전한 되돌리기 가드를 유지한다.
- 한 패널의 Unsupported/Unavailable/Skip이 **다른 패널의 처리를 중단하지 않는다.**
- 해당 타깃이 정확히 확인될 수 없는 경우 임의의 다른 IGCL 출력으로 폴백하지 않는다.

### 3.4 모듈 분리 방향 (강제적인 신규 프레임워크 요구 아님)

기존 모듈과 테스트 가능 경계를 최대한 재사용한다.

```cpp
// 설계 의사코드: 기존 API에 그대로 존재하는 타입/함수가 아님.
for (const auto& panel : discoveredSupportedPanels) {
    const auto target = FindExactActiveTarget(panel, activeWindowsPaths);
    if (!target) {
        RecordPanelResult(panel, "SkippedNoExactTarget");
        continue;
    }

    const auto igcl = QueryExactIgclTarget(target->adapterLuid, target->targetId);
    const auto decision = EvaluatePanelSpecificPolicy(panel.kind, igcl);
    if (decision == Decision::Apply && RevalidateSameTarget(*target, igcl)) {
        ApplyAndVerifyPanelSpecificProfile(panel.kind, igcl);
    }
    RecordPanelResult(panel, decision);
}
```

내장·LG는 같은 *검색/타깃 확정* 절차를 사용하되 **정책(지원 capability, 허용 프로파일, setter 재검증 조건)**은 명시적인 지원 모델 클래스별로 분리한다. 기존 LG `SetProfileForExactTarget()`의 `48–180Hz` 하드코딩을 내장 패널에 사용하거나 조건을 제거해 일반 setter로 만드는 것은 금지한다.

## 4. VRR 전용 통합 진단 로그

### 4.1 단일 목적지

`%LOCALAPPDATA%\ClawHUD\logs\tweaks-vrr.log`

- Windows DRR + Intel VRR(내장 및 LG)을 **이 파일에 함께 기록**한다.
- VRR 관련 상세 정보를 일반 `clawhud.log`에 이중 기록하지 않는다. 애플리케이션의 기존 비VRR 로깅은 그대로 둔다.
- 기존 `tweaks-intel-vrr-result.ini`는 **설정 UI/IPC 호환용 요약 결과**로 유지한다. 이 INI에 DRR 다중 패널 진단 로그를 억지로 포함하지 않는다.
- 전용 로거는 작은 별도 구현(예: `VrrDiagnosticLogger`)을 사용하거나 기존 로깅 기반의 **파일 목적지 주입**처럼 최소 변경으로 구성한다. 로그 실패가 VRR Fix 실행이나 앱 시작을 막아서는 안 된다.
- 한 번의 시작 실행에서 `RunStart/RunEnd`를 동일한 run ID/타임스탬프로 묶고, 각 패널/DRR 대상의 `Before → Decision → Apply → Verify → Result`를 동일 로그에서 추적할 수 있게 한다.
- INFO 레벨에 기본 식별·조회 값·결과·실패 원인을 기록한다. 진단의 핵심 내용이 `DebugLoggingEnabled=true`에만 의존하면 안 된다.
- 파일은 append, 크기 제한 및 소수 롤오버(예: 2MiB × 3세대)를 적용해 무제한 증가 방지. 잠금/동시 접근이 필요하면 VRR 전용 로거 내부에서만 처리한다. 로그용 상태 머신·상시 폴링은 추가하지 않는다.

### 4.2 필수 기록 항목

**실행 공통**

- `RunStart`: 실행 ID/날짜, VRR Fix enabled, 앱/빌드 식별(사용 가능한 경우), 총 후보 수.
- `RunEnd`: DRR 결과 1개, 각 Intel 타깃별 결과, 총 적용/스킵/실패 수.
- 앱 종료나 재시작 시 중복 실행을 피하고 Intel 기존 `2/5/15초` Unavailable 재시도 횟수를 구별하여 남긴다.

**Windows DRR**

- 활성 display path 개수 및 매 경로의 monitor friendly/device name, adapter LUID, source ID, target ID.
- `DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE` Before / After, virtual refresh / physical signal refresh.
- `AlreadyOff`라면 **SetDisplayConfig 호출 없이** 결과를 기록한다.
- `VALIDATE` / `APPLY` / 재조회 검증을 진행한 경우 단계별 Win32 반환 코드, 변경 경로 수, 최종 결과.
- Windows DRR는 **모든 활성 경로**에 적용하며 Intel GPU/지원 패널 식별에 종속되지 않는다. 기존 Win32 flag 조작과 display mode 보존 검증 동작은 변경하지 않는다.

**Intel VRR — 패널별로 하나의 섹션**

- WMI 발견 항목: manufacturer/product/name/instance/`Active`.
- 판정한 지원 모델 클래스 또는 `Unsupported`.
- Windows target discovery: active target 수, 해당 target 이름·device path·adapter LUID·target ID, WMI와의 매핑 근거.
- IGCL: 열거 결과(출력 수/완전성), `Exact/Unknown/Ambiguous`, target match count, capability supported 및 min/max, 현재 profile 및 범위.
- 결정 이유(AlreadyCorrect / Apply / SkippedUserProfile / SkippedNotActive / Ambiguous / Unsupported / Unavailable 등).
- 쓰기 시 요청 profile, API 반환 코드, 재조회 profile·범위, 검증 결과, LG rollback 발생 시 사유·검증 여부.
- 실패 시 **실패 단계 + Windows/IGCL 오류 코드 + 실제 대상 식별자**를 남긴다. 단지 “UnsupportedPanel”만 기록해선 안 된다.

### 4.3 출력 예시 (예상 형식이며 실측 로그 아님)

```text
[VRR] RunStart id=20261028-... enabled=1
[DRR] activePaths=1
[DRR][Target] display=LG ULTRAGEAR adapter=... target=... boostBefore=1
[DRR][Target] boostAfter=0 virtualHz=... physicalHz=...
[DRR] validate=0 apply=0 verify=SUCCESS result=Applied

[IGCL][WMI] panel=PN8007QB1-2 mfg=CSW product=0801 active=1
[IGCL][Claw] targetMapping=NoActiveTarget decision=Skip
[IGCL][WMI] panel=LG ULTRAGEAR mfg=GSM product=5C8C active=1
[IGCL][LG] targetMapping=Exact adapter=... target=...
[IGCL][LG] capability=48-180 profileBefore=COMPATIBLE rangeBefore=90-180
[IGCL][LG] decision=ApplyExcellent setApi=0 verify=48-180 result=Applied
[VRR] RunEnd DRR=Applied IGCL_Claw=Skipped IGCL_LG=Applied
```

### 4.4 로깅 구현상 주의

- 지금 `WindowsDrrDisable.cpp::LogSnapshot` / DRR SetDisplayConfig 로그와 `TweakStartupCoordinator.cpp::LogDrrResult`는 `RuntimeLogger`를 호출한다. **동일 메시지가 새로운 전용 로그에 누락 없이 기록되도록 출력 sink를 바꿔야 한다.** 전역 `RuntimeLogger::Log`의 목적지를 통째로 바꾸면 앱의 모든 로그가 오염되므로 금지한다.
- 지금 Intel `log_` / `LastLog()`와 LG `outcome.log`를 실제 VRR 파일로 연결한다. 함수가 실패/조기 반환해도 누락이 없어야 한다.
- DRR/Intel 각각에 별도의 로그 파일을 만들지 않는다.
- 문자열 변환·파일 쓰기 실패는 best-effort 진단 실패로 취급하고 fix/IPC/UI 처리를 막지 않는다.

## 5. 결과 저장/IPC/재시도 호환

- `IntelVrrRunResult`/`IntelVrrResultStore`/설정 IPC 구조와 기존 상태 이름을 **임의 변경하지 않는다**.
- 예전처럼 패널 루프에서 `finish()`가 호출될 때마다 결과 INI를 덮어쓰거나 `Clear()`하면, 처리 후반의 WMI stale/Unsupported가 앞선 LG 성공을 지워버릴 수 있다. 결과 저장은 **한 번의 실행을 집계한 다음 마지막에 1회**만 수행한다.
- 활성·정확 매핑된 지원 패널이 1개면 그 패널의 요약 결과를 기록한다. 여러 개면 기계적으로 마지막 패널을 선택하지 말고 **전체 성패를 오도하지 않는 결정적 요약 정책**(오류 우선·적용 성공 등)을 정한 뒤 테스트한다. 상세 패널별 내역은 `tweaks-vrr.log`가 진실의 원천이다.
- 실제 매핑된 지원 패널이 하나도 없다면 기존 UI의 stale 결과를 남기지 않는다. 단, 비활성 후보가 남아 있다는 이유만으로 활성 LG의 결과를 지우지 않는다.
- `TweakStartupCoordinator::RunStartupSequence`의 DRR 1회 독립 실행, Intel Unavailable `2/5/15초` 재시도 상한, Start/Stop의 기존 생명주기를 유지한다.
- 다중 패널 처리 중 Unavailable인 타깃의 재시도가 다른 패널의 정상 적용을 반복하지 않도록 설계한다. 이미 `Applied`/`AlreadyCorrect`를 얻은 타깃을 매번 다시 쓰지 않도록 한다. 불필요한 복잡한 동기화나 새로운 상시 워커는 요구하지 않는다.
- 기존 VRR Fix ON/OFF 설정 저장 및 **다음 앱 시작에 적용**되는 동작 유지. OFF 시 DRR·IGCL 복원 금지.

## 6. 구현 대상 파일과 경계

| 구성요소 | 변경 방향 |
| --- | --- |
| `src/ClawHUD/Tweaks/IntelVrr/AffectedPanelDetector.{h,cpp}` | `Active`와 모델 식별 분리, 양쪽 모델 발견과 WMI 증거 유지 |
| `src/ClawHUD/Tweaks/IntelVrr/IntelVrrRangeTweak.{h,cpp}` | 전역 단일 선택 제거, 모델별 결과 처리/정확한 타깃 검증, 자세한 결과 전달 |
| `src/ClawHUD/Tweaks/IntelVrr/Lg27gs65fVrrRangeTweak.{h,cpp}` | 전역 1모니터 조건 제거, 대상 LG의 1:1 매핑·기존 안전 정책 유지 |
| `src/ClawHUD/Tweaks/IntelVrr/Probe/*` (애드온) | 기존 Exact mapping 재사용; 내장용 48–120 안전 setter 필요 시 정책 한정 확장 |
| `src/ClawHUD/Tweaks/WindowsDrr/WindowsDrrDisable.cpp` | DRR 진단을 VRR 전용 sink로 전달, OS 변경 로직은 그대로 |
| `src/ClawHUD/Tweaks/TweakStartupCoordinator.{h,cpp}` | 단일 실행 단위 로깅, DRR+IGCL 결과 요약, 기존 재시도 경계 유지 |
| 신규 `src/ClawHUD/Tweaks/.../VrrDiagnosticLogger.{h,cpp}` 등 | VRR 전용 append/rotation/실패 격리 및 단일 로그 파일 |
| `src/ClawHUD/Tweaks/IntelVrr/IntelVrrResultStore.cpp` | 다중 대상 집계 저장을 마지막에 1회; 기존 INI 형식 유지 |
| `CMakeLists.txt` / `cmake/ClawHUDTests.cmake` | 신규 소스·테스트를 **각 브랜치에 맞게** 추가 |

파일 배치는 가장 작고 일관된 방식으로 선택할 수 있다. 일반 `RuntimeLogger`/HUD/Settings UI/Control IPC의 광범위 리팩터링은 이번 범위에서 제외한다.

## 7. 필수 테스트/실기기 검증

### 7.1 패널 선택 및 보호 회귀

1. LG 27GS65F만 활성, WMI에 내장 `PN8007QB1-2 Active=true` stale 항목이 남음 → 내장 writer 0회, **LG 정확한 타깃에만** 쓰기 또는 AlreadyCorrect.
2. 내장만 활성, WMI에 비활성 LG 후보 있음 → 내장 48–120 정책만 적용.
3. 내장+LG 둘 다 활성 → 타깃별 capability/profile 독립 처리, 다른 패널의 status에 영향 없음.
4. WMI `Active=false`로 보고되지만 live Windows target과 정확히 일치하는 지원 패널 → Active만을 이유로 무조건 배제하지 않음.
5. WMI에만 있고 활성 Windows target 없음 → 발견 로그는 남기되 IGCL setter 0회.
6. WMI 모델명 중복/다른 제품 ID/동일 capability의 타사 모니터 → 중복 적용 및 오인 적용 금지.
7. IGCL 출력 0개/다중/불완전 열거, target ID 불일치, LUID 충돌 → 명시적 Skip/Unavailable, setter 0회.
8. 기존 Claw `48–120` 추천/우수/사용자 Custom/Off 상태 및 LG `48–180` / `90–180` 정책, 실패/검증/rollback 보호 회귀.
9. 패널 한쪽 `Unavailable`·`Unsupported`여도 다른 쪽 검사 정상 진행.
10. DRR ON/OFF 및 Intel 결과 각각 구분되고, DRR 호출은 Intel 재시도 횟수에 영향받지 않음.

### 7.2 진단 로그 검증

1. `tweaks-vrr.log` 파일 하나에 DRR Before/After/Result와 Intel 패널별 discovery/mapping/capability/profile/Result가 같은 Run ID로 존재.
2. IGCL capability 불일치가 재현되면 대상/예상 범위/실제 범위/조회 오류가 명시돼 이전의 “UnsupportedPanel 한 줄” 문제 재발 없음.
3. `AlreadyOff`, DRR validate/apply/verify 오류, WMI 실패, LG target not found, IGCL 불가, 설정 OFF 등 각 조기 반환에도 구조화된 결과 기록.
4. 로그 실패(읽기 전용 폴더, 파일 잠김, 회전 실패)는 앱 실행 및 VRR 기능을 실패시키지 않음.
5. 일반 `clawhud.log`로 VRR 세부 로그가 들어가지 않으며 다른 런타임 로그는 기존 그대로 기록.
6. `tweaks-intel-vrr-result.ini` 형식과 UI/IPC 기존 읽기 동작 유지. 한 패널의 결괏값이 다른 패널의 집계를 실수로 삭제하지 않음.
7. 파일 크기 상한/회전, 다중 실행 로그의 append 경계, UTF-8 문자열 처리 테스트.

### 7.3 실기기 확인

- Claw 내장 단독 / LG 27GS65F 단독 / 두 모니터 동시, 각각 VRR Fix ON으로 재시작.
- 로그의 WMI Active / Windows target ID / IGCL capability 값이 실제 연결 상태와 일치하는지 확인.
- LG 원래 90–180Hz인 경우에만 정책 조건이 맞으면 Excellent로 전환되고 48–180Hz가 확인되는지 확인.
- Windows DRR OFF 적용 결과를 Windows 설정 및 DisplayConfig 재조회로 확인하고, 해상도/고정 최대 주사율/HDR/VRR 자체를 건드리지 않았는지 확인.
- 실기기 시험 없이는 API/모의 테스트 통과를 실제 모니터 성공으로 단정하지 않는다.

## 8. 절대 변경 금지 — VRR-critical HUD contract

다음은 변경·대체·우회 금지:

- HUD `windowExStyle` 및 `WS_EX_TRANSPARENT`, `WS_EX_NOACTIVATE`, `WS_EX_TOPMOST`, 기존 `WS_EX_LAYERED`.
- `WM_NCHITTEST -> HTTRANSPARENT`, `WM_MOUSEACTIVATE -> MA_NOACTIVATE`.
- `ProductionHudPresentationContract()`, independent flip requirement.
- 기존 Presentation API / DirectComposition production path, premultiplied alpha.
- Background Opacity는 background-only (HUD 전경 요소의 전체 불투명도를 바꾸지 않음).

HUD 출력 및 프레젠테이션은 이번 변경과 무관하다. 관련 기존 테스트/assertions도 보존한다. 추가 사용자 UI 진행/상태/알림/옵션 카드도 금지한다.

## 9. PR·커밋 실행 순서 (애드온 우선 → Main 체리픽)

**애드온 구현 PR (1개):** 새 작업 브랜치를 `integration/steamaddon`에서 생성한다.

- **커밋 1:** 모델 발견과 활성 Windows target/IGCL 정확 매핑, 패널별 독립 적용, 다중 패널·기존 정책 회귀 테스트.
- **커밋 2:** `tweaks-vrr.log` 통합, DRR/Intel 로그 흐름 정리, result INI 1회 집계, 로깅/재시도/호환 테스트.
- Windows MSVC Release 빌드·CTest 및 관련 회귀를 통과하고 코드 리뷰 완료 후 **Squash Merge**한다.

**Main 구현 PR (1개):** 애드온 작업 브랜치의 **검증된 최종 구현 커밋 2개**를 `main` 기반 신규 브랜치로 **순서대로 체리픽**한다. 애드온 전체 브랜치 병합 금지, 애드온의 Squash Merge SHA만 가져오는 방식은 기본안이 아니다.

- `main`의 `src/ClawHUD.Diag/{DisplayPathProbe,DiagIntelVrrStateProbe}` 경로와 자체 CMake/테스트 구성을 유지한다.
- LG/내장 적용 동작과 로그 계약은 애드온과 동일하게 구현하되 Probe 타입·빌드 경로가 다르면 그 부분만 체리픽 충돌을 수동으로 적응시킨다.
- Main PR도 **별도 Windows CI·CTest·기존 HUD 불변식 회귀·코드 리뷰** 완료 후 Squash Merge한다.
- 각 브랜치 1개 PR, 권장 각 2개 커밋. 변경 LOC 제한 없음. 실기기 검증은 가능할 때 별도로 실시하고 보고한다.

## 10. 이번 작업에 포함하지 않을 것

- Windows DRR 기능의 재설계, 원복/저널, 최대 주사율 강제 변경, CRU/EDID 수정.
- 타사·알파스캔 패널 등 신규 IGCL VRR 지원 범위 확대. 업로드된 알파스캔 원본 CRU 백업은 **이 작업의 적용 대상이 아니다**.
- 단순 capability 값만으로 모델을 추측해 setter 호출하는 자동 범용 VRR Fix.
- 사용자 설정 토글 변경 즉시 적용, 새 UI, Windows 알림, HUD 프레젠테이션 변경.
- 무조건적인 리트라이 증가, 상시 모니터링 워커, 별도 IPC 프로토콜, 대규모 로깅 아키텍처 개편.

---

**인수 조건:** LG 단독 연결 + WMI 내장 패널 흔적 환경에서 LG가 정상적으로 처리되고, 내장·LG 동시 활성화 환경에서도 잘못된 IGCL 출력에 쓰지 않으며, DRR/Intel 상세 로그가 `tweaks-vrr.log` 하나에 남고, 양쪽 브랜치가 각각 빌드·테스트를 통과해야 한다.
