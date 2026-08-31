#pragma once

#include <windows.h>

namespace clawhud
{
// True when a process with this id exists and has not yet exited. PID 0 is
// never alive. Opens the process with
// SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION and treats an open failure
// as not alive.
bool ProcessAlive(DWORD processId);
}
