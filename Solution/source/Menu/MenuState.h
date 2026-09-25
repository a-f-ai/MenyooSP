#pragma once
#include <cstdint>

inline bool CloseMenuState(std::uint16_t& active, std::uint16_t& lastOpened, std::uint16_t closed)
{
    if (active == closed) return false;
    lastOpened = active;
    active = closed;
    return true;
}
