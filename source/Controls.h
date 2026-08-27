#pragma once

#include "Heads.h"
#include "Spring.h"
#include "Transport.h"

namespace astronaught
{
/**
    The one place a slider position becomes a physical quantity.

    **A ranged FF_TYPE_STANDARD parameter cannot have a ranged default.** The
    SDK's `SetParamInfo` clamps a default into 0..1 before returning, and
    `SetParamRange` can only be called afterwards because it finds the parameter
    by id. There is no `SetParamDefault`. So every numeric control here is 0..1
    and is converted on the way through, which is this file.

    ------------------------------------------------ the panel, and what is not

    The RE-201's front panel is: Input Volume, the Mode Selector, Repeat Rate,
    Intensity, Echo Volume, Reverb Volume, Bass and Treble. Those eight are here
    under names a person who has used one would recognise, and they do what the
    hardware's do.

    Everything else is an **extension** and is listed as one in the README, so
    that nobody has to guess which half of the panel is Roland's:

      Head Spread   moves the playback heads. The hardware bolts them at equal
                    intervals and 1 : 2 : 3 is geometry, not taste -- see
                    Heads.h -- so this control does something the machine could
                    only do with a screwdriver. It is nominal at 1.
      Saturation    and
      Wow & Flutter appear on BOSS's own RE-20 and RE-202 panels but not on the
                    RE-201, where they were the state of the machine rather than
                    settings.
      Wow, Flutter, Scrape
                    break that one knob into the three components a transport
                    actually has.
      Head Wear, Hiss, Dropouts
                    the condition of the tape and the heads.
      Dispersion    how much chirp the tank has. See Spring.h.
      Mix           the fleet's wet/dry, which no piece of hardware has.

    ------------------------------------------------------------- the curves

    A **rate** converts exponentially, so half a slider is the geometric middle
    and equal distances either side are reciprocal factors. An **amount**
    converts linearly. Anything centred on "no change" -- Bass, Treble -- puts
    that at 0.5.

    ⚠️ **`Repeat Rate` runs backwards from the quantity it sets, on purpose.**
    The knob is a tape speed and the delay is its reciprocal, so turning it up
    makes the echo shorter. That is what the hardware does and it is the whole
    reason the pitch effects exist. Reversing it would leave a control that
    still looks alive in a sweep and does the opposite of its label.
*/
namespace controls
{
/// The controls exactly as the host holds them: every one 0..1, option
/// parameters holding their element index.
struct HostValues
{
	//Echo. Mode 4 -- element index 3, heads 2+3, no reverb -- is the default:
	//two taps at 2:3 is immediately legible as a multi-head machine, where a
	//single head reads as an ordinary delay and could be any plugin.
	float mode       = 3.0f;
	float repeatRate = 0.52f;
	float intensity  = 0.55f;
	float headSpread = 0.602f;///< maps to exactly 1.0, the hardware's spacing

	//☠️ 0.602 is not a round number and must not be tidied to 0.6. The control
	//is exponential over 0.25 to 2.5, so nominal spacing sits at
	//log10(1/0.25)/log10(10) = 0.60206 -- and `astest --ratios` fails if the
	//default stops landing on the spacing the heads are actually bolted at.

	//Tape
	float inputLevel = 0.5f;
	float saturation = 0.35f;
	float headWear   = 0.25f;
	float hiss       = 0.18f;
	float dropouts   = 0.0f;

	//Transport. Not zero: a Space Echo that runs dead steady is a digital
	//delay, and the chorus on the repeats is the thing people buy the machine
	//for. Roland calls it "the RE-201's characteristic chorus effect".
	float wowFlutter = 0.35f;
	float wow        = 0.45f;
	float flutter    = 0.40f;
	float scrape     = 0.20f;

	//Reverb
	float reverbTime = 0.45f;
	float dispersion = 0.5f;

