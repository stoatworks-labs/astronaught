// The C++ half of the browser demo's cross-check: the REAL plugin class,
// Astronaught from Astronaught.cpp, in a headless OpenGL 4.1 context, driven
// through scenario.mjs's frames the way a host drives it (SetFloatParameter,
// SetTime, ProcessOpenGL into a framebuffer of its own). After every frame it
// prints the plugin's CPU state through the accessors it already exposes to
// astest (the tape position, the stride, the slot table), every parameter as
// GetFloatParameter returns it, and every active uniform of its four shader
// programs read back out of GL. frames.mjs prints the same from demo/port.js;
// compare.py compares the two.
//
//     uniforms < scenario.txt > cpp-frames.txt
//
// Two kinds of host. `set` is a bare SetFloatParameter, which is what Resolume
// does: it pushes the values it believes in and never consumes the plugin's
// value events. `host` and `push` are a host that DOES consume them, as the
// demo page's panel does: `host` changes the host's own idea of a control (an
// operator moving a knob), and `push` sends every control in id order, and
// after each one reads back any parameter the plugin raised a value event on
// -- so a preset moves the host's knobs, and a drop to Custom moves its Preset
// dropdown before the push reaches it.
//
// Nothing here computes a uniform: what is printed is what the plugin handed
// to GL. Floats print as their bits, doubles as theirs, ints as decimals.

#include "Astronaught.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::string d( double v )
{
	uint64_t bits;
	std::memcpy( &bits, &v, sizeof bits );
	char buffer[ 24 ];
	std::snprintf( buffer, sizeof buffer, "%016" PRIx64, bits );
	return buffer;
}

std::string f( float v )
{
	uint32_t bits;
	std::memcpy( &bits, &v, sizeof bits );
	char buffer[ 12 ];
	std::snprintf( buffer, sizeof buffer, "%08" PRIx32, bits );
	return buffer;
}

float fromBits32( const std::string& s )
{
	const uint32_t bits = static_cast< uint32_t >( std::stoul( s, nullptr, 16 ) );
	float v;
	std::memcpy( &v, &bits, sizeof v );
	return v;
}

double fromBits64( const std::string& s )
{
	const uint64_t bits = std::stoull( s, nullptr, 16 );
	double v;
	std::memcpy( &v, &bits, sizeof v );
	return v;
}

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute attributes[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	CGLPixelFormatObj format = nullptr;
	GLint count              = 0;
	if( CGLChoosePixelFormat( attributes, &format, &count ) != kCGLNoError || format == nullptr )
		return nullptr;
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

/// The host's side: an output framebuffer and an 8-bit clip, as Resolume's are.
/// The clip's pixels decide no uniform, so it is left empty.
struct Host
{
	std::unique_ptr< Astronaught > plugin;
	int width = 0, height = 0;
	bool initialised = false;
	GLuint output = 0, fbo = 0, clip = 0;
	ProcessOpenGLStruct process    = {};
	FFGLTextureStruct input        = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };
	std::map< std::string, GLuint > programs;

	void release()
	{
		if( fbo )
			glDeleteFramebuffers( 1, &fbo );
		if( output )
			glDeleteTextures( 1, &output );
		if( clip )
			glDeleteTextures( 1, &clip );
		fbo = output = clip = 0;
	}

	GLuint texture()
	{
		GLuint t = 0;
		glGenTextures( 1, &t );
		glBindTexture( GL_TEXTURE_2D, t );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glBindTexture( GL_TEXTURE_2D, 0 );
		return t;
	}

	bool attach( int w, int h )
	{
		release();
		width  = w;
		height = h;
		output = texture();
		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0 );
		const bool complete = glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );

		clip        = texture();
		input.Width = input.HardwareWidth = static_cast< FFUInt32 >( width );
		input.Height = input.HardwareHeight = static_cast< FFUInt32 >( height );
		input.Handle                        = clip;
		inputs[ 0 ]                         = &input;
		process.numInputTextures            = 1;
		process.inputTextures               = inputs;
		process.HostFBO                     = fbo;
		return complete;
	}

	/// The plugin's four programs, told apart by a uniform only each one has.
	void findPrograms()
	{
		programs.clear();
		const std::pair< const char*, const char* > marks[] = {
			{ "playback", "SlotStart" }, { "record", "TapeDrive" }, { "spring", "TankFeedback" }, { "output", "EchoVolume" }
		};
		for( GLuint id = 1; id < 4096; ++id )
		{
			if( !glIsProgram( id ) )
				continue;
			for( const auto& mark : marks )
				if( glGetUniformLocation( id, mark.second ) >= 0 )
					programs[ mark.first ] = id;
		}
	}

	bool frame( double seconds )
	{
		if( !initialised )
		{
			FFGLViewportStruct viewport {};
			viewport.width  = static_cast< FFUInt32 >( width );
			viewport.height = static_cast< FFUInt32 >( height );
			if( plugin->InitGL( &viewport ) != FF_SUCCESS )
				return false;
			initialised = true;
			findPrograms();
		}

		plugin->SetTime( seconds );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glViewport( 0, 0, width, height );
		const bool ok = plugin->ProcessOpenGL( &process ) == FF_SUCCESS;
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		return ok;
	}
};

