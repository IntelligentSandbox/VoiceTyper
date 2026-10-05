#pragma once

#include "build_time_constants.h"
#include "host_services.h"
#include "state.h"

#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

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
	std::string Version = VERSION_FULL;
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
			else if (Esc == 'r')
			{
				Out->push_back('\r');
			}
			else if (Esc == 'b')
			{
				Out->push_back('\b');
			}
			else if (Esc == 'f')
			{
				Out->push_back('\f');
			}
			else if (Esc == 'u' && *Pos + 4 <= Json.size())
			{
				unsigned int Code = 0;
				for (int i = 0; i < 4; i++)
				{
					char Hex = Json[*Pos + i];
					unsigned int Digit;
					if (Hex >= '0' && Hex <= '9') Digit = (unsigned int)(Hex - '0');
					else if (Hex >= 'a' && Hex <= 'f') Digit = (unsigned int)(Hex - 'a' + 10);
					else if (Hex >= 'A' && Hex <= 'F') Digit = (unsigned int)(Hex - 'A' + 10);
					else Digit = 0xFFFFFFFFu;
					Code = (Code << 4) | Digit;
				}

				if (Code == 0xFFFFFFFFu)
				{
					Out->push_back(Esc);
				}
				else
				{
					*Pos += 4;
					if (Code < 0x80)
					{
						Out->push_back((char)Code);
					}
					else if (Code < 0x800)
					{
						Out->push_back((char)(0xC0 | (Code >> 6)));
						Out->push_back((char)(0x80 | (Code & 0x3F)));
					}
					else
					{
						Out->push_back((char)(0xE0 | (Code >> 12)));
						Out->push_back((char)(0x80 | ((Code >> 6) & 0x3F)));
						Out->push_back((char)(0x80 | (Code & 0x3F)));
					}
				}
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
updater_json_read_bool(const std::string &Json, size_t *Pos, bool *Out)
{
	if (Json.compare(*Pos, 4, "true") == 0)
	{
		*Out = true;
		*Pos += 4;
		return true;
	}
	if (Json.compare(*Pos, 5, "false") == 0)
	{
		*Out = false;
		*Pos += 5;
		return true;
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

static size_t
updater_json_object_end(const std::string &Json, size_t Pos)
{
	int Depth = 0;
	bool InString = false;
	for (size_t At = Pos; At < Json.size(); At++)
	{
		char Ch = Json[At];
		if (InString)
		{
			if (Ch == '\\') At++;
			else if (Ch == '"') InString = false;
			continue;
		}

		if (Ch == '"') InString = true;
		else if (Ch == '{') Depth++;
		else if (Ch == '}')
		{
			Depth--;
			if (Depth == 0) return At;
		}
	}
	return std::string::npos;
}

struct UpdateReleaseInfo
{
	std::string TagName;
	std::string HtmlUrl;
	std::string Body;
	bool IsDraft;
	bool IsPrerelease;
	std::vector<UpdateAssetInfo> Assets;
};

static bool
updater_parse_releases_json(const std::string &Body, std::vector<UpdateReleaseInfo> *Out)
{
	size_t Pos = updater_skip_ws(Body, 0);
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

		size_t ObjectEnd = updater_json_object_end(Body, Pos);
		if (ObjectEnd == std::string::npos) return false;

		UpdateReleaseInfo Release;
		size_t Field = Pos;
		if (updater_json_find_key(Body, &Field, ObjectEnd, "tag_name"))
		{
			updater_json_read_string(Body, &Field, &Release.TagName);
		}

		Field = Pos;
		if (updater_json_find_key(Body, &Field, ObjectEnd, "html_url"))
		{
			updater_json_read_string(Body, &Field, &Release.HtmlUrl);
		}

		Field = Pos;
		if (updater_json_find_key(Body, &Field, ObjectEnd, "body"))
		{
			updater_json_read_string(Body, &Field, &Release.Body);
		}

		Field = Pos;
		if (updater_json_find_key(Body, &Field, ObjectEnd, "draft"))
		{
			updater_json_read_bool(Body, &Field, &Release.IsDraft);
		}

		Field = Pos;
		if (updater_json_find_key(Body, &Field, ObjectEnd, "prerelease"))
		{
			updater_json_read_bool(Body, &Field, &Release.IsPrerelease);
		}

		Field = Pos;
		if (updater_json_find_key(Body, &Field, ObjectEnd, "assets"))
		{
			size_t AssetPos = updater_skip_ws(Body, Field);
			if (AssetPos < Body.size() && Body[AssetPos] == '[')
			{
				AssetPos++;
				for (;;)
				{
					AssetPos = updater_skip_ws(Body, AssetPos);
					if (AssetPos >= Body.size()) return false;
					if (Body[AssetPos] == ']') break;
					if (Body[AssetPos] == ',')
					{
						AssetPos++;
						continue;
					}
					if (Body[AssetPos] != '{') return false;

					size_t AssetObjectEnd = updater_json_object_end(Body, AssetPos);
					if (AssetObjectEnd == std::string::npos) return false;

					UpdateAssetInfo Asset;
					size_t AssetField = AssetPos;
					if (updater_json_find_key(Body, &AssetField, AssetObjectEnd, "name"))
					{
						updater_json_read_string(Body, &AssetField, &Asset.Name);
					}

					AssetField = AssetPos;
					if (updater_json_find_key(Body, &AssetField, AssetObjectEnd, "browser_download_url"))
					{
						updater_json_read_string(Body, &AssetField, &Asset.Url);
					}

					AssetField = AssetPos;
					if (updater_json_find_key(Body, &AssetField, AssetObjectEnd, "size"))
					{
						Asset.Size = (int64_t)strtoll(Body.c_str() + AssetField, nullptr, 10);
					}

					if (!Asset.Name.empty() && !Asset.Url.empty())
					{
						Release.Assets.push_back(Asset);
					}

					AssetPos = AssetObjectEnd + 1;
				}
			}
		}

		if (!Release.TagName.empty())
		{
			Out->push_back(std::move(Release));
		}

		Pos = ObjectEnd + 1;
	}

	return !Out->empty();
}

static bool
updater_is_hex_digit(char Ch)
{
	return (Ch >= '0' && Ch <= '9') || (Ch >= 'a' && Ch <= 'f') || (Ch >= 'A' && Ch <= 'F');
}

static void
updater_strip_trailing_commit_hash(std::string *Line)
{
	if (Line->size() < 8 || (*Line)[Line->size() - 1] != ')') return;

	size_t HashEnd = Line->size() - 2;
	size_t HashStart = HashEnd + 1;
	for (size_t At = HashEnd + 1; At-- > 1; )
	{
		char Ch = (*Line)[At];
		if (updater_is_hex_digit(Ch))
		{
			HashStart = At;
			continue;
		}

		size_t HashLength = HashEnd - HashStart + 1;
		if (Ch == '(' && (*Line)[At - 1] == ' ' && HashLength >= 4 && HashLength <= 40)
		{
			Line->resize(At - 1);
		}
		return;
	}
}

static std::string
updater_clean_release_notes(const std::string &Body)
{
	std::string Cleaned;
	size_t Pos = 0;
	while (Pos <= Body.size())
	{
		size_t Nl = Body.find('\n', Pos);
		size_t LineEnd = (Nl == std::string::npos) ? Body.size() : Nl;
		std::string Line = Body.substr(Pos, LineEnd - Pos);
		if (!Line.empty() && Line.back() == '\r') Line.pop_back();

		bool IsChangesSinceHeader = Line.compare(0, 13, "Changes since") == 0 && Line.back() == ':';
		if (!IsChangesSinceHeader && !Line.empty())
		{
			updater_strip_trailing_commit_hash(&Line);
			if (!Cleaned.empty()) Cleaned.push_back('\n');
			Cleaned += Line;
		}

		if (Nl == std::string::npos) break;
		Pos = Nl + 1;
	}

	return Cleaned;
}

static void
updater_check_thread(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;

	std::string Body;
	bool Fetched = platform_http_get_string(UPDATER_API_RELEASES_URL, &Body);

	std::vector<UpdateReleaseInfo> Releases;
	if (!Fetched || !updater_parse_releases_json(Body, &Releases))
	{
		U->StagingCheckSucceeded = false;
		U->CheckRunning.store(false);
		return;
	}

	const char *PlatformTag = platform_update_asset_tag();

	const UpdateReleaseInfo *Latest = nullptr;
	for (const UpdateReleaseInfo &Release : Releases)
	{
		if (Release.IsDraft || Release.IsPrerelease) continue;
		Latest = &Release;
		break;
	}

	std::vector<UpdateAssetInfo> Matching;
	if (Latest)
	{
		for (const UpdateAssetInfo &Asset : Latest->Assets)
		{
			// The modular CUDA plugin package is an in-place GPU upgrade, not an
			// app update; it is downloaded from its own flow instead.
			if (Asset.Name.find(CUDA_PLUGIN_ASSET_MARKER) != std::string::npos) continue;
			if (Asset.Name.find(PlatformTag) == std::string::npos) continue;
			Matching.push_back(Asset);
		}
	}

	UpdaterVersion LatestVersion = {};
	UpdaterVersion Current = {};
	bool HaveLatest = Latest && updater_parse_version(Latest->TagName, &LatestVersion);
	bool HaveCurrent = updater_parse_version(updater_current_version_base(), &Current);

	std::vector<UpdateChangelogEntry> Newer;
	bool IsNewerAvailable = HaveLatest && HaveCurrent && updater_version_is_newer(LatestVersion, Current);
	if (IsNewerAvailable)
	{
		for (const UpdateReleaseInfo &Release : Releases)
		{
			if (Release.IsDraft || Release.IsPrerelease) continue;

			UpdaterVersion Version = {};
			if (!updater_parse_version(Release.TagName, &Version)) continue;
			if (!updater_version_is_newer(Version, Current)) break;

			std::string Notes = updater_clean_release_notes(Release.Body);
			if (Notes.empty()) continue;

			UpdateChangelogEntry Entry;
			Entry.Version = Release.TagName;
			Entry.Notes = std::move(Notes);
			Newer.push_back(std::move(Entry));
		}
	}

	U->StagingLatestVersion = Latest ? Latest->TagName : "";
	U->StagingReleaseUrl = (!Latest || Latest->HtmlUrl.empty()) ? UPDATER_RELEASES_URL : Latest->HtmlUrl;
	U->StagingAssets = std::move(Matching);
	U->StagingNewerReleases = std::move(Newer);
	U->StagingIsNewerAvailable = IsNewerAvailable;
	U->StagingCheckSucceeded = true;

	U->CheckRunning.store(false);
}


inline void
updater_publish_finished_check(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;
	if (U->StagingCheckSucceeded)
	{
		U->LatestVersion = std::move(U->StagingLatestVersion);
		U->ReleaseUrl = std::move(U->StagingReleaseUrl);
		U->Assets = std::move(U->StagingAssets);
		U->NewerReleases = std::move(U->StagingNewerReleases);
		U->IsNewerAvailable = U->StagingIsNewerAvailable;
		U->CheckFailed.store(false);
		U->CheckSucceeded.store(true);
	}
	else
	{
		U->CheckSucceeded.store(false);
		U->CheckFailed.store(true);
	}
}

inline bool
start_update_check(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;
	if (U->CheckRunning.load() || U->DownloadRunning.load()) return false;
	if (U->Thread.joinable())
	{
		bool WasUnpublishedCheck = U->ThreadIsCheck && !U->CheckJustFinished;
		U->Thread.join();
		if (WasUnpublishedCheck) updater_publish_finished_check(AppState);
	}

	U->CheckJustFinished = false;
	U->ThreadIsCheck = true;

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
	U->ThreadIsCheck = false;
	U->ChildPid.store(0);

	U->DownloadRunning.store(true);
	U->Thread = std::thread(platform_update_download_thread, AppState, Asset.Url, DestPath);
	return true;
}

inline void
cancel_update_download(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;
	if (!U->DownloadRunning.load()) return;
	U->DownloadCancelRequested.store(true);
	platform_cancel_update_download(AppState);
}

inline void
poll_update_check(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;
	if (U->CheckRunning.load()) return;
	if (!U->Thread.joinable()) return;
	if (!U->ThreadIsCheck) return;
	if (U->CheckJustFinished) return;

	U->Thread.join();
	updater_publish_finished_check(AppState);
	U->CheckJustFinished = true;
}

inline void
poll_update_download(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;
	if (U->DownloadRunning.load()) return;
	if (!U->Thread.joinable()) return;
	if (U->ThreadIsCheck) return;
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


inline bool
updater_apply_downloaded_update(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;
	if (U->DownloadDestPath.empty()) return false;
	if (!U->DownloadSucceeded.load()) return false;

	return platform_apply_update_package(U->DownloadDestPath);
}
