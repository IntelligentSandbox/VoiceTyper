# CLAUDE.md

## Environment

You are running on **Windows**, but the shell is Unix-like (Git Bash / MSYS2) — not cmd.exe.
Use Unix commands and forward slashes for paths: `ls`, `cp`, `mv`, `E:/repos/project`.
You are ALWAYS running from the project root. NEVER use cd before build commands.

## Project Overview

This is a voice typing application that runs as a native application on the host OS.
It performs real-time audio transcription using a locally hosted, in-memory Speech-to-Text model
via **Whisper.cpp** (an external project and core dependency).

The application's primary responsibility is facilitating user control of:

	audio input -> Whisper.cpp model -> insert text into focused text input field (if available)

	build.bat          - release build
	build.bat debug    - debug build

## Workflow
Always ensure changes pass **both** release and debug builds before considering work complete.
Build the project in release mode `build.bat` and in debug mode `build.bat debug`.
You are ALWAYS running from the project root. NEVER use cd before build commands.
After completing each task, commit changes with: `git add -A && git commit -m "<brief description of what was done>"`

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
