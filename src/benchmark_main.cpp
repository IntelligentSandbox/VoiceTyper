#define NOMINMAX

#include "host_services.h"
#include "perf.h"
#include "state.h"
#include "transcription_core.h"
#include "whisper_wrapper.h"
#include "stream_chunker.h"
#include "audio_pipeline.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

struct BenchOptions
{
	std::string ModelPath = "stt_models/ggml-base.en.bin";
	std::string AudioPath;
	std::string ExpectedText;
	bool HasExpectedText = false;
	std::string Mode = "record";
	bool EnableVad = false;
	std::string VadModelPath = "vad_models/ggml-silero-v5.1.2.bin";
	std::string Device = "cpu";
	int AudioDeviceIndex = -1;
	int WarmupCount = 1;
	int IterationCount = 5;
	int ThreadCount = 1;
	int BeamSize = 1;
	std::string LogMode = "off";
	std::string NoisePath;
	bool HasNoise = false;
	double SnrDb = 10.0;
	bool HasSnr = false;
	bool HasTakeMs = false;
	int SoakTakeMs = 0;
	bool SoakFast = false;
};

struct BenchPerfGuard
{
	~BenchPerfGuard()
	{
		perf_finish();
	}
};

static FILE     *g_BenchLogFile  = nullptr;
static std::mutex g_BenchLogMutex;
static bool       g_BenchVerbose  = false;

static void
bench_log_callback(ggml_log_level Level, const char *Message, void *)
{
	if (!g_BenchLogFile) return;
	if (!g_BenchVerbose && Level < GGML_LOG_LEVEL_WARN) return;
	if (!Message || Message[0] == '\0') return;

	std::lock_guard<std::mutex> Lock(g_BenchLogMutex);
	fputs(Message, g_BenchLogFile);
	if (Level >= GGML_LOG_LEVEL_WARN) fflush(g_BenchLogFile);
}

static void
bench_log_nop(ggml_log_level, const char *, void *)
{
}

static void
setup_bench_logging(const BenchOptions &Options)
{
	if (Options.LogMode == "off")
	{
		whisper_log_set(bench_log_nop, nullptr);
		ggml_log_set(bench_log_nop, nullptr);
		return;
	}

	if (Options.LogMode == "verbose") g_BenchVerbose = true;

	std::string LogPath = platform_join_path(platform_get_binary_dir(), "bench.log");
	g_BenchLogFile = fopen(LogPath.c_str(), "w");

	whisper_log_set(bench_log_callback, nullptr);
	ggml_log_set(bench_log_callback, nullptr);
}

static void
shutdown_bench_logging()
{
	if (g_BenchLogFile)
	{
		std::lock_guard<std::mutex> Lock(g_BenchLogMutex);
		fflush(g_BenchLogFile);
		fclose(g_BenchLogFile);
		g_BenchLogFile = nullptr;
	}
}

static void
print_usage(const char *ExeName)
{
	std::cerr << "Usage: " << ExeName
		<< " --audio <path> [--model <path>] [--expected-text <text>]"
		<< " [--mode <record|streaming|capture-latency|leak-soak>] [--vad <on|off>] [--vad-model <path>]"
		<< " [--device <cpu|gpu>] [--audio-device <index>] [--warmup <count>] [--iterations <count>]"
		<< " [--threads <count>] [--log <off|file|verbose>]"
		<< " [--beam <1-16>] [--noise <wav>] [--snr <db>]"
		<< " [--take-ms <ms>] [--soak-fast]\n";
}

static bool
parse_int_arg(const char *Value, int MinValue, int *OutValue)
{
	char *End = nullptr;
	long Parsed = std::strtol(Value, &End, 10);
	if (End == Value || *End != '\0' || Parsed < MinValue || Parsed > INT32_MAX) return false;
	*OutValue = (int)Parsed;
	return true;
}

static bool
parse_double_arg(const char *Value, double *OutValue)
{
	char *End = nullptr;
	double Parsed = std::strtod(Value, &End);
	if (End == Value || *End != '\0') return false;
	*OutValue = Parsed;
	return true;
}

static bool
parse_options(int ArgCount, char **Args, BenchOptions *Options)
{
	unsigned int HardwareThreads = std::thread::hardware_concurrency();
	Options->ThreadCount = HardwareThreads > 0 ? (int)HardwareThreads : 1;

	for (int i = 1; i < ArgCount; i++)
	{
		std::string Arg = Args[i];
		auto require_value = [&](const char *Name) -> const char * {
			if (i + 1 >= ArgCount)
			{
				std::cerr << Name << " requires a value\n";
				return nullptr;
			}

			return Args[++i];
		};

		if (Arg == "--model")
		{
			const char *Value = require_value("--model");
			if (!Value) return false;
			Options->ModelPath = Value;
		}
		else if (Arg == "--audio")
		{
			const char *Value = require_value("--audio");
			if (!Value) return false;
			Options->AudioPath = Value;
		}
		else if (Arg == "--expected-text")
		{
			const char *Value = require_value("--expected-text");
			if (!Value) return false;
			Options->ExpectedText = Value;
			Options->HasExpectedText = true;
		}
		else if (Arg == "--noise")
		{
			const char *Value = require_value("--noise");
			if (!Value) return false;
			Options->NoisePath = Value;
			Options->HasNoise = true;
		}
		else if (Arg == "--snr")
		{
			const char *Value = require_value("--snr");
			if (!Value || !parse_double_arg(Value, &Options->SnrDb)) return false;
			Options->HasSnr = true;
		}
		else if (Arg == "--warmup")
		{
			const char *Value = require_value("--warmup");
			if (!Value || !parse_int_arg(Value, 0, &Options->WarmupCount)) return false;
		}
		else if (Arg == "--iterations")
		{
			const char *Value = require_value("--iterations");
			if (!Value || !parse_int_arg(Value, 1, &Options->IterationCount)) return false;
		}
		else if (Arg == "--threads")
		{
			const char *Value = require_value("--threads");
			if (!Value || !parse_int_arg(Value, 1, &Options->ThreadCount)) return false;
		}
		else if (Arg == "--beam")
		{
			const char *Value = require_value("--beam");
			if (!Value || !parse_int_arg(Value, 1, &Options->BeamSize)) return false;
		}
		else if (Arg == "--mode")
		{
			const char *Value = require_value("--mode");
			if (!Value) return false;
			if (std::string(Value) != "record" && std::string(Value) != "streaming" &&
				std::string(Value) != "capture-latency" && std::string(Value) != "leak-soak")
			{
				std::cerr << "--mode must be 'record', 'streaming', 'capture-latency' or 'leak-soak'\n";
				return false;
			}
			Options->Mode = Value;
		}
		else if (Arg == "--take-ms")
		{
			const char *Value = require_value("--take-ms");
			if (!Value || !parse_int_arg(Value, 100, &Options->SoakTakeMs)) return false;
			Options->HasTakeMs = true;
		}
		else if (Arg == "--soak-fast")
		{
			Options->SoakFast = true;
		}
		else if (Arg == "--audio-device")
		{
			const char *Value = require_value("--audio-device");
			if (!Value || !parse_int_arg(Value, 0, &Options->AudioDeviceIndex)) return false;
		}
		else if (Arg == "--vad")
		{
			const char *Value = require_value("--vad");
			if (!Value) return false;
			std::string VadStr = Value;
			if (VadStr == "on" || VadStr == "true" || VadStr == "1")
			{
				Options->EnableVad = true;
			}
			else if (VadStr == "off" || VadStr == "false" || VadStr == "0")
			{
				Options->EnableVad = false;
			}
			else
			{
				std::cerr << "--vad must be 'on' or 'off'\n";
				return false;
			}
		}
		else if (Arg == "--vad-model")
		{
			const char *Value = require_value("--vad-model");
			if (!Value) return false;
			Options->VadModelPath = Value;
		}
		else if (Arg == "--device")
		{
			const char *Value = require_value("--device");
			if (!Value) return false;
			std::string DevStr = Value;
			if (DevStr != "cpu" && DevStr != "gpu")
			{
				std::cerr << "--device must be 'cpu' or 'gpu'\n";
				return false;
			}
			Options->Device = DevStr;
		}
		else if (Arg == "--log")
		{
			const char *Value = require_value("--log");
			if (!Value) return false;
			std::string LogStr = Value;
			if (LogStr != "off" && LogStr != "file" && LogStr != "verbose")
			{
				std::cerr << "--log must be 'off', 'file', or 'verbose'\n";
				return false;
			}
			Options->LogMode = LogStr;
		}
		else
		{
			std::cerr << "Unknown argument: " << Arg << "\n";
			return false;
		}
	}

	if (Options->AudioPath.empty() && Options->Mode != "capture-latency")
	{
		std::cerr << "--audio is required\n";
		return false;
	}

	if (!Options->HasNoise && Options->HasSnr)
	{
		std::cerr << "--snr requires --noise\n";
		return false;
	}

	return true;
}

