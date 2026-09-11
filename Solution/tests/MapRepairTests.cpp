/*
* Tests for undoing the name inflation that the old ISO-8859-1 declaration
* caused. The interesting property is not that it repairs damage, but that it
* leaves correct text alone without needing a threshold to decide.
*/
#include "MapRepair.h"

#include <cstdio>
#include <string>

using namespace sub::Spooner::MapRepair;

namespace
{
	int g_failures = 0;

	void Check(bool condition, const std::string& what)
	{
		if (condition)
		{
			std::printf("  ok   %s\n", what.c_str());
			return;
		}
		std::printf("  FAIL %s\n", what.c_str());
		++g_failures;
	}

	// What the bug did: every byte reinterpreted as latin1 and re-encoded.
	std::string InflateOnce(const std::string& text)
	{
		std::string out;
		for (unsigned char byte : text)
		{
			if (byte < 0x80)
			{
				out += static_cast<char>(byte);
			}
			else
			{
				out += static_cast<char>(0xC0 | (byte >> 6));
				out += static_cast<char>(0x80 | (byte & 0x3F));
			}
		}
		return out;
	}

	const std::string kCyrillic = "\xD0\x94\xD0\xB5\xD1\x82\xD1\x81\xD0\xBA\xD0\xB0\xD1\x8F"
	                              " \xD0\xBC\xD0\xB0\xD1\x88\xD0\xB8\xD0\xBD\xD0\xBA\xD0\xB0";

	void InflationIsUndoneExactly()
	{
		std::printf("inflated names unwind to exactly what they were\n");

		std::string damaged = kCyrillic;
		for (int round = 0; round < 11; ++round)
			damaged = InflateOnce(damaged);
		Check(damaged.size() > 50000,
			std::string("eleven rounds really is enormous: ") + std::to_string(damaged.size()) + " bytes");

		std::string recovered;
		int rounds = 0;
		Check(Unwind(damaged, recovered, rounds), "the damage is recognised");
		Check(rounds == 11, std::string("it took exactly eleven rounds back, got ") + std::to_string(rounds));
		Check(recovered == kCyrillic, "and lands on the original text");
	}

	// The safety property. A real Cyrillic letter is a code point above 0xFF,
	// which no latin1 round could have produced, so there is nothing to undo
	// and no length threshold is needed to know that.
	void CorrectTextIsLeftAlone()
	{
		std::printf("correctly encoded text is not touched\n");

		std::string out;
		int rounds = 0;
		Check(!Unwind(kCyrillic, out, rounds), "a real Cyrillic name does not unwind");
		Check(!Unwind("prop_bench_01a", out, rounds), "a plain ASCII name does not unwind");
		Check(!Unwind("", out, rounds), "an empty name does not unwind");
		Check(!Unwind("Bati 801RR", out, rounds), "a name with spaces does not unwind");

		// Emoji and CJK are three and four byte sequences: also untouchable.
		Check(!Unwind("\xE6\x97\xA5\xE6\x9C\xAC", out, rounds), "CJK does not unwind");
		Check(!Unwind("\xF0\x9F\x9A\x97", out, rounds), "an emoji does not unwind");
	}

	void OneRoundIsStillCaught()
	{
		std::printf("a single round of damage is caught too\n");
		const std::string damaged = InflateOnce(kCyrillic);
		std::string recovered;
		int rounds = 0;
		Check(Unwind(damaged, recovered, rounds), "one round is recognised");
		Check(rounds == 1 && recovered == kCyrillic, "and reverses cleanly");
	}

	void LatinOneAccentsSurviveARoundTrip()
	{
		std::printf("accented latin text behaves the same way\n");
		const std::string french = "Caf\xC3\xA9 Rouge";          // Café in UTF-8
		std::string out;
		int rounds = 0;
		Check(!Unwind(french, out, rounds), "correct accented text is left alone");

		const std::string damaged = InflateOnce(french);
		Check(Unwind(damaged, out, rounds) && out == french, "damaged accented text is recovered");
	}
}

int main()
{
	InflationIsUndoneExactly();
	CorrectTextIsLeftAlone();
	OneRoundIsStillCaught();
	LatinOneAccentsSurviveARoundTrip();

	if (g_failures == 0)
	{
		std::printf("\nall checks passed\n");
		return 0;
	}
	std::printf("\n%d check(s) failed\n", g_failures);
	return 1;
}
