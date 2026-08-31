#include <windows.h>

#include "DiagnosticSession.h"

#include <iostream>

int main()
{
    // All window / monitor geometry must be captured in one physical coordinate
    // space; without this the diagnostic mixes DPI-virtualized and real pixels.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        std::cerr << "Warning: failed to enable Per-Monitor-V2 DPI awareness, error="
                  << GetLastError() << '\n';

    DiagnosticSession session;
    for (;;)
    {
        std::cout << "\nClawHUD Game Detection Diagnostic\n\n"
                  << "1. Start capture\n2. Stop capture\n3. Show session status\n4. Exit\n\nSelect: ";
        int command{};
        if (!(std::cin >> command)) break;
        if (command == 1)
            std::cout << (session.Start() ? "Capture started: " + session.LogPath().string() : "Capture is already running or could not start.") << '\n';
        else if (command == 2) { session.Stop(); std::cout << "Capture stopped.\n"; }
        else if (command == 3) std::cout << (session.Running() ? "Capture is running: " : "Capture is stopped.") << session.LogPath().string() << '\n';
        else if (command == 4) break;
        else std::cout << "Unknown command.\n";
    }
    session.Stop();
    return 0;
}
