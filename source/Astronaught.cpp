#include "Astronaught.h"

//The SDK's umbrella FFGLSDK.h pulls in every other scoped binding but leaves
//this one out (SDK b1afaf9), so it has to be reached for by hand.
#include <ffglex/FFGLScopedFBOBinding.h>

#include "Diag.h"
#include "Shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

using namespace ffglex;
using namespace astronaught;

//---------------------------------------------------------------------------
// The description is the seventh argument and it is NOT a caption. Arena
// ingests it and shows it as help text -- 110 of the effects that ship with
// 7.27.1 carry one, and its own CRT effect spends two paragraphs on how it
// works. It is the only prose an FFGL plugin can put in front of a user inside
// the host, there is no per-parameter tooltip of any kind, and most of this
// fleet wastes it on a one-line label.
//---------------------------------------------------------------------------
static CFFGLPluginInfo PluginInfo(
	PluginFactory< Astronaught >,
	"AN01",
	"Astronaught",
	2,
	1,
	0,
	1,
	FF_EFFECT,
	"A tape echo, modelled on the Roland RE-201 Space Echo, with the picture as "
	"the signal on the tape.\n\n"
	"Three playback heads sit at equal intervals along a loop, so their delays "
	"are locked at 1:2:3 and the Mode Selector chooses which are listening. "
	"Repeat Rate is a tape speed, not a delay time: turning it drags every echo "
	"already recorded, and the picture comes back stretched and torn because a "
	"frame is written down the tape while the tape is moving. Intensity feeds "
	"the heads back onto the tape, so each repeat is another generation.\n\n"
	"Modes 5 to 12 add a three-spring tank, fed in parallel with the tape.",
	"Astronaught FFGL effect"
);

namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;

/// The longest frame delta the transport will accept. A host that stalls --
/// loading a clip, or an operator dragging the timeline -- hands over an
/// enormous delta on the next frame, and integrating it would wind the tape
/// past everything on it in one step and erase the echo. Clamping costs nothing
/// anybody can see.
constexpr double kMaxFrameDelta = 0.1;

/// Where the grain phase wraps. Far enough apart that the seam is uncorrelated
/// with what came before it, near enough that a float still separates adjacent
/// texels.
constexpr double kGrainWrap = 65536.0;

/// Dropout blocks across one scanline.
constexpr float kDropBlocks = 20.0f;

/// Hiss correlation length along the scan, in tape texels, from a fresh head to
/// a worn one. A worn head has less bandwidth, so its noise is smeared wider.
constexpr float kHissWidthNew = 1.5f;
constexpr float kHissWidthOld = 5.0f;

/// Wall clock, for hosts that never call SetTime. Steady rather than system, so
/// it cannot go backwards when the machine's clock is corrected mid-show.
double wallSeconds()
{
	using namespace std::chrono;
	static const auto origin = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - origin ).count();
}

/// glGetString returns nullptr with no current context, and feeding that to
/// std::string is undefined behaviour. A logging call must never be the thing
/// that brings the host down.
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

double wrapGrain( double v )
{
	return v - kGrainWrap * std::floor( v / kGrainWrap );
}
} // namespace

