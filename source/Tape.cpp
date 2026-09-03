#include "Tape.h"

#include <algorithm>
#include <cmath>

namespace astronaught
{
namespace tape
{
namespace
{
/// Rebase when the record head gets this far past the origin. Well inside where
/// a float stops separating adjacent scanlines, and far enough apart that the
/// rebase itself is rare. See Loop::Origin.
///
/// 4096 was not inside it. `HeadPos`, `SlotStart` and the shader's `local` are
/// float32, and one output scanline is `span / height` material units: at the
/// default speed, 60 fps and 1080 lines that is 6.2e-5, so a float ulp exceeds
/// one scanline above roughly 6.2e-5 * 2^24 ≈ 1040 units — about four minutes.
/// At the slowest Repeat Rate it is ≈260. Past that the phase quantises to
/// several lines and the tear steps instead of rolling, which is the horizontal
/// banding Tape.h predicts. 256 stays inside the resolvable region at every
/// speed the plugin offers, and a rebase is cheap: kSlots float subtractions.
constexpr double kRebaseAt = 256.0;
} // namespace

Loop::Loop()
{
	starts.assign( kSlots, 0.0f );
	spans.assign( kSlots, 0.0f );
}

void Loop::Erase()
{
	std::fill( starts.begin(), starts.end(), 0.0f );
	std::fill( spans.begin(), spans.end(), 0.0f );

	head    = 0.0;
	origin  = 0.0;
	newest  = -1;
	written = 0;
}

void Loop::Rebase()
{
	// Shift every recorded position down by how far the head has run past the
	// origin, and move the origin up to meet it. Only the differences ever
	// matter, so this is exact in the sense that counts: no head lands anywhere
	// different afterwards.
	//
	// `head` stays ABSOLUTE and is deliberately not reset. Record() is handed
	// the plugin's absolute, monotonic position, so a head zeroed here made the
	// very next pass compute `span = position - 0` — the whole ~4096 units back
	// to the start of time instead of one frame — and store its start at
	// `0 - origin`, i.e. ~-4096. The rebase condition then fired again on that
	// pass, and on every pass after it: origin ran away by ~4096 each time,
	// every head landed inside the newest kilometre-long slot at phase ~1, and
	// all three read the bottom of the frame just written. The echoes vanished,
	// replaced by a near-zero-delay copy, permanently — 5 to 68 minutes into a
	// show depending on Repeat Rate, and only a resolution change (which erases
	// the loop) recovered it.
	const double shift = head - origin;

	for( int i = 0; i < kSlots; ++i )
	{
		if( spans[ i ] > 0.0f )
			starts[ i ] = static_cast< float >( starts[ i ] - shift );
	}

	origin = head;
}

int Loop::Record( double position )
{
	const double previous = head;
	head                  = std::max( position, previous );

	const int slot = ( newest + 1 ) % kSlots;

	// The span is the tape this pass covers: from where the last pass ended to
	// where this one does. The first pass has nothing behind it, so it is given
	// the same span it is about to advance by rather than zero -- a zero span is
	// a slot no head can ever land inside, and the first picture would be
	// invisible for one pass of the ring.
	const double span = ( newest < 0 ) ? std::max( 1e-6, head - previous ) : head - previous;

	starts[ slot ] = static_cast< float >( previous - origin );
	spans[ slot ]  = static_cast< float >( std::max( 1e-6, span ) );

	newest  = slot;
	written = std::min( written + 1, kSlots );

	// Distance PAST THE ORIGIN, which is what the shift is measured in — `head`
	// is absolute, so comparing it against zero would rebase once and then
	// never again.
	if( head - origin > kRebaseAt )
		Rebase();

	return slot;
}

double Loop::Oldest() const
{
	if( written == 0 )
		return head;

	// The ring's oldest live slot is the one about to be overwritten.
	const int oldest = ( written < kSlots ) ? ( newest + 1 - written + kSlots ) % kSlots
	                                        : ( newest + 1 ) % kSlots;

	return origin + starts[ oldest ];
}

//= mirrored in Shaders.cpp, kTapeFunctions -- astest --read compares them
Read Loop::Resolve( double q, double span ) const
{
	Read out;

	if( written == 0 || !( span > 0.0 ) )
		return out;

	const float local = static_cast< float >( q - origin );

	// Walk back from the newest slot. The ring is monotonic in position, so this
	// could binary-search; it does not, because the answer is nearly always
	// within a handful of slots of where the last frame's answer was and the
	// GLSL mirror has to do something a fragment shader can afford. Both sides
	// walk, so both sides agree.
	for( int i = 0; i < written; ++i )
	{
		const int slot = ( newest - i + kSlots * 2 ) % kSlots;
		if( spans[ slot ] <= 0.0f )
			continue;

		if( local >= starts[ slot ] )
		{
			// Past the newest slot's end means the head is over tape that has
			// not been recorded yet. That is not the same as finding nothing --
			// it happens every frame for a head whose delay is shorter than one
			// pass of the record head -- so it clamps into the newest slot
			// rather than dropping out.
			out.valid   = true;
			out.slot    = slot;
			out.phase   = std::clamp( ( local - starts[ slot ] ) / spans[ slot ], 0.0f, 1.0f );
			out.stretch = static_cast< float >( span ) / spans[ slot ];
			return out;
		}
	}

	// Older than anything on the tape.
	return out;
}

int strideFor( double needed, double perFrame )
{
	if( !( perFrame > 0.0 ) || !( needed > 0.0 ) )
		return 1;

	// Two slots of headroom: one is being written this frame and one is the
	// slot a head lands *inside*, which has to still be there.
	const double framesNeeded = needed / perFrame;
	const double perSlot      = framesNeeded / std::max( 1.0, static_cast< double >( kSlots - 2 ) );

	// Clamped at the ring length. A degenerate `perFrame` -- the first frame,
	// where no time has passed yet, or a host that stalled -- otherwise produces
	// a stride in the tens of millions, and the record head then never passes
	// again: the tape runs on and nothing is written to it, which looks like the
	// effect switching itself off. Beyond kSlots the ring cannot help anyway, so
	// there is nothing to lose by capping it.
	const int wanted = static_cast< int >( std::ceil( perSlot ) );
	return std::clamp( wanted, 1, kSlots );
}

float reachOf( const heads::Mode& m, float spread )
{
	float reach = 0.0f;
	for( int i = 0; i < heads::kHeadCount; ++i )
	{
		if( m.head[ i ] )
			reach = std::max( reach, heads::kSpacing[ i ] * spread );
	}

	return reach;
}

} // namespace tape
} // namespace astronaught
