# Tasks

Tasks are worked top to bottom. Each task is committed individually.

- [x] Disable settings button/settings dialog from being allowed to be opened if in the middle of a transcription or streaming, should be a simple check the bool flag in gamestate in the toggle settings fn that gets triggered.
- [x] Add a sound for when it's cancelled. It should be a different beep sound than the start/end ones. Preferably like a deeper sound that should indicate cancellation.
- [x] address this TODO in system.h (Should be done by checking if the corresponding model files are found in their expected path. if not found, the select option in the drop down combo select should be disabled or not available. If no models are found at all, then the combo select should have a default option that is disabled but like say No Models found. And then we should not allow any transcription/streaming to start.)
      // TODO(warren): Assuming all the model files are present. 
      // Maybe want to actually look to see if they are present on disk, but if they
      // aren't, do we want to just download them automatically from huggingface?
- Refactor Audio Pipeline (goal is to clean things up for future cross platform implementations)
  - [ ] Move Win32 audio capture structs (WaveInBuffer, AudioPipelineContext) and wavein_proc callback from audio_pipeline.h into platform_win32.h
  - [ ] Extract run_wavein_capture() from audio_pipeline.h into platform_win32.h, keeping the same function signature (takes GlobalState*, int DeviceIndex; writes float samples into AudioAccumBuffer via mutex)
  - [ ] Add platform-agnostic function declaration/interface comment in audio_pipeline.h that documents the contract: `run_platform_audio_capture(GlobalState*, int)` — returns bool, pumps float samples into AudioAccumBuffer until CaptureRunning goes false
  - [ ] Rename run_wavein_capture to run_platform_audio_capture in platform_win32.h and update all call sites in audio_pipeline.h (record_pipeline_thread, streaming_pipeline_thread)
  - [ ] Remove `#include <windows.h>` and `#include <mmsystem.h>` from audio_pipeline.h (these should only be needed in platform_win32.h now)
  - [ ] Verify both release and debug builds pass with the refactored structure
- Add a way to display an icon for the application (PNG file path based)
  - [ ] Add a platform-agnostic icon path constant or config (e.g. `#define APP_ICON_PATH "data/icon.png"`) in state.h or a shared location
  - [ ] In system.h, add `set_application_icon(GlobalState*)` that uses Qt's QIcon/QPixmap to load the PNG and call `AppState->QtMainWindow->setWindowIcon(...)` — this is platform-agnostic via Qt
  - [ ] In platform_win32.h, add `set_taskbar_icon(HWND, const char*)` to set the Win32 window icon via LoadImage/SendMessage WM_SETICON for the taskbar and title bar (needed because Qt's setWindowIcon doesn't always update the Win32 taskbar icon reliably)
  - [ ] Call both icon-setting functions from main.cpp after the window is shown and OwnWindow is set
  - [ ] Create or source a placeholder PNG icon file at the configured path (e.g. data/icon.png)
  - [ ] Verify both release and debug builds pass and the icon shows in the title bar and taskbar
