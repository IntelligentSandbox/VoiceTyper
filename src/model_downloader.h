#pragma once

#include "host_services.h"
#include "state.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
	#include <windows.h>
	#include <winhttp.h>
	#pragma comment(lib, "winhttp.lib")
#else
	#include <cerrno>
	#include <csignal>
	#include <cstring>
	#include <sys/stat.h>
	#include <sys/wait.h>
	#include <unistd.h>
#endif

#ifdef _WIN32
static void
winhttp_download_thread(GlobalState *AppState, std::string Url, std::string DestPath, int64_t ExpectedSize)
{
	AppState->Ui.Download.DownloadedBytes.store(0);
	AppState->Ui.Download.TotalBytes.store(ExpectedSize);

	std::wstring WideUrl(Url.begin(), Url.end());
	URL_COMPONENTSW Comp = {};
	Comp.dwStructSize = sizeof(Comp);
	wchar_t HostBuf[256] = {};
	wchar_t PathBuf[2048] = {};
	Comp.lpszHostName = HostBuf;
	Comp.dwHostNameLength = sizeof(HostBuf) / sizeof(wchar_t);
	Comp.lpszUrlPath = PathBuf;
	Comp.dwUrlPathLength = sizeof(PathBuf) / sizeof(wchar_t);

	auto Fail = [&]()
	{
		AppState->Ui.Download.Failed.store(true);
		AppState->Ui.Download.IsRunning.store(false);
	};

	if (!WinHttpCrackUrl(WideUrl.c_str(), (DWORD)WideUrl.size(), 0, &Comp)) { Fail(); return; }

	HINTERNET Session = WinHttpOpen(L"VoiceTyper",
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!Session) { Fail(); return; }

	WinHttpSetTimeouts(Session, 30000, 30000, 30000, 5000);

	INTERNET_PORT Port = Comp.nPort ? Comp.nPort : INTERNET_DEFAULT_HTTPS_PORT;
	HINTERNET Connect = WinHttpConnect(Session, Comp.lpszHostName, Port, 0);
	if (!Connect) { WinHttpCloseHandle(Session); Fail(); return; }

	DWORD Flags = WINHTTP_FLAG_SECURE;
	HINTERNET Request = WinHttpOpenRequest(Connect, L"GET", Comp.lpszUrlPath,
		nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, Flags);
	if (!Request) { WinHttpCloseHandle(Connect); WinHttpCloseHandle(Session); Fail(); return; }

	BOOL Ok = WinHttpSendRequest(Request,
		WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0,
		WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH, 0);
	if (!Ok || !WinHttpReceiveResponse(Request, nullptr))
	{
		WinHttpCloseHandle(Request); WinHttpCloseHandle(Connect); WinHttpCloseHandle(Session);
		Fail();
		return;
	}

	DWORD StatusCode = 0;
	DWORD StatusCodeSize = sizeof(StatusCode);
	bool GoodStatus = WinHttpQueryHeaders(Request,
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &StatusCode, &StatusCodeSize, WINHTTP_NO_HEADER_INDEX)
		&& StatusCode >= 200 && StatusCode < 300;

	if (!GoodStatus)
	{
		WinHttpCloseHandle(Request); WinHttpCloseHandle(Connect); WinHttpCloseHandle(Session);
		Fail();
		return;
	}

	DWORD ContentLength = 0;
	DWORD ContentLengthSize = sizeof(ContentLength);
	if (WinHttpQueryHeaders(Request,
		WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &ContentLength, &ContentLengthSize, WINHTTP_NO_HEADER_INDEX)
		&& ContentLength > 0)
	{
		AppState->Ui.Download.TotalBytes.store((int64_t)ContentLength);
	}

	FILE *File = nullptr;
	fopen_s(&File, DestPath.c_str(), "wb");
	if (!File)
	{
		WinHttpCloseHandle(Request); WinHttpCloseHandle(Connect); WinHttpCloseHandle(Session);
		Fail();
		return;
	}

	const DWORD BufSize = 64 * 1024;
	std::vector<char> Buffer(BufSize);
	int64_t Total = 0;
	bool Error = false;

	for (;;)
	{
		if (AppState->Ui.Download.CancelRequested.load()) { Error = true; break; }

		DWORD BytesRead = 0;
		if (!WinHttpReadData(Request, Buffer.data(), BufSize, &BytesRead)) { Error = true; break; }
		if (BytesRead == 0) break;

		if (fwrite(Buffer.data(), 1, BytesRead, File) != BytesRead) { Error = true; break; }

		Total += BytesRead;
		AppState->Ui.Download.DownloadedBytes.store(Total);
	}

	fclose(File);
	WinHttpCloseHandle(Request);
	WinHttpCloseHandle(Connect);
	WinHttpCloseHandle(Session);

	bool Canceled = AppState->Ui.Download.CancelRequested.load();
	if (Error || Canceled || Total == 0)
	{
		remove(DestPath.c_str());
		AppState->Ui.Download.Failed.store(true);
	}
	else
	{
		AppState->Ui.Download.Succeeded.store(true);
	}

	AppState->Ui.Download.IsRunning.store(false);
}
#else
static bool
linux_spawn_download(const std::string &Url, const std::string &DestPath, pid_t *OutPid)
{
	pid_t Pid = fork();
	if (Pid < 0) return false;
	if (Pid == 0)
	{
		execlp("curl", "curl", "-L", "-s", "--fail", "-o", DestPath.c_str(), Url.c_str(), (char *)nullptr);
		execlp("wget", "wget", "-q", "-O", DestPath.c_str(), Url.c_str(), (char *)nullptr);
		_exit(127);
	}
	*OutPid = Pid;
	return true;
}