static bool
read_file_bytes(const std::string &Path, std::vector<uint8_t> *OutBytes, std::string *Error)
{
	std::ifstream File(Path, std::ios::binary | std::ios::ate);
	if (!File)
	{
		*Error = "failed to open WAV file: " + Path;
		return false;
	}

	std::streamsize Size = File.tellg();
	if (Size < 0)
	{
		*Error = "failed to determine WAV file size";
		return false;
	}

	File.seekg(0, std::ios::beg);
	OutBytes->resize((size_t)Size);
	if (Size > 0 && !File.read((char *)OutBytes->data(), Size))
	{
		*Error = "failed to read WAV file";
		return false;
	}

	return true;
}

static uint16_t
read_le_u16(const uint8_t *Data)
{
	return (uint16_t)(Data[0] | (Data[1] << 8));
}

static uint32_t
read_le_u32(const uint8_t *Data)
{
	return (uint32_t)Data[0] | ((uint32_t)Data[1] << 8) | ((uint32_t)Data[2] << 16) |
		((uint32_t)Data[3] << 24);
}

static bool
chunk_id_equals(const uint8_t *Data, const char *Id)
{
	return std::memcmp(Data, Id, 4) == 0;
}

static bool
load_wav_mono_16khz(const std::string &Path, std::vector<float> *OutSamples, std::string *Error)
{
	std::vector<uint8_t> Bytes;
	if (!read_file_bytes(Path, &Bytes, Error)) return false;

	if (Bytes.size() < 12 || !chunk_id_equals(Bytes.data(), "RIFF") || !chunk_id_equals(Bytes.data() + 8, "WAVE"))
	{
		*Error = "unsupported WAV file: expected RIFF/WAVE";
		return false;
	}

	bool FoundFmt = false;
	bool FoundData = false;
	uint16_t FormatTag = 0;
	uint16_t ChannelCount = 0;
	uint32_t SampleRate = 0;
	uint16_t BitsPerSample = 0;
	uint16_t BlockAlign = 0;
	const uint8_t *DataBytes = nullptr;
	uint32_t DataByteCount = 0;
	size_t Offset = 12;

	while (Offset + 8 <= Bytes.size())
	{
		const uint8_t *Chunk = Bytes.data() + Offset;
		uint32_t ChunkSize = read_le_u32(Chunk + 4);
		size_t ChunkDataOffset = Offset + 8;
		if (ChunkDataOffset + ChunkSize > Bytes.size())
		{
			*Error = "invalid WAV file: chunk extends beyond file size";
			return false;
		}

		if (chunk_id_equals(Chunk, "fmt "))
		{
			if (ChunkSize < 16)
			{
				*Error = "invalid WAV file: fmt chunk is too small";
				return false;
			}

			const uint8_t *Fmt = Bytes.data() + ChunkDataOffset;
			FormatTag = read_le_u16(Fmt);
			ChannelCount = read_le_u16(Fmt + 2);
			SampleRate = read_le_u32(Fmt + 4);
			BlockAlign = read_le_u16(Fmt + 12);
			BitsPerSample = read_le_u16(Fmt + 14);
			FoundFmt = true;
		}
		else if (chunk_id_equals(Chunk, "data"))
		{
			DataBytes = Bytes.data() + ChunkDataOffset;
			DataByteCount = ChunkSize;
			FoundData = true;
		}

		Offset = ChunkDataOffset + ChunkSize + (ChunkSize & 1);
	}

	if (!FoundFmt || !FoundData)
	{
		*Error = "unsupported WAV file: missing fmt or data chunk";
		return false;
	}

	if (ChannelCount != 1 || SampleRate != 16000)
	{
		*Error = "unsupported WAV file: expected 16 kHz mono audio";
		return false;
	}

	if (BlockAlign == 0 || DataByteCount % BlockAlign != 0)
	{
		*Error = "invalid WAV file: data size is not aligned to sample frames";
		return false;
	}

	if (FormatTag == 1 && BitsPerSample == 16)
	{
		OutSamples->resize(DataByteCount / 2);
		for (size_t i = 0; i < OutSamples->size(); i++)
		{
			uint16_t Raw = read_le_u16(DataBytes + i * 2);
			int Sample = Raw >= 32768 ? (int)Raw - 65536 : (int)Raw;
			(*OutSamples)[i] = (float)Sample / 32768.0f;
		}

		return true;
	}

	if (FormatTag == 3 && BitsPerSample == 32)
	{
		OutSamples->resize(DataByteCount / 4);
		for (size_t i = 0; i < OutSamples->size(); i++)
		{
			uint32_t Raw = read_le_u32(DataBytes + i * 4);
			float Sample = 0.0f;
			std::memcpy(&Sample, &Raw, sizeof(Sample));
			(*OutSamples)[i] = Sample;
		}

		return true;
	}

	*Error = "unsupported WAV file: expected PCM signed 16-bit or IEEE float32 little-endian audio";
	return false;
}

