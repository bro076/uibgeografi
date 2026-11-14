#include "QrCode.h"

namespace DroneQr
{
namespace
{
	// Data / error-correction codewords for versions 1-5 at level L (all single block).
	const int DataCodewords[6] = { 0, 19, 34, 55, 80, 108 };
	const int EccCodewords[6] = { 0, 7, 10, 15, 20, 26 };

	uint8_t GfMul(uint8_t A, uint8_t B)
	{
		int Z = 0;
		for (int i = 7; i >= 0; --i)
		{
			Z = (Z << 1) ^ ((Z >> 7) * 0x11D);
			Z ^= ((B >> i) & 1) * A;
		}
		return static_cast<uint8_t>(Z);
	}

	std::vector<uint8_t> ReedSolomon(const std::vector<uint8_t>& Data, int Degree)
	{
		// Generator polynomial with roots a^0 .. a^(Degree-1)
		std::vector<uint8_t> Gen(Degree, 0);
		Gen[Degree - 1] = 1;
		uint8_t Root = 1;
		for (int i = 0; i < Degree; ++i)
		{
			for (int j = 0; j < Degree; ++j)
			{
				Gen[j] = GfMul(Gen[j], Root);
				if (j + 1 < Degree)
				{
					Gen[j] ^= Gen[j + 1];
				}
			}
			Root = GfMul(Root, 0x02);
		}

		std::vector<uint8_t> Rem(Degree, 0);
		for (uint8_t Byte : Data)
		{
			const uint8_t Factor = Byte ^ Rem[0];
			Rem.erase(Rem.begin());
			Rem.push_back(0);
			for (int j = 0; j < Degree; ++j)
			{
				Rem[j] ^= GfMul(Gen[j], Factor);
			}
		}
		return Rem;
	}

	struct FBuilder
	{
		int Size;
		std::vector<uint8_t> Mod;
		std::vector<uint8_t> Func;

		explicit FBuilder(int InSize) : Size(InSize), Mod(InSize * InSize, 0), Func(InSize * InSize, 0) {}

		void Set(int X, int Y, bool bDark)
		{
			if (X < 0 || Y < 0 || X >= Size || Y >= Size)
			{
				return;
			}
			Mod[Y * Size + X] = bDark ? 1 : 0;
			Func[Y * Size + X] = 1;
		}

		void Finder(int Cx, int Cy)
		{
			for (int Dy = -4; Dy <= 4; ++Dy)
			{
				for (int Dx = -4; Dx <= 4; ++Dx)
				{
					const int Ax = Dx < 0 ? -Dx : Dx;
					const int Ay = Dy < 0 ? -Dy : Dy;
					const int Dist = Ax > Ay ? Ax : Ay;
					Set(Cx + Dx, Cy + Dy, Dist != 2 && Dist != 4);
				}
			}
		}

