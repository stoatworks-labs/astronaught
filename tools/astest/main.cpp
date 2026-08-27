/**
    astest -- the offline harness.

    It drives the **real** `Astronaught` class through the real FFGL sequence in
    a headless core-profile context, so what it measures is the plugin and not a
    model of it. `--read` goes further and compares the GLSL against the C++ it
    mirrors, through a probe shader assembled from the same string the render
    uses.

    ----------------------------------------------------- what each check is for

    `--modes`    the Mode Selector against a second transcription of Roland's
                 printed table. The one check here that is about a FACT rather
                 than about this code being self-consistent.
    `--ratios`   the heads are at 1 : 2 : 3, and the delays follow.
    `--delay`    Repeat Rate spans the range the README claims, and runs the
                 right way round.
    `--rate`     the transport is frame-rate independent.
    `--drag`     ☠️ the load-bearing one. Changing Repeat Rate must DRAG the
                 echoes already on the tape rather than re-timing them, which is
                 the difference between a tape loop and a frame queue.
    `--chorus`   the three heads wobble independently.
    `--read`     the GLSL tape read against Tape.cpp.
    `--identity` Mix at zero leaves the picture alone, byte for byte.
    `--presets`  every preset renders something with structure in it.
    `--guard`    a hostile machine leaves no NaN.

    ⚠️ **A brightness floor is not an acceptance test here.** This plugin has a
    feedback loop in it, so its characteristic failures are a black frame *and*
    a saturated one, and "is anything lit" cannot see the second. `--presets`
    measures the standard deviation of luma and rejects dark, flat and blown-out
    separately. Escapement shipped five flat washes and a solid green rectangle
    past a `mean > 0.002` test before that was understood.

    ⚠️ **A fresh plugin per render, always.** The tape is state. Two settings
    compared through one instance differ because of history, not because of the
    settings, and every comparison in this file would be measuring the order the
    tests ran in.

    None of this catches a dead control. See `tools/sweep.py`.
*/

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Astronaught.h"
#include "Controls.h"
#include "Heads.h"
#include "Presets.h"
#include "Shaders.h"
#include "Spring.h"
#include "Tape.h"
#include "Transport.h"

using namespace astronaught;

namespace
{
int failures = 0;

void pass( const std::string& what )
{
	std::printf( "  ok    %s\n", what.c_str() );
}

void fail( const std::string& what )
{
	std::printf( "  FAIL  %s\n", what.c_str() );
	++failures;
}

void check( bool ok, const std::string& what )
{
	ok ? pass( what ) : fail( what );
}

//---------------------------------------------------------------------------
// PNG. zlib ships with the OS, so this is fifty lines rather than a dependency.
//---------------------------------------------------------------------------
void putBigEndian( std::vector< unsigned char >& out, unsigned int value )
{
	out.push_back( static_cast< unsigned char >( ( value >> 24 ) & 0xff ) );
	out.push_back( static_cast< unsigned char >( ( value >> 16 ) & 0xff ) );
	out.push_back( static_cast< unsigned char >( ( value >> 8 ) & 0xff ) );
	out.push_back( static_cast< unsigned char >( value & 0xff ) );
}

void putChunk( std::vector< unsigned char >& out, const char* type, const unsigned char* data,
               size_t length )
{
	putBigEndian( out, static_cast< unsigned int >( length ) );
	const size_t crcStart = out.size();
	out.insert( out.end(), type, type + 4 );
	if( length > 0 )
		out.insert( out.end(), data, data + length );
	const unsigned long crc =
	    crc32( 0, out.data() + crcStart, static_cast< unsigned int >( out.size() - crcStart ) );
	putBigEndian( out, static_cast< unsigned int >( crc ) );
}

bool writePng( const std::string& path, int width, int height,
               const std::vector< unsigned char >& rgba )
{
	std::vector< unsigned char > raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = 0; y < height; ++y )
	{
		raw.push_back( 0 );
		const unsigned char* row = rgba.data() + static_cast< size_t >( y ) * width * 4;
		raw.insert( raw.end(), row, row + static_cast< size_t >( width ) * 4 );
	}

	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	std::vector< unsigned char > compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(),
	               static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	std::vector< unsigned char > png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

	std::vector< unsigned char > ihdr;
	putBigEndian( ihdr, static_cast< unsigned int >( width ) );
	putBigEndian( ihdr, static_cast< unsigned int >( height ) );
	ihdr.push_back( 8 );
	ihdr.push_back( 6 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	putChunk( png, "IHDR", ihdr.data(), ihdr.size() );
	putChunk( png, "IDAT", compressed.data(), compressed.size() );
	putChunk( png, "IEND", nullptr, 0 );

	FILE* file = std::fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = std::fwrite( png.data(), 1, png.size(), file );
	std::fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// The test card.
//
// Built to make an ECHO measurable, which is a different job from making a
// picture look nice. It has:
//
//   - a moving bar, so one repeat can be told from another and from the direct
//     signal by where it is;
//   - hard horizontal edges, so the vertical tear has something to cut;
//   - a fine vertical grating, so head wear and the tone shelves have detail
//     along the scan to take away;
//   - a flat mid-grey field, so hiss and dropouts are visible against it.
//---------------------------------------------------------------------------
std::vector< unsigned char > testCard( int width, int height, int frame )
{
	std::vector< unsigned char > rgba( static_cast< size_t >( width ) * height * 4 );

	const float t = static_cast< float >( frame );

	for( int y = 0; y < height; ++y )
	{
		for( int x = 0; x < width; ++x )
		{
			const float u = static_cast< float >( x ) / width;
			const float v = static_cast< float >( y ) / height;

			float r = 0.18f, g = 0.18f, b = 0.20f;

			//A fine vertical grating across the lower third: detail along the
			//scan, which is the only axis anything here filters on.
			if( v > 0.68f )
			{
				const float bars = ( static_cast< int >( u * width ) % 6 < 3 ) ? 0.62f : 0.10f;
				r = g = b = bars;
			}

			//Hard horizontal bands: something for the tear to cut.
			if( v > 0.30f && v < 0.36f )
			{
				r = 0.85f;
				g = 0.20f;
				b = 0.15f;
			}

			//The moving bar.
			const float barX = std::fmod( t * 0.013f, 1.0f );
			if( std::fabs( u - barX ) < 0.035f && v > 0.06f && v < 0.26f )
			{
				r = 0.95f;
				g = 0.95f;
				b = 0.30f;
			}

			const size_t p = ( static_cast< size_t >( y ) * width + x ) * 4;
			rgba[ p + 0 ]  = static_cast< unsigned char >( std::lround( r * 255.0f ) );
			rgba[ p + 1 ]  = static_cast< unsigned char >( std::lround( g * 255.0f ) );
			rgba[ p + 2 ]  = static_cast< unsigned char >( std::lround( b * 255.0f ) );
			rgba[ p + 3 ]  = 255;
		}
	}

	return rgba;
}

//---------------------------------------------------------------------------
// GL plumbing.
//---------------------------------------------------------------------------
CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated, kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute software[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ), kCGLPFAAlphaSize,
		static_cast< CGLPixelFormatAttribute >( 8 ), static_cast< CGLPixelFormatAttribute >( 0 )
	};

	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( software, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}

	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;

	CGLSetCurrentContext( context );
	return context;
}

struct Target
{
	GLuint texture = 0;
	GLuint fbo     = 0;
	int width      = 0;
	int height     = 0;
};

