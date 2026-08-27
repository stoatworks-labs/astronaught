#include "../Shaders.h"

#include <string>

namespace astronaught::shaders
{
namespace
{
const char* const kVersion = "#version 410 core\n";

const char* const kPreamble = R"(
in vec2 uv;
out vec4 fragColor;
)";
} // namespace

//---------------------------------------------------------------------------
// Pass one. Every head that is up, summed.
//
// Nothing else at all: no level, no filtering, no guard. This is the point in
// the machine where the three heads meet, and the whole of it is a sum.
//---------------------------------------------------------------------------
std::string PlaybackFragment()
{
	return std::string( kVersion ) + kPreamble + kTapeFunctions + R"(
void main()
{
	fragColor = headBus( uv );
}
)";
}

//---------------------------------------------------------------------------
// Pass two. Onto the tape.
//
// The signal the record head sees is the input plus Intensity times the
// playback bus -- the feedback -- and it goes through the record amplifier and
// the oxide on the way. Every trip round the loop therefore adds another
// generation of saturation, gap loss, hiss and dropouts, which is what makes a
// tape echo's tenth repeat a different object from its first rather than a
// quieter copy of it.
//
// ⚠️ The five taps compose the recorded signal at each tap position rather than
// filtering the input and the feedback separately. Those are different: gap
// loss is a property of the head, and the head writes the sum.
//---------------------------------------------------------------------------
std::string RecordFragment()
{
	return std::string( kVersion ) + kPreamble + kMediumFunctions + R"(
uniform sampler2D InputTexture;
uniform vec2 InputMaxUV;
uniform sampler2D PlaybackTexture;
uniform float Intensity;

/// How much of the record amplifier's headroom Intensity takes from the input.
///
/// ☠️ Not a taste constant. A picture is unipolar, so a delay whose repeats
/// simply add settles at input/(1-g) -- at Intensity 0.55 that is more than
/// twice the input, and any bright area of the source is white after a few
/// transits. The hardware does not have this problem because audio repeats
/// cancel as often as they add.
///
/// The record amplifier has ONE headroom, and this is it being shared: more
/// regeneration leaves less room for the input. That is not a workaround, it is
/// the standing advice for driving a Space Echo -- bring the input down when
/// Intensity is up -- made automatic, and it puts the loop's fixed point back at
/// the input level instead of well above it.
///
/// Deliberately less than 1, so a hot setting still BUILDS rather than merely
/// holding, and Intensity past unity still runs away.
const float kHeadroomShare = 0.65;

/// The signal presented to the record head at one point along the scan.
vec4 recordSignal( vec2 p )
{
	vec4 dry  = texture( InputTexture, clamp( p, 0.0, 1.0 ) * InputMaxUV );
	vec4 back = texture( PlaybackTexture, clamp( p, 0.0, 1.0 ) );

	float room = max( 0.0, 1.0 - Intensity * kHeadroomShare );

	return dry * TapeDrive * room + back * Intensity;
}

void main()
{
	float dx = TapeTexelX;

	vec4 c  = recordSignal( uv );
	vec4 l1 = recordSignal( uv + vec2( -dx, 0.0 ) );
	vec4 r1 = recordSignal( uv + vec2( dx, 0.0 ) );
	vec4 l2 = recordSignal( uv + vec2( -dx * 2.0, 0.0 ) );
	vec4 r2 = recordSignal( uv + vec2( dx * 2.0, 0.0 ) );

	vec3 sig = gapLoss( c.rgb, l1.rgb, r1.rgb, l2.rgb, r2.rgb, TapeWear );

	sig = tapeKnee( sig, TapeSat );
	sig += tapeHiss( uv );
	sig *= dropoutGain( uv );

	// Alpha carries so that an echo of a keyed source is keyed too, but it is
	// not put through the knee: alpha is coverage and not a level, and
	// compressing it would make a hard-edged source come off the tape with a
	// soft matte for no reason anybody asked for.
	float a = clamp( c.a, 0.0, 1.0 ) * dropoutGain( uv );

	fragColor = vec4( clamp( sig, 0.0, 1.0 ), a );
}
)";
}

