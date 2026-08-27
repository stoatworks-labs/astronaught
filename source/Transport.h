#pragma once

namespace astronaught
{
/**
    The capstan, and the tape position it winds up.

    ------------------------------------------------- position, not delay

    ☠️ **The tape is indexed by POSITION, never by time.** This is the one
    decision the whole plugin rests on, and getting it the other way round
    produces something that looks similar for about four seconds and is not a
    tape echo at all.

    A frame is laid on the tape at whatever position the tape had reached when
    it was recorded, and it stays there. A playback head is a fixed distance
    down the path, so it reads whatever material has arrived under it. Delay is
    therefore not stored anywhere -- it is the *time the tape takes to cover the
    head spacing*, which is a consequence of the speed history and not a
    parameter.

    Everything that makes a Space Echo recognisable falls out of that one
    substitution, with no code of its own:

    - **Turning Repeat Rate drags every echo already on the tape.** The material
      does not move; the rate at which the heads reach it does. Roland writes it
      out in two halves -- "sounds are played back more closely together, and the
      pitch begins to rise", and "the density of the sounds during recording
      gradually decreases, so when those sounds reach the playback heads, the
      pitches that were raised then begin to fall". Both halves are the same
      integral read in opposite directions. Neither is coded.

    - **The three echoes wobble independently.** Roland: the speed "is always
      changing slightly due to the resistance from mechanical friction, tape
      slippage", which "creates oscillations in the pitch of each of the three
      echo sounds, automatically producing the RE-201's characteristic chorus
      effect." A model that applies one wobble to all three heads at once
      produces a vibrato and not a chorus. Here each head reads a *different
      point on the tape*, so it sees the speed error from a different moment in
      the past, and the decorrelation is free.

    If a future edit is ever tempted to store a frame with a timestamp and
    subtract a delay from it, both of those disappear silently and the tests in
    `astest --drag` and `astest --chorus` are what will notice.

    ------------------------------------------------------------- the units

    Position is in **material units**: one unit is the length of tape that
    passes a head in one second at nominal speed. Head 1 sits exactly one unit
    downstream of the record head (`heads::kSpacing`), so at nominal speed its
    delay is one second, and `Repeat Rate` is a speed multiplier -- 10x for the
    fastest tape, 1x for the slowest.

    Speed and delay are reciprocal, so the control runs backwards from the
    quantity, exactly as ferric's `Tape Speed` does: the slider reads
    high-is-fast because that is what a transport control means.

    ---------------------------------------------------------- the wobble

    Three components, the same three any tape machine has, and they are summed
    as a fractional error on the speed rather than added to a delay:

      Wow      sub-hertz, from an out-of-round capstan or a dragging reel
      Flutter  a few hertz to tens, from the pinch roller and the tape guides
      Scrape   high and rough, tape stick-slip against the heads

    Scrape is deliberately not band-limited to anything: it is friction, and a
    smooth scrape is not scrape.
*/
namespace transport
{
/// What the operator asked the transport to do.
struct Settings
{
	/// Nominal speed, in material units per second. Head 1's delay at this
	/// speed is `heads::kSpacing[0] / speed` seconds.
	float speed = 4.0f;

	//Instability, each 0..1 of its own maximum.
	float wow     = 0.25f;
	float flutter = 0.30f;
	float scrape  = 0.15f;

	/// Overall instability trim. Multiplies all three, so an operator who wants
	/// a serviced machine has one control to reach for rather than three.
	float amount = 1.0f;

	//Rates, in Hz.
	float wowRate     = 0.8f;
	float flutterRate = 6.0f;

	/// Peak fractional speed error at full deflection of everything. A real
	/// deck is a fraction of a percent; this is an effect, so the top of the
	/// control is well past broken.
	float depth = 0.06f;
};

/// The speed error at one instant, as a fraction: 0 is dead on, +0.02 is two
/// percent fast. Deterministic in `seconds`, so a scrub lands where it should
/// and two runs of the harness agree.
float speedError( const Settings& s, double seconds );

/// The instantaneous speed, in material units per second. Never returns
/// anything at or below zero: a transport that stops, or runs backwards,
/// unwinds the position integral and there is no sensible tape behind it.
float speedAt( const Settings& s, double seconds );

/// Advance the position integral across one frame.
///
/// Integrated by substepping rather than by evaluating the error once: at 60 Hz
/// a frame is 16 ms and flutter runs to 30 Hz, so a single sample per frame
/// aliases the flutter into a slow beat against the frame rate -- which looks
/// like wow, on a control marked Flutter. Eight substeps put the Nyquist limit
/// well above the fastest thing here.
///
/// `seconds` is the time at the START of the frame.
double advance( const Settings& s, double position, double seconds, double dt );

/// Weighted flutter figure, as a percentage, for the readout. Peak-to-peak
/// speed error over a two-second window -- the honest measurement for an effect,
/// and NOT the DIN/IEC weighted figure a deck would be sold on. See ferric for
/// the weighted one; this plugin is not pretending to be a test set.
float flutterPercent( const Settings& s );

} // namespace transport
} // namespace astronaught