Astronaught::Astronaught()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//Without this the host is entitled never to call SetTime, and a transport
	//with no clock is a tape that never moves.
	SetTimeSupported( true );

	//---------------------------------------------------------------------
	// Defaults. SetParamInfof reads each one back out of GetFloatParameter, so
	// these assignments are what the host is told the defaults are.
	//
	// They are a machine that is audibly -- visibly -- a Space Echo the moment
	// it lands on a layer: selector position 4, which is heads 2 and 3, so the
	// two-tap 2:3 rhythm is there straight away and nobody has to find the Mode
	// Selector to discover that this is not an ordinary delay. Intensity is up
	// far enough to give several repeats and nowhere near where it runs away,
	// and the transport is unsteady, because a Space Echo that runs dead steady
	// is a digital delay.
	//---------------------------------------------------------------------
	const controls::HostValues defaults;

	params[ PT_MODE ]        = defaults.mode;
	params[ PT_REPEAT_RATE ] = defaults.repeatRate;
	params[ PT_INTENSITY ]   = defaults.intensity;
	params[ PT_HEAD_SPREAD ] = defaults.headSpread;

	params[ PT_INPUT_LEVEL ] = defaults.inputLevel;
	params[ PT_SATURATION ]  = defaults.saturation;
	params[ PT_HEAD_WEAR ]   = defaults.headWear;
	params[ PT_HISS ]        = defaults.hiss;
	params[ PT_DROPOUTS ]    = defaults.dropouts;

	params[ PT_WOW_FLUTTER ] = defaults.wowFlutter;
	params[ PT_WOW ]         = defaults.wow;
	params[ PT_FLUTTER ]     = defaults.flutter;
	params[ PT_SCRAPE ]      = defaults.scrape;

	params[ PT_REVERB_TIME ] = defaults.reverbTime;
	params[ PT_DISPERSION ]  = defaults.dispersion;

	params[ PT_ECHO_VOLUME ]   = defaults.echoVolume;
	params[ PT_REVERB_VOLUME ] = defaults.reverbVolume;
	params[ PT_DIRECT_VOLUME ] = defaults.directVolume;
	params[ PT_BASS ]          = defaults.bass;
	params[ PT_TREBLE ]        = defaults.treble;
	params[ PT_MIX ]           = defaults.mix;

	params[ PT_PRESET ] = 0.0f;//Custom: the sliders are the truth

	//---------------------------------------------------------------------
	// Declaration.
	//
	// ⚠️ Every name here is 16 characters or fewer. FFGL's legacy
	// FF_GET_PARAMETER_NAME hands the host a 16-character buffer and the SDK
	// does not enforce it -- the plugin stores the whole string, hands over a
	// pointer to all of it, and Resolume copies sixteen. Every offline harness
	// passes, --list prints the full name, and only the host is ever wrong. Six
	// plugins in this fleet shipped a control called `Background Opaci` before
	// anybody noticed. `astest --names` is the gate.
	//
	// Group names are NOT subject to it.
	//---------------------------------------------------------------------
	SetOptionParamInfo( PT_MODE, "Mode", heads::kModeCount, params[ PT_MODE ] );
	for( int i = 0; i < heads::kModeCount; ++i )
		SetParamElementInfo( PT_MODE, i, heads::kModes[ i ].label, static_cast< float >( i ) );

	SetParamInfof( PT_REPEAT_RATE, "Repeat Rate", FF_TYPE_STANDARD );
	SetParamInfof( PT_INTENSITY, "Intensity", FF_TYPE_STANDARD );
	SetParamInfof( PT_HEAD_SPREAD, "Head Spread", FF_TYPE_STANDARD );

	SetParamInfof( PT_INPUT_LEVEL, "Input Level", FF_TYPE_STANDARD );
	SetParamInfof( PT_SATURATION, "Saturation", FF_TYPE_STANDARD );
	SetParamInfof( PT_HEAD_WEAR, "Head Wear", FF_TYPE_STANDARD );
	SetParamInfof( PT_HISS, "Hiss", FF_TYPE_STANDARD );
	SetParamInfof( PT_DROPOUTS, "Dropouts", FF_TYPE_STANDARD );

	SetParamInfof( PT_WOW_FLUTTER, "Wow & Flutter", FF_TYPE_STANDARD );
	SetParamInfof( PT_WOW, "Wow", FF_TYPE_STANDARD );
	SetParamInfof( PT_FLUTTER, "Flutter", FF_TYPE_STANDARD );
	SetParamInfof( PT_SCRAPE, "Scrape", FF_TYPE_STANDARD );

	SetParamInfof( PT_REVERB_TIME, "Reverb Time", FF_TYPE_STANDARD );
	SetParamInfof( PT_DISPERSION, "Dispersion", FF_TYPE_STANDARD );

	SetParamInfof( PT_ECHO_VOLUME, "Echo Volume", FF_TYPE_STANDARD );
	SetParamInfof( PT_REVERB_VOLUME, "Reverb Volume", FF_TYPE_STANDARD );
	SetParamInfof( PT_DIRECT_VOLUME, "Direct Volume", FF_TYPE_STANDARD );
	SetParamInfof( PT_BASS, "Bass", FF_TYPE_STANDARD );
	SetParamInfof( PT_TREBLE, "Treble", FF_TYPE_STANDARD );
	SetParamInfof( PT_MIX, "Mix", FF_TYPE_STANDARD );

	SetOptionParamInfo( PT_PRESET, "Preset", presets::elementCount(), params[ PT_PRESET ] );
	for( int i = 0; i < presets::elementCount(); ++i )
		SetParamElementInfo( PT_PRESET, i, presets::label( i ), static_cast< float >( i ) );

	// The About block. Inline rather than through a helper: SetParamInfo is
	// protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_FIRST, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_FIRST + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}

	// Groups, in the order the panel reads.
	for( FFUInt32 i = PT_MODE; i <= PT_HEAD_SPREAD; ++i )
		SetParamGroup( i, "Echo" );
	for( FFUInt32 i = PT_INPUT_LEVEL; i <= PT_DROPOUTS; ++i )
		SetParamGroup( i, "Tape" );
	for( FFUInt32 i = PT_WOW_FLUTTER; i <= PT_SCRAPE; ++i )
		SetParamGroup( i, "Transport" );
	for( FFUInt32 i = PT_REVERB_TIME; i <= PT_DISPERSION; ++i )
		SetParamGroup( i, "Reverb" );
	for( FFUInt32 i = PT_ECHO_VOLUME; i <= PT_MIX; ++i )
		SetParamGroup( i, "Output" );

	SetParamGroup( PT_PRESET, "Preset" );

	FFGLLog::LogToHost( "Created Astronaught effect" );

	diag::init();
}

