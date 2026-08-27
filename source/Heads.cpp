#include "Heads.h"

#include <algorithm>

namespace astronaught
{
namespace heads
{
//---------------------------------------------------------------------------
// The Mode Selector, transcribed from the printed table rather than derived.
//
//   MODE        1  2  3  4  5  6  7  8  9 10 11 12
//   head 1      *           *        *     *  *
//   head 2         *     *     *     *  *     *
//   head 3            *  *        *     *  *  *
//   REVERB                  *  *  *  *  *  *  *  *
//
// Read it down the columns, not across: position 4 is heads 2+3 with no
// reverb, position 10 is heads 1+3 with reverb, and position 11 is the whole
// block plus the tank. There is no position at all for heads 1+2 dry, or for
// all three dry -- the machine simply does not offer them.
//---------------------------------------------------------------------------
const Mode kModes[ kModeCount ] = {
	//                     h1     h2     h3     reverb
	{ "1  Head 1",      { true,  false, false }, false },
	{ "2  Head 2",      { false, true,  false }, false },
	{ "3  Head 3",      { false, false, true  }, false },
	{ "4  Heads 2+3",   { false, true,  true  }, false },
	{ "5  H1 + Rev",    { true,  false, false }, true },
	{ "6  H2 + Rev",    { false, true,  false }, true },
	{ "7  H3 + Rev",    { false, false, true  }, true },
	{ "8  H1+2 + Rev",  { true,  true,  false }, true },
	{ "9  H2+3 + Rev",  { false, true,  true  }, true },
	{ "10 H1+3 + Rev",  { true,  false, true  }, true },
	{ "11 All + Rev",   { true,  true,  true  }, true },
	{ "12 Reverb Only", { false, false, false }, true },
};

const Mode& mode( int index )
{
	return kModes[ std::clamp( index, 0, kModeCount - 1 ) ];
}

bool anyHead( int index )
{
	const Mode& m = mode( index );
	for( int i = 0; i < kHeadCount; ++i )
	{
		if( m.head[ i ] )
			return true;
	}

	return false;
}

int headCount( int index )
{
	const Mode& m = mode( index );
	int n         = 0;
	for( int i = 0; i < kHeadCount; ++i )
		n += m.head[ i ] ? 1 : 0;

	return n;
}

} // namespace heads
} // namespace astronaught