static double
compute_samples_rms(const std::vector<float> &Samples)
{
	if (Samples.empty()) return 0.0;

	double Sum = 0.0;
	for (float S : Samples)
	{
		Sum += (double)S * (double)S;
	}

	return std::sqrt(Sum / (double)Samples.size());
}

static void
mix_noise_at_snr(std::vector<float> *Samples, const std::vector<float> &Noise, double SnrDb)
{
	double SignalRms = compute_samples_rms(*Samples);
	double NoiseRms = compute_samples_rms(Noise);
	if (SignalRms <= 0.0 || NoiseRms <= 0.0) return;

	double NoiseGain = SignalRms / (NoiseRms * std::pow(10.0, SnrDb / 20.0));
	for (size_t i = 0; i < Samples->size(); i++)
	{
		double Mixed = (double)(*Samples)[i] + (double)Noise[i % Noise.size()] * NoiseGain;
		if (Mixed > 1.0) Mixed = 1.0;
		else if (Mixed < -1.0) Mixed = -1.0;
		(*Samples)[i] = (float)Mixed;
	}
}

static double
elapsed_ms(std::chrono::steady_clock::time_point Start, std::chrono::steady_clock::time_point End)
{
	return std::chrono::duration<double, std::milli>(End - Start).count();
}

static std::string
json_escape(const std::string &Text)
{
	std::ostringstream Out;
	for (unsigned char Ch : Text)
	{
		switch (Ch)
		{
			case '\\': Out << "\\\\"; break;
			case '"': Out << "\\\""; break;
			case '\b': Out << "\\b"; break;
			case '\f': Out << "\\f"; break;
			case '\n': Out << "\\n"; break;
			case '\r': Out << "\\r"; break;
			case '\t': Out << "\\t"; break;
			default:
			{
				if (Ch < 0x20)
				{
					Out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)Ch
						<< std::dec << std::setfill(' ');
				}
				else
				{
					Out << Ch;
				}
			} break;
		}
	}

	return Out.str();
}

static std::string
normalize_expected_text(std::string Text)
{
	std::string Normalized;
	Normalized.reserve(Text.size());
	for (size_t i = 0; i < Text.size(); i++)
	{
		if (Text[i] == '\r')
		{
			if (i + 1 < Text.size() && Text[i + 1] == '\n') i++;
			Normalized += '\n';
		}
		else
		{
			Normalized += Text[i];
		}
	}

	size_t Begin = Normalized.find_first_not_of(" \t\n\r\f\v");
	if (Begin == std::string::npos) return "";

	size_t End = Normalized.find_last_not_of(" \t\n\r\f\v");
	return Normalized.substr(Begin, End - Begin + 1);
}

