#pragma once
#include "../Http/CommandQueue.h"
#include <string>

namespace MapEnvironment
{
    inline constexpr const char* Policy = "extrasunny-noon-v1";
    bool IsActive();
    bool Acquire(const std::string& mapPath);
    void Release(const char* reason);
    bool RejectManualChange();
    Http::Response Describe();
}
