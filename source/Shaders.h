#pragma once

#include <string>

namespace astronaught::shaders
{
/**
    The GLSL. Four passes, and they are the machine's signal flow in order.

      Playback  Every head that is up, resolved per fragment against the tape's
                slot table, summed. At tape resolution, because it feeds the
                record head and because an echo off a tape is not sharp.
      Record    Input, plus Intensity times the playback bus, through the
                record amplifier and onto one layer of the store: drive,
                saturation, head-gap loss, hiss, dropouts.
      Spring    The tank. Three coupled legs, ping-ponged.
      Output    Direct + Echo + Reverb, the tone controls, and the wet/dry mix,
                straight into the host's framebuffer.

    ⚠️ **The tank is fed from the INPUT, not from the playback bus.** Roland:
    "the reverb signal path is connected in parallel to the tape echo section."
    Feeding it from the heads would put every repeat through the springs again
    and give a wash that grows instead of a tank that rings. It is one line in
    `Astronaught.cpp` and it is the easiest thing in this plugin to get wrong.

    -------------------------------------------------------- the mirrored read

    `Tape.cpp`'s `Loop::Resolve` and `kTapeFunctions`' `tapeRead` are the same
    walk over the same slot table. They have to be: the tape position differs
    per output row -- that is what makes the tear and the stretch -- so the GPU
    has to resolve it per fragment, while the harness needs to be able to check
    the answer without a picture in the way. Both blocks are marked
    `//= mirrored` and `astest --read` compares them.

    The walk is a linear search back from the newest slot, and it is linear on
    both sides on purpose. A binary search would be faster on the CPU and would
    then be a different algorithm from the one the shader can afford, which is
    exactly the situation a mirror exists to avoid.

    ------------------------------------------- the head bus, and why it is normalised

    ☠️ The head bus is divided by the number of heads the Mode Selector has up.
    That is a departure from the circuit, it looks exactly like the averaging bug
    that rendered five of escapement's rigs black, and it is a different thing.
    The distinction is worth stating precisely, because the next person to read
    this will recognise the shape and reach for the fix.

    Escapement's `acc /= tapCount` was **inside a feedback loop**. Dividing there
    is a loop gain of 1/N wearing a normalisation's clothes: it changes whether
    the loop converges, and it deleted the attractor the whole plugin exists to
    find.

    This division is on the bus that leaves the machine. The loop gain is set
    separately and explicitly by `Intensity`, on a bus that is already
    normalised, so it means the same thing in every selector position -- which is
    what makes position 11 behave like a machine rather than like a fault.

    And it is needed because **a video signal is unipolar**. Three heads over a
    part of the picture that has not moved are three copies of the same value,
    and they sum to three times it -- every time, with no cancellation, because
    there is no negative half for anything to cancel into. In audio the same
    circuit is survivable and is why a real RE-201 is usable with all three heads
    up. Here, modelled literally, `astest --presets` rendered six of ten presets
    as flat white, and the machine had four usable selector positions out of
    twelve.

    What is lost is that position 11 is no longer three times brighter than
    position 1. That is the right thing to lose: the difference between them is
    that one has three taps and the other has one, and density is not brightness.
    ------------------------------------------------------ frequency along the scan

    Head wear and the Bass/Treble shelves filter **horizontally only**. Tape has
    one frequency axis -- along its length -- and the picture is laid on it a
    scanline at a time, so along the tape lands along the scan. A symmetrical
    blur would be a lens, and this is not a lens. ferric makes the same argument
    at more length.

    ---------------------------------------------------------------- reserved words

    `sample`, `input`, `output`, `filter`, `common`, `active` and `layout` are
    GLSL keywords. A shader that will not compile surfaces at runtime as "the
    effect does nothing", with no message anywhere the operator can see -- and
    these programs are concatenated from several strings, so any line number the
    driver reports refers to a file that does not exist. `Diag` logs which
    program failed, which is the part that narrows it down.
*/

/// Shared by every pass. MaxUV is always 1 here and the scaling is applied at
/// each fetch instead: the output pass reads a host texture (which may be
/// padded) and two buffers of our own (which are not) in one program, and a
/// single vertex-stage scale cannot serve both.
extern const char* const kVertex;

/// The slot table, the mirrored `tapeRead`, and the head bus. Not a complete
/// shader -- no `#version`, no `main` -- because it is concatenated into three.
extern const char* const kTapeFunctions;

/// The medium: the record amplifier's knee, gap loss, hiss and dropouts. Also
/// not a complete shader.
extern const char* const kMediumFunctions;

/// Pass one. Every head that is up.
std::string PlaybackFragment();

/// Pass two. Onto the tape.
std::string RecordFragment();

/// Pass three. The tank.
std::string SpringFragment();

/// Pass four. The output stage.
std::string OutputFragment();

/// The test probe. Writes what `tapeRead` resolved -- the slot index, the phase
/// and the stretch -- into the three channels, so `astest --read` can compare
/// the GPU's answer against `Tape.cpp` directly rather than inferring it from a
/// finished picture.
///
/// Inferring it is the worse test and was the alternative: the tear, the
/// medium and the output stage would all sit between the thing under test and
/// the measurement, so a wrong slot and a wrong fetch would be
/// indistinguishable.
std::string ReadProbeFragment();

} // namespace astronaught::shaders