static std::string
wer_normalize(const std::string &Text)
{
	std::string Out;
	Out.reserve(Text.size());
	for (size_t i = 0; i < Text.size(); i++)
	{
		char C = Text[i];
		if (C >= 'A' && C <= 'Z') C = (char)(C - 'A' + 'a');
		bool Keep = (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '\'';
		Out.push_back(Keep ? C : ' ');
	}

	return Out;
}

static std::vector<std::string>
split_words(const std::string &Text)
{
	std::vector<std::string> Words;
	std::string Current;
	for (size_t i = 0; i < Text.size(); i++)
	{
		char C = Text[i];
		if (C == ' ' || C == '\t' || C == '\n' || C == '\r')
		{
			if (!Current.empty())
			{
				Words.push_back(Current);
				Current.clear();
			}
		}
		else
		{
			Current.push_back(C);
		}
	}

	if (!Current.empty()) Words.push_back(Current);

	return Words;
}

struct WerCounts
{
	int Substitutions = 0;
	int Deletions = 0;
	int Insertions = 0;
	int ReferenceWords = 0;
};

static WerCounts
compute_wer(const std::vector<std::string> &Reference, const std::vector<std::string> &Hypothesis)
{
	size_t RefCount = Reference.size();
	size_t HypCount = Hypothesis.size();
	std::vector<std::vector<int>> Table(RefCount + 1, std::vector<int>(HypCount + 1, 0));
	for (size_t i = 0; i <= RefCount; i++) Table[i][0] = (int)i;
	for (size_t j = 0; j <= HypCount; j++) Table[0][j] = (int)j;

	for (size_t i = 1; i <= RefCount; i++)
	{
		for (size_t j = 1; j <= HypCount; j++)
		{
			int SubCost = Table[i - 1][j - 1] + (Reference[i - 1] == Hypothesis[j - 1] ? 0 : 1);
			int DelCost = Table[i - 1][j] + 1;
			int InsCost = Table[i][j - 1] + 1;
			Table[i][j] = std::min(SubCost, std::min(DelCost, InsCost));
		}
	}

	WerCounts Counts;
	Counts.ReferenceWords = (int)RefCount;

	size_t i = RefCount;
	size_t j = HypCount;
	while (i > 0 || j > 0)
	{
		if (i > 0 && j > 0 && Reference[i - 1] == Hypothesis[j - 1])
		{
			i--;
			j--;
		}
		else if (i > 0 && j > 0 && Table[i][j] == Table[i - 1][j - 1] + 1)
		{
			Counts.Substitutions++;
			i--;
			j--;
		}
		else if (i > 0 && Table[i][j] == Table[i - 1][j] + 1)
		{
			Counts.Deletions++;
			i--;
		}
		else
		{
			Counts.Insertions++;
			j--;
		}
	}

	return Counts;
}

static std::string
format_ms(double Milliseconds)
{
	std::ostringstream Out;
	Out << std::fixed << std::setprecision(3) << Milliseconds;
	return Out.str();
}

static void
load_cpu_backend()
{
	std::string PluginPath = platform_ggml_backend_library_path(platform_get_binary_dir(), "cpu");

	FILE *F = std::fopen(PluginPath.c_str(), "rb");
	if (!F) return;
	std::fclose(F);

	ggml_backend_load(PluginPath.c_str());
}

static bool
load_cuda_plugin(std::string *Error)
{
	std::string PluginPath = platform_ggml_backend_library_path(platform_get_binary_dir(), "cuda");

	FILE *F = std::fopen(PluginPath.c_str(), "rb");
	if (!F)
	{
		*Error = "cuda plugin not found: " + PluginPath;
		return false;
	}
	std::fclose(F);

	ggml_backend_reg_t Reg = ggml_backend_load(PluginPath.c_str());
	if (Reg == nullptr)
	{
		*Error = "ggml_backend_load failed: " + PluginPath;
		return false;
	}

	size_t DevCount = ggml_backend_dev_count();
	for (size_t i = 0; i < DevCount; i++)
	{
		ggml_backend_dev_t Dev = ggml_backend_dev_get(i);
		if (Dev && ggml_backend_dev_type(Dev) == GGML_BACKEND_DEVICE_TYPE_GPU)
		{
			return true;
		}
	}

	*Error = "cuda plugin loaded but no GPU device registered";
	return false;
}

struct CaptureLatencyRun
{
	double OpenMs;
	double StartMs;
	double FirstAudioMs;
	double StopMs;
	double CapturedMs;
	double TailGapMs;
};

static double
latency_median(std::vector<double> &Values)
{
	size_t N = Values.size();
	if (N == 0) return 0.0;

	std::sort(Values.begin(), Values.end());
	return Values[N / 2];
}

static void
print_latency_metric(const char *Name, std::vector<double> Values)
{
	if (Values.empty()) return;

	double Min = Values[0], Max = Values[0], Sum = 0.0;
	for (double V : Values)
	{
		if (V < Min) Min = V;
		if (V > Max) Max = V;
		Sum += V;
	}

	std::cout << ",\"" << Name << "\":{"
		<< "\"min\":" << format_ms(Min)
		<< ",\"med\":" << format_ms(latency_median(Values))
		<< ",\"avg\":" << format_ms(Sum / (double)Values.size())
		<< ",\"max\":" << format_ms(Max)
		<< "}";
}

static int
run_capture_latency_bench(const BenchOptions &Options)
{
	GlobalState AppState = {};
	AppState.PipelineRequestNs.store(0);
	AppState.LastRecordDeviceOpenMs.store(-1.0);
	AppState.LastRecordCaptureStartMs.store(-1.0);
	AppState.LastRecordFirstAudioMs.store(-1.0);

	AppState.AudioInputDevices = platform_query_audio_devices();
	if (AppState.AudioInputDevices.empty())
	{
		std::cerr << "no audio capture devices found\n";
		return 1;
	}

	int DeviceIndex = Options.AudioDeviceIndex;
	if (DeviceIndex < 0)
	{
		DeviceIndex = 0;
		for (int i = 0; i < (int)AppState.AudioInputDevices.size(); i++)
		{
			if (AppState.AudioInputDevices[i].IsDefault)
			{
				DeviceIndex = i;
				break;
			}
		}
	}
	if (DeviceIndex >= (int)AppState.AudioInputDevices.size())
	{
		std::cerr << "audio device index out of range (found "
			<< AppState.AudioInputDevices.size() << " devices)\n";
		return 1;
	}

	std::vector<CaptureLatencyRun> Runs;

	for (int Iteration = 0; Iteration < Options.IterationCount; Iteration++)
	{
		{
			std::lock_guard<std::mutex> Lock(AppState.AudioBufferMutex);
			clip_release(&AppState.AudioPool, &AppState.AudioAccum);
		}

		AppState.LastRecordDeviceOpenMs.store(-1.0);
		AppState.LastRecordCaptureStartMs.store(-1.0);
		AppState.LastRecordFirstAudioMs.store(-1.0);

		perf_event("capture_latency_request");
		AppState.PipelineRequestNs.store(perf_now_ns());
		AppState.CaptureRunning.store(true);

		std::thread CaptureThread(platform_audio_capture, &AppState.Platform, &AppState, DeviceIndex);

		const int64_t FirstAudioTimeoutMs = 4000;
		int64_t WaitedMs = 0;
		while (AppState.LastRecordFirstAudioMs.load() < 0.0 && WaitedMs < FirstAudioTimeoutMs)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			WaitedMs += 2;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(150));

		std::chrono::steady_clock::time_point StopStart = std::chrono::steady_clock::now();
		int64_t StopRequestNs = perf_now_ns();
		AppState.CaptureRunning.store(false);
		CaptureThread.join();
		double StopMs = elapsed_ms(StopStart, std::chrono::steady_clock::now());

		CaptureLatencyRun Run = {};
		Run.OpenMs = AppState.LastRecordDeviceOpenMs.load();
		Run.StartMs = AppState.LastRecordCaptureStartMs.load();
		Run.FirstAudioMs = AppState.LastRecordFirstAudioMs.load();
		Run.StopMs = StopMs;
		{
			std::lock_guard<std::mutex> Lock(AppState.AudioBufferMutex);
			Run.CapturedMs = (double)AppState.AudioAccum.TotalSamples * 1000.0 / AUDIO_CAPTURE_SAMPLE_RATE;
		}
		double ExpectedMs = (double)(StopRequestNs - AppState.PipelineRequestNs.load()) / 1000000.0 - Run.StartMs;
		Run.TailGapMs = ExpectedMs - Run.CapturedMs;
		Runs.push_back(Run);

		perf_event("capture_latency_iteration_done");

		if (Iteration + 1 < Options.IterationCount)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(150));
		}
	}

	std::vector<double> OpenMs, StartMs, FirstAudioMs, StopMs, TailGapMs;
	for (const CaptureLatencyRun &Run : Runs)
	{
		OpenMs.push_back(Run.OpenMs);
		StartMs.push_back(Run.StartMs);
		FirstAudioMs.push_back(Run.FirstAudioMs);
		StopMs.push_back(Run.StopMs);
		TailGapMs.push_back(Run.TailGapMs);
	}

	std::cout << "{\"mode\":\"capture-latency\""
		<< ",\"device_index\":" << DeviceIndex
		<< ",\"device_name\":\"" << json_escape(AppState.AudioInputDevices[DeviceIndex].Name) << "\""
		<< ",\"iterations\":" << Options.IterationCount
		<< ",\"per_iteration\":[";

	for (size_t i = 0; i < Runs.size(); i++)
	{
		std::cout << (i > 0 ? "," : "")
			<< "{\"open_ms\":" << format_ms(Runs[i].OpenMs >= 0 ? Runs[i].OpenMs : -1.0)
			<< ",\"start_ms\":" << format_ms(Runs[i].StartMs >= 0 ? Runs[i].StartMs : -1.0)
			<< ",\"first_audio_ms\":" << format_ms(Runs[i].FirstAudioMs >= 0 ? Runs[i].FirstAudioMs : -1.0)
			<< ",\"stop_ms\":" << format_ms(Runs[i].StopMs)
			<< ",\"captured_ms\":" << format_ms(Runs[i].CapturedMs)
			<< ",\"tail_gap_ms\":" << format_ms(Runs[i].TailGapMs)
			<< "}";
	}

	std::cout << "]";
	print_latency_metric("open_ms", OpenMs);
	print_latency_metric("start_ms", StartMs);
	print_latency_metric("first_audio_ms", FirstAudioMs);
	print_latency_metric("stop_ms", StopMs);
	print_latency_metric("tail_gap_ms", TailGapMs);
	std::cout << "}\n";

	return 0;
}

