#!/usr/bin/env python3

# Checks that tests/servers/rendering/test_shader_rd_variants.cpp still covers the renderer's shaders. That test
# compiles every ShaderRD variant from define lists that mirror the renderer's C++, so a renderer change that adds a
# shader class or a variant define would otherwise lose coverage without failing anything.
#
# Fails when a ShaderRD class the renderer uses, or a define the renderer passes to its shaders (any `#define` in a
# string literal of servers/rendering/renderer_rd), doesn't appear in the test, unless it is listed in IGNORED_DEFINES.
# Run from the repository root:
#   python3 misc/scripts/check_shader_rd_variants.py

import glob
import re
import sys

RENDERER = "servers/rendering/renderer_rd"
TEST = "tests/servers/rendering/test_shader_rd_variants.cpp"

# Defines the test doesn't need to name, with the reason.
IGNORED_DEFINES = {
    # Added by ShaderRD itself to every variant it builds, which the test builds through ShaderRD too.
    "MATERIAL_UNIFORMS_USED": "ShaderRD, from material code",
    "RENDER_DRIVER_": "ShaderRD",
    "samplerExternalOES": "ShaderRD",
    "textureExternalOES": "ShaderRD",
    "NATIVE_LOCAL_GROUP_SIZE": "ShaderRD, never added to any source",
    "NATIVE_LOCAL_SIZE_2D_X": "ShaderRD, never added to any source",
    "NATIVE_LOCAL_SIZE_2D_Y": "ShaderRD, never added to any source",
    # The path tracer's custom hit groups, compiled from user material code at run time.
    "RT_CUSTOM_HIT_GROUP": "custom hit groups need material code",
    "RT_USES_INSTANCE_CUSTOM": "custom hit groups need material code",
    "RT_USES_SCENE_DEPTH": "custom hit groups need material code",
    "m_": "a macro parameter in scene_shader_raytracing.cpp, not a define",
}

STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
DEFINE = re.compile(r"#define\s+([A-Za-z_][A-Za-z0-9_]*)")
SHADER_CLASS = re.compile(r"\b([A-Z][A-Za-z0-9]*ShaderRD)\b")


def code_lines(path):
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            # Commented-out code passes nothing to a shader.
            code = line.split("//", 1)[0] if not line.lstrip().startswith("//") else ""
            yield number, code


def main():
    renderer_defines = {}
    renderer_classes = {}
    sources = glob.glob(RENDERER + "/**/*.cpp", recursive=True) + glob.glob(RENDERER + "/**/*.h", recursive=True)
    for path in sorted(sources):
        if "/shaders/" in path:
            continue
        for number, code in code_lines(path):
            # Material compiler actions map render modes and usages of user code, not variants.
            if "#define" in code and "actions." not in code:
                for literal in STRING_LITERAL.findall(code):
                    for name in DEFINE.findall(literal):
                        renderer_defines.setdefault(name, f"{path}:{number}")
            for name in SHADER_CLASS.findall(code):
                if name != "ShaderRD":
                    renderer_classes.setdefault(name, f"{path}:{number}")

    with open(TEST, encoding="utf-8") as f:
        test = f.read()
    test_defines = set(DEFINE.findall(test))
    test_classes = set(SHADER_CLASS.findall(test))

    missing = []
    for name, where in sorted(renderer_defines.items()):
        if name not in test_defines and name not in IGNORED_DEFINES:
            missing.append(f"define {name} ({where})")
    for name, where in sorted(renderer_classes.items()):
        if name not in test_classes:
            missing.append(f"shader class {name} ({where})")

    if missing:
        print(f"{TEST} doesn't cover these, add them to it (or to IGNORED_DEFINES with a reason):")
        for line in missing:
            print("  " + line)
        sys.exit(1)
    print(f"{TEST} covers the renderer's {len(renderer_classes)} shader classes and {len(renderer_defines)} defines.")


if __name__ == "__main__":
    main()
