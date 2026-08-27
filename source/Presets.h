#pragma once

namespace astronaught
{
/**
    Factory presets, in the host-facing 0..1 space.

    ---------------------------------------- a preset is an OVERRIDE, not a write

    ☠️ **The obvious implementation does not work in Resolume, and it fails in a
    way that looks like the dropdown is broken.** Copying a preset's values into
    `params[]` and raising value events assumes the host consumes those events.
    Resolume does not. It goes on pushing the values it still believes in -- the
    ones from before the preset -- as ordinary `SetFloatParameter` calls, so any
    "an operator edited a covered control, so drop to Custom" rule fires on the
    host's own echo, immediately, every time. The dropdown snaps back to Custom
    and nothing changes. This cost the fleet a shipped release and an external
    bug report.

    So the plugin keeps two things apart: `params[]`, what the render uses, and
    `hostSent[]`, **what the host last sent**. A restatement that matches what a
    preset already put there is not written; a genuine edit differs and is. The
    judgement is on what the value **is**, never on the fact that it changed.

    The tolerance is a **quantisation allowance, 1e-3**, not a float epsilon.
    Resolume rounds to about a thousandth on the way through, and 1e-4 reads a
    rounded echo of our own value as an edit.

    ------------------------------------------------------- what is not covered

    `Mix` only. It is a compositing decision that belongs to whoever is running
    the show, and a preset that quietly took the effect to fully wet would
    override that.

    ⚠️ **Mode is covered and it is an option parameter, so it holds an ELEMENT
    INDEX here** -- `3` is selector position 4, heads 2+3, not "three tenths of
    the way along". The fleet has already shipped a preset that gave a boolean
    0.35, where one build read 0.35 and the other read `true`.
*/
namespace presets
{
/// The parameters a preset covers, in table-column order. The binding of these
/// to real ParamIDs lives in Astronaught.h, so this table stays host-agnostic.
enum Param
{
	P_MODE,
	P_REPEAT_RATE,
	P_INTENSITY,
	P_HEAD_SPREAD,
	P_INPUT_LEVEL,
	P_SATURATION,
	P_HEAD_WEAR,
	P_HISS,
	P_DROPOUTS,
	P_WOW_FLUTTER,
	P_WOW,
	P_FLUTTER,
	P_SCRAPE,
	P_REVERB_TIME,
	P_DISPERSION,
	P_ECHO_VOLUME,
	P_REVERB_VOLUME,
	P_DIRECT_VOLUME,
	P_BASS,
	P_TREBLE,
	kParamCount
};

struct Preset
{
	const char* name;
	float values[ kParamCount ];
};

/// Mode holds a 0-based element index: 0 is selector position 1, 11 is Reverb
/// Only. Bass and Treble are centred, so 0.5 is flat.
inline constexpr Preset kPresets[] = {
	//                     mode  rate  int  sprd  in   sat  wear hiss drop  w&f  wow  flut scrp rvbT disp echo rvbV dirV bass treb
	{ "Slapback",       {0.f, .78f, .18f,  .602f, .50f, .30f, .20f, .12f, .00f, .22f, .40f, .40f, .15f, .40f, .50f, .65f, .00f, 1.0f, .50f, .50f} },
	{ "Three Heads",    {10.f, .50f, .45f,  .602f, .50f, .35f, .25f, .18f, .00f, .35f, .45f, .40f, .20f, .45f, .50f, .70f, .40f, 1.0f, .50f, .50f} },
	{ "Long Throw",     {2.f, .10f, .55f,  .602f, .45f, .30f, .35f, .22f, .05f, .30f, .50f, .35f, .18f, .55f, .55f, .75f, .00f, 1.0f, .55f, .42f} },
	{ "Dub Siren",      {3.f, .62f, .82f,  .602f, .62f, .55f, .30f, .25f, .00f, .40f, .45f, .45f, .25f, .45f, .50f, .80f, .00f, .85f, .60f, .55f} },
	{ "Runaway",        {3.f, .70f, .95f,  .602f, .55f, .70f, .40f, .30f, .08f, .45f, .50f, .50f, .30f, .50f, .50f, .85f, .00f, .70f, .55f, .60f} },
	{ "Chorus Tape",    {0.f, .88f, .35f,  .450f, .50f, .28f, .22f, .15f, .00f, .70f, .60f, .75f, .40f, .40f, .50f, .60f, .00f, 1.0f, .50f, .52f} },
	{ "Roll Tear",      {3.f, .30f, .60f,  .850f, .55f, .40f, .30f, .20f, .12f, .55f, .70f, .45f, .25f, .45f, .50f, .78f, .00f, .90f, .50f, .48f} },
	{ "Tape Ambience",  {10.f, .45f, .40f,  .602f, .45f, .30f, .45f, .20f, .00f, .30f, .45f, .35f, .15f, .70f, .70f, .55f, .70f, 1.0f, .58f, .40f} },
	{ "Sick Transport", {3.f, .40f, .58f,  .680f, .58f, .60f, .70f, .45f, .55f, .85f, .75f, .70f, .55f, .45f, .50f, .78f, .00f, .80f, .55f, .35f} },
	{ "Spring Only",    {11.f, .50f, .00f,  .602f, .50f, .25f, .20f, .10f, .00f, .25f, .45f, .40f, .15f, .60f, .65f, .00f, .85f, 1.0f, .50f, .50f} },
};

inline constexpr int kCount = static_cast< int >( sizeof( kPresets ) / sizeof( kPresets[ 0 ] ) );

/// Dropdown element count, including Custom.
inline constexpr int elementCount()
{
	return kCount + 1;
}

/// Dropdown label. 0 is Custom.
inline const char* label( int element )
{
	if( element <= 0 || element > kCount )
		return "Custom";

	return kPresets[ element - 1 ].name;
}

/// A preset's values. `element` is 1-based; 0 (Custom) has none.
inline const float* values( int element )
{
	if( element <= 0 || element > kCount )
		return nullptr;

	return kPresets[ element - 1 ].values;
}

/// The comparison tolerance. A host-quantisation allowance, not a float
/// epsilon. See the header.
inline constexpr float kEchoTolerance = 1e-3f;

} // namespace presets
} // namespace astronaught
