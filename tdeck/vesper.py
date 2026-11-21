# Pre-build: what the SF32 build gets from src/platform/variant.h and SConscript, for the T-Deck.
# VESPER_VERSION from src/platform/variant.h (one place for the version: the release workflow
# checks it), and the build date (SOURCE_DATE_EPOCH if set) as FIRMWARE_BUILD_DATE.
import os
import re
import time

Import("env")  # type: ignore

root = os.path.join(env.subst("$PROJECT_DIR"), "..")  # type: ignore
variant = open(os.path.join(root, "src", "platform", "variant.h")).read()
version = re.search(r'#define VESPER_VERSION "([^"]+)"', variant).group(1)
t = time.localtime(int(os.environ["SOURCE_DATE_EPOCH"])) if os.environ.get("SOURCE_DATE_EPOCH") else time.localtime()
date = "%d %s %d" % (t.tm_mday, "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec".split()[t.tm_mon - 1], t.tm_year)
env.Append(CPPDEFINES=[("VESPER_VERSION", env.StringifyMacro(version)),  # type: ignore
                       ("FIRMWARE_BUILD_DATE", env.StringifyMacro(date))])

# Every C++ file gets src/tdeck/tdeck_platform.h first (like the SF32's sf32_platform.h). An
# absolute path: libraries compile from other folders. (A list item: SCons quotes the spaces.)
env.Append(CXXFLAGS=["-include", os.path.realpath(os.path.join(root, "src", "tdeck", "tdeck_platform.h"))])  # type: ignore