Target makeTarget( int width, int height, GLenum internalFormat = GL_RGBA8 )
{
	Target target;
	target.width  = width;
	target.height = height;

	const bool isFloat = internalFormat == GL_RGBA32F;

	glGenTextures( 1, &target.texture );
	glBindTexture( GL_TEXTURE_2D, target.texture );
	glTexImage2D( GL_TEXTURE_2D, 0, static_cast< GLint >( internalFormat ), width, height, 0,
	              GL_RGBA, isFloat ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glBindTexture( GL_TEXTURE_2D, 0 );

	glGenFramebuffers( 1, &target.fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, target.fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0 );
	return target;
}

void releaseTarget( Target& target )
{
	if( target.fbo != 0 )
		glDeleteFramebuffers( 1, &target.fbo );
	if( target.texture != 0 )
		glDeleteTextures( 1, &target.texture );
	target = Target();
}

/// Upload a **top-down** picture as a texture in GL's convention.
///
/// ☠️ The rows are flipped on the way in, and that is not cosmetic. glTexImage2D
/// treats the first row of data as t = 0, which in GL is the BOTTOM of the
/// picture -- so handing it a top-down card puts the card's first scanline at
/// t = 0, and the harness is then feeding the plugin an upside-down frame.
///
/// The plugin computes `scan = 1.0 - uv.y`, its progress down the raster, from
/// the assumption that uv.y = 1 is the first scanline, which is what a real host
/// hands it. Uploading unflipped inverts that: the vertical tear rolls the
/// opposite way from the way it rolls in Resolume, and every judgement made by
/// looking at a rendered frame here is a judgement about a picture the host will
/// never produce. `--identity` is what caught it -- the top-left pixel came back
/// holding the grating that belongs in the bottom third.
GLuint uploadTexture( const std::vector< unsigned char >& topDown, int width, int height )
{
	std::vector< unsigned char > glOrder( topDown.size() );
	const size_t stride = static_cast< size_t >( width ) * 4;
	for( int y = 0; y < height; ++y )
	{
		std::memcpy( glOrder.data() + static_cast< size_t >( y ) * stride,
		             topDown.data() + static_cast< size_t >( height - 1 - y ) * stride, stride );
	}

	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
	              glOrder.data() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

/// Straight out of GL, bottom row first.
std::vector< unsigned char > readBytes( const Target& target )
{
	std::vector< unsigned char > pixels( static_cast< size_t >( target.width ) * target.height * 4 );
	glBindFramebuffer( GL_FRAMEBUFFER, target.fbo );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( 0, 0, target.width, target.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
	return pixels;
}

std::vector< float > readFloats( const Target& target )
{
	std::vector< float > pixels( static_cast< size_t >( target.width ) * target.height * 4 );
	glBindFramebuffer( GL_FRAMEBUFFER, target.fbo );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( 0, 0, target.width, target.height, GL_RGBA, GL_FLOAT, pixels.data() );
	return pixels;
}

std::vector< unsigned char > flipRows( const std::vector< unsigned char >& image, int width,
                                       int height )
{
	std::vector< unsigned char > flipped( image.size() );
	const size_t stride = static_cast< size_t >( width ) * 4;
	for( int y = 0; y < height; ++y )
		std::memcpy( flipped.data() + static_cast< size_t >( y ) * stride,
		             image.data() + static_cast< size_t >( height - 1 - y ) * stride, stride );
	return flipped;
}

//---------------------------------------------------------------------------
// Driving the plugin.
//
// InitGL is called only when the size changes. Not tidiness: InitGL compiles
// the shaders and FFGLShader::Compile does not free the program it replaces, so
// calling it per frame leaks four programs per frame.
//---------------------------------------------------------------------------
struct Driver
{
	Astronaught plugin;
	int width  = 0;
	int height = 0;

	Driver()
	{
		//The harness sends SECONDS. Declared rather than inferred: an absolute
		//time handed over in one frame is genuinely ambiguous, and an implicit
		//unit is what lets a milliseconds bug through in the first place.
		plugin.SetClockScaleForTest( 1.0 );
	}

	bool render( const Target& target, GLuint input, int inputWidth, int inputHeight )
	{
		FFGLTextureStruct inputStruct {};
		inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( inputWidth );
		inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( inputHeight );
		inputStruct.Handle                              = input;
		FFGLTextureStruct* inputs[ 1 ]                  = { &inputStruct };

		ProcessOpenGLStruct process {};
		process.numInputTextures = 1;
		process.inputTextures    = inputs;
		process.HostFBO          = target.fbo;

		glBindFramebuffer( GL_FRAMEBUFFER, target.fbo );
		glViewport( 0, 0, target.width, target.height );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );

		if( target.width != width || target.height != height )
		{
			FFGLViewportStruct viewport {};
			viewport.width  = static_cast< FFUInt32 >( target.width );
			viewport.height = static_cast< FFUInt32 >( target.height );
			if( plugin.InitGL( &viewport ) != FF_SUCCESS )
				return false;
			width  = target.width;
			height = target.height;
		}

		return plugin.ProcessOpenGL( &process ) == FF_SUCCESS;
	}

	/// Run `frames` frames at `fps`, returning what the last one drew.
	bool run( const Target& target, int frames, double fps )
	{
		for( int i = 0; i < frames; ++i )
		{
			const std::vector< unsigned char > card = testCard( target.width, target.height, i );
			const GLuint input = uploadTexture( card, target.width, target.height );

			plugin.SetTime( i / fps );
			const bool ok = render( target, input, target.width, target.height );
			glDeleteTextures( 1, &input );

			if( !ok )
				return false;
		}
		return true;
	}
};

/// Every parameter's host-facing name, read out of the plugin itself.
///
/// Built at runtime rather than kept as a table beside Controls.h: a
/// hand-written table is a second place for a name to live, and the failure it
/// produces is a `--set` that silently addresses nothing while everything else
/// about the run looks correct.
std::map< std::string, unsigned int > parameterIndex( Astronaught& plugin )
{
	std::map< std::string, unsigned int > byName;
	for( unsigned int id = 0; id < Astronaught::PT_COUNT; ++id )
	{
		const char* name = plugin.GetParamName( id );
		if( name != nullptr && name[ 0 ] != '\0' )
			byName[ name ] = id;
	}
	return byName;
}

//---------------------------------------------------------------------------
// Statistics on a rendered frame.
//---------------------------------------------------------------------------
struct Stats
{
	float mean   = 0.0f;
	float stddev = 0.0f;
	float blown  = 0.0f;///< fraction of pixels at or above 0.99 luma
	bool finite  = true;
};

Stats lumaStats( const std::vector< unsigned char >& rgba )
{
	Stats s;
	const size_t pixels = rgba.size() / 4;
	if( pixels == 0 )
		return s;

	double sum = 0.0, sumSq = 0.0;
	size_t hot = 0;

	for( size_t i = 0; i < pixels; ++i )
	{
		const double r = rgba[ i * 4 + 0 ] / 255.0;
		const double g = rgba[ i * 4 + 1 ] / 255.0;
		const double b = rgba[ i * 4 + 2 ] / 255.0;
		const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;

		sum += y;
		sumSq += y * y;
		if( y >= 0.99 )
			++hot;
	}

	s.mean   = static_cast< float >( sum / pixels );
	s.stddev = static_cast< float >( std::sqrt( std::max( 0.0, sumSq / pixels - ( sum / pixels ) * ( sum / pixels ) ) ) );
	s.blown  = static_cast< float >( static_cast< double >( hot ) / pixels );
	return s;
}
} // namespace

//---------------------------------------------------------------------------
// A recorded transport, for the checks that ask "how old is the material under
// this head".
//
// ⚠️ `ageAt` INTERPOLATES between samples, and that is not a refinement. The
// snapping version reports a quantisation error of one frame per head, which on
// a 500 ms delay at 120 fps is 1.7% -- and because the check compares head 3
// against three times head 1, the errors add to about 6.5%. That is the same
// order as the effect being measured, so the snapping version showed a steady
// transport "decorrelating" and failed a model that was correct. The lesson is
// the usual one: when a measurement and the thing it measures are the same
// size, the measurement is the bug.
//---------------------------------------------------------------------------
struct Tapeline
{
	std::vector< double > position;
	std::vector< double > time;

	void mark( double p, double t )
	{
		position.push_back( p );
		time.push_back( t );
	}

	/// Seconds since the material now at `p` was laid down. Negative if `p` is
	/// older than anything recorded.
	double ageAt( double p, double now ) const
	{
		for( size_t i = position.size(); i-- > 1; )
		{
			if( position[ i - 1 ] <= p && p <= position[ i ] )
			{
				const double denom = position[ i ] - position[ i - 1 ];
				const double f     = denom > 0.0 ? ( p - position[ i - 1 ] ) / denom : 0.0;
				return now - ( time[ i - 1 ] + f * ( time[ i ] - time[ i - 1 ] ) );
			}
		}
		return -1.0;
	}
};

//===========================================================================
// The checks.
//===========================================================================
namespace
{
//---------------------------------------------------------------------------
// The Mode Selector, transcribed a SECOND time.
//
// This is the only check in the file that tests a fact about the world rather
// than this code's consistency with itself, and it is written out again here
// rather than looped over Heads.cpp so that an edit to the table has to be made
// twice, deliberately, in two places, by somebody who has looked at the source.
//
// From the BOSS RE-20 owner's manual, "About the Variation Mode" (p.18). NOT
// the RE-202's table, which has a fourth playback head and differs in five
// positions.
//---------------------------------------------------------------------------
int checkModes()
{
	std::printf( "the Mode Selector\n" );

	struct Row
	{
		int position;
		bool h1, h2, h3, reverb;
	};

	const Row expected[] = {
		{ 1, true, false, false, false },  { 2, false, true, false, false },
		{ 3, false, false, true, false },  { 4, false, true, true, false },
		{ 5, true, false, false, true },   { 6, false, true, false, true },
		{ 7, false, false, true, true },   { 8, true, true, false, true },
		{ 9, false, true, true, true },    { 10, true, false, true, true },
		{ 11, true, true, true, true },    { 12, false, false, false, true },
	};

	check( heads::kModeCount == 12, "twelve positions" );

	for( const Row& row : expected )
	{
		const heads::Mode& m = heads::mode( row.position - 1 );
		const bool ok = m.head[ 0 ] == row.h1 && m.head[ 1 ] == row.h2 && m.head[ 2 ] == row.h3
		                && m.reverb == row.reverb;

		char what[ 128 ];
		std::snprintf( what, sizeof( what ), "position %2d: heads %d%d%d reverb %d", row.position,
		               row.h1 ? 1 : 0, row.h2 ? 1 : 0, row.h3 ? 1 : 0, row.reverb ? 1 : 0 );
		check( ok, what );
	}

	//The negative half, and the reason this table is worth a test at all: the
	//obvious guess is all eight subsets dry and wet, and the machine does not
	//offer heads 1+2 dry or all three dry at any position.
	bool dry12 = false, dry123 = false, dry13 = false;
	for( int i = 0; i < heads::kModeCount; ++i )
	{
		const heads::Mode& m = heads::mode( i );
		if( m.reverb )
			continue;
		if( m.head[ 0 ] && m.head[ 1 ] && !m.head[ 2 ] )
			dry12 = true;
		if( m.head[ 0 ] && m.head[ 1 ] && m.head[ 2 ] )
			dry123 = true;
		if( m.head[ 0 ] && !m.head[ 1 ] && m.head[ 2 ] )
			dry13 = true;
	}

	check( !dry12, "there is NO echo-only 1+2 position" );
	check( !dry123, "there is NO echo-only 1+2+3 position" );
	check( !dry13, "there is NO echo-only 1+3 position" );

	//Modes 1-4 are echo only, 5-12 have the tank in.
	bool split = true;
	for( int i = 0; i < 4; ++i )
		split = split && !heads::mode( i ).reverb;
	for( int i = 4; i < 12; ++i )
		split = split && heads::mode( i ).reverb;
	check( split, "1-4 are echo only, 5-12 have the reverb in" );

	check( !heads::anyHead( 11 ), "position 12 is Reverb Only: no head is up" );

	return 0;
}

//---------------------------------------------------------------------------
// The head block's geometry, and the delays that follow from it.
//---------------------------------------------------------------------------
int checkRatios()
{
	std::printf( "head spacing\n" );

	check( heads::kSpacing[ 0 ] == 1.0f && heads::kSpacing[ 1 ] == 2.0f
	           && heads::kSpacing[ 2 ] == 3.0f,
	       "the heads are at equal intervals: 1 : 2 : 3" );

	//Through the real conversion, at several Repeat Rate positions, so that a
	//change to Controls.cpp that broke the ratio would be caught here and not
	//only in Heads.cpp.
	for( float rate : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f } )
	{
		controls::HostValues host;
		host.repeatRate = rate;
		host.headSpread = 0.4f;

		const controls::Render r = controls::render( host );

		const float d1 = r.baseDelay * heads::kSpacing[ 0 ];
		const float d2 = r.baseDelay * heads::kSpacing[ 1 ];
		const float d3 = r.baseDelay * heads::kSpacing[ 2 ];

		const bool ok = std::fabs( d2 - 2.0f * d1 ) < 1e-6f && std::fabs( d3 - 3.0f * d1 ) < 1e-6f;

		char what[ 128 ];
		std::snprintf( what, sizeof( what ), "rate %.2f: %.0f / %.0f / %.0f ms", rate, d1 * 1000.0f,
		               d2 * 1000.0f, d3 * 1000.0f );
		check( ok, what );
	}

	//Head Spread is nominal at its default, which is what makes the default a
	//real machine rather than an approximation of one.
	controls::HostValues host;
	const controls::Render r = controls::render( host );
	check( std::fabs( r.spread - 1.0f ) < 0.02f,
	       "Head Spread's default is 1.00, the hardware's own spacing" );

	return 0;
}

//---------------------------------------------------------------------------
// Repeat Rate.
//---------------------------------------------------------------------------
int checkDelay()
{
	std::printf( "Repeat Rate\n" );

	controls::HostValues slow;
	slow.repeatRate = 0.0f;
	controls::HostValues fast;
	fast.repeatRate = 1.0f;

	const float dSlow = controls::render( slow ).baseDelay;
	const float dFast = controls::render( fast ).baseDelay;

	check( std::fabs( dSlow - controls::kDelaySlow ) < 1e-4f, "at 0 the head-1 delay is 1.000 s" );
	check( std::fabs( dFast - controls::kDelayFast ) < 1e-4f, "at 1 the head-1 delay is 0.070 s" );

	//☠️ The control runs BACKWARDS from the quantity. A sweep that only checks
	//the control is alive cannot tell this from the right way round.
	check( dFast < dSlow, "turning Repeat Rate UP makes the delay SHORTER" );

	//Head 3 at the slow end is the three seconds the machine is known for.
	check( std::fabs( dSlow * heads::kSpacing[ 2 ] - 3.0f ) < 1e-3f,
	       "head 3 at the slow end is 3.000 s" );

	//The midpoint is geometric, which is the whole reason the curve is
	//exponential.
	controls::HostValues mid;
	mid.repeatRate    = 0.5f;
	const float dMid  = controls::render( mid ).baseDelay;
	const float wanted = std::sqrt( controls::kDelaySlow * controls::kDelayFast );
	check( std::fabs( dMid - wanted ) < 1e-4f, "half a slider is the geometric middle" );

	//Speed and delay are exact reciprocals through one material unit.
	const controls::Render r = controls::render( mid );
	check( std::fabs( r.transport.speed * r.baseDelay - heads::kSpacing[ 0 ] ) < 1e-4f,
	       "speed x delay = the head-1 spacing" );

	return 0;
}

//---------------------------------------------------------------------------
// Frame-rate independence.
//
// The transport is integrated, so a frame rate that changed the answer would be
// a plugin whose echo times depend on the machine it is running on.
//---------------------------------------------------------------------------
int checkRate()
{
	std::printf( "the transport is frame-rate independent\n" );

	controls::HostValues host;
	const controls::Render r = controls::render( host );

	const double seconds = 4.0;

	auto travel = [ & ]( double fps ) {
		double p = 0.0;
		const int frames = static_cast< int >( seconds * fps );
		for( int i = 0; i < frames; ++i )
			p = transport::advance( r.transport, p, i / fps, 1.0 / fps );
		return p;
	};

	const double at30  = travel( 30.0 );
	const double at60  = travel( 60.0 );
	const double at144 = travel( 144.0 );

	//The nominal distance in four seconds.
	const double nominal = r.transport.speed * seconds;

	char what[ 160 ];
	std::snprintf( what, sizeof( what ), "4 s of tape: %.4f at 30 fps, %.4f at 60, %.4f at 144 (nominal %.4f)",
	               at30, at60, at144, nominal );
	std::printf( "  ..    %s\n", what );

	check( std::fabs( at30 - at60 ) / nominal < 0.002, "30 fps and 60 fps agree to 0.2%" );
	check( std::fabs( at60 - at144 ) / nominal < 0.002, "60 fps and 144 fps agree to 0.2%" );
	check( std::fabs( at60 - nominal ) / nominal < 0.02,
	       "the wobble averages out: within 2% of nominal over 4 s" );

	return 0;
}

//---------------------------------------------------------------------------
// ☠️ The drag. The load-bearing check.
//
// Roland, on the RE-201: turning Repeat Rate up means "sounds are played back
// more closely together, and the pitch begins to rise", and simultaneously "the
// density of the sounds during recording gradually decreases, so when those
// sounds reach the playback heads, the pitches that were raised then begin to
// fall."
//
// Both halves are the same statement: the material does not move when the speed
// changes, only the rate at which the heads reach it. So the age of what is
// under a head immediately after a speed change is still close to the OLD
// delay, and it converges on the new one over the following transit.
//
// A plugin that stores a frame with a timestamp and subtracts a delay would
// jump straight to the new delay on the very next frame. That version renders
// something plausible and is not a tape echo, and this is the check that knows
// the difference.
//---------------------------------------------------------------------------
int checkDrag()
{
	std::printf( "changing Repeat Rate DRAGS the echoes already on the tape\n" );

	//A steady transport, so the measurement is about the speed change and not
	//about the wobble.
	transport::Settings slow;
	slow.speed  = 2.0f;///< head 1 at 0.5 s
	slow.amount = 0.0f;

	transport::Settings fast = slow;
	fast.speed               = 8.0f;///< head 1 at 0.125 s

	const double fps = 60.0;
	const double dt  = 1.0 / fps;
	const double D   = heads::kSpacing[ 0 ];

	//Wind on at the slow speed, recording where the tape was at each frame, so
	//the age of any position can be looked up afterwards.
	Tapeline line;

	double p = 0.0;
	double t = 0.0;
	for( int i = 0; i < 240; ++i )
	{
		line.mark( p, t );
		p = transport::advance( slow, p, t, dt );
		t += dt;
	}

	const double settled = line.ageAt( p - D, t );
	char what[ 160 ];
	std::snprintf( what, sizeof( what ), "settled at the slow speed, head 1 reads %.0f ms old material",
	               settled * 1000.0 );
	std::printf( "  ..    %s\n", what );
	check( std::fabs( settled - 0.5 ) < 0.02, "which is the 500 ms the slow speed asks for" );

	//Now the knob is turned. Advance ONE frame at the new speed.
	const double pAfter = transport::advance( fast, p, t, dt );
	line.mark( pAfter, t + dt );

	const double immediately = line.ageAt( pAfter - D, t + dt );

	std::snprintf( what, sizeof( what ), "one frame after the change it reads %.0f ms old material",
	               immediately * 1000.0 );
	std::printf( "  ..    %s\n", what );

	//The new speed's delay is 125 ms. A frame-queue model would be there
	//already. A tape has barely moved.
	check( immediately > 0.4, "it has NOT jumped to the new delay: still near 500 ms" );
	check( std::fabs( immediately - 0.125 ) > 0.3, "and it is nowhere near the new 125 ms" );

	//Wind on at the new speed and watch it converge.
	double p2 = pAfter;
	double t2 = t + dt;
	for( int i = 0; i < 240; ++i )
	{
		line.mark( p2, t2 );
		p2 = transport::advance( fast, p2, t2, dt );
		t2 += dt;
	}

	const double converged = line.ageAt( p2 - D, t2 );
	std::snprintf( what, sizeof( what ), "after another 4 s it reads %.0f ms old material",
	               converged * 1000.0 );
	std::printf( "  ..    %s\n", what );
	check( std::fabs( converged - 0.125 ) < 0.02, "which is the 125 ms the new speed asks for" );

	return 0;
}

//---------------------------------------------------------------------------
// The chorus.
//
// Roland: the speed "is always changing slightly", which "creates oscillations
// in the pitch of each of the three echo sounds, automatically producing the
// RE-201's characteristic chorus effect."
//
// Each head is a different distance down the tape, so each sees the speed error
// from a different moment. The test: head 3's delay must NOT be exactly three
// times head 1's when the transport is unsteady, and must be exactly three
// times it when the transport is steady. A model that applies one wobble to all
// three heads passes the second half and fails the first.
//---------------------------------------------------------------------------
int checkChorus()
{
	std::printf( "the three heads wobble independently\n" );

	const double fps = 120.0;
	const double dt  = 1.0 / fps;

	auto worstRatioError = []( const transport::Settings& s, double dtIn, double fpsIn ) {
		Tapeline line;
		double p = 0.0, t = 0.0;
		const int frames = static_cast< int >( fpsIn * 12 );
		for( int i = 0; i < frames; ++i )
		{
			line.mark( p, t );
			p = transport::advance( s, p, t, dtIn );
			t += dtIn;
		}

		double worst = 0.0;
		//Only the last third, so the tape is long enough for head 3 to reach.
		for( size_t i = line.position.size() * 2 / 3; i < line.position.size(); ++i )
		{
			const double d1 = line.ageAt( line.position[ i ] - heads::kSpacing[ 0 ], line.time[ i ] );
			const double d3 = line.ageAt( line.position[ i ] - heads::kSpacing[ 2 ], line.time[ i ] );
			if( d1 <= 0.0 || d3 <= 0.0 )
				continue;

			worst = std::max( worst, std::fabs( d3 - 3.0 * d1 ) / d1 );
		}
		return worst;
	};

	controls::HostValues host;

	//Three transports that differ ONLY in the Wow & Flutter trim, so what is
	//being measured is the trim and not the sampling.
	auto atTrim = [ & ]( float trim ) {
		controls::Render r  = controls::render( host );
		r.transport.amount  = trim;
		return worstRatioError( r.transport, dt, fps );
	};

	const double steadyErr  = atTrim( 0.0f );
	const double defaultErr = worstRatioError( controls::render( host ).transport, dt, fps );
	const double fullErr    = atTrim( 1.0f );

	char what[ 200 ];
	std::snprintf( what, sizeof( what ),
	               "head 3 against 3x head 1 -- steady %.4f%%, default %.2f%%, full %.2f%%",
	               steadyErr * 100.0, defaultErr * 100.0, fullErr * 100.0 );
	std::printf( "  ..    %s\n", what );

	//A steady transport has the heads at exactly the ratio the head block bolts
	//them at. Anything else here would mean the position integral is wrong.
	check( steadyErr < 0.001, "a steady transport keeps the heads at exactly 1 : 2 : 3" );

	//An unsteady one does not, and the threshold is a physical quantity rather
	//than a tuned one: the default trim is 0.35 of a 6% peak speed error, so a
	//relative wander of about 2% between head 1 and head 3 is what the transport
	//asks for. One percent is comfortably below that and three orders of
	//magnitude above the steady case.
	check( defaultErr > 0.01, "at the default trim the heads have decorrelated by over 1%" );

	//And the trim is what drives it. A model that applied ONE wobble to all
	//three heads would keep every one of these at zero.
	check( fullErr > defaultErr, "more Wow & Flutter decorrelates them further" );
	check( defaultErr > steadyErr * 100.0, "the difference is the transport, not the measurement" );

	return 0;
}

//---------------------------------------------------------------------------
// The mirrored tape read: GLSL against Tape.cpp.
//---------------------------------------------------------------------------
int checkRead()
{
	std::printf( "the GLSL tape read against Tape.cpp\n" );

	const int w = 64;
	const int h = 128;

	//A tape with an uneven recording on it, so the slot boundaries are not at
	//regular intervals and an off-by-one has somewhere to show up.
	tape::Loop loop;
	double p = 0.0;
	for( int i = 0; i < 40; ++i )
	{
		p += 0.05 + 0.03 * std::sin( i * 0.7 );
		loop.Record( p );
	}

	const double headPos = p;
	const double span    = 0.04;
	const double offset  = heads::kSpacing[ 0 ];

	//The probe, assembled from the SAME string the render uses.
	ffglex::FFGLShader probe;
	if( !probe.Compile( shaders::kVertex, shaders::ReadProbeFragment().c_str() ) )
	{
		fail( "the probe shader would not compile" );
		return 1;
	}

	ffglex::FFGLScreenQuad quad;
	if( !quad.Initialise() )
	{
		fail( "quad geometry" );
		return 1;
	}

	//A one-texel store, so the sampler is bound to something real. Nothing here
	//samples it, but an unbound sampler on some drivers is an error on draw.
	GLuint dummy = 0;
	glGenTextures( 1, &dummy );
	glBindTexture( GL_TEXTURE_2D_ARRAY, dummy );
	glTexImage3D( GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, tape::kSlots, 0, GL_RGBA,
	              GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

	Target target = makeTarget( w, h, GL_RGBA32F );
	glBindFramebuffer( GL_FRAMEBUFFER, target.fbo );
	glViewport( 0, 0, w, h );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );

	{
		ffglex::ScopedShaderBinding binding( probe.GetGLID() );

		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D_ARRAY, dummy );

		probe.Set( "MaxUV", 1.0f, 1.0f );
		probe.Set( "TapeTex", 0 );

		glUniform1fv( probe.FindUniform( "SlotStart" ), tape::kSlots, loop.Starts().data() );
		glUniform1fv( probe.FindUniform( "SlotSpan" ), tape::kSlots, loop.Spans().data() );
		glUniform1i( probe.FindUniform( "TapeNewest" ), loop.Newest() );
		glUniform1i( probe.FindUniform( "TapeWritten" ), loop.Written() );

		float offsets[ 3 ] = { static_cast< float >( offset ), 0.0f, 0.0f };
		float up[ 3 ]      = { 1.0f, 0.0f, 0.0f };
		glUniform1fv( probe.FindUniform( "HeadOffset" ), 3, offsets );
		glUniform1fv( probe.FindUniform( "HeadUp" ), 3, up );

		probe.Set( "HeadPos", static_cast< float >( headPos - loop.Origin() ) );
		probe.Set( "HeadSpan", static_cast< float >( span ) );

		quad.Draw();
	}

	const std::vector< float > pixels = readFloats( target );

	int compared    = 0;
	int slotWrong   = 0;
	float worstPhase = 0.0f;

	//One sample per output row. uv.y is bottom-origin in GL, so row y maps to
	//uv.y = (y + 0.5) / h and the C++ side is asked the same question.
	for( int y = 0; y < h; ++y )
	{
		const float uvY  = ( y + 0.5f ) / h;
		const float scan = 1.0f - uvY;

		//The same question the shader asks: the head is `offset` behind the
		//record head and sweeps `span` down the picture. Leaving the offset off
		//here compares the CPU's answer for the record head against the GPU's
		//for head 1, which disagrees on every row and looks like a broken
		//mirror.
		const double q = headPos - offset + scan * span;

		const tape::Read cpu = loop.Resolve( q, span );

		const float* px = pixels.data() + ( static_cast< size_t >( y ) * w + w / 2 ) * 4;
		const bool gpuOk = px[ 3 ] > 0.5f;

		if( !cpu.valid && !gpuOk )
			continue;

		++compared;

		if( !cpu.valid || !gpuOk )
		{
			++slotWrong;
			continue;
		}

		const int gpuSlot = static_cast< int >( std::lround( px[ 0 ] * tape::kSlots ) );
		if( gpuSlot != cpu.slot )
			++slotWrong;

		worstPhase = std::max( worstPhase, std::fabs( px[ 1 ] - cpu.phase ) );
	}

	char what[ 160 ];
	std::snprintf( what, sizeof( what ), "%d rows compared, %d slots disagreed, worst phase error %.2e",
	               compared, slotWrong, worstPhase );
	std::printf( "  ..    %s\n", what );

	check( compared > h / 2, "the probe resolved most rows" );
	check( slotWrong == 0, "every row picked the same slot on both sides" );

	//A stated tolerance, not an epsilon somebody tuned until it passed: the
	//phase is a ratio of two 32-bit floats computed in a different order on each
	//side, and 1e-5 is comfortably above the last bits and far below anything a
	//picture could show.
	check( worstPhase < 1e-5f, "and the same phase, to 1e-5" );

	glDeleteTextures( 1, &dummy );
	releaseTarget( target );
	probe.FreeGLResources();
	quad.Release();

	return 0;
}

