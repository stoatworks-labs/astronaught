#include "Controls.h"

#include "Tape.h"

#include <algorithm>
#include <cmath>

namespace astronaught
{
namespace controls
{
namespace
{
/// Wow and flutter rates, in Hz, at the ends of their controls. The wow band
/// stops below where a picture is long, so it reads as a lean; the flutter band
/// starts above it and runs to where it becomes texture.
constexpr float kWowRateLo     = 0.15f;
constexpr float kWowRateHi     = 3.0f;
constexpr float kFlutterRateLo = 3.0f;
constexpr float kFlutterRateHi = 34.0f;

/// Peak fractional speed error at full deflection of the master trim. Six
/// percent is two orders of magnitude worse than a serviced RE-201 and about
/// where a picture stops reading as unsteady and starts reading as broken,
/// which is where the top of an effect's control belongs.
constexpr float kFlutterDepth = 0.06f;

float dbToGain( float db )
{
	return std::pow( 10.0f, db / 20.0f );
}
} // namespace

float expRange( float t, float lo, float hi )
{
	const float u = std::clamp( t, 0.0f, 1.0f );
	return lo * std::pow( hi / lo, u );
}

int option( float value, int elementCount )
{
	if( elementCount <= 0 )
		return 0;

	return std::clamp( static_cast< int >( std::lround( value ) ), 0, elementCount - 1 );
}

Render render( const HostValues& host )
{
	Render r;

	//---------------------------------------------------------------------
	// The selector. Everything downstream reads the pattern rather than the
	// position, so nothing else in the plugin knows that twelve is a number
	// with any meaning.
	//---------------------------------------------------------------------
	r.mode     = option( host.mode, heads::kModeCount );
	r.selector = heads::mode( r.mode );
	r.spread   = expRange( std::clamp( host.headSpread, 0.0f, 1.0f ), kSpreadLo, kSpreadHi );

	//---------------------------------------------------------------------
	// Speed. The control is a tape speed and the delay is its reciprocal, so
	// this converts a delay range and inverts it -- see Controls.h. Written as
	// a delay rather than as a speed because the delay is the number a person
	// can check against Roland's spec, and the speed is not.
	//---------------------------------------------------------------------
	r.baseDelay = expRange( std::clamp( host.repeatRate, 0.0f, 1.0f ), kDelaySlow, kDelayFast );

	transport::Settings& t = r.transport;

	// One material unit is one second of tape at nominal speed, and head 1 is
	// one unit down the path, so speed and head-1 delay are exact reciprocals.
	// Spread moves the heads, which changes the delay without touching the
	// speed -- and therefore without changing the pitch of anything. That is
	// the physical distinction the two controls exist to keep apart.
	t.speed = heads::kSpacing[ 0 ] / std::max( 1e-3f, r.baseDelay );

	const float trim = std::clamp( host.wowFlutter, 0.0f, 1.0f );
	t.amount         = trim;
	t.depth          = kFlutterDepth;
	t.wow            = std::clamp( host.wow, 0.0f, 1.0f );
	t.flutter        = std::clamp( host.flutter, 0.0f, 1.0f );
	t.scrape         = std::clamp( host.scrape, 0.0f, 1.0f );

	// The rates are not on the panel and are not exposed. They sit where a
	// transport's components sit; the master trim is what an operator reaches
	// for. Kept as named constants rather than literals so a future control can
	// be added without moving the numbers.
	t.wowRate     = expRange( 0.5f, kWowRateLo, kWowRateHi );
	t.flutterRate = expRange( 0.5f, kFlutterRateLo, kFlutterRateHi );

	r.reach = tape::reachOf( r.selector, r.spread );

	//---------------------------------------------------------------------
	// The medium.
	//---------------------------------------------------------------------
	Medium& m    = r.tape;
	m.drive      = expRange( std::clamp( host.inputLevel, 0.0f, 1.0f ), kDriveLo, kDriveHi );
	m.saturation = std::clamp( host.saturation, 0.0f, 1.0f );
	m.headWear   = std::clamp( host.headWear, 0.0f, 1.0f );

	// Squared, because linear hiss is invisible for most of the control and
	// then grainy within a few percent of the top.
	const float hiss = std::clamp( host.hiss, 0.0f, 1.0f );
	m.hiss           = kHissMax * hiss * hiss;

	const float drop = std::clamp( host.dropouts, 0.0f, 1.0f );
	m.dropouts       = kDropoutMax * drop * drop;

	// A plain loop gain. The head bus it multiplies is already normalised by the
	// number of heads up (see Shaders.h), so Intensity means the same thing in
	// every selector position and there is exactly ONE normalisation in the
	// signal path rather than two that have to agree.
	m.intensity = std::clamp( host.intensity, 0.0f, 1.0f ) * kIntensityMax;

	//---------------------------------------------------------------------
	// The tank. Whether it is in circuit comes from the Mode Selector and not
	// from a control: on the hardware there is no reverb switch, there are
	// eight positions of the selector that have the springs in.
	//---------------------------------------------------------------------
	spring::Settings& tank = r.tank;
	tank.active            = r.selector.reverb;
	tank.decay             = std::clamp( host.reverbTime, 0.0f, 1.0f );
	tank.dispersion        = std::clamp( host.dispersion, 0.0f, 1.0f );
	tank.level             = std::clamp( host.reverbVolume, 0.0f, 1.0f );

	//---------------------------------------------------------------------
	// Output.
	//---------------------------------------------------------------------
	r.echoVolume   = std::clamp( host.echoVolume, 0.0f, 1.0f );
	r.reverbVolume = tank.active ? std::clamp( host.reverbVolume, 0.0f, 1.0f ) : 0.0f;
	r.directVolume = std::clamp( host.directVolume, 0.0f, 1.0f );

	r.bass   = dbToGain( ( std::clamp( host.bass, 0.0f, 1.0f ) - 0.5f ) * 2.0f * kToneDb );
	r.treble = dbToGain( ( std::clamp( host.treble, 0.0f, 1.0f ) - 0.5f ) * 2.0f * kToneDb );

	r.mix = std::clamp( host.mix, 0.0f, 1.0f );

	return r;
}

} // namespace controls
} // namespace astronaught
