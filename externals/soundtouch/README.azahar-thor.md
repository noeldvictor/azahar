# Azahar Thor SoundTouch Vendor Note

This directory vendors `azahar-emu/soundtouch` commit
`9ef8458d8561d9471dd20e9619e3be4cfe564796`, formerly referenced as a Git
submodule. It is kept in the main repository so Thor-specific ARM64 changes do
not depend on a separate customized dependency repository.

The unused prebuilt Android wrapper JAR and example DLL/shared-library binaries
from the source package are intentionally omitted. Source, build files,
documentation, and the upstream LGPL-2.1-or-later license remain here.

Fork-specific changes:

- AArch64 integer stereo overlap uses NEON widening multiply-accumulate and
  exact power-of-two division with C++ truncation-toward-zero semantics.