controls::HostValues Astronaught::hostValues() const
{
	controls::HostValues out;

	out.mode       = params[ PT_MODE ];
	out.repeatRate = params[ PT_REPEAT_RATE ];
	out.intensity  = params[ PT_INTENSITY ];
	out.headSpread = params[ PT_HEAD_SPREAD ];

	out.inputLevel = params[ PT_INPUT_LEVEL ];
	out.saturation = params[ PT_SATURATION ];
	out.headWear   = params[ PT_HEAD_WEAR ];
	out.hiss       = params[ PT_HISS ];
	out.dropouts   = params[ PT_DROPOUTS ];

	out.wowFlutter = params[ PT_WOW_FLUTTER ];
	out.wow        = params[ PT_WOW ];
	out.flutter    = params[ PT_FLUTTER ];
	out.scrape     = params[ PT_SCRAPE ];

	out.reverbTime = params[ PT_REVERB_TIME ];
	out.dispersion = params[ PT_DISPERSION ];

	out.echoVolume   = params[ PT_ECHO_VOLUME ];
	out.reverbVolume = params[ PT_REVERB_VOLUME ];
	out.directVolume = params[ PT_DIRECT_VOLUME ];
	out.bass         = params[ PT_BASS ];
	out.treble       = params[ PT_TREBLE ];
	out.mix          = params[ PT_MIX ];

	return out;
}

bool Astronaught::compileShaders()
{
	struct Stage
	{
		FFGLShader* shader;
		std::string fragment;
		const char* name;
	};

	const Stage stages[] = {
		{ &playbackShader, shaders::PlaybackFragment(), "playback" },
		{ &recordShader, shaders::RecordFragment(), "record" },
		{ &springShader, shaders::SpringFragment(), "spring" },
		{ &outputShader, shaders::OutputFragment(), "output" },
	};

	for( const Stage& stage : stages )
	{
		if( !stage.shader->Compile( shaders::kVertex, stage.fragment.c_str() ) )
		{
			//Returning FF_FAIL from InitGL is invisible to the operator: the
			//effect simply does nothing, with no message anywhere. This line is
			//the only record of which stage it was -- and each of these is
			//assembled from several strings at runtime, so any line number the
			//driver reports refers to a file that does not exist.
			diag::error( std::string( "the " ) + stage.name
			             + " shader failed to compile - the effect will do nothing" );
			FFGLLog::LogToHost( "Astronaught: shader failed to compile" );
			return false;
		}
	}

	return true;
}

