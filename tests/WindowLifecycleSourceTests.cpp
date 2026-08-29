#include "GameDetection/WindowLifecycleSource.h"

#include <cstdlib>
#include <iostream>
#include <vector>

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
    Check(MapWinEvent(EVENT_OBJECT_CREATE) == WindowLifecycleEventType::Create,
        "CREATE mapping");
    Check(MapWinEvent(EVENT_OBJECT_DESTROY) == WindowLifecycleEventType::Destroy,
        "DESTROY mapping");
    Check(MapWinEvent(EVENT_OBJECT_SHOW) == WindowLifecycleEventType::Show,
        "SHOW mapping");
    Check(MapWinEvent(EVENT_OBJECT_HIDE) == WindowLifecycleEventType::Hide,
        "HIDE mapping");
    Check(MapWinEvent(EVENT_OBJECT_NAMECHANGE) == WindowLifecycleEventType::NameChange,
        "NAMECHANGE mapping");
    Check(!MapWinEvent(EVENT_SYSTEM_FOREGROUND), "unknown event is rejected");
    Check(IsWindowLifecycleObject(OBJID_WINDOW, CHILDID_SELF), "window object accepted");
    Check(!IsWindowLifecycleObject(OBJID_CLIENT, CHILDID_SELF), "client object rejected");
    Check(!IsWindowLifecycleObject(OBJID_WINDOW, 1), "child object rejected");

    const std::vector<DWORD> eventOrder{
        EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, EVENT_OBJECT_NAMECHANGE,
        EVENT_OBJECT_HIDE, EVENT_OBJECT_DESTROY};
    const std::vector<WindowLifecycleEventType> expectedOrder{
        WindowLifecycleEventType::Create, WindowLifecycleEventType::Show,
        WindowLifecycleEventType::NameChange, WindowLifecycleEventType::Hide,
        WindowLifecycleEventType::Destroy};
    for (std::size_t index = 0; index < eventOrder.size(); ++index)
        Check(MapWinEvent(eventOrder[index]) == expectedOrder[index], "event order preserved");

    WindowSnapshot first;
    first.hwnd = reinterpret_cast<HWND>(0x1234);
    first.processId = 100;
    first.windowThreadId = 200;
    first.title = L"Game \"Test\"\\Path\r\n\t";
    first.className = L"WindowClass";
    first.root = first.hwnd;
    WindowLifecycleCache cache;
    cache.ReplaceOnCreate(first);
    const auto shown = cache.Find(first.hwnd);
    Check(shown && shown->processId == 100 && shown->title == first.title,
        "cached metadata survives SHOW");
    const auto escaped = EscapeWindowLifecycleValue(first.title);
    Check(escaped == L"Game \\\"Test\\\"\\\\Path\\r\\n\\t",
        "window text escaping");
    cache.Remove(first.hwnd);
    Check(!cache.Find(first.hwnd), "DESTROY removes cache entry");

    WindowSnapshot reused = first;
    reused.processId = 200;
    reused.title = L"New window";
    cache.ReplaceOnCreate(reused);
    const auto replacement = cache.Find(reused.hwnd);
    Check(replacement && replacement->processId == 200 &&
        replacement->title == L"New window", "HWND reuse replaces metadata");

    for (std::size_t index = 0; index + 1 < WindowLifecycleCache::kCapacity; ++index)
    {
        WindowSnapshot snapshot;
        snapshot.hwnd = reinterpret_cast<HWND>(0x10000 + index);
        snapshot.processId = static_cast<DWORD>(index);
        cache.ReplaceOnCreate(snapshot);
    }
    WindowSnapshot overCapacity = first;
    overCapacity.hwnd = reinterpret_cast<HWND>(0xFFFF);
    const auto evicted = [&cache, &overCapacity]
    {
        cache.ReplaceOnCreate(overCapacity);
        return cache.EvictIfOverCapacity();
    }();
    Check(evicted.has_value() && cache.Size() <= WindowLifecycleCache::kCapacity,
        "cache overflow is bounded and observable");

    WindowLifecycleQueue queue;
    RawWindowLifecycleEvent event;
    for (std::size_t index = 0; index < WindowLifecycleQueue::kCapacity; ++index)
        Check(queue.TryPush(event), "queue accepts events up to capacity");
    Check(!queue.TryPush(event), "queue rejects overflow without overwrite");
    Check(queue.TakeDroppedCount() == 1, "queue reports dropped event count");
    Check(queue.TryPop().has_value(), "queue preserves FIFO event");

    WindowLifecycleSource source;
    source.Stop();
    source.Stop();
    std::cout << "PASS\n";
}