//---------------------------------------------------------------------------
// Mix at zero must leave the picture alone, byte for byte.
//---------------------------------------------------------------------------
int checkIdentity()
{
	std::printf( "Mix at zero is transparent\n" );

	const int w = 320;
	const int h = 180;

	Driver driver;
	const auto names = parameterIndex( driver.plugin );
	driver.plugin.SetFloatParameter( names.at( "Mix" ), 0.0f );

	Target target = makeTarget( w, h );
	if( !driver.run( target, 30, 60.0 ) )
	{
		fail( "the plugin would not render" );
		releaseTarget( target );
		return 1;
	}

	std::printf( "  ..    the plugin resolved Mix to %.3f, direct %.3f, echo %.3f\n",
	             driver.plugin.lastRender().mix, driver.plugin.lastRender().directVolume,
	             driver.plugin.lastRender().echoVolume );

	const std::vector< unsigned char > got  = readBytes( target );
	const std::vector< unsigned char > want = testCard( w, h, 29 );

	//The card is generated top-down and GL reads bottom-up.
	const std::vector< unsigned char > flipped = flipRows( got, w, h );

	size_t differ  = 0;
	int worst      = 0;
	size_t firstAt = 0;
	bool haveFirst = false;

	for( size_t i = 0; i < flipped.size(); ++i )
	{
		const int d = std::abs( static_cast< int >( flipped[ i ] ) - static_cast< int >( want[ i ] ) );
		if( d == 0 )
			continue;

		++differ;
		worst = std::max( worst, d );
		if( !haveFirst )
		{
			firstAt   = i;
			haveFirst = true;
		}
	}

	char what[ 220 ];
	if( differ == 0 )
	{
		std::snprintf( what, sizeof( what ), "0 of %zu bytes differ", want.size() );
	}
	else
	{
		const size_t pixel = firstAt / 4;
		std::snprintf( what, sizeof( what ),
		               "%zu of %zu bytes differ, worst by %d; first at pixel (%zu,%zu) "
		               "channel %zu: got %d want %d",
		               differ, want.size(), worst, pixel % w, pixel / w, firstAt % 4,
		               static_cast< int >( flipped[ firstAt ] ), static_cast< int >( want[ firstAt ] ) );
	}

	check( differ == 0, what );

	releaseTarget( target );
	return 0;
}

