// SPDX-License-Identifier: MIT
#pragma once

#include <objbase.h>
#include <ole2.h>
#include <windows.h>

namespace ui::win32
{

// Lets WinRT and free-threaded COM run on the calling thread without choosing its apartment: the
// process's multithreaded apartment is kept alive, and a thread that is in no apartment joins it
// implicitly. The UI thread then stays free for the single-threaded apartment that OLE's drag and
// drop needs (enter_ole_apartment()). A thread already in either apartment keeps it.
inline void keep_mta_alive()
{
    static const bool kept = []
    {
        CO_MTA_USAGE_COOKIE cookie{};
        return SUCCEEDED(CoIncrementMTAUsage(&cookie)); // held for the life of the process
    }();
    (void)kept;
}

// Puts the calling thread in OLE's single-threaded apartment the first time it asks. False when
// the app had already put the thread in the multithreaded apartment, where OLE cannot run.
inline bool enter_ole_apartment()
{
    thread_local const bool entered = SUCCEEDED(OleInitialize(nullptr));
    return entered;
}

} // namespace ui::win32
