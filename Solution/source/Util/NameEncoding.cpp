/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "NameEncoding.h"

namespace ige::NameEncoding
{
	namespace
	{
		constexpr int kMaxRounds = 64;

		// One layer off. Every byte must decode from a two-byte UTF-8 sequence
		// whose code point fits in a byte, because that is exactly what the
		// latin1 round produced; anything else means this is not inflated.
		bool UnwindOnce(const std::string& in, std::string& out)
		{
			out.clear();
			out.reserve(in.size() / 2 + 1);

			for (size_t i = 0; i < in.size();)
			{
				const unsigned char lead = static_cast<unsigned char>(in[i]);
				if (lead < 0x80)
				{
					out += static_cast<char>(lead);
					++i;
					continue;
				}
				if ((lead & 0xE0) != 0xC0 || i + 1 >= in.size())
					return false;

				const unsigned char trail = static_cast<unsigned char>(in[i + 1]);
				if ((trail & 0xC0) != 0x80)
					return false;

				const unsigned code = ((lead & 0x1Fu) << 6) | (trail & 0x3Fu);
				if (code > 0xFF)
					return false;   // a genuine character, not a doubled byte

				out += static_cast<char>(code);
				i += 2;
			}
			return out != in;
		}
	}

	bool IsValidUtf8(const std::string& text)
	{
		for (size_t i = 0; i < text.size();)
		{
			const unsigned char lead = static_cast<unsigned char>(text[i]);
			int extra;
			if (lead < 0x80)                { ++i; continue; }
			else if ((lead & 0xE0) == 0xC0) extra = 1;
			else if ((lead & 0xF0) == 0xE0) extra = 2;
			else if ((lead & 0xF8) == 0xF0) extra = 3;
			else return false;

			if (i + extra >= text.size())
				return false;
			for (int k = 1; k <= extra; ++k)
			{
				if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
					return false;
			}
			i += extra + 1;
		}
		return true;
	}

	bool Unwind(const std::string& text, std::string& out, int& rounds)
	{
		std::string current = text;
		std::string next;
		std::string best;
		int bestRounds = 0;

		for (int round = 1; round <= kMaxRounds; ++round)
		{
			if (!UnwindOnce(current, next))
				break;
			current.swap(next);
			if (IsValidUtf8(current))
			{
				best = current;
				bestRounds = round;
			}
		}

		if (bestRounds == 0)
			return false;
		out = best;
		rounds = bestRounds;
		return true;
	}
}
