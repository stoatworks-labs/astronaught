#include "../Shaders.h"

namespace astronaught::shaders
{
/**
    What the oxide and the heads do to what is written on them.

    Everything here filters **along the scan and only along the scan**. Tape has
    one frequency axis, its own length, and the picture is laid on it one
    scanline at a time -- so a loss of bandwidth is a loss of horizontal detail
    and nothing else. A symmetrical blur here would be the single fastest way to
    make this look like a lens effect wearing a tape effect's controls.

    The order is the record amplifier's order and not a convenient one: gap loss
    before saturation, because the head cannot resolve what it never wrote;
    hiss after saturation, because the noise floor is the oxide's and is not
    something the record amplifier compresses; dropouts last, because losing
    contact removes signal that was already there.
*/
const char* const kMediumFunctions = R"(
uniform float TapeDrive;     // input level into the record amplifier
uniform float TapeSat;       // 0..1 knee
uniform float TapeWear;      // 0..1 head gap loss
uniform float TapeHiss;      // peak amplitude on a 0..1 picture
uniform float TapeHissW;     // hiss correlation length, in tape texels
uniform float TapeDrop;      // probability a block loses contact
uniform float TapeDropBlk;   // blocks across one scanline
uniform float TapeGrain;     // advances the grain along the tape
uniform float TapeTexelX;    // 1 / tape width, in texture coordinates
uniform float TapeLines;     // tape height in texels

//---------------------------------------------------------------------------
// An integer hash. Not the fract(sin(x)*43758.5453) idiom: sin is hardware on
// one side of a mirror and a library call on the other, and multiplying the
// disagreement by forty thousand promotes it to the whole answer.
//---------------------------------------------------------------------------
float hash21( vec2 p )
{
	vec3 q = fract( vec3( p.xyx ) * vec3( 0.1031, 0.1030, 0.0973 ) );
	q += dot( q, q.yzx + 33.33 );
	return fract( ( q.x + q.y ) * q.z );
}

/// Value noise along one axis, smooth, 0..1.
float noise11( float x )
{
	float i = floor( x );
	float f = fract( x );
	float u = f * f * ( 3.0 - 2.0 * f );
	return mix( hash21( vec2( i, 7.0 ) ), hash21( vec2( i + 1.0, 7.0 ) ), u );
}

//---------------------------------------------------------------------------
// The record amplifier's knee. A LIMITER, not a drive.
//
// ☠️ It must have a gain of one or less everywhere, and the obvious curve does
// not. `tanh(x*k)/tanh(k)` is the standard normalised soft clip and it is what
// was here first: it maps 1 to 1, which looked like the right property to want,
// because otherwise Saturation doubles as a level control.
//
// Its small-signal gain is `k/tanh(k)`, which is 2.8 at Saturation 0.35. Inside
// a feedback loop that is a 2.8x amplifier, and the loop is white in three
// transits however low Intensity is. `astest --presets` failed on it and
// `--stats` found it: sweeping Intensity changed nothing and sweeping
// Saturation moved the cliff, which is not how a feedback problem behaves and
// is exactly how a gain in the wrong place does.
//
// So the trade goes the other way. Below the knee this is exactly transparent;
// above it, it compresses towards a ceiling. Full scale therefore comes out
// below full scale at high Saturation -- but only the PEAKS move, which makes it
// a limiter and not a fader, and peaks folding down is what over-driven tape
// does. At Saturation 0 it is the identity, exactly.
//
// Referred to black rather than to mid grey, because a video signal is
// unipolar. Tape compresses the highlights and leaves the blacks; a bipolar
// knee would compress the blacks upwards and lift the whole picture.
//---------------------------------------------------------------------------
vec3 tapeKnee( vec3 x, float sat )
{
	float t    = 1.0 - clamp( sat, 0.0, 1.0 ) * 0.9;
	float room = max( 1e-4, 1.0 - t );

	vec3 over   = max( x - t, vec3( 0.0 ) );
	vec3 folded = t + room * ( 1.0 - exp( -over / room ) );

	return mix( x, folded, step( vec3( t ), x ) );
}

/// Head gap loss: a five-tap low-pass along the scan, mixed in by wear. The
/// radius grows as well as the mix, so a badly worn head is soft rather than
/// merely half-soft.
vec3 gapLoss( vec3 centre, vec3 l1, vec3 r1, vec3 l2, vec3 r2, float wear )
{
	vec3 soft = ( centre * 0.34 + ( l1 + r1 ) * 0.22 + ( l2 + r2 ) * 0.11 );
	return mix( centre, soft, clamp( wear, 0.0, 1.0 ) );
}

//---------------------------------------------------------------------------
// The oxide's noise floor.
//
// One-dimensional, like the tape. It is correlated over a few texels along the
// scan and independent from one scanline to the next, which is why video noise
// reads as horizontal grain where film grain does not.
//
// It also dithers the store's eight bits, which is what makes RGBA8 survivable
// inside a feedback loop. See Store.h.
//---------------------------------------------------------------------------
vec3 tapeHiss( vec2 uvIn )
{
	float line = floor( uvIn.y * TapeLines );
	float along = uvIn.x / max( 1e-5, TapeTexelX * max( 0.5, TapeHissW ) ) + TapeGrain;

	float n = noise11( along + line * 131.7 ) - 0.5;
	// Slightly coloured rather than flat: a little correlation between the
	// channels reads as tape noise, three independent channels read as digital
	// confetti.
	float c = noise11( along * 1.7 + line * 57.3 + 11.0 ) - 0.5;

	return ( vec3( n ) * 0.7 + vec3( c, -c * 0.5, c * 0.3 ) * 0.3 ) * 2.0 * TapeHiss;
}

/// Loss of contact. Whole blocks of a scanline, not speckle: a dropout is the
/// tape lifting off the head, and it lifts for a distance.
float dropoutGain( vec2 uvIn )
{
	if( TapeDrop <= 0.0 )
		return 1.0;

	float blockX = uvIn.x * TapeDropBlk;
	float block  = floor( blockX );
	float line   = floor( uvIn.y * TapeLines );
	float r      = hash21( vec2( block + floor( TapeGrain ), line ) );

	if( r >= TapeDrop )
		return 1.0;

	// Soft at the block's edges: contact is lost and regained over a distance,
	// so a hard-edged rectangle reads as a digital mask rather than as tape.
	float f    = fract( blockX );
	float edge = smoothstep( 0.0, 0.18, f ) * smoothstep( 0.0, 0.18, 1.0 - f );

	return mix( 1.0, 0.06, edge );
}
)";
} // namespace astronaught::shaders
