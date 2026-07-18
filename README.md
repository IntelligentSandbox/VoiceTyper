# VoiceTyper
VoiceTyper aspires to be a fast, lightweight, native, fully-local and offline dictation application.
As a standalone program, it can be used to input text directly from your voice into other 
desktop applications such as your web browser, note taking app, or even messaging
app that doesn't have a voice input feature. 

## LLM Usage Disclaimer:
LLMs are used to write code in this project with human review done at our discretion.

LLM Coding Agent Harnesses Used:
- [OpenCode](https://github.com/anomalyco/opencode)
- [Claude Code](https://code.claude.com/docs/en/overview)

LLMs Used:
- [GLM 5.2](https://z.ai/blog/glm-5.2)
- [OpenCode Zen Big Pickle](https://grokipedia.com/page/Big_Pickle_model)
- [Claude Opus 4.6](https://www.anthropic.com/news/claude-opus-4-6)
- [Claude Sonnet 4.6](https://www.anthropic.com/news/claude-sonnet-4-6)
- [OpenAI GPT-5.5](https://platform.openai.com/docs/models/gpt-5.5)

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
Currently we only target Windows OS.
Precompiled binary releases are available via GitHub Releases.

To compile the project for yourself, you will need:
- C++ compiler toolchain (e.g. Visual Studio 17 2022 MSVC)
- `cmake` (e.g. 3.31.6)
Optional
- NVIDIA CUDA toolkit (e.g. v13.2)

To download ggml whisper models, get them from huggingface [here](https://huggingface.co/ggerganov/whisper.cpp/tree/main).
