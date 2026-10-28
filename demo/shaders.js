// GENERATED from source/shaders/*.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \` for a backtick inside a comment.
// The four pass bodies are the raw string each Fragment() function in
// Passes.cpp appends; plugin.js joins them to the rest as Passes.cpp does.
export const VERSION = '#version 410 core\n';

// kVertex (source/shaders/Vertex.cpp)
export const VERTEX = `#version 410 core
uniform vec2 MaxUV;

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV * MaxUV;
}
`;

// kTapeFunctions (source/shaders/Tape.cpp)
export const TAPE_FUNCTIONS = `
//---------------------------------------------------------------------------
// The store, and the slot table.
//---------------------------------------------------------------------------
#define TAPE_SLOTS 96

uniform sampler2DArray TapeTex;
uniform float SlotStart[ TAPE_SLOTS ];  // relative to the loop origin
uniform float SlotSpan[ TAPE_SLOTS ];   // material units this pass covers
uniform int   TapeNewest;               // ring index of the newest pass
uniform int   TapeWritten;              // slots holding real material

//---------------------------------------------------------------------------
// The heads. Positions are material units BEHIND the record head, already
// multiplied by Head Spread on the CPU, so nothing here knows what the 1:2:3
// spacing is. HeadUp is the Mode Selector's pattern.
//---------------------------------------------------------------------------
uniform float HeadOffset[ 3 ];
uniform float HeadUp[ 3 ];   // 1 or 0; a float because FFGLShader has no bool
uniform float HeadPos;       // record head position now, relative to the origin
uniform float HeadSpan;      // material this output frame sweeps
uniform float HeadNorm;      // 1 / number of heads up. See Shaders.h.

//= mirrored from Tape.cpp -- astest --read compares them
//
// Find the slot whose recorded span contains \`local\`, and how far into it.
// Returns false only when \`local\` is older than anything still on the tape.
bool tapeRead( float local, out int slot, out float phase, out float span )
{
	slot  = 0;
	phase = 0.0;
	span  = 0.0;

	if( TapeWritten <= 0 )
		return false;

	for( int i = 0; i < TAPE_SLOTS; ++i )
	{
		if( i >= TapeWritten )
			break;

		int s = ( TapeNewest - i + TAPE_SLOTS * 2 ) % TAPE_SLOTS;
		if( SlotSpan[ s ] <= 0.0 )
			continue;

		if( local >= SlotStart[ s ] )
		{
			// Past the newest slot's end is not "nothing found" -- it happens
			// every frame for a head whose delay is shorter than one pass of
			// the record head -- so it clamps into that slot instead.
			slot  = s;
			phase = clamp( ( local - SlotStart[ s ] ) / SlotSpan[ s ], 0.0, 1.0 );
			span  = SlotSpan[ s ];
			return true;
		}
	}

	return false;
}

//---------------------------------------------------------------------------
// One head, at output position \`uv\`.
//
// \`scan\` is progress down the picture: 0 at the first scanline. uv.y is
// bottom-origin, so it is 1 - uv.y, and getting that upside down puts the tear
// on the wrong side of the frame and rolls the picture the wrong way.
//---------------------------------------------------------------------------
vec4 headSample( int head, vec2 uvIn )
{
	if( HeadUp[ head ] < 0.5 )
		return vec4( 0.0 );

	float scan  = 1.0 - uvIn.y;
	float local = ( HeadPos - HeadOffset[ head ] ) + scan * HeadSpan;

	int slot;
	float phase;
	float span;
	if( !tapeRead( local, slot, phase, span ) )
		return vec4( 0.0 );

	// \`phase\` is progress through the recorded raster, so it converts back to a
	// texture coordinate the same way scan did. The stretch is implicit: a slot
	// recorded over more tape than this frame sweeps returns a phase that moves
	// more slowly than \`scan\` does, and the picture comes back taller.
	vec2 src = vec2( uvIn.x, 1.0 - phase );

	return texture( TapeTex, vec3( src, float( slot ) ) );
}

//---------------------------------------------------------------------------
// The head bus.
//
// Normalised by the number of heads up -- see Shaders.h for why this is not the
// averaging bug it resembles, and for what it costs. The loop gain lives in
// Intensity, on this already-normalised bus, so it means the same thing in every
// selector position.
//---------------------------------------------------------------------------
vec4 headBus( vec2 uvIn )
{
	vec4 acc = vec4( 0.0 );
	for( int i = 0; i < 3; ++i )
		acc += headSample( i, uvIn );

	return acc * HeadNorm;
}
`;

// kMediumFunctions (source/shaders/Medium.cpp)
export const MEDIUM_FUNCTIONS = `
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
// not. \`tanh(x*k)/tanh(k)\` is the standard normalised soft clip and it is what
// was here first: it maps 1 to 1, which looked like the right property to want,
// because otherwise Saturation doubles as a level control.
//
// Its small-signal gain is \`k/tanh(k)\`, which is 2.8 at Saturation 0.35. Inside
// a feedback loop that is a 2.8x amplifier, and the loop is white in three
// transits however low Intensity is. \`astest --presets\` failed on it and
// \`--stats\` found it: sweeping Intensity changed nothing and sweeping
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
`;

// kPreamble (source/shaders/Passes.cpp)
export const PREAMBLE = `
in vec2 uv;
out vec4 fragColor;
`;

// PlaybackFragment() (source/shaders/Passes.cpp)
export const PLAYBACK = `
void main()
{
	fragColor = headBus( uv );
}
`;

// RecordFragment() (source/shaders/Passes.cpp)
export const RECORD = `
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
`;

// SpringFragment() (source/shaders/Passes.cpp)
export const SPRING = `
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
`;

// OutputFragment() (source/shaders/Passes.cpp)
export const OUTPUT = `
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
`;
