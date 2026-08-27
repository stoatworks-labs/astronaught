#include "../Shaders.h"

namespace astronaught::shaders
{
/**
    The tape, on the GPU.

    ⚠️ **`tapeRead` is a MIRROR of `Tape.cpp`'s `Loop::Resolve`.** Both are
    marked `//= mirrored`, and `astest --read` fails if they stop agreeing.

    It has to be resolved here rather than handed down as a uniform, because the
    answer is different for every output row. The head sweeps its own span of
    tape while the picture is drawn, so row 0 and row 1079 are over different
    material -- usually different *slots* -- and that difference is the tear and
    the vertical stretch, which is most of what this plugin looks like.

    The search walks back from the newest slot rather than binary-searching. On
    the CPU a binary search would be faster; it is not used, because then the
    mirror would be two different algorithms and the test would be checking that
    two implementations of different things happen to agree.

    Slot positions arrive **relative to the loop's origin**, which is rebased
    periodically. An absolute position stops separating adjacent scanlines in a
    32-bit float within about twenty minutes, and the symptom -- echoes
    quantising into horizontal bands -- reads as a filtering bug and is not one.
*/
const char* const kTapeFunctions = R"(
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
// Find the slot whose recorded span contains `local`, and how far into it.
// Returns false only when `local` is older than anything still on the tape.
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
// One head, at output position `uv`.
//
// `scan` is progress down the picture: 0 at the first scanline. uv.y is
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

	// `phase` is progress through the recorded raster, so it converts back to a
	// texture coordinate the same way scan did. The stretch is implicit: a slot
	// recorded over more tape than this frame sweeps returns a phase that moves
	// more slowly than `scan` does, and the picture comes back taller.
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
)";
} // namespace astronaught::shaders
