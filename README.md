# uFMOD C

A standalone, decoupled C reimplementation of the [uFMOD](https://sourceforge.net/projects/ufmod/) XM playback engine and software mixer.

## Building

Build the static library and tools:

```sh
# Static library and dump tool
make

# ALSA CLI player (requires libasound2-dev)
make ufmod_player
```

## Integration

To use public API, include:

```c
#include "ufmod.h"
```

Link against `libufmod.a` and `-lm`.

> **Note:** This port is not intended to match the raw performance of the original x86 assembly engine. It was developed primarily for portability and to preserve uFMOD's distinct playback quirks.