static void
linux_download_thread(GlobalState *AppState, std::string Url, std::string DestPath, int64_t ExpectedSize)
{
	AppState->Ui.Download.DownloadedBytes.store(0);
	AppState->Ui.Download.TotalBytes.store(ExpectedSize);

	pid_t Pid = -1;
	if (!linux_spawn_download(Url, DestPath, &Pid))
	{
		AppState->Ui.Download.Failed.store(true);
		AppState->Ui.Download.IsRunning.store(false);
		return;
	}

	AppState->Ui.Download.ChildPid.store((int64_t)Pid);

	int Status = 0;
	bool Canceled = false;

	for (;;)
	{
		pid_t Result = waitpid(Pid, &Status, WNOHANG);
		if (Result == Pid) break;
		if (Result == -1) { Status = -1; break; }

		if (AppState->Ui.Download.CancelRequested.load())
		{
			Canceled = true;
			kill(Pid, SIGTERM);
			bool Reaped = false;
			for (int i = 0; i < 50; i++)
			{
				if (waitpid(Pid, &Status, WNOHANG) == Pid) { Reaped = true; break; }
				usleep(10000);
			}
			if (!Reaped)
			{
				kill(Pid, SIGKILL);
				waitpid(Pid, &Status, 0);
			}
			break;
		}

		struct stat St;
		if (stat(DestPath.c_str(), &St) == 0 && S_ISREG(St.st_mode))
		{
			AppState->Ui.Download.DownloadedBytes.store(St.st_size);
		}

		usleep(200000);
	}

	AppState->Ui.Download.ChildPid.store(0);

	bool Success = !Canceled && WIFEXITED(Status) && WEXITSTATUS(Status) == 0;
	if (!Success)
	{
		remove(DestPath.c_str());
		AppState->Ui.Download.Failed.store(true);
	}
	else
	{
		struct stat St;
		if (stat(DestPath.c_str(), &St) == 0) AppState->Ui.Download.DownloadedBytes.store(St.st_size);
		AppState->Ui.Download.Succeeded.store(true);
	}

	AppState->Ui.Download.IsRunning.store(false);
}
#endif

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
	AppState->Ui.Download.JustFinished = false;
	AppState->Ui.Download.CurrentModelName = ModelName;
#ifndef _WIN32
	AppState->Ui.Download.ChildPid.store(0);
#endif

	AppState->Ui.Download.Thread = std::thread(
#ifdef _WIN32
		winhttp_download_thread,
#else
		linux_download_thread,
#endif
		AppState, Url, DestPath, ExpectedSize);

	return true;
}

inline void
cancel_model_download(GlobalState *AppState)
{
	if (!AppState->Ui.Download.IsRunning.load()) return;
	AppState->Ui.Download.CancelRequested.store(true);
#ifndef _WIN32
	int64_t Pid = AppState->Ui.Download.ChildPid.load();
	if (Pid > 0) kill((pid_t)Pid, SIGTERM);
#endif
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
