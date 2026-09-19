#define NOMINMAX

#include "host_services.h"
#include "perf.h"
#include "state.h"
#include "transcription_core.h"
#include "whisper_wrapper.h"
#include "stream_chunker.h"

#include <algorithm>
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
		<< " [--mode <record|streaming|capture-latency>] [--vad <on|off>] [--vad-model <path>]"
		<< " [--device <cpu|gpu>] [--audio-device <index>] [--warmup <count>] [--iterations <count>]"
		<< " [--threads <count>] [--log <off|file|verbose>]"
		<< " [--beam <1-16>] [--noise <wav>] [--snr <db>]\n";
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
				std::string(Value) != "capture-latency")
			{
				std::cerr << "--mode must be 'record', 'streaming' or 'capture-latency'\n";
				return false;
			}
			Options->Mode = Value;
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
		AppState.CaptureRunning.store(false);
		CaptureThread.join();
		double StopMs = elapsed_ms(StopStart, std::chrono::steady_clock::now());

		CaptureLatencyRun Run = {};
		Run.OpenMs = AppState.LastRecordDeviceOpenMs.load();
		Run.StartMs = AppState.LastRecordCaptureStartMs.load();
		Run.FirstAudioMs = AppState.LastRecordFirstAudioMs.load();
		Run.StopMs = StopMs;
		Runs.push_back(Run);

		perf_event("capture_latency_iteration_done");

		if (Iteration + 1 < Options.IterationCount)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(150));
		}
	}

	std::vector<double> OpenMs, StartMs, FirstAudioMs, StopMs;
	for (const CaptureLatencyRun &Run : Runs)
	{
		OpenMs.push_back(Run.OpenMs);
		StartMs.push_back(Run.StartMs);
		FirstAudioMs.push_back(Run.FirstAudioMs);
		StopMs.push_back(Run.StopMs);
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
			<< "}";
	}

	std::cout << "]";
	print_latency_metric("open_ms", OpenMs);
	print_latency_metric("start_ms", StartMs);
	print_latency_metric("first_audio_ms", FirstAudioMs);
	print_latency_metric("stop_ms", StopMs);
	std::cout << "}\n";

	return 0;
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

	WhisperModelState ModelState = {};
	init_whisper_state(&ModelState);

	setup_bench_logging(Options);

	// GGML_BACKEND_DL: the CPU backend ships as a separate ggml-cpu.dll that
	// must be registered before whisper can init it. Load it by exact path
	// (not ggml_backend_load_all) so a ggml-cuda.dll next to the exe is not
	// eagerly loaded here; the CUDA plugin is loaded on demand below.
	load_cpu_backend();

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
		Loaded = load_whisper_model(&ModelState, Options.ModelPath.c_str(), 0, InferenceDeviceIndex);
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
				ModelState.Context, Params, Units[u].Samples, Units[u].Count, &UnitText);
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
			unload_whisper_model(&ModelState);
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
			unload_whisper_model(&ModelState);
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

	unload_whisper_model(&ModelState);
	perf_event("bench_process_end");
	shutdown_bench_logging();
	return 0;
}
