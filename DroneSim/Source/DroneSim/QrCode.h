#pragma once

// Minimal QR code generator: byte mode, error correction level L, versions 1-5
// (up to 106 bytes), fixed mask pattern 0. Enough for a short URL.
// Deliberately plain standard C++ so it can be unit-tested outside Unreal.

#include <cstdint>
#include <string>
#include <vector>

namespace DroneQr
{
	struct FQrMatrix
	{
		int Size = 0;                  // modules per side, 0 if encoding failed
		std::vector<uint8_t> Modules;  // Size*Size, 1 = dark

		bool IsDark(int X, int Y) const { return Modules[Y * Size + X] != 0; }
	};

	FQrMatrix Encode(const std::string& Text);
}