//---------------------------------------------------------------------------
// Pass three. The tank.
//
// Fed from the SOURCE, in parallel with the tape -- see Shaders.h. The three
// legs read one shared field rather than three separate lines, so the coupling
// between them is total where a real Z tank's is partial. That is a real
// simplification and docs/NOTES.md records it as one; what it costs is that the
// tank cannot ring on a mode of one spring alone.
//---------------------------------------------------------------------------
std::string SpringFragment()
{
	return std::string( kVersion ) + kPreamble + R"(
uniform sampler2D TankTexture;    // the tank, as it was last frame
uniform sampler2D SourceTexture;  // the input, in parallel with the tape
uniform vec2 SourceMaxUV;

uniform float TankFeedback;
uniform float TankSpread;         // blur added per frame, fraction of height
uniform float TankDrive;
uniform float LegGain[ 3 ];
uniform vec2 LegOffset[ 3 ];
uniform float TankAspectWH;

/// One leg: the tank read at an offset, with the dispersion blur applied.
///
/// Four taps on a ring rather than a Gaussian. The blur compounds every frame,
/// so what a single frame's tap pattern looks like matters far less than that
/// it is cheap -- after a dozen trips the difference between a ring and a
/// Gaussian is invisible and the cost difference is not.
vec3 leg( int i, vec2 p )
{
	vec2 o = LegOffset[ i ];
	vec2 q = clamp( p + o, 0.0, 1.0 );

	float rx = TankSpread / max( 0.0001, TankAspectWH );
	float ry = TankSpread;

	vec3 acc = texture( TankTexture, q ).rgb * 0.36;
	acc += texture( TankTexture, clamp( q + vec2( rx, 0.0 ), 0.0, 1.0 ) ).rgb * 0.16;
	acc += texture( TankTexture, clamp( q - vec2( rx, 0.0 ), 0.0, 1.0 ) ).rgb * 0.16;
	acc += texture( TankTexture, clamp( q + vec2( 0.0, ry ), 0.0, 1.0 ) ).rgb * 0.16;
	acc += texture( TankTexture, clamp( q - vec2( 0.0, ry ), 0.0, 1.0 ) ).rgb * 0.16;

	return acc;
}

void main()
{
	vec3 acc = vec3( 0.0 );
	for( int i = 0; i < 3; ++i )
		acc += leg( i, uv ) * LegGain[ i ];

	vec4 src = texture( SourceTexture, clamp( uv, 0.0, 1.0 ) * SourceMaxUV );

	// ☠️ The drive is scaled by (1 - feedback), which makes this a NORMALISED
	// one-pole rather than an accumulator. Without it the tank settles at
	// drive/(1 - feedback) -- at a two-second decay and 60 fps that is about ten
	// times the input, so any reverb position of the Mode Selector renders white
	// within a second. Same cause as the record head's shared headroom: a
	// picture is unipolar and nothing in a tank of it ever cancels.
	//
	// Normalised, a continuously driven tank settles at the input level and the
	// Reverb Time control changes how long it takes to get there and to die
	// away, which is what the control is for.
	vec3 tank = acc * TankFeedback + src.rgb * src.a * TankDrive * ( 1.0 - TankFeedback );

	// Clamped, not wrapped. A tank driven past unity is a tank that howls, and
	// a howl that wraps round to black is an artefact of the number format
	// rather than of the spring.
	fragColor = vec4( clamp( tank, 0.0, 4.0 ), 1.0 );
}
)";
}

