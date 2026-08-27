#include "Spring.h"

#include <algorithm>
#include <cmath>

namespace astronaught
{
namespace spring
{
namespace
{
/// Blur added per second at full dispersion, in fractions of picture height.
///
/// ☠️ Sized from how the blur ACCUMULATES, not from how big one step looks. The
/// tank re-blurs its own contents every frame, so the radii add in quadrature:
/// after n trips the spread is about sqrt(n) times the per-frame radius, not n
/// times it.
///
/// This was 0.020 to begin with, chosen by eye as "a small blur". Per frame at
/// 60 fps that is 0.02 * 1/60 = 0.0003 of the picture height -- a third of a
/// pixel -- and over a two-second tail it reaches sqrt(120) * 0.0003 = 0.004,
/// which is still under half a pixel at 1080. The control was completely
/// inert and `tools/sweep.py` is the only thing in the repo that noticed;
/// every other check passed, because a dead control renders a perfectly good
/// picture.
///
/// Working backwards instead: a two-second tail at 60 fps is 120 trips, and a
/// bloom worth having by the end of it is a few percent of the height. 0.35
/// gives sqrt(120) * 0.35/60 = 0.064, which is a visible bloom that is still
/// small enough per step to read as a blur rather than as four ghosts.
constexpr float kSpreadPerSecond = 0.35f;

/// Input drive. The tank is quieter than the direct path by a good margin --
/// a spring tank driven to the same level as the source reads as a smeared
/// duplicate rather than as an ambience behind it.
constexpr float kDrive = 0.55f;
} // namespace

Coeffs coeffs( const Settings& s, float dt )
{
	Coeffs c;

	if( !s.active || !( dt > 0.0f ) )
		return c;

	// Decay time to a per-frame multiplier. Framed as "how much is left after
	// dt", so the tail lasts the same number of seconds whatever the host's
	// frame rate -- the thing a plain per-frame constant gets wrong, and gets
	// wrong invisibly on the machine it was tuned on.
	const float seconds = kDecayLo * std::pow( kDecayHi / kDecayLo, std::clamp( s.decay, 0.0f, 1.0f ) );

	// -60 dB in `seconds`.
	c.feedback = std::pow( 0.001f, dt / std::max( 1e-3f, seconds ) );

	c.spread = kSpreadPerSecond * std::clamp( s.dispersion, 0.0f, 1.0f ) * dt;
	c.drive  = kDrive;

	// Each leg carries its own share plus what the other two push into it. The
	// sum is normalised so that turning coupling up smears the tank without
	// also making it louder -- those are different controls and only one of them
	// exists.
	float total = 0.0f;
	for( int i = 0; i < kLegs; ++i )
	{
		c.legGain[ i ] = kLegLength[ i ] + kCoupling * ( 1.0f - kLegLength[ i ] );
		total += c.legGain[ i ];
	}

	for( int i = 0; i < kLegs; ++i )
		c.legGain[ i ] /= std::max( 1e-6f, total );

	return c;
}

} // namespace spring
} // namespace astronaught