//---------------------------------------------------------------------------
// Every preset renders something with structure in it.
//
// ⚠️ Three separate rejections, not one. A feedback loop's failures are a black
// frame AND a saturated one AND a flat wash, and a brightness floor sees only
// the first.
//---------------------------------------------------------------------------
int checkPresets()
{
	std::printf( "every preset renders something with structure in it\n" );

	const int w = 256;
	const int h = 144;

	for( int p = 1; p <= presets::kCount; ++p )
	{
		//A FRESH plugin per preset. The tape is state; reusing one instance
		//would measure the order the presets ran in.
		Driver driver;
		const auto names = parameterIndex( driver.plugin );
		driver.plugin.SetFloatParameter( names.at( "Preset" ), static_cast< float >( p ) );

		Target target = makeTarget( w, h );
		const bool ok = driver.run( target, 90, 60.0 );

		if( !ok )
		{
			fail( std::string( presets::label( p ) ) + ": would not render" );
			releaseTarget( target );
			continue;
		}

		const Stats s = lumaStats( readBytes( target ) );

		char what[ 200 ];
		std::snprintf( what, sizeof( what ), "%-15s mean %.3f  sd %.3f  blown %.1f%%",
		               presets::label( p ), s.mean, s.stddev, s.blown * 100.0f );

		const bool dark  = s.mean < 0.010f;
		const bool flat  = s.stddev < 0.020f;
		const bool blown = s.blown > 0.60f;

		if( dark )
			fail( std::string( what ) + "  -- BLACK" );
		else if( flat )
			fail( std::string( what ) + "  -- FLAT" );
		else if( blown )
			fail( std::string( what ) + "  -- BLOWN OUT" );
		else
			pass( what );

		releaseTarget( target );
	}

	return 0;
}

