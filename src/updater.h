#pragma once

#include "host_services.h"
#include "state.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
	#include <windows.h>
	#include <winhttp.h>
	#pragma comment(lib, "winhttp.lib")
#else
	#include <csignal>
	#include <sys/stat.h>
	#include <sys/wait.h>
	#include <unistd.h>
#endif

#ifndef VOICETYPER_VERSION_FULL
	#define VOICETYPER_VERSION_FULL "0.0.0-unknown"
#endif

#define UPDATER_GITHUB_REPO     "IntelligentSandbox/VoiceTyper"
#define UPDATER_API_LATEST_URL  "https://api.github.com/repos/" UPDATER_GITHUB_REPO "/releases/latest"
#define UPDATER_RELEASES_URL    "https://github.com/" UPDATER_GITHUB_REPO "/releases/latest"

struct UpdaterVersion
{
	int Major;
	int Minor;
	int Patch;
};

static bool
updater_parse_version(const std::string &Text, UpdaterVersion *Out)
{
	const char *Cursor = Text.c_str();
	while (*Cursor == 'v' || *Cursor == 'V' || *Cursor == ' ')
	{
		Cursor++;
	}

	int Numbers[3] = {0, 0, 0};

	for (int i = 0; i < 3; i++)
	{
		char *End = nullptr;
		long Value = strtol(Cursor, &End, 10);
		if (End == Cursor)
		{
			return false;
		}
		Numbers[i] = (int)Value;
		Cursor = End;
		if (*Cursor != '.')
		{
			if (i < 2)
			{
				return false;
			}
			break;
		}
		Cursor++;
	}

	Out->Major = Numbers[0];
	Out->Minor = Numbers[1];
	Out->Patch = Numbers[2];
	return true;
}

static bool
updater_version_is_newer(const UpdaterVersion &Candidate, const UpdaterVersion &Current)
{
	if (Candidate.Major != Current.Major) return Candidate.Major > Current.Major;
	if (Candidate.Minor != Current.Minor) return Candidate.Minor > Current.Minor;
	return Candidate.Patch > Current.Patch;
}

static std::string
updater_current_version_base()
{
	std::string Version = VOICETYPER_VERSION_FULL;
	size_t Dash = Version.find('-');
	if (Dash != std::string::npos) Version.resize(Dash);
	return Version;
}

static size_t
updater_skip_ws(const std::string &Json, size_t Pos)
{
	while (Pos < Json.size() && (Json[Pos] == ' ' || Json[Pos] == '\t' || Json[Pos] == '\n' || Json[Pos] == '\r'))
	{
		Pos++;
	}
	return Pos;
}

static bool
updater_json_read_string(const std::string &Json, size_t *Pos, std::string *Out)
{
	if (*Pos >= Json.size() || Json[*Pos] != '"')
	{
		return false;
	}
	(*Pos)++;

	Out->clear();
	while (*Pos < Json.size())
	{
		char Ch = Json[*Pos];
		if (Ch == '\\')
		{
			(*Pos)++;
			if (*Pos >= Json.size())
			{
				return false;
			}
			char Esc = Json[*Pos];
			(*Pos)++;
			if (Esc == 'n')
			{
				Out->push_back('\n');
			}
			else if (Esc == 't')
			{
				Out->push_back('\t');
			}
			else
			{
				Out->push_back(Esc);
			}
			continue;
		}

		(*Pos)++;
		if (Ch == '"') return true;
		Out->push_back(Ch);
	}

	return false;
}

static bool
updater_json_find_key(const std::string &Json, size_t *Pos, size_t Limit, const char *Key)
{
	std::string Needle = "\"";
	Needle += Key;
	Needle += "\"";

	size_t At = Json.find(Needle, *Pos);
	if (At == std::string::npos || At >= Limit) return false;

	At = updater_skip_ws(Json, At + Needle.size());
	if (At >= Json.size() || Json[At] != ':') return false;

	*Pos = updater_skip_ws(Json, At + 1);
	return true;
}