FFResult Astronaught::InitGL( const FFGLViewportStruct* vp )
{
	//The GL strings first, and unconditionally. When a shader will not compile
	//it is almost always the driver or the GL version, and knowing which machine
	//reported what is the whole diagnosis.
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR )
	            + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	if( !compileShaders() )
	{
		DeInitGL();
		return FF_FAIL;
	}

	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		FFGLLog::LogToHost( "Astronaught: quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	diag::info( "initialised" );

	//Use the base class init as the success result so it retains the viewport.
	return CFFGLPlugin::InitGL( vp );
}

void Astronaught::setTapeUniforms( FFGLShader& shader, const controls::Render& r )
{
	// The slot table. glUniform1fv rather than FFGLShader::Set, which has no
	// array overload -- an array pushed through the float one resolves to
	// Set(name, float) by implicit conversion and issues a glUniform1f against
	// a float[96], which is a GL_INVALID_OPERATION that leaves the whole array
	// at zero with nothing anywhere the plugin can see.
	glUniform1fv( shader.FindUniform( "SlotStart" ), tape::kSlots, tape.Starts().data() );
	glUniform1fv( shader.FindUniform( "SlotSpan" ), tape::kSlots, tape.Spans().data() );
	glUniform1i( shader.FindUniform( "TapeNewest" ), std::max( 0, tape.Newest() ) );
	glUniform1i( shader.FindUniform( "TapeWritten" ), tape.Written() );

	// Head positions, in material units behind the record head, with Head
	// Spread already multiplied in. Nothing in GLSL knows that the hardware's
	// spacing is 1 : 2 : 3.
	float offset[ heads::kHeadCount ];
	float up[ heads::kHeadCount ];
	for( int i = 0; i < heads::kHeadCount; ++i )
	{
		offset[ i ] = heads::kSpacing[ i ] * r.spread;
		up[ i ]     = r.selector.head[ i ] ? 1.0f : 0.0f;
	}

	glUniform1fv( shader.FindUniform( "HeadOffset" ), heads::kHeadCount, offset );
	glUniform1fv( shader.FindUniform( "HeadUp" ), heads::kHeadCount, up );

	// One over the heads actually up. Position 12 has none, and the bus is zero
	// there anyway, so it gets 1 rather than an infinity.
	const int upHeads = heads::headCount( r.mode );
	shader.Set( "HeadNorm", upHeads > 0 ? 1.0f / static_cast< float >( upHeads ) : 1.0f );

	// ⚠️ The START of this frame's sweep, not the end. `position` has already
	// been advanced by the time the playback pass runs, and handing that over
	// puts every head one frame of tape further on than it is.
	shader.Set( "HeadPos", static_cast< float >( position - span - tape.Origin() ) );
	shader.Set( "HeadSpan", static_cast< float >( span ) );
	shader.Set( "TapeTex", 0 );
}

