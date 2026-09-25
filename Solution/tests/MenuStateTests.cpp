#include "MenuState.h"
#include <iostream>

int main()
{
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::cout << (ok ? "ok " : "FAIL ") << name << '\n';
        if (!ok) ++failures;
    };
    std::uint16_t active = 0, lastOpened = 1;
    check(!CloseMenuState(active, lastOpened, 0), "API map load with menu closed does not close it again");
    active = lastOpened;
    check(active == 1, "F8 after closed-menu map load opens main menu");
    active = 42;
    check(CloseMenuState(active, lastOpened, 0) && active == 0 && lastOpened == 42,
        "closing an open submenu remembers it");
    check(!CloseMenuState(active, lastOpened, 0), "repeated close is idempotent");
    active = lastOpened;
    check(active == 42, "F8 after repeated close restores actual last submenu");
    return failures ? 1 : 0;
}