std::string uniforms( GLuint program )
{
	GLint count = 0;
	glGetProgramiv( program, GL_ACTIVE_UNIFORMS, &count );

	std::map< std::string, std::string > out;
	for( GLint i = 0; i < count; ++i )
	{
		char name[ 256 ];
		GLsizei length = 0;
		GLint size     = 0;
		GLenum type    = 0;
		glGetActiveUniform( program, static_cast< GLuint >( i ), sizeof name, &length, &size, &type, name );

		std::string base( name, static_cast< size_t >( length ) );
		const size_t bracket = base.find( '[' );
		if( bracket != std::string::npos )
			base = base.substr( 0, bracket );

		if( type == GL_SAMPLER_2D || type == GL_SAMPLER_2D_ARRAY )
			continue;

		std::ostringstream values;
		for( GLint k = 0; k < size; ++k )
		{
			const std::string element = size > 1 ? base + "[" + std::to_string( k ) + "]" : base;
			const GLint loc           = glGetUniformLocation( program, element.c_str() );
			if( type == GL_INT )
			{
				GLint v = 0;
				glGetUniformiv( program, loc, &v );
				values << ( k ? "," : "" ) << v;
			}
			else
			{
				GLfloat v[ 4 ] = { 0, 0, 0, 0 };
				glGetUniformfv( program, loc, v );
				const int n = type == GL_FLOAT_VEC2 ? 2 : type == GL_FLOAT_VEC3 ? 3 : type == GL_FLOAT_VEC4 ? 4 : 1;
				for( int c = 0; c < n; ++c )
					values << ( k || c ? "," : "" ) << f( v[ c ] );
			}
		}
		out[ base ] = std::string( type == GL_INT ? "i:" : "f:" ) + values.str();
	}

	std::string line;
	for( const auto& kv : out )
		line += " " + kv.first + "=" + kv.second;
	return line;
}
} // namespace

int main()
{
	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "uniforms: no OpenGL 4.1 core context\n" );
		return 2;
	}

	Host host;
	host.plugin = std::make_unique< Astronaught >();
	// Seconds, declared, as astest declares them and as the page's clock is.
	host.plugin->SetClockScaleForTest( 1.0 );

	// The event-consuming host's knobs, read from the plugin at instantiation.
	constexpr unsigned kHostParams = Astronaught::PT_PRESET + 1;
	float belief[ kHostParams ];
	for( unsigned id = 0; id < kHostParams; ++id )
		belief[ id ] = host.plugin->GetFloatParameter( id );

	ParamEventStruct events[ Astronaught::PT_COUNT ];
	auto consume = [ & ]( bool reread ) {
		const FFUInt32 n = host.plugin->ConsumeParamEvents( events, Astronaught::PT_COUNT );
		for( FFUInt32 e = 0; e < n && reread; ++e )
		{
			const unsigned id = events[ e ].ParameterNumber;
			if( id < kHostParams && ( events[ e ].eventFlags & FF_EVENT_FLAG_VALUE ) )
				belief[ id ] = host.plugin->GetFloatParameter( id );
		}
	};

	std::string line;
	int frame = 0;
	while( std::getline( std::cin, line ) )
	{
		std::istringstream in( line );
		std::string verb;
		if( !( in >> verb ) )
			continue;

		if( verb == "size" )
		{
			int w = 0, h = 0;
			in >> w >> h;
			if( !host.attach( w, h ) )
			{
				std::fprintf( stderr, "uniforms: framebuffer incomplete at %dx%d\n", w, h );
				return 2;
			}
			std::printf( "R %dx%d\n", w, h );
		}
		else if( verb == "set" )
		{
			unsigned id = 0;
			std::string bits;
			in >> id >> bits;
			host.plugin->SetFloatParameter( id, fromBits32( bits ) );
			consume( false );
		}
		else if( verb == "host" )
		{
			unsigned id = 0;
			std::string bits;
			in >> id >> bits;
			if( id < kHostParams )
				belief[ id ] = fromBits32( bits );
		}
		else if( verb == "push" )
		{
			for( unsigned id = 0; id < kHostParams; ++id )
			{
				host.plugin->SetFloatParameter( id, belief[ id ] );
				consume( true );
			}
		}
		else if( verb == "frame" )
		{
			std::string bits;
			in >> bits;
			if( !host.frame( fromBits64( bits ) ) )
			{
				std::fprintf( stderr, "uniforms: ProcessOpenGL failed at frame %d\n", frame );
				return 2;
			}

			const Astronaught& p           = *host.plugin;
			const astronaught::tape::Loop& loop = p.loop();
			std::printf( "C %d pos=%s span=%s stride=%d newest=%d written=%d origin=%s tape=%dx%d\n", frame,
			             d( p.tapePosition() ).c_str(), d( p.lastSpan() ).c_str(), p.stride(), loop.Newest(),
			             loop.Written(), d( loop.Origin() ).c_str(), p.tapeWidth(), p.tapeHeight() );

			std::printf( "P %d", frame );
			for( unsigned id = 0; id <= Astronaught::PT_PRESET; ++id )
				std::printf( " %s", f( host.plugin->GetFloatParameter( id ) ).c_str() );
			std::printf( "\n" );

			std::printf( "H %d", frame );
			for( unsigned id = 0; id < kHostParams; ++id )
				std::printf( " %s", f( belief[ id ] ).c_str() );
			std::printf( "\n" );

			for( const char* name : { "playback", "record", "spring", "output" } )
			{
				const auto it = host.programs.find( name );
				if( it == host.programs.end() )
				{
					std::fprintf( stderr, "uniforms: no %s program found\n", name );
					return 2;
				}
				std::printf( "U %d %s%s\n", frame, name, uniforms( it->second ).c_str() );
			}
			++frame;
		}
		else
		{
			std::fprintf( stderr, "uniforms: unknown '%s'\n", verb.c_str() );
			return 2;
		}
	}

	host.plugin->DeInitGL();
	host.release();
	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return 0;
}
