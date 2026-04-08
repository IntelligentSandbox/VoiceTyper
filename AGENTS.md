## Environment
You are running on **Windows 11** cmd.exe, but you have access to git bash `bash`.
Depending on your agent harness, you will be able to directly access bash commands here.
You are always ran from the root dir of this project.
Use Unix commands and forward slashes for paths: `ls`, `cp`, `mv`.
Reference paths using relative paths like `./src/audio_pipeline.h`.
All paths will be linux style under git bash: `/c/dir1/dir2`.

## Project Overview
This is a voice typing application that runs as a native application on the host OS.
It performs real-time audio transcription using a locally hosted, in-memory Speech-to-Text model
via **Whisper.cpp** (an external project and core dependency).
The application's primary responsibility is facilitating user control of:
    audio input -> Whisper.cpp model -> insert text into focused text input field (if available)

    bash build.sh          - release cpu build
    bash build.sh cuda     - release cuda build

## Workflow
1. Read `docs/tasks.md` at the start of every session.
2. Work through tasks in order, top to bottom. Do not skip tasks.
3. For each task:
   a. Implement the change.
   b. Verify it passes all release build variations (e.g. cpu, cuda)
   c. Commit with: `git add -A && git commit -m "<task title>" -m "<brief description of what was done>"`
      - Use multiple `-m` flags for multiline messages.
      - Never use heredocs or command substitution in git commands.
   d. Mark the task complete in `docs/tasks.md` by checking its checkbox as soon as the respective task is complete and commited in git.
4. Stop and report once all tasks are complete.

## Task Complexity
If a task is marked `[PLAN]` instead of `[ ]`, do not implement it.
Instead, decompose it into concrete sub-tasks, write them into `docs/tasks.md`
replacing the `[PLAN]` entry, and stop for review before proceeding.

## Coding Guidelines

### Indentation & Formatting
- Use **tab characters** for indentation (not spaces)
- Use **LF line endings**
- Keep lines under **120 characters**; unroll onto multiple lines if needed

### Comments
- Do **not** insert comments unless explicitly directed to
- Do **not** remove existing code comments unless explicitly directed to

### Control Flow
- Use **early returns** where possible to reduce nesting/indentation
- Single-line `if` statements are allowed without curly brackets, but must fit on one line within 120 chars;
  otherwise use curly brackets on separate lines
- Any `if` and `else` statements must use curly brackets for **both** block scopes
- Single-statement `for` loops **must** use curly brackets

### General
- Follow existing coding style and conventions already present in the codebase
