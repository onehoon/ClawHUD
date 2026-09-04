#include "GameDetection/ProductionGameWindowSource.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}

int main()
{
    using namespace clawhud;
    Check(MapProductionWindowEvent(EVENT_OBJECT_CREATE) ==
        ProductionWindowEventType::Create, "CREATE mapping");
    Check(MapProductionWindowEvent(EVENT_OBJECT_SHOW) ==
        ProductionWindowEventType::Show, "SHOW mapping");
    Check(MapProductionWindowEvent(EVENT_OBJECT_HIDE) ==
        ProductionWindowEventType::Hide, "HIDE mapping");
    Check(MapProductionWindowEvent(EVENT_OBJECT_LOCATIONCHANGE) ==
        ProductionWindowEventType::LocationChange, "LOCATIONCHANGE mapping");
    Check(MapProductionWindowEvent(EVENT_OBJECT_DESTROY) ==
        ProductionWindowEventType::Destroy, "DESTROY mapping");
    Check(MapProductionWindowEvent(EVENT_OBJECT_NAMECHANGE) ==
        ProductionWindowEventType::NameChange, "NAMECHANGE mapping");
    Check(!MapProductionWindowEvent(EVENT_OBJECT_FOCUS), "unsupported event rejected");
    Check(IsProductionWindowObject(OBJID_WINDOW, CHILDID_SELF),
        "window object accepted");
    Check(!IsProductionWindowObject(OBJID_CLIENT, CHILDID_SELF),
        "client object rejected");
    Check(!IsProductionWindowObject(OBJID_WINDOW, 1),
        "child object rejected");

    const auto hwnd = reinterpret_cast<HWND>(0x1234);
    Check(IsProductionTopLevelObservation(hwnd, hwnd),
        "callback-time top-level observation accepted");
    Check(!IsProductionTopLevelObservation(hwnd,
        reinterpret_cast<HWND>(0x5678)), "non-top-level observation rejected");
    Check(IsProductionTopLevelObservation(hwnd, hwnd),
        "top-level evidence remains valid after later window changes");

    ProductionWindowEventQueue queue;
    for (std::size_t index = 1; index <= 3; ++index)
    {
        ProductionWindowEvent event;
        event.sequence = index;
        Check(queue.TryPush(event), "queue accepts events");
    }
    for (std::size_t index = 1; index <= 3; ++index)
    {
        const auto event = queue.TryPop();
        Check(event && event->sequence == index, "queue preserves order");
    }
    for (std::size_t index = 0; index < ProductionWindowEventQueue::kCapacity; ++index)
        Check(queue.TryPush({}), "queue accepts events up to capacity");
    Check(!queue.TryPush({}), "queue rejects overflow without blocking");
    Check(queue.Size() == ProductionWindowEventQueue::kCapacity,
        "queue remains bounded");
    Check(queue.TakeDroppedCount() == 1, "queue reports dropped event");

    ProductionGameWindowSource source;
    Check(!source.Start({}), "empty callback cannot start source");
    source.Stop();
    source.Stop();
    Check(!source.Running(), "repeated stop is safe");
    std::cout << "PASS\n";
}
