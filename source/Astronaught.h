#pragma once

#include <FFGLSDK.h>

#include <string>

#include "Controls.h"
#include "Heads.h"
#include "PassBuffer.h"
#include "Presets.h"
#include "Spring.h"
#include "StoatworksAboutParams.h"
#include "Store.h"
#include "Tape.h"
#include "Transport.h"

/**
    Astronaught -- a video signal put through a Space Echo.

    A model of the Roland RE-201: an erase head, a record head and three
    playback heads at equal intervals along a loop of tape, a twelve-position
    Mode Selector that decides which of them are listening, an Intensity control
    that feeds the head mix back onto the tape, and a three-spring tank hanging
    off the input in parallel.

    ---------------------------------------------------------- what to read first

    `Transport.h`, and then `Tape.h`. The tape is indexed by **position**, never
    by time, and every recognisable Space Echo behaviour is a consequence of
    that one decision rather than of code written to produce it. A change that
    replaces the position bookkeeping with a timestamp and a subtraction will
    still render something plausible, will pass a smoke test, and will have
    quietly deleted the pitch drag, the chorus and the tear.

    `Heads.h` for the Mode Selector, which is a transcribed table and not a
    derivation -- the machine offers four of the eight possible head
    combinations and only one of them without reverb.

    ------------------------------------------------------------ the pass order

        Playback   the heads, off the tape as it stands
        Record     input + Intensity * playback, onto the tape
        Spring     the tank, fed from the INPUT
        Output     direct + echo + reverb, tone, mix

    ⚠️ Playback comes before Record, and that is the feedback loop: what the
    record head lays down includes what the playback heads found on the previous
    transit. Reversing them would close the loop within a single frame and the
    delay would stop being a delay.

    ⚠️ The tank is fed from the input and not from the playback bus. Roland:
    "the reverb signal path is connected in parallel to the tape echo section."

    --------------------------------------------------------------- the clock

    ⚠️ **Resolume sends `SetTime` in MILLISECONDS.** The FFGL header never says
    so, the SDK's own Particles sample divides by 1000, and this repo's harness
    sends seconds -- so a plugin that consumes `hostTime` raw runs a thousand
    times fast in a real host and no offline test can catch it. The unit is
    settled by comparing the host's own delta against a steady wall clock over
    several frames, which names the ratio outright.

    It matters more here than in most of the fleet. The clock does not merely
    animate something; it is integrated into the tape position, so a clock that
    is wrong by a factor of a thousand does not look fast, it looks like a
    plugin whose tape has already run out.

    ------------------------------------------------------ the About block, and why

    An FFGL 2.x plugin has no window, so the name, the version and the links are
    parameters and the host draws them. `SetTextParameter` **must** be overridden
    even though the text is display-only: the SDK's `instantiateGL` sets every
    parameter's default on a fresh instance and deletes the instance if any set
    returns FF_FAIL, and the base class's implementation is a stub that returns
    exactly that. Skip it and no real host can instantiate the plugin at all,
    while every harness that drives the class directly passes.
*/
class Astronaught : public CFFGLPlugin
{
public:
	/// Clock test hook. The harness DECLARES its unit rather than leaving the
	/// calibration to infer one: an absolute time handed over in a single frame
	/// is genuinely ambiguous, and an implicit unit is what lets a millisecond
	/// bug through in the first place.
	void SetClockScaleForTest( double scale );

	Astronaught();

	//CFFGLPlugin
	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

	FFResult SetTime( double time ) override;

	char* GetTextParameter( unsigned int index ) override;

	/// Display-only text still needs this. See the class comment.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

	/// Everything the operator can reach, in the order Resolume shows it: the
	/// echo, the medium, the transport, the tank, and the output stage.
	///
	/// Public because the harness drives the plugin by parameter id and needs
	/// PT_COUNT to enumerate them.
	///
	/// A released plugin's ids are free to be reordered -- Resolume matches a
	/// saved composition's parameters **by name**, measured on Arena 7.27.1 --
	/// but a released parameter must never be RENAMED, because a renamed
	/// control silently loses its saved value and only non-default values are
	/// written, so there is nothing left in the file to notice it by.
	enum ParamID : FFUInt32
	{
		//Echo
		PT_MODE,
		PT_REPEAT_RATE,
		PT_INTENSITY,
		PT_HEAD_SPREAD,

		//Tape
		PT_INPUT_LEVEL,
		PT_SATURATION,
		PT_HEAD_WEAR,
		PT_HISS,
		PT_DROPOUTS,

		//Transport
		PT_WOW_FLUTTER,
		PT_WOW,
		PT_FLUTTER,
		PT_SCRAPE,

		//Reverb
		PT_REVERB_TIME,
		PT_DISPERSION,

		//Output
		PT_ECHO_VOLUME,
		PT_REVERB_VOLUME,
		PT_DIRECT_VOLUME,
		PT_BASS,
		PT_TREBLE,
		PT_MIX,

		//Preset. After the real controls, so that a future insertion here does
		//not move them in the inspector.
		PT_PRESET,

		//About. FFGL has no window. See StoatworksAboutParams.h.
		PT_ABOUT_FIRST,
		PT_COUNT = PT_ABOUT_FIRST + stoatworks::about::kParamCount
	};

