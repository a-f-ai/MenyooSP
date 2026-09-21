#pragma once

#include <array>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct BooleanHotkeyBinding
{
	unsigned key;
	bool control;
	bool shift;
	bool alt;

	bool operator==(const BooleanHotkeyBinding&) const = default;
};

struct BooleanHotkeyAction
{
	std::string id;
	BooleanHotkeyBinding binding;
	std::function<bool()> get;
	std::function<bool(bool)> set;
};

struct BooleanHotkeyResult
{
	std::string actionId;
	bool success;
	bool enabled;
};

class BooleanHotkeyRegistry
{
	struct Entry
	{
		BooleanHotkeyAction action;
		bool armed = false;
		bool pending = false;
	};

	std::array<bool, 256> m_down{};
	std::vector<Entry> m_entries;

	bool Control() const { return m_down[0x11] || m_down[0xA2] || m_down[0xA3]; }
	bool Shift() const { return m_down[0x10] || m_down[0xA0] || m_down[0xA1]; }
	bool Alt() const { return m_down[0x12] || m_down[0xA4] || m_down[0xA5]; }

public:
	void Register(BooleanHotkeyAction action)
	{
		if (action.id.empty()) throw std::invalid_argument("boolean hotkey action id must not be empty");
		if (action.binding.key == 0 || action.binding.key >= m_down.size())
			throw std::invalid_argument("boolean hotkey key must be between 1 and 255");
		if (!action.get || !action.set)
			throw std::invalid_argument("boolean hotkey action requires both getter and setter");
		for (const Entry& entry : m_entries)
		{
			if (entry.action.id == action.id)
				throw std::logic_error("duplicate boolean hotkey action id: " + action.id);
			if (entry.action.binding == action.binding)
				throw std::logic_error("duplicate boolean hotkey binding for: " + action.id);
		}
		m_entries.push_back({ std::move(action) });
	}

	bool Event(unsigned key, bool up, bool repeat)
	{
		if (key >= m_down.size()) return false;
		m_down[key] = !up;
		bool consumed = false;
		for (Entry& entry : m_entries)
		{
			if (key != entry.action.binding.key) continue;
			if (!up && !repeat)
			{
				const auto& binding = entry.action.binding;
				entry.armed = Control() == binding.control && Shift() == binding.shift && Alt() == binding.alt;
			}
			if (!up) continue;
			entry.pending = entry.pending || entry.armed;
			consumed = consumed || entry.armed;
			entry.armed = false;
		}
		return consumed;
	}

	std::vector<BooleanHotkeyResult> DispatchPending()
	{
		std::vector<BooleanHotkeyResult> results;
		for (Entry& entry : m_entries)
		{
			if (!std::exchange(entry.pending, false)) continue;
			const bool previous = entry.action.get();
			const bool desired = !previous;
			const bool accepted = entry.action.set(desired);
			const bool actual = entry.action.get();
			results.push_back({ entry.action.id, accepted && actual == desired, actual });
		}
		return results;
	}
};
