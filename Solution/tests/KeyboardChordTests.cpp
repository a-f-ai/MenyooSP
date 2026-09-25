#include "KeyboardChord.h"
#include <iostream>
int main()
{
    int failures=0;
    auto check=[&](bool ok,const char* name) { std::cout<<(ok?"ok ":"FAIL ")<<name<<'\n';if(!ok)++failures; };
    KeyboardChord chord;
    bool legacyF6=true;
    auto poll=[&] { return chord.Consume([&] { legacyF6=false; }); };
    chord.Event(17,false,false,117);chord.Event(16,false,false,117);chord.Event(117,false,false,117);
    chord.Event(17,true,false,117);chord.Event(16,true,false,117);chord.Event(117,true,false,117);
    check(poll(),"Ctrl and Shift released before F6 or tick do not erase the chord");
    check(!legacyF6,"chord consumes the F6 release before legacy BecomePed can see it");
    check(!poll(),"chord fires exactly once");
    legacyF6=true;
    chord.Event(117,false,false,117);chord.Event(117,true,false,117);
    check(!poll() && legacyF6,"plain F6 remains available to BecomePed");
    chord.Event(162,false,false,117);chord.Event(161,false,false,117);chord.Event(117,false,false,117);
    chord.Event(117,false,true,117);chord.Event(117,true,false,117);
    check(poll() && !poll(),"left control and right shift work without repeat dispatch");
    chord.Event(18,false,false,117);chord.Event(117,false,false,117);chord.Event(117,true,false,117);
    check(!poll(),"Alt disqualifies the chord");
    KeyboardChord canonical;
    canonical.Event(17,false,false,79);canonical.Event(16,false,false,79);
    canonical.Event(117,false,false,79);canonical.Event(117,true,false,79);
    bool oldF6=true;
    check(!canonical.Consume([&] { oldF6=false; }) && oldF6,"canonical binding ignores CtrlShiftF6 and preserves legacy F6");
    canonical.Event(79,false,false,79);canonical.Event(16,true,false,79);canonical.Event(17,true,false,79);canonical.Event(79,true,false,79);
    bool oldO=true;
    check(canonical.Consume([&] { oldO=false; }) && !oldO,"CtrlShiftO survives released modifiers and consumes only O");
    KeyboardChord configurable;
    configurable.Event(18,false,false,81,false,false,true);
    configurable.Event(81,false,false,81,false,false,true);
    configurable.Event(81,true,false,81,false,false,true);
    check(configurable.Consume([] {}),"configured Alt+Q chord is accepted");
    configurable.Event(17,false,false,81,false,false,true);
    configurable.Event(81,false,false,81,false,false,true);
    configurable.Event(81,true,false,81,false,false,true);
    check(!configurable.Consume([] {}),"extra modifier rejects configured chord");
    return failures?1:0;
}
