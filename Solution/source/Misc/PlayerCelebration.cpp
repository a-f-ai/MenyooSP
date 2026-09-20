#include "PlayerCelebration.h"
#include "../Menu/Routine.h"
#include "../Natives/natives2.h"
#include "../Scripting/Game.h"
#include "../Scripting/enums.h"
#include "../Submenus/Spooner/SpoonerMode.h"
#include "../Util/FileLogger.h"
#include <cmath>
#include <cstdint>
#include <string>

namespace PlayerCelebration
{
    namespace
    {
        constexpr const char* dictionary = "anim@arena@celeb@podium@no_prop@";
        constexpr const char* clip = "cheer_a_1st";
        enum class Phase { Idle, Loading, Starting, Playing, Stopping };
        struct State
        {
            Phase phase = Phase::Idle;
            Ped ped = 0;
            Hash model = 0;
            std::uint32_t startedAt = 0;
        } state;

        void Notify(const std::string& message, bool error = false)
        {
            addlog(error ? ige::LogType::LOG_ERROR : ige::LogType::LOG_INFO,
                "Celebration: " + message + " [" + dictionary + " / " + clip + "]");
            Game::Print::PrintBottomLeft("Celebration " + message);
        }

        void Release()
        {
            REMOVE_ANIM_DICT(dictionary);
            state = State{};
        }

        bool IsPlaying()
        {
            return IS_ENTITY_PLAYING_ANIM(state.ped, dictionary, clip, 3) != 0;
        }

        bool CanStart(Ped ped)
        {
            if (!DOES_ENTITY_EXIST(ped) || IS_ENTITY_DEAD(ped, false))
            {
                Notify("cannot start: player is unavailable or dead", true);
                return false;
            }
            if (IS_PED_IN_ANY_VEHICLE(ped, true) || IS_PED_GETTING_INTO_A_VEHICLE(ped))
            {
                Notify("cannot start while in or entering a vehicle", true);
                return false;
            }
            if (noClipToggle || sub::Spooner::SpoonerMode::bEnabled ||
                !IS_PLAYER_CONTROL_ON(PLAYER_ID()) || IS_ENTITY_ATTACHED(ped) ||
                IS_PED_RAGDOLL(ped) || IS_PED_FALLING(ped) || IS_ENTITY_IN_AIR(ped) ||
                IS_PED_SWIMMING(ped) || IS_PED_CLIMBING(ped))
            {
                Notify("cannot start: player must be standing and controllable on foot", true);
                return false;
            }
            return true;
        }

        void Stop(std::uint32_t now)
        {
            STOP_ANIM_TASK(state.ped, dictionary, clip, -4.0f);
            state.phase = Phase::Stopping;
            state.startedAt = now;
        }
    }

    void Tick(bool toggle)
    {
        if (state.phase == Phase::Idle && !toggle) return;
        const auto now = static_cast<std::uint32_t>(GET_GAME_TIMER());
        const Ped player = PLAYER_PED_ID();
        if (state.phase != Phase::Idle &&
            (player != state.ped || !DOES_ENTITY_EXIST(state.ped) || GET_ENTITY_MODEL(state.ped) != state.model))
        {
            if (DOES_ENTITY_EXIST(state.ped) && GET_ENTITY_MODEL(state.ped) == state.model &&
                state.phase != Phase::Loading)
                STOP_ANIM_TASK(state.ped, dictionary, clip, -4.0f);
            Release();
            Notify("OFF: player ped changed");
            return;
        }

        if (toggle)
        {
            if (state.phase == Phase::Loading)
            {
                Release();
                Notify("OFF: start cancelled");
                return;
            }
            if (state.phase == Phase::Stopping)
            {
                Notify("stop is still pending", true);
                return;
            }
            if (state.phase == Phase::Playing || state.phase == Phase::Starting)
            {
                Stop(now);
                return;
            }
            if (!CanStart(player)) return;
            if (!DOES_ANIM_DICT_EXIST(dictionary))
            {
                Notify("cannot start: animation dictionary is not installed", true);
                return;
            }
            if (IS_ENTITY_PLAYING_ANIM(player, dictionary, clip, 3))
            {
                Notify("cannot take ownership of an already playing celebration", true);
                return;
            }
            state = { Phase::Loading, player, GET_ENTITY_MODEL(player), now };
            REQUEST_ANIM_DICT(dictionary);
        }

        if (state.phase == Phase::Loading)
        {
            if (!HAS_ANIM_DICT_LOADED(dictionary))
            {
                if (now - state.startedAt >= 3000)
                {
                    Release();
                    Notify("cannot start: dictionary streaming timed out", true);
                }
                return;
            }
            if (!CanStart(state.ped))
            {
                Release();
                return;
            }
            const float duration = GET_ANIM_DURATION(dictionary, clip);
            if (!std::isfinite(duration) || duration <= 0)
            {
                Release();
                Notify("cannot start: animation clip has no valid duration", true);
                return;
            }
            TASK_PLAY_ANIM(state.ped, dictionary, clip, 4.0f, -4.0f, -1,
                AnimFlag::Loop | AnimFlag::NotInterruptable, 1.0f, false, false, false);
            state.phase = Phase::Starting;
            state.startedAt = now;
            return;
        }
        if (state.phase == Phase::Starting)
        {
            if (IsPlaying())
            {
                state.phase = Phase::Playing;
                Notify("ON - Ctrl+Shift+J to stop");
                return;
            }
            if (now - state.startedAt >= 1000)
            {
                Notify("start failed: native playback was not confirmed", true);
                Stop(now);
            }
            return;
        }
        if (state.phase == Phase::Stopping)
        {
            if (!IsPlaying())
            {
                Release();
                Notify("OFF");
                return;
            }
            if (now - state.startedAt >= 1000)
            {
                state.phase = Phase::Playing;
                Notify("stop failed: animation is still ON", true);
            }
            return;
        }
        if (state.phase == Phase::Playing && !IsPlaying())
        {
            Release();
            Notify("OFF: animation was interrupted", true);
        }
    }

    void ResetOnUnload()
    {
        state = State{};
    }
}