extern bool bench_real_platform_audio_capture(PlatformRuntimeState *Platform, GlobalState *AppState, int DeviceIndex);
extern void *bench_real_platform_get_foreground_window(PlatformRuntimeState *Platform);

static std::vector<float> g_SoakSamples;
static std::atomic<bool> g_SoakSynthActive{false};
static std::atomic<bool> g_SoakFast{false};
static std::atomic<int64_t> g_SoakTakeSamples{0};
static std::atomic<int64_t> g_SoakFedSamples{0};

static_assert(AUDIO_CAPTURE_SAMPLE_RATE * AUDIO_CAPTURE_BUFFER_MS / 1000 <= 2048,
	"soak synthetic capture chunk buffer is too small");

bool platform_audio_capture(PlatformRuntimeState *Platform, GlobalState *AppState, int DeviceIndex)
{
	if (!g_SoakSynthActive.load()) return bench_real_platform_audio_capture(Platform, AppState, DeviceIndex);

	float Chunk[2048];
	const int ChunkSamples = AUDIO_CAPTURE_SAMPLE_RATE * AUDIO_CAPTURE_BUFFER_MS / 1000;
	const size_t SourceLength = g_SoakSamples.size();
	const int64_t Target = g_SoakTakeSamples.load();
	g_SoakFedSamples.store(0);
	bool First = true;

	while (AppState->CaptureRunning.load())
	{
		int64_t Fed = g_SoakFedSamples.load();
		if (Fed >= Target) break;

		int Count = ChunkSamples;
		if ((int64_t)Count > Target - Fed) Count = (int)(Target - Fed);

		size_t Source = (size_t)(Fed % (int64_t)SourceLength);
		for (int i = 0; i < Count; i++)
		{
			Chunk[i] = g_SoakSamples[Source];
			Source = (Source + 1 == SourceLength) ? 0 : Source + 1;
		}

		{
			std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
			clip_append(&AppState->AudioPool, &AppState->AudioAccum, Chunk, Count);
		}

		if (First)
		{
			First = false;
			AppState->LastRecordFirstAudioMs.store(
				(double)(perf_now_ns() - AppState->PipelineRequestNs.load()) / 1000000.0);
			perf_event("audio_first_samples");
		}

		g_SoakFedSamples.store(Fed + Count);
		if (!g_SoakFast.load())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(AUDIO_CAPTURE_BUFFER_MS));
		}
	}

	AppState->CaptureRunning.store(false);
	return true;
}

void *platform_get_foreground_window(PlatformRuntimeState *Platform)
{
	if (g_SoakSynthActive.load()) return nullptr;
	return bench_real_platform_get_foreground_window(Platform);
}

struct SoakCycleStats
{
	double RecordPrivateMb;
	double RecordWorkingMb;
	double StreamPrivateMb;
	double StreamWorkingMb;
	int PoolFreeAfterRecord;
	int PoolFreeAfterStream;
	int AccumSamplesAfterRecord;
	int AccumSamplesAfterStream;
	int StagingCapKb;
	double RecordWallMs;
	double StreamWallMs;
};

static void
soak_sample_state(GlobalState *AppState, double *PrivateMb, double *WorkingMb, int *PoolFree, int *AccumSamples)
{
	PerfMemorySnapshot Mem = {};
	perf_read_process_memory(&Mem);
	*PrivateMb = (double)Mem.PrivateBytes / (1024.0 * 1024.0);
	*WorkingMb = (double)Mem.WorkingSetBytes / (1024.0 * 1024.0);

	{
		std::lock_guard<std::mutex> Lock(AppState->AudioPool.Mutex);
		*PoolFree = AppState->AudioPool.FreeCount;
	}

	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		*AccumSamples = AppState->AudioAccum.TotalSamples;
	}
}

static bool
soak_wait_pipeline_idle(GlobalState *AppState, int TimeoutMs)
{
	int64_t WaitedMs = 0;
	while (AppState->PipelineActive.load())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		WaitedMs += 5;
		if (WaitedMs >= TimeoutMs) return false;
	}

	if (AppState->CaptureThread.joinable()) AppState->CaptureThread.join();
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	return true;
}

