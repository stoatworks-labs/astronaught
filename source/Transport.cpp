#include "Transport.h"

#include <algorithm>
#include <cmath>

namespace astronaught
{
namespace transport
{
namespace
{
/// Substeps per frame for the position integral. See advance().
constexpr int kSubsteps = 8;

/// Relative weights of the three components inside `depth`. Wow leads because
/// it is the one a viewer reads as "the machine", flutter is the texture, and
/// scrape is a garnish that goes ugly fast if it is given real authority.
constexpr float kWowWeight     = 1.00f;
constexpr float kFlutterWeight = 0.55f;
constexpr float kScrapeWeight  = 0.22f;

/// Scrape's centre frequency, in Hz. Far above flutter and deliberately not
/// exposed: stick-slip is a property of the tape against the head, not
/// something a front panel ever offered.
constexpr float kScrapeRate = 47.0f;

/// A cheap deterministic hash. Same shape the rest of the fleet uses.
float hash11( float p )
{
	p = std::fabs( p ) * 0.1031f;
	p -= std::floor( p );
	p *= p + 33.33f;
	p *= p + p;
	return p - std::floor( p );
}

/// Value noise with a smooth interpolant, in 0..1.
float noise( float x )
{
	const float i = std::floor( x );
	const float f = x - i;
	const float u = f * f * ( 3.0f - 2.0f * f );
	return hash11( i ) * ( 1.0f - u ) + hash11( i + 1.0f ) * u;
}

/// Two octaves, centred on zero. Enough to stop a component reading as a pure
/// tone without becoming noise.
float wobble( float x )
{
	return ( noise( x ) - 0.5f ) * 1.3333f + ( noise( x * 2.17f + 11.3f ) - 0.5f ) * 0.6667f;
}
} // namespace

float speedError( const Settings& s, double seconds )
{
	const float t     = static_cast< float >( seconds );
	const float trim  = std::max( 0.0f, s.amount );
	const float depth = std::max( 0.0f, s.depth );

	// Wow: a real out-of-round capstan is periodic, so this is a sine with a
	// slow wander on it rather than noise. A purely random wow reads as a
	// wandering picture and not as a rotating part.
	const float wowPhase = t * s.wowRate;
	const float wow      = ( std::sin( wowPhase * 6.28318531f ) * 0.75f + wobble( wowPhase * 0.31f ) * 0.5f )
	                  * s.wow * kWowWeight;

	// Flutter: the pinch roller and the guides, faster and less regular.
	const float flutPhase = t * s.flutterRate;
	const float flutter   = ( std::sin( flutPhase * 6.28318531f + 1.7f ) * 0.5f + wobble( flutPhase * 0.73f + 5.1f ) )
	                      * s.flutter * kFlutterWeight;

	// Scrape: stick-slip. Rough on purpose.
	const float scrape = wobble( t * kScrapeRate ) * s.scrape * kScrapeWeight;

	return ( wow + flutter + scrape ) * depth * trim;
}

float speedAt( const Settings& s, double seconds )
{
	const float nominal = std::max( 1e-3f, s.speed );
	const float v       = nominal * ( 1.0f + speedError( s, seconds ) );

	// A transport that stops or reverses would unwind the position integral, and
	// there is no material behind the record head to unwind onto. Clamped rather
	// than allowed, because the failure is not visible as a wrong picture -- it
	// is a store whose positions stop being monotonic, which breaks the read
	// search silently.
	return std::max( nominal * 0.05f, v );
}

double advance( const Settings& s, double position, double seconds, double dt )
{
	if( !( dt > 0.0 ) )
		return position;

	const double step = dt / kSubsteps;
	double p          = position;
	for( int i = 0; i < kSubsteps; ++i )
		p += speedAt( s, seconds + step * i ) * step;

	return p;
}

float flutterPercent( const Settings& s )
{
	// Peak to peak over two seconds, sampled fast enough to catch scrape.
	constexpr int kSamples = 4096;
	constexpr double kSpan = 2.0;

	float lo = 0.0f;
	float hi = 0.0f;
	for( int i = 0; i < kSamples; ++i )
	{
		const float e = speedError( s, kSpan * i / kSamples );
		lo            = std::min( lo, e );
		hi            = std::max( hi, e );
	}

	return ( hi - lo ) * 100.0f;
}

} // namespace transport
} // namespace astronaught
