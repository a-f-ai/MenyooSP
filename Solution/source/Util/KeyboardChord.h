#pragma once
#include <array>
#include <utility>
class KeyboardChord
{
    std::array<bool,256> m_down{};
    bool m_armed=false,m_pending=false;
public:
    bool Control() const { return m_down[17] || m_down[162] || m_down[163]; }
    bool Shift() const { return m_down[16] || m_down[160] || m_down[161]; }
    bool Alt() const { return m_down[18] || m_down[164] || m_down[165]; }
    bool Armed() const { return m_armed; }
    void Event(unsigned key,bool up,bool repeat,unsigned binding,
        bool control=true,bool shift=true,bool alt=false)
    {
        if(key>=m_down.size()) return;
        m_down[key]=!up;
        if(key!=binding) return;
        if(!up && !repeat) m_armed=Control()==control && Shift()==shift && Alt()==alt;
        if(up)
        {
            m_pending=m_pending || m_armed;
            m_armed=false;
        }
    }
    template<class ConsumeKey> bool Consume(ConsumeKey consumeKey)
    {
        if(!std::exchange(m_pending,false)) return false;
        consumeKey();
        return true;
    }
};