static int
run_leak_soak_bench(const BenchOptions &Options, const std::vector<float> &Samples)
{
	if (Samples.empty())
	{
		std::cerr << "leak-soak requires non-empty --audio\n";
		return 1;
	}

	GlobalState AppState = {};
	init_stt_state(&AppState.WhisperState);

	AudioInputDeviceInfo Device = {};
	Device.Name = "soak-synth";
	Device.Id = "soak-synth";
	AppState.AudioInputDevices.push_back(Device);
	AppState.CurrentAudioDeviceIndex = 0;
	AppState.WhisperThreadCount = Options.ThreadCount;
	AppState.VadModelPath = Options.VadModelPath;

	pool_init(&AppState.AudioPool);
	AppState.WhisperStaging.reserve(
		(size_t)(AUDIO_CAPTURE_SAMPLE_RATE * AUDIO_STAGING_INITIAL_MS / 1000));

	{
		PerfSpan Span("soak_model_load");
		if (!load_stt_model(&AppState.WhisperState, Options.ModelPath.c_str(), 0, 0))
		{
			std::cerr << "failed to load Whisper model: " << Options.ModelPath << "\n";
			return 1;
		}
	}

	int64_t TakeSamples = (int64_t)Samples.size();
	if (Options.HasTakeMs) TakeSamples = (int64_t)Options.SoakTakeMs * AUDIO_CAPTURE_SAMPLE_RATE / 1000;

	g_SoakSamples = Samples;
	g_SoakFast.store(Options.SoakFast);
	g_SoakTakeSamples.store(TakeSamples);
	g_SoakSynthActive.store(true);

	std::vector<SoakCycleStats> Cycles;
	const int TotalCycles = Options.WarmupCount + Options.IterationCount;
	bool Failed = false;

	for (int Cycle = 0; Cycle < TotalCycles; Cycle++)
	{
		SoakCycleStats Stats = {};
		std::chrono::steady_clock::time_point Start;

		Start = std::chrono::steady_clock::now();
		if (!start_record_pipeline(&AppState))
		{
			std::cerr << "record pipeline failed to start\n";
			Failed = true;
			break;
		}
		if (!soak_wait_pipeline_idle(&AppState, 600000))
		{
			std::cerr << "record pipeline timed out\n";
			Failed = true;
			break;
		}
		Stats.RecordWallMs = elapsed_ms(Start, std::chrono::steady_clock::now());
		soak_sample_state(&AppState, &Stats.RecordPrivateMb, &Stats.RecordWorkingMb,
			&Stats.PoolFreeAfterRecord, &Stats.AccumSamplesAfterRecord);

		Start = std::chrono::steady_clock::now();
		if (!start_streaming_pipeline(&AppState))
		{
			std::cerr << "streaming pipeline failed to start\n";
			Failed = true;
			break;
		}
		if (!soak_wait_pipeline_idle(&AppState, 600000))
		{
			std::cerr << "streaming pipeline timed out\n";
			Failed = true;
			break;
		}
		Stats.StreamWallMs = elapsed_ms(Start, std::chrono::steady_clock::now());
		soak_sample_state(&AppState, &Stats.StreamPrivateMb, &Stats.StreamWorkingMb,
			&Stats.PoolFreeAfterStream, &Stats.AccumSamplesAfterStream);

		Stats.StagingCapKb = (int)(AppState.WhisperStaging.capacity() * sizeof(float) / 1024);

		if (Cycle < Options.WarmupCount)
		{
			std::cerr << "warmup cycle " << Cycle << " done (rec " << format_ms(Stats.RecordWallMs)
				<< " ms, strm " << format_ms(Stats.StreamWallMs) << " ms)\n";
			continue;
		}

		Cycles.push_back(Stats);
		size_t N = Cycles.size();
		double RecDelta = 0.0;
		double StrmDelta = 0.0;
		if (N > 1)
		{
			RecDelta = Cycles[N - 1].RecordPrivateMb - Cycles[N - 2].RecordPrivateMb;
			StrmDelta = Cycles[N - 1].StreamPrivateMb - Cycles[N - 2].StreamPrivateMb;
		}
		std::cerr << "cycle " << Cycle << ": rec private " << std::fixed << std::setprecision(1)
			<< Stats.RecordPrivateMb << " MB (" << (RecDelta >= 0 ? "+" : "") << RecDelta << ")"
			<< " | strm private " << Stats.StreamPrivateMb << " MB ("
			<< (StrmDelta >= 0 ? "+" : "") << StrmDelta << ")"
			<< " | pool free " << Stats.PoolFreeAfterRecord << " -> " << Stats.PoolFreeAfterStream
			<< " | staging " << Stats.StagingCapKb << " KB"
			<< " | rec " << format_ms(Stats.RecordWallMs) << " ms, strm "
			<< format_ms(Stats.StreamWallMs) << " ms\n";
	}

	g_SoakSynthActive.store(false);

	bool PoolLeak = false;
	bool AccumResidue = false;
	for (size_t i = 0; i < Cycles.size(); i++)
	{
		if (i > 0)
		{
			if (Cycles[i].PoolFreeAfterRecord < Cycles[i - 1].PoolFreeAfterRecord) PoolLeak = true;
			if (Cycles[i].PoolFreeAfterStream < Cycles[i - 1].PoolFreeAfterStream) PoolLeak = true;
		}
		if (Cycles[i].AccumSamplesAfterRecord != 0 || Cycles[i].AccumSamplesAfterStream != 0) AccumResidue = true;
	}

	double SlopeKbPerCycle = 0.0;
	if (Cycles.size() >= 4)
	{
		size_t Half = Cycles.size() / 2;
		double FirstMb = Cycles[Half].StreamPrivateMb;
		double LastMb = Cycles.back().StreamPrivateMb;
		SlopeKbPerCycle = (LastMb - FirstMb) * 1024.0 / (double)(Cycles.size() - 1 - Half);
	}

	const char *Verdict = "flat";
	if (Failed) Verdict = "incomplete";
	else if (AccumResidue) Verdict = "accumulator-residue";
	else if (PoolLeak) Verdict = "pool-leak";
	else if (SlopeKbPerCycle > 32.0) Verdict = "growth";

	std::cout << "{\"mode\":\"leak-soak\""
		<< ",\"audio\":\"" << json_escape(Options.AudioPath) << "\""
		<< ",\"take_ms\":" << (int)(TakeSamples * 1000 / AUDIO_CAPTURE_SAMPLE_RATE)
		<< ",\"fast\":" << (Options.SoakFast ? "true" : "false")
		<< ",\"warmup\":" << Options.WarmupCount
		<< ",\"cycles\":" << Cycles.size()
		<< ",\"peak_private_mb\":" << format_ms((double)perf_peak_private_bytes() / (1024.0 * 1024.0))
		<< ",\"private_slope_kb_per_cycle\":" << format_ms(SlopeKbPerCycle)
		<< ",\"verdict\":\"" << Verdict << "\""
		<< ",\"per_cycle\":[";

	for (size_t i = 0; i < Cycles.size(); i++)
	{
		std::cout << (i > 0 ? "," : "")
			<< "{\"record_private_mb\":" << format_ms(Cycles[i].RecordPrivateMb)
			<< ",\"stream_private_mb\":" << format_ms(Cycles[i].StreamPrivateMb)
			<< ",\"record_working_mb\":" << format_ms(Cycles[i].RecordWorkingMb)
			<< ",\"stream_working_mb\":" << format_ms(Cycles[i].StreamWorkingMb)
			<< ",\"pool_free_after_record\":" << Cycles[i].PoolFreeAfterRecord
			<< ",\"pool_free_after_stream\":" << Cycles[i].PoolFreeAfterStream
			<< ",\"accum_after_record\":" << Cycles[i].AccumSamplesAfterRecord
			<< ",\"accum_after_stream\":" << Cycles[i].AccumSamplesAfterStream
			<< ",\"staging_cap_kb\":" << Cycles[i].StagingCapKb
			<< ",\"record_ms\":" << format_ms(Cycles[i].RecordWallMs)
			<< ",\"stream_ms\":" << format_ms(Cycles[i].StreamWallMs)
			<< "}";
	}

	std::cout << "]}\n";

	unload_stt_model(&AppState.WhisperState);
	return Failed ? 1 : 0;
}

