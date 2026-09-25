#pragma once

#include <cstddef>
#include <cstdint>

class SaveRangeRefreshState
{
	bool m_initialized = false;
	bool m_wasSelected = false;
	std::uint32_t m_lastFrame = 0;
	float m_radius = 0.0f;
	std::size_t m_entityPopulation = 0;

public:
	bool ShouldRefresh(std::uint32_t frame, bool selected, float radius, std::size_t entityPopulation)
	{
		const bool enteredMenu = !m_initialized || frame != m_lastFrame + 1U;
		const bool selectionEntered = selected && !m_wasSelected;
		const bool radiusChanged = m_initialized && radius != m_radius;
		const bool populationChanged = m_initialized && entityPopulation != m_entityPopulation;

		m_initialized = true;
		m_wasSelected = selected;
		m_lastFrame = frame;
		m_radius = radius;
		m_entityPopulation = entityPopulation;
		return enteredMenu || selectionEntered || radiusChanged || populationChanged;
	}
};
