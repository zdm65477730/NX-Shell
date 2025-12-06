#pragma once

#include <switch.h>
#include <string>

namespace GUI {
    bool Init(void);
    bool SwapBuffers(void);
    bool Loop(u64 &key);
    void Render(void);
    void Exit(void);

    // Set text editor active state (true = text editor mode, false = normal mode)
    void SetTextEditorActive(bool active);
}