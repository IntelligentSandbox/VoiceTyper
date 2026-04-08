# Voice Typer
Voice Typer aspires to be a native application that you can use to synthesize text
from your voice using on device NN models that can be used in any of your other 
desktop applications such as your web browser, note taking app, or even messaging
app that doesn't have a voice input feature. Later down the line, we can also have
custom user commands via keywords/keyphrases.

## Disclaimers:
AI is used to write code in this project with human review done at our discretion.
We try our best to make something that is fast, usable, fully offline with minimal dependencies.
Let the results and code quality speak for themselves...the sloppening...
AI Coding programs used include but are not limited to:
- [opencode](https://github.com/anomalyco/opencode)
- [claude code](https://code.claude.com/docs/en/overview)

## Dependencies
Sources copied directly into the repo:
[whisper.cpp](https://github.com/ggml-org/whisper.cpp)
- This project would not be feasible without this external external dependency.
- Anytime we update our snapshot of whisper.cpp we will make a copy of their sourcetree into this repo.
- Notably, whisper.cpp also depends on [ggml](https://github.com/ggml-org/ggml)
    - ggml version of the whisper and vad models are used
[Dear ImGui](https://github.com/ocornut/imgui)
- Our UI lib of choice.
- Originally used [Qt](https://www.qt.io/development/qt-framework) for the ui, but wanted something simpler that we could just embed into the project source.

## Getting Started
Precompiled binary releases are available via GitHub Releases.

To compile the project for yourself, you will need:
- C++ compiler (e.g. Visual Studio 17 2022 MSVC)
- `cmake` (e.g. 3.31.6)
- NVIDIA CUDA toolkit (if you want to build with cuda capability) (e.g. v13.2)

```bash
./build.sh        # release cpu
./build.sh cuda   # release cuda

./build.sh debug        # debug cpu
./build.sh debug cuda   # debug cuda
```


TODO: add a nix flake, well get a linux port first then do that...
