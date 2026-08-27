#pragma once

#include "Heads.h"

#include <vector>

namespace astronaught
{
/**
    The loop of tape, as bookkeeping. No GL here -- `Store.h` owns the pixels.

    Each slot on the ring is one pass of the record head, and it remembers two
    numbers: the tape position where its first scanline was laid down, and how
    much tape it covers. That pair is the whole model, and it is what makes a
    playback head able to answer a question a frame queue cannot:

        *what was on the tape at position Q?*

    rather than

        *which frame was N frames ago?*

    ------------------------------------------------------- the raster on tape

    A picture is not an instant. It is written down the frame while the tape
    moves, so a slot occupies a *span* of tape and a scanline has a position of
    its own inside it. A playback head sweeps its own span during the output
    frame, and the two spans are only the same length when the tape is running
    at the speed it recorded at.

    So the read is: for output row `u`, the head is over tape position
    `Q + u * S`; find the slot whose span contains that; the source row is how
    far into that slot's span it fell. Three consequences arrive with it and
    none of them is written anywhere:

    - **The tear.** A head almost never lands on a slot boundary, so the top of
      the output is the bottom of one pass and the bottom is the top of the
      next. That is a rolling picture, and it is what a video signal coming off
      a tape echo actually does.

    - **Vertical stretch.** Run the tape faster than it recorded and the head
      covers more material per output row, so the picture comes back squeezed;
      slower and it stretches. This is the video form of the pitch shift Roland
      describes when the Repeat Rate knob is turned.

    - **Nothing special happens at a speed change.** The material does not move.
      Only the rate at which the heads reach it does.

    ------------------------------------------------ how far back the tape goes

    The ring is a fixed number of slots (`kSlots`), so the way to reach a longer
    delay is to put **more tape in each slot** -- to record less often. `stride`
    is how many rendered frames go by between passes of the record head.

    That is a memory cap and it is also the right physics, which is a coincidence
    worth being honest about rather than leaning on. A tape running slowly does
    genuinely carry less signal per second -- Roland: "the sonic quality of
    playback and recording also changes with the change in tape speed" -- so a
    three-second echo coming back temporally coarser is what the machine does.
    But the number 96 is chosen from a byte budget, not from a Roland spec
    sheet, and the README says so.
*/
namespace tape
{
/// Passes of the record head the ring holds. Fixed: the ring is allocated once
/// and never resized, because resizing it would throw the tape away, and the
/// tape is the instrument's memory.
inline constexpr int kSlots = 96;

/// What one playback head found.
struct Read
{
	/// False when the head is over tape that has not been recorded yet -- the
	/// first second after the plugin loads, or after a reset.
	bool valid = false;

	/// Ring index of the slot under the head at the START of the output frame.
	int slot = 0;

	/// How far into that slot's span the head is, 0..1. The tear sits at output
	/// row `(1 - phase)` when the spans match.
	float phase = 0.0f;

	/// Output rows per source row: the ratio of the head's sweep to the slot's
	/// recorded span. 1 is the speed it recorded at, above 1 is a squeeze.
	float stretch = 1.0f;
};

/// The ring, and where the record head has got to.
class Loop
{
public:
	Loop();

	/// Throw the tape away. Called on a resolution change, when the pixels
	/// behind these positions stop being the ones they described -- and NOT on
	/// a parameter change, which is the opposite of the fleet's GPU habit.
	void Erase();

	/// Advance the record head to `position`, claiming a slot for the picture
	/// about to be written. Returns the ring index to write into.
	///
	/// `position` must be greater than the last one: the tape only goes
	/// forward. transport::speedAt clamps to keep that true.
	int Record( double position );

	/// Where the record head is now.
	double Position() const
	{
		return head;
	}

	/// Resolve one playback head sweeping from `q` across `span` of tape.
	///
	/// ⚠️ Mirrored in GLSL. `Shaders.h` explains why, and every mirrored block
	/// is marked in both files. `astest --read` compares them.
	Read Resolve( double q, double span ) const;

	/// Slot positions and spans, for the shader's own search. Indexed by ring
	/// slot, so a slot that has never been written reads back with a zero span
	/// and the search skips it.
	const std::vector< float >& Starts() const
	{
		return starts;
	}
	const std::vector< float >& Spans() const
	{
		return spans;
	}

	/// The oldest position still on the tape. Below this a head finds nothing.
	double Oldest() const;

	/// Slots that hold real material, 0..kSlots.
	int Written() const
	{
		return written;
	}

	/// Ring index of the most recent pass of the record head.
	int Newest() const
	{
		return newest;
	}

	/// Positions are handed to GLSL as 32-bit floats, so they are stored
	/// relative to this origin rather than absolutely. An absolute position runs
	/// past a float's ability to separate adjacent scanlines within about twenty
	/// minutes of a show, and the symptom is echoes that quantise into bands --
	/// which reads as a shader bug and is not one.
	double Origin() const
	{
		return origin;
	}

private:
	void Rebase();

	std::vector< float > starts;
	std::vector< float > spans;

	double head   = 0.0;
	double origin = 0.0;
	int newest    = -1;
	int written   = 0;
};

/// How many rendered frames should pass between passes of the record head, so
/// that `kSlots` of them still reach `needed` material units back.
///
/// `perFrame` is the tape covered in one rendered frame. Never returns less
/// than 1: the record head cannot pass more often than the host renders.
int strideFor( double needed, double perFrame );

/// Material units from the record head to the furthest playback head in use.
/// Zero when no head is up -- mode 12 runs the tape and listens to none of it.
float reachOf( const heads::Mode& m, float spread );

} // namespace tape
} // namespace astronaught
