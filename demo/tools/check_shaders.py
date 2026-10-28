"""The demo's shaders must be the plugin's shaders, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.

------------------------------------------------------------------- why

`demo/shaders.js` holds the GLSL of `source/shaders/*.cpp`: the vertex stage,
the two function libraries (the tape read and the medium), the preamble every
fragment stage starts with, and the body each of the four passes appends.
That is two copies of the same text, and two copies drift -- quietly, because
an echo drawn by a *plausible* shader looks exactly like one drawn by the right
one. The whole claim of the page is that it runs the plugin's own shaders, so
the claim needs something enforcing it. `astest` drives the real plugin class
and has never heard of this page.

------------------------------------------------------------------- what it does

Pulls each piece out of the C++ and the matching backtick literal out of
`demo/shaders.js`, and compares them exactly -- no whitespace normalisation, no
comment stripping: a comment updated on one side only is exactly the drift
worth catching. `kVersion`, a plain string, is compared too.

The four passes are not constants in the C++. Each is a function that returns
`std::string( kVersion ) + kPreamble [+ a library] + R"( ... )"`, so the piece
compared is that trailing raw string, and the JOIN is checked separately: the
`return` line of each function must still say what it says now, and
`demo/plugin.js` must assemble the same pieces in the same order. A change to
either side's assembly fails here and needs a reader.

`ReadProbeFragment` is the one function deliberately NOT carried: it is
`astest --read`'s probe, not part of the machine, and the page never draws it.
Any other `std::string ...Fragment()` that appears in Passes.cpp fails, so a
new pass cannot be silently missing from the page.

The one transformation is a decode, not a normalisation: a backtick cannot
appear raw inside a template literal, so --write escapes one as \\`. The tape
and medium libraries have several, in comments. This undoes that escape and
REJECTS any other backslash or any `${` on the JS side; there are none in the
C++ bodies, so either could only be somebody hiding a difference.

------------------------------------------------------------------- what it cannot

Nothing here checks the PORTED half -- `demo/port.js` (Controls.cpp,
Transport.cpp, Tape.cpp, Spring.cpp, Heads.cpp, Presets.h, Store.cpp's sizing,
and the CPU half of Astronaught::ProcessOpenGL) is a hand translation.
`demo/tools/crosscheck/`, which verify.sh also runs, holds it to the real
plugin class frame for frame.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ file, C++ symbol. Constants are `const char* const kX = R"(`;
# a function piece is the raw string a `std::string X()` returns last.
CONSTANTS = [
    ("VERTEX", "source/shaders/Vertex.cpp", "kVertex"),
    ("TAPE_FUNCTIONS", "source/shaders/Tape.cpp", "kTapeFunctions"),
    ("MEDIUM_FUNCTIONS", "source/shaders/Medium.cpp", "kMediumFunctions"),
    ("PREAMBLE", "source/shaders/Passes.cpp", "kPreamble"),
]

FUNCTIONS = [
    ("PLAYBACK", "PlaybackFragment"),
    ("RECORD", "RecordFragment"),
    ("SPRING", "SpringFragment"),
    ("OUTPUT", "OutputFragment"),
]

# Functions in Passes.cpp the page does not carry, and why.
NOT_CARRIED = {
    "ReadProbeFragment": "astest --read's probe, not part of the machine",
}

# How the pieces are joined, on each side. Each entry is (file, text that must
# appear in it verbatim). The C++ lines are the plugin's assembly; the JS lines
# are plugin.js's copy of it. A change to either is a change to what runs.
ASSEMBLY = [
    ("source/shaders/Passes.cpp",
     "std::string PlaybackFragment()\n{\n\treturn std::string( kVersion ) + kPreamble + kTapeFunctions + R\"("),
    ("source/shaders/Passes.cpp",
     "std::string RecordFragment()\n{\n\treturn std::string( kVersion ) + kPreamble + kMediumFunctions + R\"("),
    ("source/shaders/Passes.cpp",
     "std::string SpringFragment()\n{\n\treturn std::string( kVersion ) + kPreamble + R\"("),
    ("source/shaders/Passes.cpp",
     "std::string OutputFragment()\n{\n\treturn std::string( kVersion ) + kPreamble + R\"("),
    ("source/Astronaught.cpp",
     "if( !stage.shader->Compile( shaders::kVertex, stage.fragment.c_str() ) )"),
    ("demo/plugin.js", "playback: new Program(gl, S.VERTEX, S.VERSION + S.PREAMBLE + S.TAPE_FUNCTIONS + S.PLAYBACK, 'playback'),"),
    ("demo/plugin.js", "record: new Program(gl, S.VERTEX, S.VERSION + S.PREAMBLE + S.MEDIUM_FUNCTIONS + S.RECORD, 'record'),"),
    ("demo/plugin.js", "spring: new Program(gl, S.VERTEX, S.VERSION + S.PREAMBLE + S.SPRING, 'spring'),"),
    ("demo/plugin.js", "output: new Program(gl, S.VERTEX, S.VERSION + S.PREAMBLE + S.OUTPUT, 'output'),"),
]

HEADER = """// GENERATED from source/shaders/*.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \\` for a backtick inside a comment.
// The four pass bodies are the raw string each Fragment() function in
// Passes.cpp appends; plugin.js joins them to the rest as Passes.cpp does.
"""


def read(path):
    with open(os.path.join(REPO, *path.split("/"))) as handle:
        return handle.read()


def cpp_version(source):
    match = re.search(r'const char\* const kVersion = "(.*?)";', source)
    return None if match is None else match.group(1)


def cpp_constant(source, symbol):
    match = re.search(r'const char\* const ' + symbol + r' = R"\((.*?)\)";', source, re.S)
    return None if match is None else match.group(1)


def cpp_function(source, name):
    """The raw string a `std::string name()` returns last, or None."""
    match = re.search(r'std::string ' + name + r'\(\)\n\{\n\treturn [^\n]*?R"\((.*?)\)";\n\}', source, re.S)
    return None if match is None else match.group(1)


def cpp_functions(source):
    return re.findall(r'^std::string (\w+Fragment)\(\)$', source, re.M)


def pieces():
    """Every (JS name, C++ label, C++ text) the page carries, or a failure."""
    out = []
    for name, path, symbol in CONSTANTS:
        out.append((name, f"{symbol} ({path})", cpp_constant(read(path), symbol)))
    passes = read("source/shaders/Passes.cpp")
    for name, function in FUNCTIONS:
        out.append((name, f"{function}() (source/shaders/Passes.cpp)", cpp_function(passes, function)))
    return out


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def write():
    out = [HEADER]
    version = cpp_version(read("source/shaders/Passes.cpp"))
    if version is None:
        print("FAIL  kVersion not found in source/shaders/Passes.cpp")
        return 1
    out.append(f"export const VERSION = '{version}';\n")
    for name, label, body in pieces():
        if body is None:
            print(f"FAIL  {label} not found")
            return 1
        if "\\" in body or "${" in body:
            print(f"FAIL  {label} holds a backslash or ${{; the escape scheme cannot carry it")
            return 1
        out.append(f"\n// {label}\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
        handle.write("".join(out))
    print("wrote demo/shaders.js")
    return 0


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def check(js):
    problems = 0
    passes = read("source/shaders/Passes.cpp")

    version_cpp = cpp_version(passes)
    version_js = re.search(r"^export const VERSION = '(.*?)';$", js, re.M)
    if version_cpp is None or version_js is None or version_cpp != version_js.group(1):
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<18} matches kVersion")

    listed = {function for _, function in FUNCTIONS} | set(NOT_CARRIED)
    for function in cpp_functions(passes):
        if function not in listed:
            print(f"FAIL  Passes.cpp defines {function}(), which this check does not list -- "
                  "add it to FUNCTIONS (and to the page), or to NOT_CARRIED with the reason")
            problems += 1
    for function, why in NOT_CARRIED.items():
        print(f"skip  {function}() -- {why}")

    for name, label, cpp_text in pieces():
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {label} not found")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            print(f"ok    {name:<18} matches {label} ({len(cpp_text)} chars)")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {label}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    for path, text in ASSEMBLY:
        try:
            body = read(path)
        except FileNotFoundError:
            print(f"FAIL  {path} is missing")
            problems += 1
            continue
        first = text.splitlines()[0]
        if text in body:
            print(f"ok    assembly: {path} still says {first[:64]}")
        else:
            print(f"FAIL  assembly: {path} no longer says: {first}")
            problems += 1
    return problems


def main(argv):
    if "--write" in argv:
        return write()
    try:
        js = read("demo/shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check(js)
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for a drifted copy "
              "(never edit shaders.js by hand); an assembly or unlisted-pass failure needs a reader")
        return 1
    print(f"all {len(CONSTANTS) + len(FUNCTIONS) + 1} shader pieces and their assembly are the plugin's")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
