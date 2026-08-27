#pragma once

namespace astronaught
{
/**
    The head block, and the twelve positions of the Mode Selector.

    This file is the machine's published geometry and nothing else. It has no
    arithmetic worth the name in it; what it has is a table that is **right**,
    and `astest --modes` fails the build if anybody edits it into a table that
    merely looks plausible.

    -------------------------------------------------------------- the heads

    The RE-201's tape path carries an erase head, a record head, and three
    playback heads "arranged sequentially at equal intervals, such that a sound
    is played back three times at equal intervals" (Roland, BOSS RE-20 owner's
    manual, *About the Configuration of the Tape Echo/Reverb*).

    Equal intervals is the load-bearing part. It means the three delays are not
    three independent numbers -- they are **one** number and the ratios 1 : 2 : 3,
    fixed by where the heads are bolted. Roland states the consequence directly:
    "the tap delay time at playback head 2 is twice that, and the tap delay time
    at playback head 3 is three times that."

    So this plugin has one delay control, not three, and `kSpacing` is geometry
    rather than taste. Extending it is what `Head Spread` is for -- see
    Controls.h -- and that control is declared an extension in the README
    because the hardware could not do it without a screwdriver.

    ------------------------------------------------------ the twelve modes

    ⚠️ **The obvious table is wrong.** Guessing produces the eight subsets of
    three heads, dry and wet, and that is not what the panel does. There is no
    delay-only `1+2` and no delay-only `1+2+3`: modes 1-4 are echo only and the
    only combination among them is `2+3`, modes 5-11 all have the reverb in, and
    12 is the reverb on its own. Six of the eight possible head subsets never
    appear without reverb, and `1+3` appears only at position 10.

    Taken from the BOSS RE-20 owner's manual, *About the Variation Mode* (p.18),
    whose selector "carries on the tradition of the RE-201"; cross-checked
    against Roland's own description of the RE-201 selector. It is NOT the
    RE-202's table -- that machine has a fourth playback head and its modes 8-12
    use it, so a table copied from the current product would be wrong in five
    positions.

    `astest --modes` asserts every cell of it.
*/
namespace heads
{
/// Playback heads on the block. Three. Not a tuning constant.
inline constexpr int kHeadCount = 3;

/// Positions on the Mode Selector.
inline constexpr int kModeCount = 12;

/// Head k's delay as a multiple of head 1's, from the equal spacing. The
/// physical fact this whole plugin's timing hangs off.
inline constexpr float kSpacing[ kHeadCount ] = { 1.0f, 2.0f, 3.0f };

/// One position of the selector.
struct Mode
{
	/// What the host's dropdown shows. Sixteen characters or fewer: FFGL's
	/// contract for option ELEMENT names is undocumented and the fleet has
	/// measured the 16-character truncation on parameter names, so these stay
	/// inside it rather than relying on a limit nobody has established.
	const char* label;

	/// Which playback heads this position lifts, in head order.
	bool head[ kHeadCount ];

	/// Whether the spring tank is in circuit. Modes 5-12.
	bool reverb;
};

/// The selector, position 1 at index 0.
extern const Mode kModes[ kModeCount ];

/// Position `index` (0-based). Clamped: a saved composition may name a position
/// that no longer exists.
const Mode& mode( int index );

/// True if any playback head is up in this position -- false only for 12,
/// Reverb Only, which is the one position where the tape is running and nothing
/// is listening to it.
bool anyHead( int index );

/// How many heads are up. 0..3.
int headCount( int index );

} // namespace heads
} // namespace astronaught