FFResult Astronaught::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;

	const FFGLTextureStruct& input = *pGL->inputTextures[ 0 ];

	//The host's viewport, not the one InitGL was handed: Resolume changes
	//composition resolution without reinitialising the plugin.
	//
	//It also has to be captured before any pass runs, because ScopedFBOBinding
	//restores the framebuffer binding and NOT the viewport -- so an off-screen
	//pass's ResizeViewPort() leaks into the output pass, which draws to the
	//host's own framebuffer and has no buffer of its own to size itself from.
	//The symptom does not look like a viewport bug: the effect renders correctly
	//into a corner of the frame and leaves the rest untouched.
	GLint hostViewport[ 4 ] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, hostViewport );
	const int frameW = std::max( 1, hostViewport[ 2 ] );
	const int frameH = std::max( 1, hostViewport[ 3 ] );

	const controls::HostValues host = hostValues();

	//---------------------------------------------------------------------
	// Time. Normalise the host's clock to seconds first -- Resolume sends
	// milliseconds, this repo's harness sends seconds, and the FFGL header says
	// nothing at all.
	//
	// steady_clock says how much real time passed, the host says how much host
	// time passed, and the ratio names the unit outright -- 1 for seconds, 1000
	// for milliseconds, and nothing plausible in between. Several frames rather
	// than one, so a single odd frame cannot decide it alone.
	//---------------------------------------------------------------------
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;

	const double raw = hostTime;

	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;

		// A paused host, a looping clip or a stalled frame tells us nothing.
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;

			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
				clockScale = millisVotes > secondsVotes ? 0.001 : 1.0;
		}
	}

	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	// Until the unit is settled -- and for a host that never calls SetTime --
	// run on the real clock: wrong in origin but right in rate, where assuming
	// seconds would be a thousand times fast on Resolume.
	const double now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;

	const double dt = ( lastHostTime >= 0.0 ) ? std::clamp( now - lastHostTime, 0.0, kMaxFrameDelta )
	                                          : 0.0;

	renderOut = controls::render( host );

	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw )
		            + " scale=" + std::to_string( clockScale ) + " seconds=" + std::to_string( now )
		            + " position=" + std::to_string( position )
		            + " stride=" + std::to_string( writeStride ) );

	//---------------------------------------------------------------------
	// Wind the tape on.
	//
	// The position advances EVERY frame whether or not the record head passes.
	// The record head passing less often is a recording density, not a stopped
	// transport, and conflating the two is what would make Repeat Rate stop
	// dragging the echoes at long delays.
	//---------------------------------------------------------------------
	const double previousPosition = position;
	position                      = transport::advance( renderOut.transport, position, lastHostTime >= 0.0 ? lastHostTime : now, dt );
	span                          = std::max( 1e-9, position - previousPosition );

	lastHostTime = now;

	// The grain advances with the TAPE and not with the clock, which is what
	// makes the hiss sit still when the transport is slowed and stream when it
	// is not.
	grainPhase = wrapGrain( grainPhase + span * 40.0 );

	//---------------------------------------------------------------------
	// Every allocation FIRST, before anything is bound.
	//
	// FFGLFBO::Initialise sizes its colour texture inside a scoped texture
	// binding, and those CLEAR to 0 on scope exit rather than restoring -- so an
	// Ensure() called after a texture was bound silently unbinds it, and the
	// frame that allocated renders black. Do not move this below the passes.
	//---------------------------------------------------------------------
	if( !store.Ensure( frameW, frameH, tape::kSlots ) )
		return FF_FAIL;

	const int tapeW = store.Width();
	const int tapeH = store.Height();

	if( !playbackBuffer.Ensure( tapeW, tapeH, GL_RGBA16F ) )
	{
		diag::error( "could not allocate the playback buffer" );
		return FF_FAIL;
	}

	for( int i = 0; i < 2; ++i )
	{
		if( !tankBuffer[ i ].Ensure( tapeW, tapeH, GL_RGBA16F ) )
		{
			diag::error( "could not allocate the tank buffer" );
			return FF_FAIL;
		}
	}

	// ☠️ The ONLY thing allowed to erase the loop. A parameter change must not
	// -- the opposite of the fleet's usual GPU habit -- because the tape is the
	// instrument's memory and clearing it on a slider move would make every
	// control a reset button.
	if( store.ConsumeResized() )
	{
		tape.Erase();
		position  = 0.0;
		span      = std::max( 1e-9, span );
		framesSinceWrite = 0;
		diag::info( "tape erased: the composition resolution changed" );
	}

	// How often the record head should pass, so that the ring still reaches the
	// furthest head in use. Mode 12 has no heads up and needs no reach, so it
	// falls back to head 3's -- otherwise switching to Reverb Only and back
	// would find the tape recorded at a density the heads cannot use.
	{
		const float reach = renderOut.reach > 0.0f
		                        ? renderOut.reach
		                        : heads::kSpacing[ heads::kHeadCount - 1 ] * renderOut.spread;

		// Off a SMOOTHED span, not this frame's. One stalled frame makes `span`
		// enormous and one resumed frame makes it tiny, and either would move
		// the recording density for a frame -- which is a step change in what
		// every head reads, from a hiccup nobody asked to hear about.
		spanAverage = spanAverage > 0.0 ? spanAverage * 0.9 + span * 0.1 : span;
		writeStride = tape::strideFor( reach, spanAverage );
	}

	const FFGLTexCoords maxCoords = GetMaxGLTexCoords( input );

	//Every pass does its geometry in picture space and applies MaxUV at the
	//fetch, so the vertex shader's scaling is always off.
	const float kNoScale = 1.0f;

	const float tapeWf = static_cast< float >( tapeW );
	const float tapeHf = static_cast< float >( tapeH );

	//------------------------------------------------------------------
	// 1. Playback. Every head that is up, off the tape as it stands.
	//------------------------------------------------------------------
	{
		ScopedFBOBinding fbo( playbackBuffer.GetGLID(), ScopedFBOBinding::RB_REVERT );
		playbackBuffer.ResizeViewPort();
		ScopedShaderBinding shader( playbackShader.GetGLID() );

		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D_ARRAY, store.Texture() );

		playbackShader.Set( "MaxUV", kNoScale, kNoScale );
		setTapeUniforms( playbackShader, renderOut );

		quad.Draw();

		glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
	}

	//------------------------------------------------------------------
	// 2. Record. Input plus Intensity times the playback bus, onto the tape.
	//
	//    Only on the frames the record head passes. Every other frame the tape
	//    is still moving and nothing is being written to it, which is a gap in
	//    the recording and not a gap in the model -- the slot that eventually
	//    gets written spans the whole interval.
	//------------------------------------------------------------------
	if( ++framesSinceWrite >= writeStride )
	{
		framesSinceWrite = 0;

		const int slot = tape.Record( position );

		GLint restoreFBO = 0;
		glGetIntegerv( GL_FRAMEBUFFER_BINDING, &restoreFBO );

		if( store.BindLayer( slot ) )
		{
			ScopedShaderBinding shader( recordShader.GetGLID() );

			glActiveTexture( GL_TEXTURE0 );
			glBindTexture( GL_TEXTURE_2D, input.Handle );
			glActiveTexture( GL_TEXTURE1 );
			glBindTexture( GL_TEXTURE_2D, playbackBuffer.GetTextureInfo().Handle );
			glActiveTexture( GL_TEXTURE0 );

			recordShader.Set( "MaxUV", kNoScale, kNoScale );
			recordShader.Set( "InputTexture", 0 );
			recordShader.Set( "InputMaxUV", maxCoords.s, maxCoords.t );
			recordShader.Set( "PlaybackTexture", 1 );
			recordShader.Set( "Intensity", renderOut.tape.intensity );

			recordShader.Set( "TapeDrive", renderOut.tape.drive );
			recordShader.Set( "TapeSat", renderOut.tape.saturation );
			recordShader.Set( "TapeWear", renderOut.tape.headWear );
			recordShader.Set( "TapeHiss", renderOut.tape.hiss );
			recordShader.Set( "TapeHissW", kHissWidthNew + ( kHissWidthOld - kHissWidthNew ) * renderOut.tape.headWear );
			recordShader.Set( "TapeDrop", renderOut.tape.dropouts );
			recordShader.Set( "TapeDropBlk", kDropBlocks );
			recordShader.Set( "TapeGrain", static_cast< float >( grainPhase ) );
			recordShader.Set( "TapeTexelX", 1.0f / tapeWf );
			recordShader.Set( "TapeLines", tapeHf );

			quad.Draw();

			glActiveTexture( GL_TEXTURE1 );
			glBindTexture( GL_TEXTURE_2D, 0 );
			glActiveTexture( GL_TEXTURE0 );
			glBindTexture( GL_TEXTURE_2D, 0 );
		}

		glBindFramebuffer( GL_FRAMEBUFFER, static_cast< GLuint >( restoreFBO ) );
	}

	//------------------------------------------------------------------
	// 3. The tank.
	//
	//    ⚠️ Fed from the INPUT. In parallel with the tape, not after it.
	//------------------------------------------------------------------
	const spring::Coeffs tank = spring::coeffs( renderOut.tank, static_cast< float >( dt ) );
	const int tankBack        = 1 - tankFront;

	if( renderOut.tank.active )
	{
		ScopedFBOBinding fbo( tankBuffer[ tankBack ].GetGLID(), ScopedFBOBinding::RB_REVERT );
		tankBuffer[ tankBack ].ResizeViewPort();
		ScopedShaderBinding shader( springShader.GetGLID() );

		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, tankBuffer[ tankFront ].GetTextureInfo().Handle );
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, input.Handle );
		glActiveTexture( GL_TEXTURE0 );

		springShader.Set( "MaxUV", kNoScale, kNoScale );
		springShader.Set( "TankTexture", 0 );
		springShader.Set( "SourceTexture", 1 );
		springShader.Set( "SourceMaxUV", maxCoords.s, maxCoords.t );
		springShader.Set( "TankFeedback", tank.feedback );
		springShader.Set( "TankSpread", tank.spread );
		springShader.Set( "TankDrive", tank.drive );
		springShader.Set( "TankAspectWH", tapeWf / tapeHf );

		float legOffsets[ spring::kLegs * 2 ];
		for( int i = 0; i < spring::kLegs; ++i )
		{
			legOffsets[ i * 2 + 0 ] = spring::kLegOffsetX[ i ];
			legOffsets[ i * 2 + 1 ] = spring::kLegOffsetY[ i ];
		}

		glUniform1fv( springShader.FindUniform( "LegGain" ), spring::kLegs, tank.legGain );
		glUniform2fv( springShader.FindUniform( "LegOffset" ), spring::kLegs, legOffsets );

		quad.Draw();

		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, 0 );

		tankFront = tankBack;
	}

	//------------------------------------------------------------------
	// 4. The output stage, straight into whatever the host handed us.
	//------------------------------------------------------------------
	{
		glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
		glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );

		ScopedShaderBinding shader( outputShader.GetGLID() );

		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, input.Handle );
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, playbackBuffer.GetTextureInfo().Handle );
		glActiveTexture( GL_TEXTURE2 );
		glBindTexture( GL_TEXTURE_2D, tankBuffer[ tankFront ].GetTextureInfo().Handle );
		glActiveTexture( GL_TEXTURE0 );

		outputShader.Set( "MaxUV", kNoScale, kNoScale );
		outputShader.Set( "InputTexture", 0 );
		outputShader.Set( "InputMaxUV", maxCoords.s, maxCoords.t );
		outputShader.Set( "PlaybackTexture", 1 );
		outputShader.Set( "TankTexture", 2 );

		outputShader.Set( "EchoVolume", renderOut.echoVolume );
		outputShader.Set( "ReverbVolume", renderOut.reverbVolume );
		outputShader.Set( "DirectVolume", renderOut.directVolume );
		outputShader.Set( "BassGain", renderOut.bass );
		outputShader.Set( "TrebleGain", renderOut.treble );
		outputShader.Set( "MixAmount", renderOut.mix );
		outputShader.Set( "OutTexelX", 1.0f / static_cast< float >( frameW ) );

		quad.Draw();

		for( int unit = 2; unit >= 0; --unit )
		{
			glActiveTexture( GL_TEXTURE0 + unit );
			glBindTexture( GL_TEXTURE_2D, 0 );
		}
		glActiveTexture( GL_TEXTURE0 );
	}

	return FF_SUCCESS;
}