	//Output
	float echoVolume   = 0.7f;
	float reverbVolume = 0.5f;
	float directVolume = 1.0f;
	float bass         = 0.5f;///< centre is flat
	float treble       = 0.5f;
	float mix          = 1.0f;
};

/// What the record head lays down, and what the tape does to it.
struct Medium
{
	/// Drive into the tape, before the knee.
	float drive = 1.0f;

	/// Saturation knee, 0..1. Zero is a linear medium, which tape is not.
	float saturation = 0.0f;

	/// High-frequency loss along the scan, 0..1. Head gap loss: a worn or
	/// mis-azimuthed head loses the fine detail first, and it loses it ALONG
	/// the tape, which lands along the scanline.
	float headWear = 0.0f;

	/// Peak hiss amplitude on a 0..1 picture.
	float hiss = 0.0f;

	/// Probability a block of a scanline loses contact.
	float dropouts = 0.0f;

	/// Head bus fed back to the record head: the loop gain. Above 1 the loop
	/// runs away, which the hardware also does and is half of what it is famous
	/// for.
	///
	/// A plain gain, because the bus it multiplies is normalised by the number
	/// of heads up before it gets here -- so this means the same thing in every
	/// selector position. Shaders.h has why that normalisation exists, why it is
	/// not the averaging bug it resembles, and what it costs.
	float intensity = 0.0f;
};

/// Everything the render needs, in physical units.
struct Render
{
	/// Selector position, 0-based.
	int mode = 0;

	/// The position's head and reverb pattern.
	heads::Mode selector = {};

	/// Head spacing multiplier. 1 is where the hardware bolts them.
	float spread = 1.0f;

	transport::Settings transport;
	spring::Settings tank;
	Medium tape;

	/// Head 1's delay in seconds at the nominal speed, for the readout and for
	/// the tests. Heads 2 and 3 are twice and three times it.
	float baseDelay = 0.0f;

	/// Material units from the record head to the furthest head in use.
	float reach = 0.0f;

	//Output stage.
	float echoVolume   = 0.0f;
	float reverbVolume = 0.0f;
	float directVolume = 1.0f;

	/// Shelf gains, as multipliers. 1 is flat.
	float bass   = 1.0f;
	float treble = 1.0f;

	float mix = 1.0f;
};

/// Read an option parameter. Option parameters hold the element the operator
/// chose -- 0, 1, 2 -- not a fraction, so they round and clamp rather than
/// scale. A stale composition naming an element that no longer exists is why it
/// clamps.
int option( float value, int elementCount );

/// Exponential conversion, exposed because the harness checks the ends and the
/// midpoint of every rate control against it.
float expRange( float t, float lo, float hi );

/// Everything the render needs.
Render render( const HostValues& host );

//---------------------------------------------------------------------------
// Ranges, public because the harness asserts on them and the README quotes
// them. A number a test can read is a number that cannot quietly drift.
//---------------------------------------------------------------------------

/// Head 1's delay, in seconds, at the slowest and fastest tape.
///
/// The fast end is where a tape echo stops being an echo; the slow end is
/// Roland's own figure for the RE-201's longest -- one second at head 1, which
/// with the 1 : 2 : 3 spacing is the three seconds the machine is known for.
inline constexpr float kDelaySlow = 1.000f;
inline constexpr float kDelayFast = 0.070f;

/// Head spacing multiplier at the ends of Head Spread. 1 is the hardware.
inline constexpr float kSpreadLo = 0.25f;
inline constexpr float kSpreadHi = 2.50f;

/// Feedback at the top of Intensity. Deliberately past 1: the RE-201
/// self-oscillates and that is not a defect to be clamped out of it.
inline constexpr float kIntensityMax = 1.15f;

/// Drive into the tape at the ends of Input Level.
inline constexpr float kDriveLo = 0.25f;
inline constexpr float kDriveHi = 2.50f;

/// Shelf range, in dB either side of flat.
inline constexpr float kToneDb = 12.0f;

/// Hiss amplitude at full, on a 0..1 picture.
inline constexpr float kHissMax = 0.10f;

/// Dropout probability at full.
inline constexpr float kDropoutMax = 0.06f;

} // namespace controls
} // namespace astronaught
