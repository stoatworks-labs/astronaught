// Drives Loop::Record past several rebases with nothing but the bookkeeping —
// no GL, no host, no plugin. Build:
//
//   clang++ -std=c++20 -O2 -I source -o /tmp/tapeprobe tools/tapeprobe.cpp source/Tape.cpp
//
// The failure this exists for: Record() is handed the plugin's absolute,
// monotonic position, and Rebase() used to zero `head`. The next pass then
// recorded a span of the entire elapsed run and a start of ~-origin, re-fired
// the rebase, and did so on every pass after it. Every head ended up inside the
// newest, enormous slot at phase ~1, so all three read the bottom of the frame
// just written and the echoes disappeared.
#include "Tape.h"

#include <cmath>
#include <cstdio>

int main()
{
	using namespace astronaught::tape;

	Loop loop;

	// 4 material units per second at the default Repeat Rate, 60 fps.
	constexpr double perFrame = 4.0 / 60.0;
	constexpr int    frames   = 200000;   // ~55 min of show

	int    failures      = 0;
	double worstSpan     = 0.0;
	double previousStart = 0.0;

	const auto fail = [ &failures ]( const char* what, int frame, double a, double b )
	{
		std::printf( "  FAIL frame %d: %s (%.6f vs %.6f)\n", frame, what, a, b );
		++failures;
	};

	for( int f = 1; f <= frames; ++f )
	{
		const double position = f * perFrame;
		const int    slot     = loop.Record( position );

		// A pass covers one frame of tape. Anything wildly larger means the
		// rebase has corrupted the span arithmetic.
		const double span = loop.Spans()[ (size_t) slot ];
		worstSpan         = std::max( worstSpan, span );

		if( span > perFrame * 4.0 )
		{
			if( failures < 5 )
				fail( "pass span is far larger than one frame", f, span, perFrame );
		}

		// The head must stay a bounded distance past the origin: that is what
		// the rebase is for.
		const double past = loop.Position() - loop.Origin();

		if( !( past >= -1e-9 ) || past > 4096.0 )
		{
			if( failures < 5 )
				fail( "head has run away from the origin", f, past, 0.0 );
		}

		// Resolve a head one unit back — an echo — and check it lands somewhere
		// sensible rather than pinned at the end of a kilometre-long slot.
		if( f > 2000 && ( f % 5000 ) == 0 )
		{
			const Read r = loop.Resolve( position - 1.0, perFrame );

			if( !r.valid )
				fail( "an echo one unit back resolved to nothing", f, 0.0, 0.0 );
			else if( r.phase > 0.999f && r.slot == slot )
				fail( "the echo collapsed onto the newest slot", f, r.phase, 0.0 );
		}

		previousStart = position;
	}

	(void) previousStart;

	std::printf( "  worst pass span %.6f (one frame is %.6f)\n", worstSpan, perFrame );
	std::printf( "  final head-past-origin %.6f\n", loop.Position() - loop.Origin() );
	std::printf( failures == 0 ? "\nOK\n" : "\n%d FAILURES\n", failures );
	return failures == 0 ? 0 : 1;
}