//---------------------------------------------------------------------------
// A hostile machine leaves no NaN.
//---------------------------------------------------------------------------
int checkGuard()
{
	std::printf( "a hostile machine leaves no NaN\n" );

	const int w = 192;
	const int h = 108;

	struct Case
	{
		const char* what;
		std::vector< std::pair< const char*, float > > set;
	};

	const Case cases[] = {
		{ "everything at maximum",
		  { { "Mode", 10.0f }, { "Repeat Rate", 1.0f }, { "Intensity", 1.0f },
		    { "Head Spread", 1.0f }, { "Input Level", 1.0f }, { "Saturation", 1.0f },
		    { "Head Wear", 1.0f }, { "Hiss", 1.0f }, { "Dropouts", 1.0f },
		    { "Wow & Flutter", 1.0f }, { "Wow", 1.0f }, { "Flutter", 1.0f }, { "Scrape", 1.0f },
		    { "Reverb Time", 1.0f }, { "Dispersion", 1.0f }, { "Echo Volume", 1.0f },
		    { "Reverb Volume", 1.0f }, { "Direct Volume", 1.0f } } },
		{ "everything at minimum",
		  { { "Mode", 0.0f }, { "Repeat Rate", 0.0f }, { "Intensity", 0.0f },
		    { "Head Spread", 0.0f }, { "Input Level", 0.0f }, { "Saturation", 0.0f },
		    { "Head Wear", 0.0f }, { "Hiss", 0.0f }, { "Dropouts", 0.0f },
		    { "Wow & Flutter", 0.0f }, { "Reverb Time", 0.0f }, { "Echo Volume", 0.0f },
		    { "Reverb Volume", 0.0f }, { "Direct Volume", 0.0f } } },
		{ "the loop past unity, in Reverb Only",
		  { { "Mode", 11.0f }, { "Intensity", 1.0f }, { "Reverb Time", 1.0f },
		    { "Reverb Volume", 1.0f } } },
		{ "all three heads, the loop past unity",
		  { { "Mode", 10.0f }, { "Intensity", 1.0f }, { "Input Level", 1.0f },
		    { "Echo Volume", 1.0f } } },
	};

	for( const Case& c : cases )
	{
		Driver driver;
		const auto names = parameterIndex( driver.plugin );
		for( const auto& kv : c.set )
		{
			const auto it = names.find( kv.first );
			if( it == names.end() )
			{
				fail( std::string( "no parameter called '" ) + kv.first + "'" );
				continue;
			}
			driver.plugin.SetFloatParameter( it->second, kv.second );
		}

		Target target = makeTarget( w, h, GL_RGBA32F );
		if( !driver.run( target, 120, 60.0 ) )
		{
			fail( std::string( c.what ) + ": would not render" );
			releaseTarget( target );
			continue;
		}

		const std::vector< float > pixels = readFloats( target );
		size_t bad                        = 0;
		for( float v : pixels )
		{
			if( !std::isfinite( v ) )
				++bad;
		}

		char what[ 200 ];
		std::snprintf( what, sizeof( what ), "%s: %zu non-finite of %zu", c.what, bad,
		               pixels.size() );
		check( bad == 0, what );

		releaseTarget( target );
	}

	return 0;
}