static bool
updater_parse_release_json(const std::string &Body, std::string *TagName, std::string *HtmlUrl,
	std::vector<UpdateAssetInfo> *Assets)
{
	size_t Pos = 0;
	if (!updater_json_find_key(Body, &Pos, Body.size(), "tag_name")) return false;
	if (!updater_json_read_string(Body, &Pos, TagName)) return false;

	Pos = 0;
	if (!updater_json_find_key(Body, &Pos, Body.size(), "html_url")) return false;
	if (!updater_json_read_string(Body, &Pos, HtmlUrl)) return false;

	Pos = 0;
	if (!updater_json_find_key(Body, &Pos, Body.size(), "assets")) return false;
	Pos = updater_skip_ws(Body, Pos);
	if (Pos >= Body.size() || Body[Pos] != '[') return false;
	Pos++;

	for (;;)
	{
		Pos = updater_skip_ws(Body, Pos);
		if (Pos >= Body.size()) return false;
		if (Body[Pos] == ']') break;
		if (Body[Pos] == ',')
		{
			Pos++;
			continue;
		}
		if (Body[Pos] != '{') return false;

		size_t ObjectStart = Pos;
		int Depth = 0;
		size_t ObjectEnd = Pos;
		while (ObjectEnd < Body.size())
		{
			if (Body[ObjectEnd] == '{')
			{
				Depth++;
			}
			else if (Body[ObjectEnd] == '}')
			{
				Depth--;
				if (Depth == 0) break;
			}
			ObjectEnd++;
		}
		if (ObjectEnd >= Body.size() || Depth != 0) return false;

		UpdateAssetInfo Asset;
		size_t Field = ObjectStart;
		if (updater_json_find_key(Body, &Field, ObjectEnd, "name"))
		{
			updater_json_read_string(Body, &Field, &Asset.Name);
		}

		Field = ObjectStart;
		if (updater_json_find_key(Body, &Field, ObjectEnd, "browser_download_url"))
		{
			updater_json_read_string(Body, &Field, &Asset.Url);
		}

		Field = ObjectStart;
		if (updater_json_find_key(Body, &Field, ObjectEnd, "size"))
		{
			Asset.Size = (int64_t)strtoll(Body.c_str() + Field, nullptr, 10);
		}

		if (!Asset.Name.empty() && !Asset.Url.empty())
		{
			Assets->push_back(Asset);
		}

		Pos = ObjectEnd + 1;
	}

	return true;
}

#ifdef _WIN32
static void
updater_winhttp_close(HINTERNET Request, HINTERNET Connect, HINTERNET Session)
{
	if (Request) WinHttpCloseHandle(Request);
	if (Connect) WinHttpCloseHandle(Connect);
	if (Session) WinHttpCloseHandle(Session);
}

static bool
updater_winhttp_get(const std::string &Url, FILE *File, std::string *OutBody,
	std::atomic<int64_t> *Downloaded, std::atomic<int64_t> *Total, std::atomic<bool> *Cancel)
{
	std::wstring WideUrl(Url.begin(), Url.end());
	URL_COMPONENTSW Comp = {};
	Comp.dwStructSize = sizeof(Comp);
	wchar_t HostBuf[256] = {};
	wchar_t PathBuf[2048] = {};
	Comp.lpszHostName = HostBuf;
	Comp.dwHostNameLength = sizeof(HostBuf) / sizeof(wchar_t);
	Comp.lpszUrlPath = PathBuf;
	Comp.dwUrlPathLength = sizeof(PathBuf) / sizeof(wchar_t);

	if (!WinHttpCrackUrl(WideUrl.c_str(), (DWORD)WideUrl.size(), 0, &Comp))
	{
		return false;
	}

	HINTERNET Session = WinHttpOpen(L"VoiceTyper",
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!Session) return false;

	WinHttpSetTimeouts(Session, 30000, 30000, 30000, 5000);

	INTERNET_PORT Port = Comp.nPort ? Comp.nPort : INTERNET_DEFAULT_HTTPS_PORT;
	HINTERNET Connect = WinHttpConnect(Session, Comp.lpszHostName, Port, 0);
	if (!Connect)
	{
		updater_winhttp_close(nullptr, nullptr, Session);
		return false;
	}

	HINTERNET Request = WinHttpOpenRequest(Connect, L"GET", Comp.lpszUrlPath,
		nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!Request)
	{
		updater_winhttp_close(nullptr, Connect, Session);
		return false;
	}

	if (!WinHttpSendRequest(Request,
		WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0,
		WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH, 0) ||
		!WinHttpReceiveResponse(Request, nullptr))
	{
		updater_winhttp_close(Request, Connect, Session);
		return false;
	}

	DWORD StatusCode = 0;
	DWORD StatusCodeSize = sizeof(StatusCode);
	if (!WinHttpQueryHeaders(Request,
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &StatusCode, &StatusCodeSize, WINHTTP_NO_HEADER_INDEX) ||
		StatusCode < 200 || StatusCode >= 300)
	{
		updater_winhttp_close(Request, Connect, Session);
		return false;
	}

	if (Total)
	{
		DWORD ContentLength = 0;
		DWORD ContentLengthSize = sizeof(ContentLength);
		if (WinHttpQueryHeaders(Request,
			WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &ContentLength, &ContentLengthSize, WINHTTP_NO_HEADER_INDEX) &&
			ContentLength > 0)
		{
			Total->store((int64_t)ContentLength);
		}
	}

	const DWORD BufSize = 64 * 1024;
	std::vector<char> Buffer(BufSize);
	int64_t TotalRead = 0;

	for (;;)
	{
		if (Cancel && Cancel->load())
		{
			updater_winhttp_close(Request, Connect, Session);
			return false;
		}

		DWORD BytesRead = 0;
		if (!WinHttpReadData(Request, Buffer.data(), BufSize, &BytesRead))
		{
			updater_winhttp_close(Request, Connect, Session);
			return false;
		}
		if (BytesRead == 0) break;

		if (File)
		{
			if (fwrite(Buffer.data(), 1, BytesRead, File) != BytesRead)
			{
				updater_winhttp_close(Request, Connect, Session);
				return false;
			}
		}
		else if (OutBody)
		{
			OutBody->append(Buffer.data(), BytesRead);
		}

		TotalRead += BytesRead;
		if (Downloaded) Downloaded->store(TotalRead);
	}

	updater_winhttp_close(Request, Connect, Session);
	return TotalRead > 0;
}
#else
static bool
updater_fetch_string_linux(const std::string &Url, std::string *OutBody)
{
	std::string Cmd = "curl -sL --fail --max-time 30 -A VoiceTyper \"" + Url + "\" 2>/dev/null";

	FILE *Pipe = popen(Cmd.c_str(), "r");
	if (!Pipe)
	{
		return false;
	}

	char Buffer[16384];
	size_t Read = 0;
	while ((Read = fread(Buffer, 1, sizeof(Buffer), Pipe)) > 0)
	{
		OutBody->append(Buffer, Read);
	}

	int Status = pclose(Pipe);
	return Status == 0 && !OutBody->empty();
}
#endif

