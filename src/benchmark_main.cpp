#include "transcription_core.h"
#include "whisper_wrapper.h"
#include "stream_chunker.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
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
	int WarmupCount = 1;
	int IterationCount = 5;
	int ThreadCount = 1;
	int CudaDevice = 0;
};

static void
print_usage(const char *ExeName)
{
	std::cerr << "Usage: " << ExeName
		<< " --audio <path> [--model <path>] [--expected-text <text>]"
		<< " [--mode <record|streaming>] [--vad <on|off>] [--vad-model <path>]"
		<< " [--warmup <count>] [--iterations <count>] [--threads <count>] [--cuda-device <index>]\n";
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
		else if (Arg == "--cuda-device")
		{
			const char *Value = require_value("--cuda-device");
			if (!Value || !parse_int_arg(Value, 0, &Options->CudaDevice)) return false;
		}
		else if (Arg == "--mode")
		{
			const char *Value = require_value("--mode");
			if (!Value) return false;
			if (std::string(Value) != "record" && std::string(Value) != "streaming")
			{
				std::cerr << "--mode must be 'record' or 'streaming'\n";
				return false;
			}
			Options->Mode = Value;
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
		else
		{
			std::cerr << "Unknown argument: " << Arg << "\n";
			return false;
		}
	}

	if (Options->AudioPath.empty())
	{
		std::cerr << "--audio is required\n";
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
format_ms(double Milliseconds)
{
	std::ostringstream Out;
	Out << std::fixed << std::setprecision(3) << Milliseconds;
	return Out.str();
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

	std::vector<float> Samples;
	std::string Error;
	if (!load_wav_mono_16khz(Options.AudioPath, &Samples, &Error))
	{
		std::cerr << Error << "\n";
		return 1;
	}

	WhisperModelState ModelState = {};
	init_whisper_state(&ModelState);

	int InferenceDeviceIndex = 0;
#if defined(VOICETYPER_CUDA)
	InferenceDeviceIndex = Options.CudaDevice + 1;
#endif

	auto LoadStart = std::chrono::steady_clock::now();
	bool Loaded = load_whisper_model(&ModelState, Options.ModelPath.c_str(), 0, InferenceDeviceIndex);
	auto LoadEnd = std::chrono::steady_clock::now();
	if (!Loaded)
	{
		std::cerr << "failed to load Whisper model: " << Options.ModelPath << "\n";
		return 1;
	}

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
	PerUnitTimes.reserve((size_t)Options.IterationCount);
	for (int i = 0; i < Options.IterationCount; i++)
	{
		std::vector<double> UnitTimes;
		int Ret = run_one_pass(&Text, &UnitTimes);
		if (Ret != 0)
		{
			std::cerr << "whisper_full failed during iteration (ret=" << Ret << ")\n";
			unload_whisper_model(&ModelState);
			return 1;
		}
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

	std::string NormalizedText = normalize_expected_text(Text);
	std::string NormalizedExpected;
	if (Options.HasExpectedText) NormalizedExpected = normalize_expected_text(Options.ExpectedText);

	std::cout << "{\"mode\":\"" << Options.Mode
		<< "\",\"vad\":" << (Options.EnableVad ? "true" : "false")
		<< ",\"model_load_ms\":" << format_ms(elapsed_ms(LoadStart, LoadEnd))
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

	std::cout << "],\"text\":\"" << json_escape(Text) << "\"";
	if (Options.HasExpectedText)
	{
		std::cout << ",\"expected_text\":\"" << json_escape(Options.ExpectedText) << "\""
			<< ",\"expected_text_match\":" << (NormalizedText == NormalizedExpected ? "true" : "false");
	}

	std::cout << "}\n";

	unload_whisper_model(&ModelState);
	return 0;
}