FFResult Astronaught::DeInitGL()
{
	playbackShader.FreeGLResources();
	recordShader.FreeGLResources();
	springShader.FreeGLResources();
	outputShader.FreeGLResources();
	quad.Release();

	playbackBuffer.Destroy();
	tankBuffer[ 0 ].Destroy();
	tankBuffer[ 1 ].Destroy();
	store.Destroy();
	tape.Erase();

	position         = 0.0;
	framesSinceWrite = 0;

	return FF_SUCCESS;
}

FFResult Astronaught::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

bool Astronaught::isPresetParam( unsigned int index )
{
	for( unsigned int id : kPresetParamIDs )
	{
		if( id == index )
			return true;
	}

	return false;
}

void Astronaught::seedHostSent()
{
	if( hostSeeded )
		return;

	for( unsigned int i = 0; i < PT_COUNT; ++i )
		hostSent[ i ] = params[ i ];

	hostSeeded = true;
}

FFResult Astronaught::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// ☠️ Before the preset branch, always. Seeding lazily from inside it would
	// record the preset's own values as the host's opening position. See
	// Astronaught.h.
	seedHostSent();

	// An About button is a press, not a value to keep: it opens a browser and
	// nothing about the effect changes.
	if( index >= PT_ABOUT_FIRST )
		return stoatworks::about::handleParam( index - PT_ABOUT_FIRST, value ) ? FF_SUCCESS : FF_FAIL;

	if( index == PT_PRESET )
	{
		hostSent[ index ] = value;

		const int chosen = static_cast< int >( std::lround( value ) );
		if( chosen != static_cast< int >( std::lround( params[ PT_PRESET ] ) ) )
			applyPreset( chosen );

		return FF_SUCCESS;
	}

	const float lastFromHost = hostSent[ index ];
	hostSent[ index ]        = value;

	const int active = static_cast< int >( std::lround( params[ PT_PRESET ] ) );
	if( active > 0 && isPresetParam( index ) )
	{
		// Three cases, and only the third is an operator.
		//
		//   The host restating what it already said  -> ignore
		//   The host echoing the preset back at us   -> ignore
		//   Anything else                            -> a real edit
		//
		// Judged on what the value IS. Judging on "did it change" is the bug:
		// Resolume does not consume value events, so after a preset lands it
		// carries on pushing the values it still believes in, and those have
		// changed relative to the preset every single frame.
		const bool restatement  = std::fabs( value - lastFromHost ) <= presets::kEchoTolerance;
		const bool echoOfPreset = std::fabs( value - params[ index ] ) <= presets::kEchoTolerance;

		if( restatement || echoOfPreset )
			return FF_SUCCESS;

		params[ index ]     = value;
		params[ PT_PRESET ] = 0.0f;
		diag::info( "preset dropped to Custom: parameter " + std::to_string( index ) + " moved to "
		            + std::to_string( value ) );
		RaiseParamEvent( PT_PRESET, FF_EVENT_FLAG_VALUE );
		return FF_SUCCESS;
	}

	params[ index ] = value;
	return FF_SUCCESS;
}

