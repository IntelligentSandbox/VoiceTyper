## Voice Typer
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
[whisper.cpp](https://github.com/ggml-org/whisper.cpp)
- This project would not be feasible without this external external dependency.
- Anytime we update our snapshot of whisper.cpp we will make a copy of their sourcetree into this repo.
- Notably, whisper.cpp also depends on [ggml](https://github.com/ggml-org/ggml)
    - ggml version of the whisper and vad models are used
[Dear ImGui](https://github.com/ocornut/imgui)
- Our UI lib of choice.
- Originally used [Qt](https://www.qt.io/development/qt-framework) for the ui, but wanted something simpler that we could just embed into the project source.
