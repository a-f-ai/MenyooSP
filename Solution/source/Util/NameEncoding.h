/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Undoing the name inflation that builds before the UTF-8 fix produced.
*
* Kept free of game headers on purpose: this is the part that has to be right,
* and it can be tested on any machine without a copy of GTA.
*/
#pragma once

#include <string>

namespace ige::NameEncoding
{
	bool IsValidUtf8(const std::string& text);

	// Undoes the doubling a latin1 round applied. Returns false when `text` is
	// not inflated - which includes correct text, because peeling lands on
	// well-formed UTF-8 only for real damage. "Café" is byte-identical to one
	// damaged 0xE9 and is left alone for exactly that reason.
	bool Unwind(const std::string& text, std::string& out, int& rounds);
}
