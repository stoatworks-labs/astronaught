#include "Store.h"

#include "Diag.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace astronaught
{
Store::~Store()
{
	Destroy();
}

void Store::Destroy()
{
	if( fbo != 0 )
	{
		glDeleteFramebuffers( 1, &fbo );
		fbo = 0;
	}

	if( texture != 0 )
	{
		glDeleteTextures( 1, &texture );
		texture = 0;
	}

	width = height = layers = 0;
	compW = compH = 0;
}

bool Store::Ensure( int compWidth, int compHeight, int slots )
{
	compWidth  = std::max( 1, compWidth );
	compHeight = std::max( 1, compHeight );
	slots      = std::max( 2, slots );

	if( texture != 0 && compW == compWidth && compH == compHeight && layers == slots )
		return true;

	Destroy();

	// Scale to fit the budget. Area scales as the square of the linear factor,
	// so the linear factor is the square root of the ratio.
	const double perPixel = static_cast< double >( slots ) * 4.0;
	const double budget   = static_cast< double >( kByteBudget );
	const double area     = static_cast< double >( compWidth ) * compHeight;

	double scale = 1.0;
	if( perPixel * area > budget )
		scale = std::sqrt( budget / ( perPixel * area ) );

	scale = std::clamp( scale, static_cast< double >( kMinScale ), 1.0 );

	const int w = std::max( 16, static_cast< int >( std::lround( compWidth * scale ) ) );
	const int h = std::max( 16, static_cast< int >( std::lround( compHeight * scale ) ) );

	glGenTextures( 1, &texture );
	if( texture == 0 )
	{
		diag::error( "could not create the tape texture" );
		return false;
	}

	// Save and restore the array binding. Nothing else in this plugin binds a
	// 2D array, but the SDK's scoped bindings CLEAR to zero rather than
	// restoring, so the habit of not trusting the binding to survive is worth
	// keeping even where it is currently safe.
	GLint previous = 0;
	glGetIntegerv( GL_TEXTURE_BINDING_2D_ARRAY, &previous );

	glBindTexture( GL_TEXTURE_2D_ARRAY, texture );
	glTexImage3D( GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, w, h, slots, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );

	// GL_LINEAR across the picture and GL_CLAMP_TO_EDGE at its edges. There is
	// deliberately no filtering BETWEEN layers -- a head landing between two
	// passes reads a tear, not a dissolve, because that is what a tape does and
	// the tear is half of what this plugin looks like. GL_TEXTURE_2D_ARRAY does
	// not filter across layers in any case; this comment is here so that nobody
	// later "fixes" the tear by reaching for a 3D texture.
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );

	const GLenum err = glGetError();
	glBindTexture( GL_TEXTURE_2D_ARRAY, static_cast< GLuint >( previous ) );

	if( err != GL_NO_ERROR )
	{
		diag::error( "the tape would not allocate: " + std::to_string( slots ) + " layers of "
		             + std::to_string( w ) + "x" + std::to_string( h ) + ", GL error "
		             + std::to_string( static_cast< unsigned >( err ) ) );
		Destroy();
		return false;
	}

	glGenFramebuffers( 1, &fbo );
	if( fbo == 0 )
	{
		diag::error( "could not create the tape framebuffer" );
		Destroy();
		return false;
	}

	width  = w;
	height = h;
	layers = slots;
	compW  = compWidth;
	compH  = compHeight;

	resized = true;

	diag::info( "tape allocated: " + std::to_string( slots ) + " passes of " + std::to_string( w )
	            + "x" + std::to_string( h ) + " ("
	            + std::to_string( ( perPixel * w * h ) / ( 1024.0 * 1024.0 ) ) + " MB) for a "
	            + std::to_string( compWidth ) + "x" + std::to_string( compHeight ) + " composition" );

	return true;
}

bool Store::ConsumeResized()
{
	const bool was = resized;
	resized        = false;
	return was;
}

bool Store::BindLayer( int layer )
{
	if( texture == 0 || fbo == 0 )
		return false;

	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture, 0,
	                           std::clamp( layer, 0, layers - 1 ) );

	if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
	{
		diag::error( "the tape framebuffer is incomplete at layer " + std::to_string( layer ) );
		return false;
	}

	glViewport( 0, 0, width, height );
	return true;
}

} // namespace astronaught