//---------------------------------------------------------------------------
// Parameter names against FFGL's 16-character truncation.
//---------------------------------------------------------------------------
int checkNames()
{
	std::printf( "parameter names fit FFGL's 16-character buffer\n" );

	Astronaught plugin;
	int atLimit = 0;

	for( unsigned int id = 0; id < Astronaught::PT_COUNT; ++id )
	{
		const char* name = plugin.GetParamName( id );
		if( name == nullptr || name[ 0 ] == '\0' )
			continue;

		const size_t length = std::strlen( name );
		if( length > 16 )
		{
			char what[ 160 ];
			std::snprintf( what, sizeof( what ), "'%s' is %zu characters -- Resolume will show '%.16s'",
			               name, length, name );
			fail( what );
		}
		else if( length == 16 )
		{
			//⚠️ The host cannot tell "truncated" from "fits exactly", and neither
			//can this test. Reported so a human looks at it.
			char what[ 160 ];
			std::snprintf( what, sizeof( what ), "'%s' is exactly 16 -- complete, but review it", name );
			std::printf( "  ..    %s\n", what );
			++atLimit;
		}
	}

	//The Mode Selector's element names have no documented buffer contract, and
	//whether Resolume truncates them is unverified either way. Kept inside 16
	//rather than relying on a limit nobody has established.
	for( int i = 0; i < heads::kModeCount; ++i )
	{
		const size_t length = std::strlen( heads::kModes[ i ].label );
		if( length > 16 )
		{
			char what[ 160 ];
			std::snprintf( what, sizeof( what ), "mode element '%s' is %zu characters",
			               heads::kModes[ i ].label, length );
			fail( what );
		}
	}

	char what[ 80 ];
	std::snprintf( what, sizeof( what ), "no name over 16 characters (%d exactly at the limit)",
	               atLimit );
	pass( what );

	return 0;
}

int listParameters()
{
	Astronaught plugin;

	std::printf( "%-4s %-16s %-10s %8s  %s\n", "id", "name", "type", "default", "" );
	for( unsigned int id = 0; id < Astronaught::PT_COUNT; ++id )
	{
		const char* name = plugin.GetParamName( id );
		if( name == nullptr || name[ 0 ] == '\0' )
			continue;

		const unsigned int type = plugin.GetParamType( id );
		const char* typeName    = type == FF_TYPE_STANDARD  ? "standard"
		                          : type == FF_TYPE_BOOLEAN ? "boolean"
		                          : type == FF_TYPE_OPTION  ? "option"
		                          : type == FF_TYPE_TEXT    ? "text"
		                          : type == FF_TYPE_EVENT   ? "event"
		                                                    : "other";

		std::printf( "%-4u %-16s %-10s %8.3f  %s\n", id, name, typeName,
		             plugin.GetFloatParameter( id ), std::strlen( name ) == 16 ? "(at the limit)" : "" );
	}

	return 0;
}

int renderFrame( const std::string& path, int width, int height, int frames, double fps,
                 const std::vector< std::pair< std::string, float > >& sets, int preset )
{
	Driver driver;
	const auto names = parameterIndex( driver.plugin );

	if( preset > 0 )
		driver.plugin.SetFloatParameter( names.at( "Preset" ), static_cast< float >( preset ) );

	for( const auto& kv : sets )
	{
		const auto it = names.find( kv.first );
		if( it == names.end() )
		{
			std::printf( "no parameter called '%s'\n", kv.first.c_str() );
			return 1;
		}
		driver.plugin.SetFloatParameter( it->second, kv.second );
	}

	Target target = makeTarget( width, height );
	if( !driver.run( target, frames, fps ) )
	{
		std::printf( "the plugin would not render\n" );
		releaseTarget( target );
		return 1;
	}

	const std::vector< unsigned char > image = flipRows( readBytes( target ), width, height );
	const bool ok                            = writePng( path, width, height, image );

	std::printf( "%s %s (%dx%d, %d frames at %.0f fps, stride %d, tape at %.3f)\n",
	             ok ? "wrote" : "FAILED to write", path.c_str(), width, height, frames, fps,
	             driver.plugin.stride(), driver.plugin.tapePosition() );

	releaseTarget( target );
	return ok ? 0 : 1;
}

