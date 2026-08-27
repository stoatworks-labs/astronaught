#pragma once

#include <FFGLSDK.h>

namespace astronaught
{
/**
    The tape's pixels: `tape::kSlots` passes of the record head, as the layers
    of one `GL_TEXTURE_2D_ARRAY`.

    ------------------------------------------------- why an array and not a queue

    A playback head does not know which slot it wants until it has looked at
    where the tape is, and after a speed change the answer differs per output
    row -- so the choice of slot has to be made **per fragment**, which means the
    shader has to be able to index the store. A texture array is the only
    storage that lets it. A queue of separate textures would have to bind all
    ninety-six of them, and a single wide atlas would put a filter tap from one
    pass into the pass next door at every seam.

    ------------------------------------------------------- eight bits, on purpose

    `GL_RGBA8`. Two reasons, and only one of them is about memory.

    Tape is not a high-headroom medium. It saturates -- that is the effect --
    and it has a noise floor around fifty decibels down, which is roughly eight
    bits. Storing the loop at sixteen would be modelling a medium this plugin
    then spends a shader pass taking apart.

    The quantisation would be a real objection in a feedback loop, where an
    error compounds every trip: eight-bit banding normally accumulates into
    posterised steps. It does not here because **the hiss dithers it**, and the
    hiss is inside the loop and on by default. Turn Hiss to zero, put Intensity
    near unity and leave it running and the banding is visible -- which is
    correct behaviour for a tape with no noise floor, and is also the honest
    limit of this choice.

    -------------------------------------------------------------- the budget

    Ninety-six layers at the composition's own resolution is more memory than an
    effect on one layer has any business taking, so the store is scaled down
    until it fits `kByteBudget`. It is never scaled up, and never below a
    quarter of the composition in each direction.

    This costs less than it sounds like it should. The direct path never goes
    near the store -- `Direct Volume` is a dry feed straight off the input -- so
    what loses resolution is the echoes, and an echo off a tape has less
    bandwidth than the signal that made it. The picture that reaches a viewer is
    a sharp direct image with soft repeats behind it, which is the right
    picture. But the number is a memory budget and not a Roland figure, and the
    README says so rather than dressing it up.
*/
class Store
{
public:
	~Store();

	/// Bytes the loop is allowed. 96 layers of RGBA8 is 384 bytes a pixel, so
	/// this is about a megapixel of tape.
	static constexpr size_t kByteBudget = 384u * 1024u * 1024u;

	/// The store is never scaled below this fraction of the composition in each
	/// direction. Past it the echoes stop reading as the same picture.
	static constexpr float kMinScale = 0.25f;

	/// Allocate for a composition of this size, reusing what is there if it
	/// already matches. `slots` is the ring length.
	bool Ensure( int compWidth, int compHeight, int slots );

	/// True on the frame `Ensure` had to reallocate. The tape's positions
	/// describe pixels that no longer exist, so `tape::Loop::Erase` must follow
	/// -- and that is the ONLY thing allowed to erase the loop. A parameter
	/// change must not.
	bool ConsumeResized();

	void Destroy();

	bool IsValid() const
	{
		return texture != 0;
	}

	GLuint Texture() const
	{
		return texture;
	}

	/// Bind the store's framebuffer with `layer` attached, ready to draw one
	/// pass of the record head into. Leaves the framebuffer bound; the caller
	/// restores.
	bool BindLayer( int layer );

	int Width() const
	{
		return width;
	}
	int Height() const
	{
		return height;
	}
	int Slots() const
	{
		return layers;
	}

private:
	GLuint texture = 0;
	GLuint fbo     = 0;

	int width  = 0;
	int height = 0;
	int layers = 0;

	int compW = 0;
	int compH = 0;

	bool resized = false;
};

} // namespace astronaught