int
main(int ArgCount, char **Args)
{
	BenchOptions Options;
	if (!parse_options(ArgCount, Args, &Options))
	{
		print_usage(Args[0]);
		return 2;
	}

	perf_start(platform_get_binary_dir().c_str(), 10);
	perf_event("bench_process_start");
	BenchPerfGuard PerfGuard;

	if (Options.Mode == "capture-latency")
	{
		int Ret = run_capture_latency_bench(Options);
		perf_event("bench_process_end");
		return Ret;
	}

	std::vector<float> Samples;
	std::string Error;
	if (!load_wav_mono_16khz(Options.AudioPath, &Samples, &Error))
	{
		std::cerr << Error << "\n";
		return 1;
	}

	if (Options.HasNoise)
	{
		std::vector<float> Noise;
		if (!load_wav_mono_16khz(Options.NoisePath, &Noise, &Error))
		{
			std::cerr << Error << "\n";
			return 1;
		}

		mix_noise_at_snr(&Samples, Noise, Options.SnrDb);
	}

	setup_bench_logging(Options);

	// GGML_BACKEND_DL: the CPU backend ships as a separate ggml-cpu.dll that
	// must be registered before whisper can init it. Load it by exact path
	// (not ggml_backend_load_all) so a ggml-cuda.dll next to the exe is not
	// eagerly loaded here; the CUDA plugin is loaded on demand below.
	load_cpu_backend();

	if (Options.Mode == "leak-soak")
	{
		int Ret = run_leak_soak_bench(Options, Samples);
		perf_event("bench_process_end");
		shutdown_bench_logging();
		return Ret;
	}

	WhisperModelState ModelState = {};
	init_stt_state(&ModelState);

	bool UseGpu = (Options.Device == "gpu");
	int InferenceDeviceIndex = 0;
	if (UseGpu)
	{
		std::string PluginError;
		if (!load_cuda_plugin(&PluginError))
		{
			std::cerr << "failed to enable GPU device: " << PluginError << "\n";
			return 1;
		}
		InferenceDeviceIndex = 1;
	}

	PerfMemorySnapshot MemBeforeLoad = {};
	perf_read_process_memory(&MemBeforeLoad);
	PerfCpuSnapshot CpuBeforeLoad = {};
	perf_read_process_cpu(&CpuBeforeLoad);

	bool Loaded = false;
	auto LoadStart = std::chrono::steady_clock::now();
	{
		PerfSpan ModelLoadSpan("bench_model_load");
		Loaded = load_stt_model(&ModelState, Options.ModelPath.c_str(), 0, InferenceDeviceIndex);
	}
	auto LoadEnd = std::chrono::steady_clock::now();
	if (!Loaded)
	{
		std::cerr << "failed to load Whisper model: " << Options.ModelPath << "\n";
		return 1;
	}

	PerfMemorySnapshot MemAfterLoad = {};
	perf_read_process_memory(&MemAfterLoad);
	PerfCpuSnapshot CpuAfterLoad = {};
	perf_read_process_cpu(&CpuAfterLoad);
	double CpuModelLoadMs = CpuAfterLoad.TotalMs() - CpuBeforeLoad.TotalMs();

	static const int BENCH_SAMPLE_RATE = 16000;
	bool IsStreaming = (Options.Mode == "streaming");
	bool SingleSegment = IsStreaming;

	struct TranscribeUnit
	{
		const float *Samples;
		int Count;
	};
	std::vector<TranscribeUnit> Units;
	std::vector<std::vector<float>> Chunks;
	std::vector<int> UnitDurationsMs;

	if (IsStreaming)
	{
		Chunks = chunk_audio_for_streaming(Samples, BENCH_SAMPLE_RATE);
		for (size_t c = 0; c < Chunks.size(); c++)
		{
			UnitDurationsMs.push_back((int)Chunks[c].size() * 1000 / BENCH_SAMPLE_RATE);
			Units.push_back(TranscribeUnit{Chunks[c].data(), (int)Chunks[c].size()});
		}
	}
	else
	{
		UnitDurationsMs.push_back((int)Samples.size() * 1000 / BENCH_SAMPLE_RATE);
		Units.push_back(TranscribeUnit{Samples.data(), (int)Samples.size()});
	}

	const char *VadModelArg = Options.EnableVad ? Options.VadModelPath.c_str() : nullptr;

	auto run_one_pass = [&](std::string *OutText, std::vector<double> *UnitTimes) -> int {
	whisper_full_params Params = make_transcription_whisper_params(
		Options.ThreadCount, Options.EnableVad, VadModelArg);
	Params.single_segment = SingleSegment;
	if (Options.BeamSize > 1)
	{
		Params.strategy             = WHISPER_SAMPLING_BEAM_SEARCH;
		Params.beam_search.beam_size = Options.BeamSize;
	}
		OutText->clear();
		for (size_t u = 0; u < Units.size(); u++)
		{
			std::string UnitText;
			auto UnitStart = std::chrono::steady_clock::now();
			int Ret = transcribe_pcm_to_string(
				ModelState.WhisperContext, Params, Units[u].Samples, Units[u].Count, &UnitText);
			auto UnitEnd = std::chrono::steady_clock::now();
			if (Ret != 0) return Ret;
			if (UnitTimes) UnitTimes->push_back(elapsed_ms(UnitStart, UnitEnd));
			if (!UnitText.empty())
			{
				if (!OutText->empty()) OutText->append(" ");
				OutText->append(UnitText);
			}
		}
		return 0;
	};

	std::string Text;
	for (int i = 0; i < Options.WarmupCount; i++)
	{
		int Ret = run_one_pass(&Text, nullptr);
		if (Ret != 0)
		{
			std::cerr << "whisper_full failed during warmup (ret=" << Ret << ")\n";
			unload_stt_model(&ModelState);
			return 1;
		}
	}

	std::vector<std::vector<double>> PerUnitTimes;
	std::vector<double> CpuTimes;
	PerUnitTimes.reserve((size_t)Options.IterationCount);
	CpuTimes.reserve((size_t)Options.IterationCount);
	for (int i = 0; i < Options.IterationCount; i++)
	{
		PerfCpuSnapshot CpuIterStart = {};
		perf_read_process_cpu(&CpuIterStart);

		std::vector<double> UnitTimes;
		int Ret = run_one_pass(&Text, &UnitTimes);
		if (Ret != 0)
		{
			std::cerr << "whisper_full failed during iteration (ret=" << Ret << ")\n";
			unload_stt_model(&ModelState);
			return 1;
		}

		PerfCpuSnapshot CpuIterEnd = {};
		perf_read_process_cpu(&CpuIterEnd);
		CpuTimes.push_back(CpuIterEnd.TotalMs() - CpuIterStart.TotalMs());

		PerUnitTimes.push_back(std::move(UnitTimes));
	}

	std::vector<double> TranscribeTimes;
	TranscribeTimes.reserve((size_t)Options.IterationCount);
	for (const auto &UnitTimes : PerUnitTimes)
	{
		double Total = 0.0;
		for (double T : UnitTimes) Total += T;
		TranscribeTimes.push_back(Total);
	}

	double TotalAudioMs = 0.0;
	for (int Ms : UnitDurationsMs) TotalAudioMs += (double)Ms;

	double TranscribeWallSum = 0.0;
	double TranscribeWallMin = TranscribeTimes.empty() ? 0.0 : TranscribeTimes[0];
	for (double T : TranscribeTimes)
	{
		TranscribeWallSum += T;
		if (T < TranscribeWallMin) TranscribeWallMin = T;
	}
	double TranscribeWallAvg = TranscribeTimes.empty() ? 0.0 : TranscribeWallSum / (double)TranscribeTimes.size();

	double CpuTranscribeSum = 0.0;
	for (double T : CpuTimes) CpuTranscribeSum += T;
	double CpuTranscribeAvg = CpuTimes.empty() ? 0.0 : CpuTranscribeSum / (double)CpuTimes.size();

	std::string NormalizedText = normalize_expected_text(Text);
	std::string NormalizedExpected;
	if (Options.HasExpectedText) NormalizedExpected = normalize_expected_text(Options.ExpectedText);

	std::cout << "{\"mode\":\"" << Options.Mode
		<< "\",\"vad\":" << (Options.EnableVad ? "true" : "false")
		<< ",\"device\":\"" << Options.Device << "\""
		<< ",\"threads\":" << Options.ThreadCount
		<< ",\"beam\":" << Options.BeamSize
		<< ",\"log\":\"" << Options.LogMode << "\"";
	if (Options.HasNoise)
	{
		std::cout << ",\"noise\":\"" << json_escape(Options.NoisePath) << "\""
			<< ",\"snr_db\":" << std::fixed << std::setprecision(1) << Options.SnrDb;
	}
	std::cout << ",\"model_load_ms\":" << format_ms(elapsed_ms(LoadStart, LoadEnd))
		<< ",\"cpu_model_load_ms\":" << format_ms(CpuModelLoadMs)
		<< ",\"mem_before_private_mb\":" << format_ms((double)MemBeforeLoad.PrivateBytes / (1024.0 * 1024.0))
		<< ",\"mem_after_private_mb\":" << format_ms((double)MemAfterLoad.PrivateBytes / (1024.0 * 1024.0))
		<< ",\"mem_model_private_mb\":" << format_ms(
			(double)(MemAfterLoad.PrivateBytes - MemBeforeLoad.PrivateBytes) / (1024.0 * 1024.0))
		<< ",\"mem_model_working_mb\":" << format_ms(
			(double)(MemAfterLoad.WorkingSetBytes - MemBeforeLoad.WorkingSetBytes) / (1024.0 * 1024.0))
		<< ",\"peak_private_mb\":" << format_ms((double)perf_peak_private_bytes() / (1024.0 * 1024.0))
		<< ",\"cpu_transcribe_avg_ms\":" << format_ms(CpuTranscribeAvg)
		<< ",\"rtf_avg\":" << format_ms(TranscribeWallAvg > 0.0 ? TotalAudioMs / TranscribeWallAvg : 0.0)
		<< ",\"rtf_best\":" << format_ms(TranscribeWallMin > 0.0 ? TotalAudioMs / TranscribeWallMin : 0.0)
		<< ",\"unit_count\":" << Units.size()
		<< ",\"unit_durations_ms\":[";
	for (size_t i = 0; i < UnitDurationsMs.size(); i++)
	{
		if (i > 0) std::cout << ",";
		std::cout << UnitDurationsMs[i];
	}

	std::cout << "],\"transcribe_ms\":[";
	for (size_t i = 0; i < TranscribeTimes.size(); i++)
	{
		if (i > 0) std::cout << ",";
		std::cout << format_ms(TranscribeTimes[i]);
	}

	std::cout << "],\"per_unit_ms\":[";
	for (size_t i = 0; i < PerUnitTimes.size(); i++)
	{
		if (i > 0) std::cout << ",";
		std::cout << "[";
		for (size_t u = 0; u < PerUnitTimes[i].size(); u++)
		{
			if (u > 0) std::cout << ",";
			std::cout << format_ms(PerUnitTimes[i][u]);
		}
		std::cout << "]";
	}

	std::cout << "],\"cpu_transcribe_ms\":[";
	for (size_t i = 0; i < CpuTimes.size(); i++)
	{
		if (i > 0) std::cout << ",";
		std::cout << format_ms(CpuTimes[i]);
	}

	std::cout << "],\"text\":\"" << json_escape(Text) << "\"";
	if (Options.HasExpectedText)
	{
		std::vector<std::string> RefWords = split_words(wer_normalize(NormalizedExpected));
		std::vector<std::string> HypWords = split_words(wer_normalize(NormalizedText));
		WerCounts Counts = compute_wer(RefWords, HypWords);
		int ErrorWords = Counts.Substitutions + Counts.Deletions + Counts.Insertions;
		double Wer = 0.0;
		if (Counts.ReferenceWords > 0) Wer = (double)ErrorWords / (double)Counts.ReferenceWords;
		else if (ErrorWords > 0) Wer = 1.0;

		std::cout << ",\"expected_text\":\"" << json_escape(Options.ExpectedText) << "\""
			<< ",\"expected_text_match\":" << (NormalizedText == NormalizedExpected ? "true" : "false")
			<< ",\"wer\":" << std::fixed << std::setprecision(6) << Wer
			<< ",\"wer_ref_words\":" << Counts.ReferenceWords
			<< ",\"wer_substitutions\":" << Counts.Substitutions
			<< ",\"wer_deletions\":" << Counts.Deletions
			<< ",\"wer_insertions\":" << Counts.Insertions;
	}

	std::cout << "}\n";

	unload_stt_model(&ModelState);
	perf_event("bench_process_end");
	shutdown_bench_logging();
	return 0;
}
