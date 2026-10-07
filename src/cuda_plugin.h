#pragma once

#include "control.h"
#include "host_services.h"
#include "state.h"
#include "system.h"
#include "updater.h"

inline bool
cuda_plugin_supported()
{
	return platform_cuda_plugin_asset_tag()[0] != '\0';
}

// The plugin archive is built against the same ggml as the running exe, so it
// must come from the release matching the CURRENT app version - not the latest
// one (a newer plugin would fail ggml's backend API version check on load).
static void
cuda_plugin_download_thread(GlobalState *AppState)
{
	CudaPluginState *P = &AppState->Ui.CudaPlugin;

	P->Stage.store(CUDA_PLUGIN_STAGE_RESOLVE);

	std::string Body;
	bool NotModified = false;
	if (!updater_fetch_releases(&Body, &NotModified))
	{
		P->FailureReason = "Could not reach GitHub.";
		P->Failed.store(true);
		P->IsRunning.store(false);
		return;
	}

	std::vector<UpdateReleaseInfo> Releases;
	if (!updater_parse_releases_json(Body, &Releases))
	{
		P->FailureReason = "Could not read the release list from GitHub.";
		P->Failed.store(true);
		P->IsRunning.store(false);
		return;
	}

	std::string CurrentTag = std::string("v") + updater_current_version_base();
	const char *AssetTag = platform_cuda_plugin_asset_tag();

	const UpdateAssetInfo *PluginAsset = nullptr;
	for (const UpdateReleaseInfo &Release : Releases)
	{
		if (Release.TagName != CurrentTag) continue;

		for (const UpdateAssetInfo &Asset : Release.Assets)
		{
			if (Asset.Name.find(AssetTag) == std::string::npos) continue;
			PluginAsset = &Asset;
			break;
		}
		break;
	}

	if (PluginAsset == nullptr)
	{
		P->FailureReason = "No CUDA plugin package for " + CurrentTag +
			" (update the app first, or grab it from the releases page).";
		P->Failed.store(true);
		P->IsRunning.store(false);
		return;
	}

	std::string DestPath = platform_join_path(platform_get_temp_dir(), PluginAsset->Name);

	P->Stage.store(CUDA_PLUGIN_STAGE_DOWNLOAD);
	P->TotalBytes.store(PluginAsset->Size);
	platform_cuda_plugin_download_run(AppState, PluginAsset->Url, DestPath);

	if (!P->Succeeded.load())
	{
		if (!P->CancelRequested.load())
		{
			P->FailureReason = "Downloading the CUDA plugin failed.";
			P->Failed.store(true);
		}
		P->IsRunning.store(false);
		return;
	}

	P->Stage.store(CUDA_PLUGIN_STAGE_EXTRACT);
	if (!platform_extract_archive(DestPath, platform_get_binary_dir()))
	{
		remove(DestPath.c_str());
		P->Succeeded.store(false);
		P->FailureReason = "Could not install the plugin files next to the app.";
		P->Failed.store(true);
		P->IsRunning.store(false);
		return;
	}

	remove(DestPath.c_str());
	P->IsRunning.store(false);
}

inline bool
start_cuda_plugin_download(GlobalState *AppState)
{
	CudaPluginState *P = &AppState->Ui.CudaPlugin;
	if (P->IsRunning.load()) return false;
	if (P->Thread.joinable()) P->Thread.join();

	P->IsRunning.store(true);
	P->CancelRequested.store(false);
	P->Succeeded.store(false);
	P->Failed.store(false);
	P->DownloadedBytes.store(0);
	P->TotalBytes.store(0);
	P->ChildPid.store(0);
	P->FailureReason.clear();
	P->JustFinished = false;

	P->Thread = std::thread(cuda_plugin_download_thread, AppState);
	return true;
}

inline void
cancel_cuda_plugin_download(GlobalState *AppState)
{
	CudaPluginState *P = &AppState->Ui.CudaPlugin;
	if (!P->IsRunning.load()) return;
	P->CancelRequested.store(true);
	platform_cancel_cuda_plugin_download(AppState);
}

inline void
poll_cuda_plugin_download(GlobalState *AppState)
{
	CudaPluginState *P = &AppState->Ui.CudaPlugin;
	if (P->IsRunning.load()) return;
	if (!P->Thread.joinable()) return;
	if (P->JustFinished) return;

	P->Thread.join();
	P->JustFinished = true;
	P->Stage.store(CUDA_PLUGIN_STAGE_IDLE);

	if (!P->Succeeded.load()) return;

	// The plugin landed next to the exe; re-run the (cheap, one-shot) device
	// probe so the combo picks the freshly loadable ggml-cuda up. The finished
	// startup probe thread must be joined before its slot is reused.
	if (AppState->InferenceDevicesThread.joinable()) AppState->InferenceDevicesThread.join();
	AppState->InferenceDevicesLoaded.store(false);
	AppState->InferenceDevicesLoading.store(false);
	refresh_inference_devices(AppState);
	show_success_toast(AppState, "CUDA GPU support installed");
}

inline void
shutdown_cuda_plugin_download(GlobalState *AppState)
{
	cancel_cuda_plugin_download(AppState);
	if (AppState->Ui.CudaPlugin.Thread.joinable()) AppState->Ui.CudaPlugin.Thread.join();
}
