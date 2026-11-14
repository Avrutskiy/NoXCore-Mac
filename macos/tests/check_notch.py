"""Link the safe-area harness against an already-built Ninja Mac client.

No game assets or graphics session are needed. Compiler output and the test binary
stay in <build>/mac-notch-tests; the production objects and archives are untouched.
"""
import json
from pathlib import Path
import shlex
import subprocess
import sys


def main():
    build = Path(sys.argv[1]).resolve()
    source = Path(__file__).resolve().parent
    output = build / "mac-notch-tests"
    output.mkdir(exist_ok=True)
    commands = json.loads((build / "compile_commands.json").read_text())

    def run(args):
        subprocess.run(list(map(str, args)), cwd=build, check=True)

    def compile_as(original, replacement, obj):
        item = next(c for c in commands if c["file"].endswith(original))
        args = shlex.split(item["command"])
        args[args.index("-c") + 1] = str(source / replacement)
        args[args.index("-o") + 1] = str(output / obj)
        # Keep production flags, including the optimization level of SDL's PCH.
        run(args)

    compile_as("/noxsdl/src/main.cpp", "notch_integration.cpp", "integration.o")
    compile_as("/src/video/dummy/SDL_nullvideo.c", "notch_dummy_confine.c", "dummy.o")
    target = "noxsdl/noxcore"
    lines = subprocess.check_output(
        ["ninja", "-t", "commands", target], cwd=build, text=True).splitlines()
    link = next(line for line in reversed(lines) if f" -o {target} " in line)
    args = shlex.split(next(part.strip() for part in link.split("&&") if f" -o {target} " in part))
    executable = output / "integration"
    args[args.index("-o") + 1] = str(executable)
    args = [str(output / "integration.o") if arg.endswith("/src/main.cpp.o") else arg
            for arg in args if not arg.endswith("/macos/launcher.cpp.o")]
    run([*args, output / "dummy.o"])
    run([executable])


if __name__ == "__main__":
    main()