void Astronaught::applyPreset( int presetIndex )
{
	params[ PT_PRESET ] = static_cast< float >( presetIndex );

	const float* values = presets::values( presetIndex );
	if( values == nullptr )
		return;//Custom: the sliders keep whatever they said

	for( int j = 0; j < presets::kParamCount; ++j )
	{
		const unsigned int id = kPresetParamIDs[ j ];
		if( std::fabs( params[ id ] - values[ j ] ) <= 1e-6f )
			continue;

		// The write is what changes the picture. The event only asks the host to
		// re-read the slider, and a host that ignores it -- Resolume does --
		// renders the preset correctly and merely shows stale knobs.
		params[ id ] = values[ j ];
		RaiseParamEvent( id, FF_EVENT_FLAG_VALUE );
	}
}

float Astronaught::GetFloatParameter( unsigned int index )
{
	if( index >= PT_COUNT )
		return 0.0f;

	return params[ index ];
}

char* Astronaught::GetTextParameter( unsigned int index )
{
	// The host is handed a bare pointer, so the string is kept as a member
	// rather than built on the stack here.
	if( index == PT_ABOUT_FIRST )
	{
		aboutText = stoatworks::about::textParam( 0 );
		return const_cast< char* >( aboutText.c_str() );
	}

	return CFFGLPlugin::GetTextParameter( index );
}

FFResult Astronaught::SetTextParameter( unsigned int index, const char* value )
{
	// See the declaration: the base class returns FF_FAIL, and instantiateGL
	// deletes the whole instance when setting any default fails. The About text
	// is display-only, so there is genuinely nothing to store -- but it has to
	// say so successfully.
	(void)value;

	if( index == PT_ABOUT_FIRST )
		return FF_SUCCESS;

	return CFFGLPlugin::SetTextParameter( index, value );
}

void Astronaught::SetClockScaleForTest( double scale )
{
	clockScale = scale;
}
