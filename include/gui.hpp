#pragma once

#include <switch.h>
#include <string>

namespace GUI {
    bool Init(void);
    bool SwapBuffers(void);
    bool Loop(u64 &key);
    void Render(void);
    void Exit(void);
}