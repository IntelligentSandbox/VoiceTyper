## Environment
You are always run from the root dir of this project. Use Unix commands and forward
slashes for paths (`ls`, `cp`, `mv`) and reference paths relatively, e.g. `./src/audio_pipeline.h`.

Detect the host platform at the start of each session by running `uname -s`, since the
same project is built on both Windows and NixOS/Linux and the tooling differs:
- `Linux`            -> native Linux (NixOS). Native paths like `/home/<user>/VoiceTyper`.
                       Use the `tools/nix-build.sh` flake builds (see Project Overview).
- `MINGW*` / `MSYS*` -> Windows under git bash. Paths like `/c/dir1/dir2`.
                       Use the `tools/win-build.sh` Visual Studio builds (see Project Overview).

Match build commands and any platform-specific source (`src/*_win32.*`, `src/*_linux.*`)
to the detected platform rather than assuming one.

## Project Overview
This is a voice typing application that runs as a native application on the host OS.
It performs real-time audio transcription using a locally hosted, in-memory Speech-to-Text model
via **Whisper.cpp** (an external project and core dependency).
The application's primary responsibility is facilitating user control of:
    audio input -> Whisper.cpp model -> insert text into focused text input field (if available)

Windows (Visual Studio toolchain):
    bash tools/win-build.sh          - release cpu build
    bash tools/win-build.sh cuda     - release cuda build

NixOS / Linux (nix flake, defined in flake.nix, driven by tools/nix-build.sh):
    tools/nix-build.sh           - release cpu build (.#default)
    tools/nix-build.sh cuda      - release cuda build (.#cuda, needs allowUnfree)
    tools/nix-build.sh static    - release statically-linked build (.#static, musl, truly static)
    tools/nix-build.sh appimage  - release AppImage build (.#appimage, FHS-portable)
    tools/nix-build.sh cuda-static   - portable CUDA bundle (.#cuda-static, bundles ld-linux +
                                      full .so closure incl. cudart/cublas; not truly static.
                                      needs allowUnfree)
    tools/nix-build.sh cuda-appimage - self-contained CUDA AppImage (.#cuda-appimage, needs allowUnfree)

    Any of the above can be combined with `--ccache` (e.g. `tools/nix-build.sh static --ccache`)
    to reuse object files across rebuilds. Requires a ccache dir that nixbld can write
    (default /var/cache/voicetyper-ccache, or $CCACHE_DIR; see tools/nix-build.sh).

Releases (run from a Windows host in git bash):
    tools/release-all.sh            - build + package Windows cpu/cuda and portable Linux (cpu + cuda: static + AppImage)
                                      outputs and create a GitHub release. Builds Windows locally and sshes into
                                      the NixOS box (VOICETYPER_NIX_SSH, default 'rock') for the portable Linux
                                      outputs. Linux packaging: tools/nix-package.sh.
    tools/win-release.sh            - Windows-only release flow (tag + windows package + release).

## Workflow
1. Read `.dev/tasks.md` at the start of every session. The user may give you an explicit request, which should take priority over the tasklist. However, if the tasklist contains items that may conflict with the one-off request, notify the user and ask what to do.
2. Work through tasks in order, top to bottom. Do not skip tasks.
3. For each task:
   a. Implement the change.
   b. Verify it passes all release build variations (e.g. cpu, cuda)
   c. Commit with: `git add -A && git commit -m "<task title>" -m "<brief description of what was done>"`
      - Use multiple `-m` flags for multiline messages.
      - Never use heredocs or command substitution in git commands.
   d. Mark the task complete in `.dev/tasks.md` by checking its checkbox as soon as the respective task is complete and commited in git.
4. Stop and report once all tasks are complete.

## Task Complexity
If a task is marked `[PLAN]` instead of `[ ]`, do not implement it.
Instead, decompose it into concrete sub-tasks, write them into `.dev/tasks.md`
replacing the `[PLAN]` entry, and stop for review before proceeding.

## Coding Guidelines

### Indentation & Formatting
- Use **tab characters** for indentation (not spaces)
- Exception: `*.nix` files use **2 spaces** per [Nix RFC 166](https://github.com/NixOS/rfcs/blob/master/rfcs/0166-nix-formatting.md). Run `nixfmt` (available in the nix devShell via `nix develop`) to canonicalize.
- Use **LF line endings**
- Keep lines under **120 characters**; unroll onto multiple lines if needed

### Comments
- Do **not** insert comments unless explicitly directed to
- Do **not** remove existing code comments unless explicitly directed to

### Control Flow
- Use **early returns** where possible to reduce nesting/indentation
- Single-statement `if` and `else` bodies go on the **same line** as the condition, without curly brackets (the 120-char line limit does not apply to these)
- Multi-statement `if`/`else` bodies use curly brackets on **separate lines**
- Single-statement `for` loops **must** use curly brackets

### General
- Follow existing coding style and conventions already present in the codebase
