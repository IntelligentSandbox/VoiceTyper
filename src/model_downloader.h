#pragma once

#include "host_services.h"
#include "state.h"

inline bool
start_model_download(GlobalState *AppState, const std::string &ModelName,
	const std::string &Url, const std::string &DestPath, int64_t ExpectedSize)
{
	if (AppState->Ui.Download.IsRunning.load()) return false;
	if (AppState->Ui.Download.Thread.joinable()) AppState->Ui.Download.Thread.join();

	AppState->Ui.Download.IsRunning.store(true);
	AppState->Ui.Download.CancelRequested.store(false);
	AppState->Ui.Download.Succeeded.store(false);
	AppState->Ui.Download.Failed.store(false);
	AppState->Ui.Download.DownloadedBytes.store(0);
	AppState->Ui.Download.TotalBytes.store(ExpectedSize);
	AppState->Ui.Download.ChildPid.store(0);
	AppState->Ui.Download.JustFinished = false;
	AppState->Ui.Download.CurrentModelName = ModelName;

	AppState->Ui.Download.Thread = std::thread(
		platform_download_file_thread,
		AppState, Url, DestPath, ExpectedSize);

	return true;
}

inline void
cancel_model_download(GlobalState *AppState)
{
	if (!AppState->Ui.Download.IsRunning.load()) return;
	AppState->Ui.Download.CancelRequested.store(true);
	platform_cancel_model_download(AppState);
}

inline void
poll_model_download(GlobalState *AppState)
{
	if (AppState->Ui.Download.IsRunning.load()) return;
	if (!AppState->Ui.Download.Thread.joinable()) return;
	if (AppState->Ui.Download.JustFinished) return;

	AppState->Ui.Download.Thread.join();
	AppState->Ui.Download.JustFinished = true;
}

inline void
shutdown_model_download(GlobalState *AppState)
{
	cancel_model_download(AppState);
	if (AppState->Ui.Download.Thread.joinable()) AppState->Ui.Download.Thread.join();
}