	/// params[] as the shared control struct. Public for the harness.
	astronaught::controls::HostValues hostValues() const;

	/// The resolved render settings from the frame just rendered, so the harness
	/// can assert on the plugin's own arithmetic rather than on a second copy of
	/// it in the test.
	const astronaught::controls::Render& lastRender() const
	{
		return renderOut;
	}

	/// The tape, for the same reason. `astest --read` compares this against what
	/// the shader resolved.
	const astronaught::tape::Loop& loop() const
	{
		return tape;
	}

	/// Where the record head is, absolutely.
	double tapePosition() const
	{
		return position;
	}

	/// Rendered frames between passes of the record head, as of the last frame.
	int stride() const
	{
		return writeStride;
	}

	/// The material the last rendered frame swept.
	double lastSpan() const
	{
		return span;
	}

private:
	/// The ParamID each presets::Param drives, in presets::Param order. The
	/// preset table stays host-agnostic; this is the binding of it.
	static constexpr unsigned int kPresetParamIDs[ astronaught::presets::kParamCount ] = {
		PT_MODE, PT_REPEAT_RATE, PT_INTENSITY, PT_HEAD_SPREAD, PT_INPUT_LEVEL,
		PT_SATURATION, PT_HEAD_WEAR, PT_HISS, PT_DROPOUTS, PT_WOW_FLUTTER,
		PT_WOW, PT_FLUTTER, PT_SCRAPE, PT_REVERB_TIME, PT_DISPERSION,
		PT_ECHO_VOLUME, PT_REVERB_VOLUME, PT_DIRECT_VOLUME, PT_BASS, PT_TREBLE
	};

	static bool isPresetParam( unsigned int index );

	/// Write a factory preset into `params[]`. 1-based; 0 is Custom.
	void applyPreset( int presetIndex );

	/// Record the host's opening position, once.
	///
	/// ☠️ MUST run before any preset can be applied. Seeding lazily from
	/// `params[]` inside the preset guard records the preset's own values as the
	/// host's last word, so the host's very next restatement looks like an
	/// operator edit and the dropdown snaps back to Custom. Called at the top of
	/// SetFloatParameter, ahead of the PT_PRESET branch.
	void seedHostSent();

	bool compileShaders();

	/// Push the slot table and the head positions. Shared by the playback pass
	/// and the probe, so the probe cannot drift from what it is probing.
	void setTapeUniforms( ffglex::FFGLShader& shader, const astronaught::controls::Render& r );

	ffglex::FFGLShader playbackShader;
	ffglex::FFGLShader recordShader;
	ffglex::FFGLShader springShader;
	ffglex::FFGLShader outputShader;
	ffglex::FFGLScreenQuad quad;

	/// The tape's pixels, and the bookkeeping that says what is where on it.
	astronaught::Store store;
	astronaught::tape::Loop tape;

	/// The head bus, at tape resolution. It feeds the record head, so it has to
	/// be at the resolution the record head writes.
	astronaught::PassBuffer playbackBuffer;

	/// The tank, ping-ponged: a spring's contents this frame are a function of
	/// its contents last frame, so it cannot be read and written at once.
	astronaught::PassBuffer tankBuffer[ 2 ];
	int tankFront = 0;

	//---------------------------------------------------------------------
	// Time. See the class comment: the host's unit is not knowable in advance.
	//---------------------------------------------------------------------
	double hostTime     = -1.0;
	double lastHostTime = -1.0;

	double clockScale   = 0.0;///< 0 until decided; then 1.0 or 0.001
	double lastWallTime = -1.0;
	double wallStart    = -1.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;
	double lastRawTime  = -1.0;
	int clockFrames     = 0;

	//---------------------------------------------------------------------
	// The transport's own state.
	//---------------------------------------------------------------------

	/// The record head, in material units. Absolute and monotonic; the loop
	/// keeps a rebased copy for the shader.
	double position = 0.0;

	/// Material the last rendered frame swept.
	double span = 0.0;

	/// A smoothed `span`, used for the stride decision and nothing else, so
	/// that one stalled frame cannot step the recording density.
	double spanAverage = 0.0;

	/// Rendered frames since the record head last passed, and how many it
	/// should wait.
	int framesSinceWrite = 0;
	int writeStride      = 1;

	/// Advances the hiss and the dropouts along the tape. Wrapped, and handed
	/// to the shader already reduced: an absolute count passes a float's
	/// resolution within a minute and the grain freezes into blocks.
	double grainPhase = 0.0;

	astronaught::controls::Render renderOut;

	/// What the render uses.
	///
	/// Zero-initialised: the constructor writes a default for every real
	/// control, but the About block's ids are never stored to -- pressing a
	/// button opens a browser and returns -- so without this GetFloatParameter
	/// hands the host whatever was on the stack for them.
	float params[ PT_COUNT ] = {};

	/// What the host last SENT, which is not the same thing. See Presets.h.
	float hostSent[ PT_COUNT ] = {};
	bool hostSeeded            = false;

	/// GetTextParameter hands the host a bare pointer, so the string has to
	/// outlive the call.
	std::string aboutText;
};