/// Render one setting and report what it looks like, without judging it.
///
/// The tuning counterpart of `--presets`: that check says a preset is blown out,
/// and this says at what value it stopped being usable. Added while tuning the
/// two presets that sit near self-oscillation, where the difference between "on
/// the edge" and "a white rectangle" is about three hundredths of Intensity and
/// is not guessable.
int statsOf( int width, int height, int frames, double fps,
             const std::vector< std::pair< std::string, float > >& sets, int preset )
{
	Driver driver;
	const auto names = parameterIndex( driver.plugin );

	if( preset > 0 )
		driver.plugin.SetFloatParameter( names.at( "Preset" ), static_cast< float >( preset ) );

	for( const auto& kv : sets )
	{
		const auto it = names.find( kv.first );
		if( it == names.end() )
		{
			std::printf( "no parameter called '%s'\n", kv.first.c_str() );
			return 1;
		}
		driver.plugin.SetFloatParameter( it->second, kv.second );
	}

	Target target = makeTarget( width, height );
	if( !driver.run( target, frames, fps ) )
	{
		std::printf( "the plugin would not render\n" );
		releaseTarget( target );
		return 1;
	}

	const Stats s = lumaStats( readBytes( target ) );
	std::printf( "mean %.3f  sd %.3f  blown %.1f%%  (loop gain %.3f, stride %d)\n", s.mean,
	             s.stddev, s.blown * 100.0f, driver.plugin.lastRender().tape.intensity,
	             driver.plugin.stride() );

	releaseTarget( target );
	return 0;
}

/// Render cost, per frame, with the whole machine running.
///
/// ⚠️ Measured with `glFinish()` around each frame, not by timing the calls.
/// GL is asynchronous: without it this measures how fast the driver can accept
/// commands, which on a modern driver is roughly constant and roughly a lie.
///
/// The first frames are excluded. This plugin allocates a large texture array
/// on its first frame and compiles four programs before that, so an average
/// that includes them reports the cost of starting up rather than the cost of
/// running.
int benchmark( int width, int height, int frames, int preset )
{
	Driver driver;
	const auto names = parameterIndex( driver.plugin );
	if( preset > 0 )
		driver.plugin.SetFloatParameter( names.at( "Preset" ), static_cast< float >( preset ) );

	Target target = makeTarget( width, height );

	const std::vector< unsigned char > card = testCard( width, height, 0 );
	const GLuint input = uploadTexture( card, width, height );

	const int warmup = 20;
	double best = 1e9, total = 0.0;
	int counted = 0;

	for( int i = 0; i < frames + warmup; ++i )
	{
		driver.plugin.SetTime( i / 60.0 );

		const auto t0 = std::chrono::steady_clock::now();
		const bool ok = driver.render( target, input, width, height );
		glFinish();
		const auto t1 = std::chrono::steady_clock::now();

		if( !ok )
		{
			std::printf( "the plugin would not render\n" );
			glDeleteTextures( 1, &input );
			releaseTarget( target );
			return 1;
		}

		if( i < warmup )
			continue;

		const double ms = std::chrono::duration< double, std::milli >( t1 - t0 ).count();
		best = std::min( best, ms );
		total += ms;
		++counted;
	}

	std::printf( "%dx%d  %.3f ms/frame mean, %.3f ms best  (%.0f fps at the mean, tape %dx%d, stride %d)\n",
	             width, height, total / counted, best, 1000.0 * counted / total,
	             driver.plugin.tapeWidth(), driver.plugin.tapeHeight(), driver.plugin.stride() );

	glDeleteTextures( 1, &input );
	releaseTarget( target );
	return 0;
}

//---------------------------------------------------------------------------
// Real footage through the real shaders.
//
//   ffmpeg -i in.mov -f rawvideo -pix_fmt rgba - \
//     | astest --pipe --size 1920x1080 --fps 30 --script cues.txt \
//     | ffmpeg -f rawvideo -pix_fmt rgba -s 1920x1080 -r 30 -i - out.mov
//
// This exists so the project video and the hero shot are the SHIPPED plugin
// acting on real frames, rather than a screen capture of a host or a synthetic
// card. Everything a viewer sees was rendered by the same class Resolume loads.
//
// ⚠️ Frames arrive TOP-DOWN from ffmpeg and GL wants bottom-up, so the upload
// flips and the readback flips again. Getting that wrong does not look like a
// flip -- the picture comes out upright either way because it is flipped twice
// -- it makes `scan` run backwards, and the tear rolls the opposite way from
// the way it does in Resolume. See uploadTexture.
//---------------------------------------------------------------------------

/// One key in a cue sheet.
struct Cue
{
	int frame;
	std::string name;
	float value;
};

/// Read `frame  Parameter Name  value` lines. Blank lines and `#` are ignored.
///
/// ⚠️ Option and boolean parameters must STEP, never ramp: two keys one frame
/// apart. A ramped Mode sweeps through every selector position on the way,
/// which is not what anybody writing the cue meant and looks like a fault.
std::vector< Cue > readCues( const std::string& path )
{
	std::vector< Cue > cues;
	FILE* f = std::fopen( path.c_str(), "r" );
	if( f == nullptr )
	{
		std::fprintf( stderr, "astest: cannot open cue sheet '%s'\n", path.c_str() );
		return cues;
	}

	char line[ 512 ];
	while( std::fgets( line, sizeof( line ), f ) != nullptr )
	{
		char* p = line;
		while( *p == ' ' || *p == '\t' )
			++p;
		if( *p == '#' || *p == '\n' || *p == '\0' )
			continue;

		int frame = 0;
		int used  = 0;
		if( std::sscanf( p, "%d %n", &frame, &used ) != 1 )
			continue;
		p += used;

		// The name runs to the last whitespace-separated field, which is the
		// value -- names contain spaces, so splitting on whitespace from the
		// left loses every multi-word control.
		std::string rest( p );
		while( !rest.empty() && ( rest.back() == '\n' || rest.back() == '\r' || rest.back() == ' ' ) )
			rest.pop_back();

		const size_t space = rest.find_last_of( " \t" );
		if( space == std::string::npos )
			continue;

		Cue cue;
		cue.frame = frame;
		cue.name  = rest.substr( 0, space );
		cue.value = static_cast< float >( std::atof( rest.c_str() + space + 1 ) );
		while( !cue.name.empty() && ( cue.name.back() == ' ' || cue.name.back() == '\t' ) )
			cue.name.pop_back();

		cues.push_back( cue );
	}

	std::fclose( f );
	return cues;
}

/// The value a cued parameter should hold at `frame`, ramped between keys.
float cueValueAt( const std::vector< Cue >& cues, const std::string& name, int frame, float fallback )
{
	const Cue* before = nullptr;
	const Cue* after  = nullptr;

	for( const Cue& c : cues )
	{
		if( c.name != name )
			continue;
		if( c.frame <= frame && ( before == nullptr || c.frame >= before->frame ) )
			before = &c;
		if( c.frame > frame && ( after == nullptr || c.frame < after->frame ) )
			after = &c;
	}

	if( before == nullptr )
		return after != nullptr ? after->value : fallback;
	if( after == nullptr || after->frame == before->frame )
		return before->value;

	const float t = static_cast< float >( frame - before->frame )
	                / static_cast< float >( after->frame - before->frame );
	return before->value + ( after->value - before->value ) * t;
}

