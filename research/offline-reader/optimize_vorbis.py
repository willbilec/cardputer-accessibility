from pathlib import Path

Import("env")


def optimize_decoder(build_env, source):
    if "micro-vorbis" not in Path(source.srcnode().get_path()).parts:
        return source
    # Match microVorbis's ESP-IDF release optimization for the codec only.
    flags = [flag for flag in build_env["CCFLAGS"] if flag not in ("-Os", "-Oz")]
    return build_env.Object(source, CCFLAGS=[*flags, "-O3"])[0]


env.AddBuildMiddleware(optimize_decoder)