		void Alignment(int Cx, int Cy)
		{
			for (int Dy = -2; Dy <= 2; ++Dy)
			{
				for (int Dx = -2; Dx <= 2; ++Dx)
				{
					const int Ax = Dx < 0 ? -Dx : Dx;
					const int Ay = Dy < 0 ? -Dy : Dy;
					const int Dist = Ax > Ay ? Ax : Ay;
					Set(Cx + Dx, Cy + Dy, Dist != 1);
				}
			}
		}
	};
}

FQrMatrix Encode(const std::string& Text)
{
	FQrMatrix Result;

	int Version = 0;
	for (int V = 1; V <= 5; ++V)
	{
		if (static_cast<int>(Text.size()) + 2 <= DataCodewords[V])
		{
			Version = V;
			break;
		}
	}
	if (Version == 0)
	{
		return Result;
	}

	// --- Data bit stream: mode (4) + length (8) + bytes + terminator + padding
	const int Capacity = DataCodewords[Version];
	std::vector<uint8_t> Bits;
	auto Append = [&Bits](unsigned Value, int Count)
	{
		for (int i = Count - 1; i >= 0; --i)
		{
			Bits.push_back(static_cast<uint8_t>((Value >> i) & 1));
		}
	};
	Append(0x4, 4);
	Append(static_cast<unsigned>(Text.size()), 8);
	for (unsigned char Ch : Text)
	{
		Append(Ch, 8);
	}
	for (int i = 0; i < 4 && static_cast<int>(Bits.size()) < Capacity * 8; ++i)
	{
		Bits.push_back(0);
	}
	while (Bits.size() % 8 != 0)
	{
		Bits.push_back(0);
	}
	for (unsigned Pad = 0xEC; static_cast<int>(Bits.size()) < Capacity * 8; Pad ^= 0xEC ^ 0x11)
	{
		Append(Pad, 8);
	}

	std::vector<uint8_t> Codewords(Capacity, 0);
	for (size_t i = 0; i < Bits.size(); ++i)
	{
		Codewords[i >> 3] = static_cast<uint8_t>(Codewords[i >> 3] | (Bits[i] << (7 - (i & 7))));
	}
	const std::vector<uint8_t> Ecc = ReedSolomon(Codewords, EccCodewords[Version]);
	Codewords.insert(Codewords.end(), Ecc.begin(), Ecc.end());

	// --- Function patterns
	const int Size = Version * 4 + 17;
	FBuilder B(Size);

	for (int i = 0; i < Size; ++i)
	{
		B.Set(6, i, i % 2 == 0);
		B.Set(i, 6, i % 2 == 0);
	}
	B.Finder(3, 3);
	B.Finder(Size - 4, 3);
	B.Finder(3, Size - 4);
	if (Version >= 2)
	{
		B.Alignment(Size - 7, Size - 7);
	}

	// Format information: level L, mask 0  ->  0x77C4 (BCH encoded and masked)
	const unsigned Format = 0x77C4;
	auto FormatBit = [Format](int i) { return ((Format >> i) & 1) != 0; };
	for (int i = 0; i <= 5; ++i)
	{
		B.Set(8, i, FormatBit(i));
	}
	B.Set(8, 7, FormatBit(6));
	B.Set(8, 8, FormatBit(7));
	B.Set(7, 8, FormatBit(8));
	for (int i = 9; i < 15; ++i)
	{
		B.Set(14 - i, 8, FormatBit(i));
	}
	for (int i = 0; i < 8; ++i)
	{
		B.Set(Size - 1 - i, 8, FormatBit(i));
	}
	for (int i = 8; i < 15; ++i)
	{
		B.Set(8, Size - 15 + i, FormatBit(i));
	}
	B.Set(8, Size - 8, true);

	// --- Zig-zag data placement with mask 0 ((x + y) % 2 == 0)
	const int TotalBits = static_cast<int>(Codewords.size()) * 8;
	int BitIndex = 0;
	for (int Right = Size - 1; Right >= 1; Right -= 2)
	{
		if (Right == 6)
		{
			Right = 5;
		}
		for (int Vert = 0; Vert < Size; ++Vert)
		{
			for (int j = 0; j < 2; ++j)
			{
				const int X = Right - j;
				const bool bUpward = ((Right + 1) & 2) == 0;
				const int Y = bUpward ? Size - 1 - Vert : Vert;
				if (B.Func[Y * Size + X])
				{
					continue;
				}
				bool bDark = false;
				if (BitIndex < TotalBits)
				{
					bDark = ((Codewords[BitIndex >> 3] >> (7 - (BitIndex & 7))) & 1) != 0;
					++BitIndex;
				}
				if ((X + Y) % 2 == 0)
				{
					bDark = !bDark;
				}
				B.Mod[Y * Size + X] = bDark ? 1 : 0;
			}
		}
	}

	Result.Size = Size;
	Result.Modules = B.Mod;
	return Result;
}
}
