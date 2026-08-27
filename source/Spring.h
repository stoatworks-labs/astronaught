#pragma once

namespace astronaught
{
/**
    The tank. Three springs, in a Z.

    Roland, on the RE-201: "The reverb section is equipped with a spring reverb.
    The reverb signal path is connected in parallel to the tape echo section" --
    and "the RE-201's reverb section includes three springs arranged in a 'Z'
    formation. The vibration of each spring influences the motion of the others
    to produce the reverb's characteristic sound."

    Two facts to hold on to, because both are easy to model away by accident:

    **Parallel, not in series.** The tank hangs off the same input the tape
    does. It is not the echo passed through a reverb -- feeding it from the head
    mix would put every repeat through the tank again and give a wash that
    grows, which is not what modes 5-11 sound or look like. `Astronaught.cpp`'s
    pass order is where this is enforced and it is the easiest thing here to get
    wrong.

    **Three springs that talk to each other.** Three independent lines summed is
    a comb filter and it rings on one note. What makes a tank a tank is that
    each leg is driven by the others as well as by the input, so the modes
    smear. `kCoupling` is that, and it is the difference between a smear and a
    ghost image at a fixed offset.

    ---------------------------------------------------------- what a spring is

    A spring is a **dispersive** delay line: a disturbance travels along it and
    the high frequencies travel more slowly than the low, so a click goes in and
    a descending chirp comes out. That is the "boing", and it is why a spring
    tank sounds nothing like a room.

    In a picture, frequency is spatial detail. So a leg of this tank is a delay
    whose fine detail lags behind its coarse detail -- modelled as a blur that
    **grows with every trip round the leg**, which is dispersion integrated. A
    fixed blur applied once is a smear; a blur that compounds is a bloom that
    arrives, and it is the closest thing a raster has to a chirp.

    The three legs are displaced from each other geometrically. That is the Z:
    the springs are not coaxial, so the tank has a shape, and the picture in it
    picks the shape up.
*/
namespace spring
{
/// Legs in the tank.
inline constexpr int kLegs = 3;

/// Leg lengths as a fraction of the longest. Not a Roland figure -- the service
/// manual gives no spring lengths -- but they are deliberately not simple
/// ratios: three legs at 1, 1/2 and 1/3 share every harmonic and ring as one
/// spring. These are mutually irrational enough to smear.
inline constexpr float kLegLength[ kLegs ] = { 1.0f, 0.7573f, 0.5761f };

/// How much of each leg's output drives the other two. The Z coupling. Above
/// about 0.4 the tank stops decaying and starts to howl, which is a real spring
/// tank's behaviour and not one worth putting on a slider.
inline constexpr float kCoupling = 0.28f;

/// Where each leg sits, as a fraction of the picture. The Z, as geometry.
inline constexpr float kLegOffsetX[ kLegs ] = { -0.0075f, 0.0000f, 0.0075f };
inline constexpr float kLegOffsetY[ kLegs ] = { 0.0042f, -0.0060f, 0.0031f };

/// What the operator asked the tank for.
struct Settings
{
	/// Whether the tank is in circuit at all. Comes from the Mode Selector, not
	/// from a control of its own -- see heads::Mode::reverb.
	bool active = false;

	/// Decay per second, 0..1 of the control's range.
	float decay = 0.5f;

	/// Dispersion: how fast the bloom grows per trip. 0 is a plain delay line.
	float dispersion = 0.5f;

	/// Tank level into the output.
	float level = 0.0f;
};

/// The coefficients a frame's worth of tank needs.
struct Coeffs
{
	/// Multiplier applied to the tank's contents each rendered frame. Derived
	/// from the decay TIME and the frame delta, so the tank rings for the same
	/// number of seconds at 30 fps and at 60.
	float feedback = 0.0f;

	/// Blur radius added per frame, in fractions of picture height.
	float spread = 0.0f;

	/// Input drive into the tank.
	float drive = 0.0f;

	/// Per-leg gains after coupling is folded in, in leg order.
	float legGain[ kLegs ] = { 0.0f, 0.0f, 0.0f };
};

/// Decay time in seconds at the bottom and top of the control. The bottom is a
/// short plate rather than nothing, because a tank with no tail is just a blur
/// and the Mode Selector would then have eight positions that all look alike.
inline constexpr float kDecayLo = 0.35f;
inline constexpr float kDecayHi = 6.0f;

/// Resolve the settings for a frame of `dt` seconds.
Coeffs coeffs( const Settings& s, float dt );

} // namespace spring
} // namespace astronaught
