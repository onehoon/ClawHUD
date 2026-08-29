#include "GameDetection/ProcessLifecycleSource.h"

#include <iostream>

namespace
{
bool Check(bool condition, const char* name)
{
    if (!condition) std::cerr << "FAIL: " << name << '\n';
    return condition;
}
}

int main()
{
    using namespace clawhud;
    bool ok = true;

    ProcessLifecycleTraceFields startFields;
    startFields.processId = 1200;
    startFields.parentProcessId = 800;
    startFields.sessionId = 1;
    startFields.processName = L"Game\\bootstrap\n.exe";
    startFields.sourceTimestamp = 123456;
    const auto start = MapProcessLifecycleTraceEvent(
        ProcessLifecycleEventType::Start, startFields, 1, 77);
    ok &= Check(start.has_value(), "start event mapping");
    ok &= Check(start && start->processId == 1200 && start->parentProcessId == 800 &&
        start->sessionId == 1 && start->sourceTimestamp == 123456 &&
        start->receivedTickMs == 77 && !start->exitStatus,
        "start fields and absent exit status");

    auto stopFields = startFields;
    stopFields.exitStatus = 5;
    const auto stop = MapProcessLifecycleTraceEvent(
        ProcessLifecycleEventType::Stop, stopFields, 2, 99);
    ok &= Check(stop.has_value() && stop->type == ProcessLifecycleEventType::Stop &&
        stop->exitStatus && *stop->exitStatus == 5 && stop->sequence == 2,
        "stop fields and exit status");

    ProcessLifecycleTraceFields incomplete = startFields;
    incomplete.processName.reset();
    ok &= Check(!MapProcessLifecycleTraceEvent(
        ProcessLifecycleEventType::Start, incomplete, 3, 100),
        "incomplete event rejected");
    stopFields.exitStatus.reset();
    ok &= Check(!MapProcessLifecycleTraceEvent(
        ProcessLifecycleEventType::Stop, stopFields, 4, 101),
        "stop without exit status rejected");

    ok &= Check(EscapeProcessLifecycleValue(L"a\\b\"c\r\n\t") ==
        L"a\\\\b\\\"c\\r\\n\\t", "event value escaping");

    ProcessLifecycleSource source;
    source.Stop();
    source.Stop();
    source.Start();
    source.Stop();
    source.Stop();

    return ok ? 0 : 1;
}
