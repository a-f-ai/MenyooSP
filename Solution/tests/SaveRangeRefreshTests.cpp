#include "SaveRangeRefresh.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	void Check(bool condition, const std::string& message)
	{
		if (condition) return;
		std::cerr << "FAIL: " << message << '\n';
		std::exit(1);
	}
}

int main()
{
	SaveRangeRefreshState state;

	Check(state.ShouldRefresh(100, false, 5.0f, 80), "entering the menu refreshes the count");
	Check(!state.ShouldRefresh(101, false, 5.0f, 80), "an unchanged menu frame does not refresh");
	Check(state.ShouldRefresh(102, true, 5.0f, 80), "selecting Save Range refreshes the count");
	Check(!state.ShouldRefresh(103, true, 5.0f, 80), "holding selection does not refresh every frame");
	Check(state.ShouldRefresh(104, true, 6.0f, 80), "changing the radius refreshes the count");
	Check(state.ShouldRefresh(105, true, 6.0f, 81), "changing the entity population refreshes the count");
	Check(state.ShouldRefresh(110, false, 6.0f, 81), "re-entering after a frame gap refreshes the count");

	std::cout << "SaveRangeRefreshTests: OK\n";
}