static void
updater_check_thread(GlobalState *AppState)
{
	std::string Body;
	bool Fetched =
#ifdef _WIN32
		updater_winhttp_get(UPDATER_API_LATEST_URL, nullptr, &Body, nullptr, nullptr, nullptr);
#else
		updater_fetch_string_linux(UPDATER_API_LATEST_URL, &Body);
#endif

	std::string TagName;
	std::string HtmlUrl;
	std::vector<UpdateAssetInfo> Assets;
	if (!Fetched || !updater_parse_release_json(Body, &TagName, &HtmlUrl, &Assets))
	{
		AppState->Ui.Update.CheckFailed.store(true);
		AppState->Ui.Update.CheckRunning.store(false);
		return;
	}

#ifdef _WIN32
	const char *PlatformTag = "-x64_win-";
#else
	const char *PlatformTag = "-x86_64-linux-";
#endif

	std::vector<UpdateAssetInfo> Matching;
	for (const UpdateAssetInfo &Asset : Assets)
	{
		if (Asset.Name.find(PlatformTag) == std::string::npos) continue;
		Matching.push_back(Asset);
	}

	UpdaterVersion Latest = {};
	UpdaterVersion Current = {};
	bool HaveLatest = updater_parse_version(TagName, &Latest);
	bool HaveCurrent = updater_parse_version(updater_current_version_base(), &Current);

	AppState->Ui.Update.LatestVersion = TagName;
	AppState->Ui.Update.ReleaseUrl = HtmlUrl.empty() ? UPDATER_RELEASES_URL : HtmlUrl;
	AppState->Ui.Update.Assets = Matching;
	AppState->Ui.Update.IsNewerAvailable =
		HaveLatest && HaveCurrent && updater_version_is_newer(Latest, Current);

	AppState->Ui.Update.CheckSucceeded.store(true);
	AppState->Ui.Update.CheckRunning.store(false);
}

