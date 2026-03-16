# Tasks

Tasks are worked top to bottom. Each task is committed individually.

- [x] Disable settings button/settings dialog from being allowed to be opened if in the middle of a transcription or streaming, should be a simple check the bool flag in gamestate in the toggle settings fn that gets triggered.
- [ ] Add a sound for when it's cancelled. It should be a different beep sound than the start/end ones. Preferably like a deeper sound that should indicate cancellation.
- [ ] address this TODO in system.h (Should be done by checking if the corresponding model files are found in their expected path. if not found, the select option in the drop down combo select should be disabled or not available. If no models are found at all, then the combo select should have a default option that is disabled but like say No Models found. And then we should not allow any transcription/streaming to start.)
      // TODO(warren): Assuming all the model files are present. 
      // Maybe want to actually look to see if they are present on disk, but if they
      // aren't, do we want to just download them automatically from huggingface?
- [PLAN] Refactor Audio Pipeline (goal is to clean things up for future cross platform implementations)
         This should involved read src/audio_pipeline.h and try to factor out the win32 specifics into the src/platform_win32.h file and have a platform agnostic interface(s) with the structs and functions that can be shared.
- [PLAN] Add a way to display an icon for the application. The icon should be a graphic that we can point a filepath to. It probably will be a simple PNG. This should be broken down into components that need to be implemented in the platform_win32.h layer and in the system.h layer which contains the platform agnostic core functions.