int pipeFrames( int width, int height, double fps, const std::string& scriptPath,
                const std::vector< std::pair< std::string, float > >& sets, int preset )
{
	Driver driver;
	const auto names = parameterIndex( driver.plugin );

	if( preset > 0 )
		driver.plugin.SetFloatParameter( names.at( "Preset" ), static_cast< float >( preset ) );

	for( const auto& kv : sets )
	{
		const auto it = names.find( kv.first );
		if( it == names.end() )
		{
			std::fprintf( stderr, "astest: no parameter called '%s'\n", kv.first.c_str() );
			return 1;
		}
		driver.plugin.SetFloatParameter( it->second, kv.second );
	}

	std::vector< Cue > cues;
	std::vector< std::string > cued;
	if( !scriptPath.empty() )
	{
		cues = readCues( scriptPath );
		for( const Cue& c : cues )
		{
			if( std::find( cued.begin(), cued.end(), c.name ) == cued.end() )
			{
				if( names.find( c.name ) == names.end() )
				{
					std::fprintf( stderr, "astest: no parameter called '%s'\n", c.name.c_str() );
					return 1;
				}
				cued.push_back( c.name );
			}
		}
		std::fprintf( stderr, "astest: %zu cues over %zu parameters\n", cues.size(), cued.size() );
	}

	Target target = makeTarget( width, height );

	const size_t frameBytes = static_cast< size_t >( width ) * height * 4;
	std::vector< unsigned char > in( frameBytes );

	int frame = 0;
	while( std::fread( in.data(), 1, frameBytes, stdin ) == frameBytes )
	{
		for( const std::string& name : cued )
		{
			driver.plugin.SetFloatParameter(
			    names.at( name ),
			    cueValueAt( cues, name, frame, driver.plugin.GetFloatParameter( names.at( name ) ) ) );
		}

		const GLuint tex = uploadTexture( in, width, height );
		driver.plugin.SetTime( frame / fps );
		const bool ok = driver.render( target, tex, width, height );
		glDeleteTextures( 1, &tex );

		if( !ok )
		{
			std::fprintf( stderr, "astest: render failed on frame %d\n", frame );
			releaseTarget( target );
			return 1;
		}

		const std::vector< unsigned char > out = flipRows( readBytes( target ), width, height );
		if( std::fwrite( out.data(), 1, out.size(), stdout ) != out.size() )
		{
			std::fprintf( stderr, "astest: short write on frame %d\n", frame );
			releaseTarget( target );
			return 1;
		}

		++frame;
	}

	std::fflush( stdout );
	std::fprintf( stderr, "astest: %d frames\n", frame );
	releaseTarget( target );
	return 0;
}

void usage()
{
	std::printf(
	    "astest -- the Astronaught offline harness\n"
	    "\n"
	    "  --list                 every parameter, with its type and default\n"
	    "  --names                names against FFGL's 16-character buffer\n"
	    "  --modes                the Mode Selector against Roland's table\n"
	    "  --ratios               the heads are at 1 : 2 : 3\n"
	    "  --delay                Repeat Rate's range, and which way it runs\n"
	    "  --rate                 the transport is frame-rate independent\n"
	    "  --drag                 changing Repeat Rate drags the echoes\n"
	    "  --chorus               the three heads wobble independently\n"
	    "  --read                 the GLSL tape read against Tape.cpp\n"
	    "  --identity             Mix at zero is transparent\n"
	    "  --presets              every preset renders structure\n"
	    "  --guard                a hostile machine leaves no NaN\n"
	    "  --all                  every check above\n"
	    "\n"
	    "  --out PATH             render a frame\n"
	    "  --stats                render, and report mean / sd / blown-out\n"
	    "  --bench                render cost per frame, with glFinish\n"
	    "  --pipe                 raw RGBA frames in on stdin, out on stdout\n"
	    "  --script PATH          cue sheet: `frame  Parameter Name  value`\n"
	    "  --size WxH             render size (default 640x360)\n"
	    "  --frames N             frames to run before the one that is kept\n"
	    "  --fps N                frame rate to run them at\n"
	    "  --preset N             apply factory preset N (1-based)\n"
	    "  --set \"Name=value\"     set a control by its host-facing name\n" );
}
} // namespace

int main( int argc, char** argv )
{
	if( argc < 2 )
	{
		usage();
		return 1;
	}

	std::string outPath;
	int width  = 640;
	int height = 360;
	int frames = 120;
	double fps = 60.0;
	int preset     = 0;
	bool wantStats = false;
	bool wantBench = false;
	bool wantPipe  = false;
	std::string scriptPath;
	std::vector< std::pair< std::string, float > > sets;
	std::vector< std::string > checks;
	bool wantList = false;

	for( int i = 1; i < argc; ++i )
	{
		const std::string arg = argv[ i ];
		auto next             = [ & ]() -> std::string { return ( i + 1 < argc ) ? argv[ ++i ] : ""; };

		if( arg == "--out" )
			outPath = next();
		else if( arg == "--size" )
		{
			const std::string s = next();
			const size_t x      = s.find( 'x' );
			if( x != std::string::npos )
			{
				width  = std::max( 16, std::atoi( s.substr( 0, x ).c_str() ) );
				height = std::max( 16, std::atoi( s.substr( x + 1 ).c_str() ) );
			}
		}
		else if( arg == "--frames" )
			frames = std::max( 1, std::atoi( next().c_str() ) );
		else if( arg == "--fps" )
			fps = std::max( 1.0, std::atof( next().c_str() ) );
		else if( arg == "--preset" )
			preset = std::atoi( next().c_str() );
		else if( arg == "--set" )
		{
			const std::string s = next();
			const size_t eq     = s.find( '=' );
			if( eq != std::string::npos )
				sets.emplace_back( s.substr( 0, eq ),
				                   static_cast< float >( std::atof( s.substr( eq + 1 ).c_str() ) ) );
		}
		else if( arg == "--list" )
			wantList = true;
		else if( arg == "--stats" )
			wantStats = true;
		else if( arg == "--bench" )
			wantBench = true;
		else if( arg == "--pipe" )
			wantPipe = true;
		else if( arg == "--script" )
			scriptPath = next();
		else if( arg == "--all" )
			checks = { "modes", "ratios", "delay", "rate",     "drag", "chorus",
				       "names", "read",   "identity", "presets", "guard" };
		else if( arg.rfind( "--", 0 ) == 0 )
			checks.push_back( arg.substr( 2 ) );
		else
		{
			usage();
			return 1;
		}
	}

	if( wantList )
		return listParameters();

	//The checks that need no GL run first and without a context, so a machine
	//that cannot make one still gets the model checked.
	std::vector< std::string > needGL;
	for( const std::string& c : checks )
	{
		if( c == "modes" )
			checkModes();
		else if( c == "ratios" )
			checkRatios();
		else if( c == "delay" )
			checkDelay();
		else if( c == "rate" )
			checkRate();
		else if( c == "drag" )
			checkDrag();
		else if( c == "chorus" )
			checkChorus();
		else if( c == "names" )
			checkNames();
		else
			needGL.push_back( c );
	}

	if( !needGL.empty() || !outPath.empty() || wantStats || wantBench || wantPipe )
	{
		CGLContextObj context = createContext();
		if( context == nullptr )
		{
			std::printf( "could not create an OpenGL context\n" );
			return 1;
		}

		for( const std::string& c : needGL )
		{
			if( c == "read" )
				checkRead();
			else if( c == "identity" )
				checkIdentity();
			else if( c == "presets" )
				checkPresets();
			else if( c == "guard" )
				checkGuard();
			else
			{
				std::printf( "no check called '%s'\n", c.c_str() );
				usage();
				CGLSetCurrentContext( nullptr );
				CGLDestroyContext( context );
				return 1;
			}
		}

		int rc = 0;
		if( wantPipe )
			rc = pipeFrames( width, height, fps, scriptPath, sets, preset );
		if( rc == 0 && wantBench )
			rc = benchmark( width, height, frames, preset );
		if( rc == 0 && wantStats )
			rc = statsOf( width, height, frames, fps, sets, preset );
		if( rc == 0 && !outPath.empty() )
			rc = renderFrame( outPath, width, height, frames, fps, sets, preset );

		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );

		if( rc != 0 )
			return rc;
	}

	if( !checks.empty() )
	{
		std::printf( "\n%s\n", failures == 0 ? "all checks passed" : "CHECKS FAILED" );
		if( failures != 0 )
			std::printf( "%d failure%s\n", failures, failures == 1 ? "" : "s" );
	}

	return failures == 0 ? 0 : 1;
}