#ifdef _WIN32
static void
updater_download_thread(GlobalState *AppState, std::string Url, std::string DestPath)
{
	UpdateState *U = &AppState->Ui.Update;

	FILE *File = nullptr;
	fopen_s(&File, DestPath.c_str(), "wb");
	if (!File)
	{
		U->DownloadFailed.store(true);
		U->DownloadRunning.store(false);
		return;
	}

	bool Ok = updater_winhttp_get(Url, File, nullptr,
		&U->DownloadedBytes, &U->TotalBytes, &U->DownloadCancelRequested);

	fclose(File);

	if (Ok)
	{
		U->DownloadSucceeded.store(true);
	}
	else
	{
		remove(DestPath.c_str());
		U->DownloadFailed.store(true);
	}

	U->DownloadRunning.store(false);
}
#else
static void
updater_download_thread(GlobalState *AppState, std::string Url, std::string DestPath)
{
	UpdateState *U = &AppState->Ui.Update;

	pid_t Pid = fork();
	if (Pid == 0)
	{
		execlp("curl", "curl", "-L", "-s", "--fail", "-o", DestPath.c_str(), Url.c_str(), (char *)nullptr);
		execlp("wget", "wget", "-q", "-O", DestPath.c_str(), Url.c_str(), (char *)nullptr);
		_exit(127);
	}
	if (Pid < 0)
	{
		U->DownloadFailed.store(true);
		U->DownloadRunning.store(false);
		return;
	}

	U->ChildPid.store((int64_t)Pid);

	int Status = 0;
	bool Canceled = false;

	for (;;)
	{
		pid_t Result = waitpid(Pid, &Status, WNOHANG);
		if (Result == Pid) break;
		if (Result == -1)
		{
			Status = -1;
			break;
		}

		if (U->DownloadCancelRequested.load())
		{
			Canceled = true;
			kill(Pid, SIGTERM);
			bool Reaped = false;
			for (int i = 0; i < 50; i++)
			{
				if (waitpid(Pid, &Status, WNOHANG) == Pid)
				{
					Reaped = true;
					break;
				}
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
			U->DownloadedBytes.store(St.st_size);
		}

		usleep(200000);
	}

	U->ChildPid.store(0);

	bool Success = !Canceled && WIFEXITED(Status) && WEXITSTATUS(Status) == 0;
	if (Success)
	{
		U->DownloadSucceeded.store(true);
	}
	else
	{
		remove(DestPath.c_str());
		U->DownloadFailed.store(true);
	}

	U->DownloadRunning.store(false);
}
#endif

inline bool
start_update_check(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;
	if (U->CheckRunning.load() || U->DownloadRunning.load()) return false;
	if (U->Thread.joinable()) U->Thread.join();

	U->CheckSucceeded.store(false);
	U->CheckFailed.store(false);
	U->CheckJustFinished = false;
	U->IsNewerAvailable = false;
	U->Assets.clear();
	U->LatestVersion.clear();

	U->CheckRunning.store(true);
	U->Thread = std::thread(updater_check_thread, AppState);
	return true;
}

inline bool
start_update_download(GlobalState *AppState, const UpdateAssetInfo &Asset, bool ApplyOnDownload)
{
	UpdateState *U = &AppState->Ui.Update;
	if (U->CheckRunning.load() || U->DownloadRunning.load()) return false;
	if (U->Thread.joinable()) U->Thread.join();

	std::string DestPath = platform_join_path(platform_get_temp_dir(), Asset.Name);

	U->DownloadSucceeded.store(false);
	U->DownloadFailed.store(false);
	U->DownloadCancelRequested.store(false);
	U->DownloadedBytes.store(0);
	U->TotalBytes.store(Asset.Size);
	U->DownloadJustFinished = false;
	U->ApplyOnDownload = ApplyOnDownload;
	U->PendingAsset = Asset;
	U->DownloadDestPath = DestPath;
#ifndef _WIN32
	U->ChildPid.store(0);
#endif

	U->DownloadRunning.store(true);
	U->Thread = std::thread(updater_download_thread, AppState, Asset.Url, DestPath);
	return true;
}

inline void
cancel_update_download(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;
	if (!U->DownloadRunning.load()) return;
	U->DownloadCancelRequested.store(true);
#ifndef _WIN32
	int64_t Pid = U->ChildPid.load();
	if (Pid > 0) kill((pid_t)Pid, SIGTERM);
#endif
}

inline void
poll_update_check(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;
	if (U->CheckRunning.load()) return;
	if (!U->Thread.joinable()) return;
	if (U->CheckJustFinished) return;

	U->Thread.join();
	U->CheckJustFinished = true;
}

inline void
poll_update_download(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;
	if (U->DownloadRunning.load()) return;
	if (!U->Thread.joinable()) return;
	if (U->DownloadJustFinished) return;

	U->Thread.join();
	U->DownloadJustFinished = true;
}

inline void
shutdown_updater(GlobalState *AppState)
{
	cancel_update_download(AppState);
	if (AppState->Ui.Update.Thread.joinable()) AppState->Ui.Update.Thread.join();
}

static bool
updater_write_file(const std::string &Path, const std::string &Content)
{
	FILE *File = nullptr;
#ifdef _WIN32
	fopen_s(&File, Path.c_str(), "wb");
#else
	File = fopen(Path.c_str(), "wb");
#endif
	if (!File)
	{
		return false;
	}

	bool Ok = fwrite(Content.data(), 1, Content.size(), File) == Content.size();
	fclose(File);
	return Ok;
}

static std::string
updater_forward_slashes(std::string Path)
{
	for (char &Ch : Path)
	{
		if (Ch == '\\') Ch = '/';
	}
	return Path;
}

static bool
updater_string_ends_with(const std::string &Text, const char *Suffix)
{
	size_t SuffixLength = strlen(Suffix);
	if (Text.size() < SuffixLength) return false;
	for (size_t i = 0; i < SuffixLength; i++)
	{
		char Ch = Text[Text.size() - SuffixLength + i];
		if (Ch >= 'A' && Ch <= 'Z') Ch = (char)(Ch - 'A' + 'a');
		if (Ch != Suffix[i]) return false;
	}
	return true;
}

#ifdef _WIN32
static bool
updater_apply_portable_zip(const std::string &ZipPath)
{
	std::string ExeDir = updater_forward_slashes(platform_get_exe_dir());
	std::string BatPath = updater_forward_slashes(
		platform_join_path(platform_get_temp_dir(), "voicetyper-apply-update.bat"));
	std::string Zip = updater_forward_slashes(ZipPath);

	char PidStr[32];
	sprintf_s(PidStr, sizeof(PidStr), "%d", platform_get_process_id());

	std::string Bat;
	Bat += "@echo off\r\n";
	Bat += ":waitloop\r\n";
	Bat += std::string("tasklist /fi \"PID eq ") + PidStr + "\" 2>nul | find /i \"" + PidStr + "\" >nul\r\n";
	Bat += "if not errorlevel 1 (\r\n";
	Bat += "ping -n 2 127.0.0.1 >nul\r\n";
	Bat += "goto waitloop\r\n";
	Bat += ")\r\n";
	Bat += "tar -xf \"" + Zip + "\" -C \"" + ExeDir + "\"\r\n";
	Bat += "if errorlevel 1 exit /b 1\r\n";
	Bat += "del \"" + Zip + "\"\r\n";
	Bat += "start \"\" \"" + ExeDir + "/VoiceTyper.exe\"\r\n";
	Bat += "del \"%~f0\"\r\n";

	if (!updater_write_file(BatPath, Bat))
	{
		return false;
	}

	std::string Cmd = "cmd.exe /c \"\"" + BatPath + "\"\"";
	return platform_spawn_detached(Cmd, platform_get_temp_dir(), true);
}

static bool
updater_apply_msi(const std::string &MsiPath)
{
	std::string Cmd = "msiexec /i \"" + MsiPath + "\"";
	return platform_spawn_detached(Cmd, "", false);
}
#else
static bool
updater_apply_portable_tarball(const std::string &TarPath)
{
	std::string ExeDir = platform_get_exe_dir();
	std::string ScriptPath = platform_join_path(platform_get_temp_dir(), "voicetyper-apply-update.sh");

	char PidStr[32];
	snprintf(PidStr, sizeof(PidStr), "%d", platform_get_process_id());

	std::string Script;
	Script += "#!/bin/sh\n";
	Script += std::string("while kill -0 ") + PidStr + " 2>/dev/null; do sleep 1; done\n";
	Script += "tar -xzf \"" + TarPath + "\" -C \"" + ExeDir + "\" --strip-components=1 && rm -f \"" + TarPath + "\"\n";
	Script += "cd \"" + ExeDir + "\" && nohup ./VoiceTyper >/dev/null 2>&1 &\n";
	Script += "rm -f \"$0\"\n";

	if (!updater_write_file(ScriptPath, Script))
	{
		return false;
	}

	std::string Cmd = "sh \"" + ScriptPath + "\"";
	return platform_spawn_detached(Cmd, "", true);
}
#endif

inline bool
updater_apply_downloaded_update(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;
	if (U->DownloadDestPath.empty()) return false;
	if (!U->DownloadSucceeded.load()) return false;

#ifdef _WIN32
	if (updater_string_ends_with(U->DownloadDestPath, ".msi"))
	{
		return updater_apply_msi(U->DownloadDestPath);
	}
	return updater_apply_portable_zip(U->DownloadDestPath);
#else
	return updater_apply_portable_tarball(U->DownloadDestPath);
#endif
}
