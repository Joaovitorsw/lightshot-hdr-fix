#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include "color.h"

namespace lightshot_hdr {
struct DesktopFrame {
    int width = 0, height = 0;
    std::vector<Bgra8> pixels;
    std::string report;
};
DesktopFrame CaptureDesktop(RECT area);
}