//---------------------------------------------------------------------------
// Pass four. The output stage: Echo Volume, Reverb Volume, Direct Volume, the
// tone controls, and the wet/dry mix. Straight into the host's framebuffer.
//
// ⚠️ Bass and Treble act on the WET bus only. On the RE-201 they are in the
// echo path, so the direct signal comes through flat -- which is also what
// makes them usable, because a tone control on the direct signal is a colour
// correction and Resolume already has several of those. docs/NOTES.md records
// that this is the reading of the hardware taken rather than a measurement of
// it.
//
// The shelves split at a spatial frequency along the scan, for the reason in
// Shaders.h: tape has one frequency axis and it lands along the scanline.
//---------------------------------------------------------------------------
std::string OutputFragment()
{
	return std::string( kVersion ) + kPreamble + R"(
uniform sampler2D InputTexture;
uniform vec2 InputMaxUV;
uniform sampler2D PlaybackTexture;
uniform sampler2D TankTexture;

uniform float EchoVolume;
uniform float ReverbVolume;
uniform float DirectVolume;
uniform float BassGain;
uniform float TrebleGain;
uniform float MixAmount;
uniform float OutTexelX;

/// The wet bus at one point: heads plus tank, at their own levels.
vec4 wetAt( vec2 p )
{
	vec2 q = clamp( p, 0.0, 1.0 );
	return texture( PlaybackTexture, q ) * EchoVolume
	       + vec4( texture( TankTexture, q ).rgb, 1.0 ) * ReverbVolume;
}

void main()
{
	float dx = OutTexelX;

	vec4 w  = wetAt( uv );
	vec4 l1 = wetAt( uv + vec2( -dx, 0.0 ) );
	vec4 r1 = wetAt( uv + vec2( dx, 0.0 ) );
	vec4 l2 = wetAt( uv + vec2( -dx * 3.0, 0.0 ) );
	vec4 r2 = wetAt( uv + vec2( dx * 3.0, 0.0 ) );

	vec3 low  = w.rgb * 0.34 + ( l1.rgb + r1.rgb ) * 0.22 + ( l2.rgb + r2.rgb ) * 0.11;
	vec3 high = w.rgb - low;

	vec3 wet = low * BassGain + high * TrebleGain;

	vec4 direct = texture( InputTexture, clamp( uv, 0.0, 1.0 ) * InputMaxUV );

	vec3 rgb = direct.rgb * direct.a * DirectVolume + wet;

	// Alpha is coverage: whichever of the direct signal and the wet bus is
	// present makes the pixel present. Summing them instead would make an echo
	// laid over its own source read as twice as opaque, which is not a thing
	// opacity can be.
	float a = clamp( max( direct.a * DirectVolume, w.a ), 0.0, 1.0 );

	vec4 processed = vec4( clamp( rgb, 0.0, 1.0 ), a );

	fragColor = mix( direct, processed, clamp( MixAmount, 0.0, 1.0 ) );
}
)";
}

//---------------------------------------------------------------------------
// The probe. Not part of the machine.
//
// Writes what tapeRead resolved for head 0 into the channels: the slot index
// normalised by the ring length, the phase, and the stretch. astest --read
// reads it back and compares against Tape.cpp's Resolve directly, so a
// disagreement between the mirror's two halves is reported as a disagreement
// rather than as a picture that looks a bit wrong.
//---------------------------------------------------------------------------
std::string ReadProbeFragment()
{
	return std::string( kVersion ) + kPreamble + kTapeFunctions + R"(
void main()
{
	float scan  = 1.0 - uv.y;
	float local = ( HeadPos - HeadOffset[ 0 ] ) + scan * HeadSpan;

	int slot;
	float phase;
	float span;
	bool ok = tapeRead( local, slot, phase, span );

	fragColor = vec4( float( slot ) / float( TAPE_SLOTS ),
	                  phase,
	                  span > 0.0 ? HeadSpan / span : 0.0,
	                  ok ? 1.0 : 0.0 );
}
)";
}

} // namespace astronaught::shaders
