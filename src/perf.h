#pragma once

// Lightweight performance instrumentation, compiled in only when the build
// defines VOICETYPER_PERF=1 (CMake option VOICETYPER_PERF). Without the
// define every probe below compiles to a no-op stub, so release builds carry
// none of this code.
//
// Public interface is platform-agnostic:
//   perf_now_ns()/perf_now_ms()      - ALWAYS REAL (not gated): steady
//                                      monotonic clock relative to first use;
//                                      powers the record-start latency values
//   perf_event(name [, dur, cpu])    - timestamped lifecycle marker, snapshots
//                                      memory + CPU at record time
//   PerfSpan                         - RAII span: wall duration + CPU delta
//   perf_note_loop_tick(delta_ms)    - app tick cadence stats (bounds the
//                                      hotkey-poll detection delay)
//   perf_start(dir, hz)              - enable background sampler + periodic
//                                      JSON report rewrite (survives kills)
//   perf_finish()                    - stop sampler, write final report
//
// Implementation note: Windows is fully implemented (K32*/GetProcessTimes).
// Other platforms compile with no-op OS counters so builds stay green; fill
// them in when that platform becomes a first-class citizen.
//
// Report file: <dir>/voicetyper-perf-<pid>.json, a JSON object with events[],
// samples[], peaks, cpu totals and tick stats. Intended to be read by humans
// and agents alike.

#include <chrono>
#include <cstdint>

#ifdef _WIN32
	#ifdef VOICETYPER_PERF
		#ifndef PSAPI_VERSION
			#define PSAPI_VERSION 2
		#endif
		#include <windows.h>
		#include <psapi.h>
	#endif
#else
	#ifdef VOICETYPER_PERF
		#include <unistd.h>
	#endif
#endif

inline std::chrono::steady_clock::time_point &
perf_clock_origin()
{
	static std::chrono::steady_clock::time_point Origin = std::chrono::steady_clock::now();
	return Origin;
}

inline int64_t
perf_now_ns()
{
	auto Now = std::chrono::steady_clock::now();
	return std::chrono::duration_cast<std::chrono::nanoseconds>(Now - perf_clock_origin()).count();
}

inline double
perf_now_ms()
{
	return (double)perf_now_ns() / 1000000.0;
}

struct PerfMemorySnapshot
{
	uint64_t WorkingSetBytes;
	uint64_t PrivateBytes;
};

struct PerfCpuSnapshot
{
	double UserMs;
	double KernelMs;

	double TotalMs() const { return UserMs + KernelMs; }
};

struct PerfTickStats
{
	int64_t Count;
	double LastMs;
	double MinMs;
	double MaxMs;
	double AvgMs;
	double P50Ms;
	double P95Ms;
	double P99Ms;
};

#ifdef VOICETYPER_PERF

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

struct PerfEvent
{
	double TimeMs;
	const char *Name;
	double DurationMs;
	double CpuTotalMs;
	double CpuDeltaMs;
	uint64_t WorkingSetBytes;
	uint64_t PrivateBytes;
};

struct PerfSample
{
	double TimeMs;
	double CpuTotalMs;
	float CpuPct;
	uint64_t WorkingSetBytes;
	uint64_t PrivateBytes;
};

inline constexpr int PERF_EVENT_CAPACITY   = 4096;
inline constexpr int PERF_SAMPLE_CAPACITY  = 8192;
inline constexpr int PERF_REPORT_FLUSH_SEC = 5;
inline constexpr int PERF_TICK_HIST_BUCKETS = 65;

struct PerfState
{
	std::mutex Mutex;

	PerfEvent Events[PERF_EVENT_CAPACITY];
	int EventCount;
	uint64_t EventsDropped;

	PerfSample Samples[PERF_SAMPLE_CAPACITY];
	int SampleCount;
	uint64_t SamplesDropped;

	int64_t TickCount;
	double TickLastMs;
	double TickMinMs;
	double TickMaxMs;
	double TickSumMs;
	int64_t TickHistMs[PERF_TICK_HIST_BUCKETS];

	uint64_t PeakWorkingSetBytes;
	uint64_t PeakPrivateBytes;

	double LastSampleCpuTotalMs;
	bool HasLastSampleCpu;

	std::string ReportPath;
	std::atomic<bool> SamplerRunning;
	std::thread SamplerThread;

	PerfState() :
		EventCount(0),
		EventsDropped(0),
		SampleCount(0),
		SamplesDropped(0),
		TickCount(0),
		TickLastMs(0.0),
		TickMinMs(0.0),
		TickMaxMs(0.0),
		TickSumMs(0.0),
		TickHistMs{},
		PeakWorkingSetBytes(0),
		PeakPrivateBytes(0),
		LastSampleCpuTotalMs(0.0),
		HasLastSampleCpu(false),
		SamplerRunning(false)
	{}
};

inline bool
perf_read_process_memory(PerfMemorySnapshot *Out)
{
	if (!Out) return false;

#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS_EX Counters = {};
	Counters.cb = sizeof(Counters);
	if (!K32GetProcessMemoryInfo(GetCurrentProcess(),
		(PROCESS_MEMORY_COUNTERS *)&Counters, sizeof(Counters)))
	{
		return false;
	}

	Out->WorkingSetBytes = (uint64_t)Counters.WorkingSetSize;
	Out->PrivateBytes = (uint64_t)Counters.PrivateUsage;
	return true;
#else
	Out->WorkingSetBytes = 0;
	Out->PrivateBytes = 0;
	return false;
#endif
}

inline bool
perf_read_process_cpu(PerfCpuSnapshot *Out)
{
	if (!Out) return false;

#ifdef _WIN32
	FILETIME CreationTime = {}, ExitTime = {}, KernelTime = {}, UserTime = {};
	if (!GetProcessTimes(GetCurrentProcess(), &CreationTime, &ExitTime, &KernelTime, &UserTime))
	{
		return false;
	}

	ULARGE_INTEGER User = {}, Kernel = {};
	User.LowPart = UserTime.dwLowDateTime;
	User.HighPart = UserTime.dwHighDateTime;
	Kernel.LowPart = KernelTime.dwLowDateTime;
	Kernel.HighPart = KernelTime.dwHighDateTime;

	Out->UserMs = (double)User.QuadPart / 10000.0;
	Out->KernelMs = (double)Kernel.QuadPart / 10000.0;
	return true;
#else
	Out->UserMs = 0.0;
	Out->KernelMs = 0.0;
	return false;
#endif
}

inline int
perf_process_id()
{
#ifdef _WIN32
	return (int)GetCurrentProcessId();
#else
	return (int)getpid();
#endif
}

inline PerfState &
perf_state()
{
	static PerfState State;
	return State;
}

inline double
perf_now_ms_locked(PerfState &State)
{
	auto Now = std::chrono::steady_clock::now();
	return std::chrono::duration<double, std::milli>(Now - perf_clock_origin()).count();
}

inline void
perf_record_event(const char *Name, double DurationMs, double CpuDeltaMs)
{
	PerfState &State = perf_state();

	PerfMemorySnapshot Mem = {0, 0};
	perf_read_process_memory(&Mem);
	PerfCpuSnapshot Cpu = {0.0, 0.0};
	perf_read_process_cpu(&Cpu);

	std::lock_guard<std::mutex> Lock(State.Mutex);
	if (State.EventCount < PERF_EVENT_CAPACITY)
	{
		PerfEvent &Event = State.Events[State.EventCount++];
		Event.TimeMs = perf_now_ms_locked(State);
		Event.Name = Name;
		Event.DurationMs = DurationMs;
		Event.CpuTotalMs = Cpu.TotalMs();
		Event.CpuDeltaMs = CpuDeltaMs;
		Event.WorkingSetBytes = Mem.WorkingSetBytes;
		Event.PrivateBytes = Mem.PrivateBytes;
	}
	else
	{
		State.EventsDropped++;
	}

	if (Mem.WorkingSetBytes > State.PeakWorkingSetBytes) State.PeakWorkingSetBytes = Mem.WorkingSetBytes;
	if (Mem.PrivateBytes > State.PeakPrivateBytes) State.PeakPrivateBytes = Mem.PrivateBytes;
}

inline void
perf_event(const char *Name, double DurationMs = 0.0, double CpuDeltaMs = 0.0)
{
	perf_record_event(Name, DurationMs, CpuDeltaMs);
}

struct PerfSpan
{
	const char *Name;
	std::chrono::steady_clock::time_point Start;
	bool CpuValid;
	PerfCpuSnapshot CpuStart;

	PerfSpan(const char *NameIn) :
		Name(NameIn),
		Start(std::chrono::steady_clock::now()),
		CpuValid(false),
		CpuStart{0.0, 0.0}
	{
		CpuValid = perf_read_process_cpu(&CpuStart);
	}

	~PerfSpan()
	{
		auto End = std::chrono::steady_clock::now();
		double DurationMs = std::chrono::duration<double, std::milli>(End - Start).count();

		double CpuDeltaMs = 0.0;
		if (CpuValid)
		{
			PerfCpuSnapshot CpuEnd = {0.0, 0.0};
			if (perf_read_process_cpu(&CpuEnd))
			{
				CpuDeltaMs = CpuEnd.TotalMs() - CpuStart.TotalMs();
			}
		}

		perf_record_event(Name, DurationMs, CpuDeltaMs);
	}
};

inline void
perf_note_loop_tick(double DeltaMs)
{
	PerfState &State = perf_state();
	std::lock_guard<std::mutex> Lock(State.Mutex);

	if (State.TickCount == 0 || DeltaMs < State.TickMinMs) State.TickMinMs = DeltaMs;
	if (DeltaMs > State.TickMaxMs) State.TickMaxMs = DeltaMs;
	State.TickSumMs += DeltaMs;
	State.TickLastMs = DeltaMs;
	int Bucket = (int)DeltaMs;
	if (Bucket < 0) Bucket = 0;
	if (Bucket >= PERF_TICK_HIST_BUCKETS) Bucket = PERF_TICK_HIST_BUCKETS - 1;
	State.TickHistMs[Bucket]++;
	State.TickCount++;
}

// Nearest-rank percentile over the per-ms tick histogram; Ms is the bucket
// floor, so the result is a slight overestimate at p50 and under at the tail.
inline double
perf_tick_percentile_locked(const PerfState &State, double Percent)
{
	if (State.TickCount <= 0) return 0.0;

	int64_t Rank = (int64_t)(State.TickCount * Percent / 100.0);
	if (Rank < 1) Rank = 1;
	int64_t Seen = 0;
	for (int i = 0; i < PERF_TICK_HIST_BUCKETS; i++)
	{
		Seen += State.TickHistMs[i];
		if (Seen >= Rank)
		{
			bool Saturated = (i == PERF_TICK_HIST_BUCKETS - 1) && State.TickMaxMs > i;
			return Saturated ? State.TickMaxMs : (double)i;
		}
	}
	return State.TickMaxMs;
}

inline bool
perf_get_tick_stats(PerfTickStats *Out)
{
	if (!Out) return false;

	PerfState &State = perf_state();
	std::lock_guard<std::mutex> Lock(State.Mutex);

	Out->Count = State.TickCount;
	Out->LastMs = State.TickLastMs;
	Out->MinMs = State.TickMinMs;
	Out->MaxMs = State.TickMaxMs;
	Out->AvgMs = State.TickCount > 0 ? State.TickSumMs / (double)State.TickCount : 0.0;
	Out->P50Ms = perf_tick_percentile_locked(State, 50.0);
	Out->P95Ms = perf_tick_percentile_locked(State, 95.0);
	Out->P99Ms = perf_tick_percentile_locked(State, 99.0);
	return true;
}

inline void
perf_record_sample(double WallDeltaMs)
{
	PerfState &State = perf_state();

	PerfMemorySnapshot Mem = {0, 0};
	perf_read_process_memory(&Mem);
	PerfCpuSnapshot Cpu = {0.0, 0.0};
	perf_read_process_cpu(&Cpu);

	std::lock_guard<std::mutex> Lock(State.Mutex);

	float CpuPct = 0.0f;
	if (State.HasLastSampleCpu && WallDeltaMs > 0.0001)
	{
		CpuPct = (float)((Cpu.TotalMs() - State.LastSampleCpuTotalMs) / WallDeltaMs * 100.0);
	}
	State.LastSampleCpuTotalMs = Cpu.TotalMs();
	State.HasLastSampleCpu = true;

	if (State.SampleCount < PERF_SAMPLE_CAPACITY)
	{
		PerfSample &Sample = State.Samples[State.SampleCount++];
		Sample.TimeMs = perf_now_ms_locked(State);
		Sample.CpuTotalMs = Cpu.TotalMs();
		Sample.CpuPct = CpuPct;
		Sample.WorkingSetBytes = Mem.WorkingSetBytes;
		Sample.PrivateBytes = Mem.PrivateBytes;
	}
	else
	{
		State.SamplesDropped++;
	}

	if (Mem.WorkingSetBytes > State.PeakWorkingSetBytes) State.PeakWorkingSetBytes = Mem.WorkingSetBytes;
	if (Mem.PrivateBytes > State.PeakPrivateBytes) State.PeakPrivateBytes = Mem.PrivateBytes;
}

inline void
perf_write_report()
{
	PerfState &State = perf_state();
	if (State.ReportPath.empty()) return;

	std::string Buffer;
	Buffer.reserve(64 * 1024);

	char Line[512];

	{
		std::lock_guard<std::mutex> Lock(State.Mutex);

		PerfCpuSnapshot Cpu = {0.0, 0.0};
		perf_read_process_cpu(&Cpu);
		PerfMemorySnapshot Mem = {0, 0};
		perf_read_process_memory(&Mem);

		snprintf(Line, sizeof(Line),
			"{\"version\":1,\"pid\":%d,"
			"\"uptime_ms\":%.3f,"
			"\"cpu_user_ms\":%.3f,\"cpu_kernel_ms\":%.3f,\"cpu_total_ms\":%.3f,"
			"\"mem_working_set_bytes\":%llu,\"mem_private_bytes\":%llu,"
			"\"peak_working_set_bytes\":%llu,\"peak_private_bytes\":%llu,"
			"\"events_dropped\":%llu,\"samples_dropped\":%llu,"
			"\"tick\":{\"count\":%lld,\"last_ms\":%.3f,\"min_ms\":%.3f,\"avg_ms\":%.3f,\"max_ms\":%.3f,"
			"\"p50_ms\":%.0f,\"p95_ms\":%.0f,\"p99_ms\":%.0f},"
			"\"events\":[",
			perf_process_id(),
			perf_now_ms_locked(State),
			Cpu.UserMs, Cpu.KernelMs, Cpu.TotalMs(),
			(unsigned long long)Mem.WorkingSetBytes,
			(unsigned long long)Mem.PrivateBytes,
			(unsigned long long)State.PeakWorkingSetBytes,
			(unsigned long long)State.PeakPrivateBytes,
			(unsigned long long)State.EventsDropped,
			(unsigned long long)State.SamplesDropped,
			(long long)State.TickCount,
			State.TickLastMs, State.TickMinMs,
			State.TickCount > 0 ? State.TickSumMs / (double)State.TickCount : 0.0,
			State.TickMaxMs,
			perf_tick_percentile_locked(State, 50.0),
			perf_tick_percentile_locked(State, 95.0),
			perf_tick_percentile_locked(State, 99.0));
		Buffer += Line;

		for (int i = 0; i < State.EventCount; i++)
		{
			const PerfEvent &Event = State.Events[i];
			snprintf(Line, sizeof(Line),
				"%s{\"t_ms\":%.3f,\"name\":\"%s\",\"dur_ms\":%.3f,\"cpu_ms\":%.3f,\"cpu_delta_ms\":%.3f,"
				"\"ws\":%llu,\"priv\":%llu}",
				i > 0 ? "," : "",
				Event.TimeMs, Event.Name, Event.DurationMs, Event.CpuTotalMs, Event.CpuDeltaMs,
				(unsigned long long)Event.WorkingSetBytes,
				(unsigned long long)Event.PrivateBytes);
			Buffer += Line;
		}

		Buffer += "],\"samples\":[";

		for (int i = 0; i < State.SampleCount; i++)
		{
			const PerfSample &Sample = State.Samples[i];
			snprintf(Line, sizeof(Line),
				"%s{\"t_ms\":%.3f,\"cpu_total_ms\":%.3f,\"cpu_pct\":%.2f,\"ws\":%llu,\"priv\":%llu}",
				i > 0 ? "," : "",
				Sample.TimeMs, Sample.CpuTotalMs, Sample.CpuPct,
				(unsigned long long)Sample.WorkingSetBytes,
				(unsigned long long)Sample.PrivateBytes);
			Buffer += Line;
		}

		Buffer += "]}";
	}

	FILE *File = fopen(State.ReportPath.c_str(), "wb");
	if (!File) return;
	fwrite(Buffer.data(), 1, Buffer.size(), File);
	fclose(File);
}

inline void
perf_sampler_loop(int SamplerHz)
{
	PerfState &State = perf_state();

	const int SampleIntervalMs = 1000 / (SamplerHz > 0 ? SamplerHz : 4);
	int MsUntilFlush = PERF_REPORT_FLUSH_SEC * 1000;
	double LastSampleWallMs = perf_now_ms();

	while (State.SamplerRunning.load())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(SampleIntervalMs));
		if (!State.SamplerRunning.load()) break;

		double NowWallMs = perf_now_ms();
		perf_record_sample(NowWallMs - LastSampleWallMs);
		LastSampleWallMs = NowWallMs;

		MsUntilFlush -= SampleIntervalMs;
		if (MsUntilFlush <= 0)
		{
			MsUntilFlush = PERF_REPORT_FLUSH_SEC * 1000;
			perf_write_report();
		}
	}
}

inline bool
perf_start(const char *ReportDir, int SamplerHz)
{
	PerfState &State = perf_state();

	if (State.SamplerRunning.load()) return true;

	if (ReportDir && ReportDir[0] != '\0')
	{
		char PidBuf[32];
		snprintf(PidBuf, sizeof(PidBuf), "%d", perf_process_id());
		State.ReportPath = std::string(ReportDir) + "/voicetyper-perf-" + PidBuf + ".json";
	}

	if (SamplerHz < 1) SamplerHz = 1;
	if (SamplerHz > 50) SamplerHz = 50;

	State.SamplerRunning.store(true);
	State.SamplerThread = std::thread(perf_sampler_loop, SamplerHz);
	return true;
}

inline void
perf_finish()
{
	PerfState &State = perf_state();

	if (State.SamplerRunning.load())
	{
		State.SamplerRunning.store(false);
		if (State.SamplerThread.joinable()) State.SamplerThread.join();
	}

	perf_write_report();
}

inline uint64_t
perf_peak_private_bytes()
{
	PerfState &State = perf_state();
	std::lock_guard<std::mutex> Lock(State.Mutex);
	return State.PeakPrivateBytes;
}

inline uint64_t
perf_peak_working_set_bytes()
{
	PerfState &State = perf_state();
	std::lock_guard<std::mutex> Lock(State.Mutex);
	return State.PeakWorkingSetBytes;
}

#else

inline bool
perf_read_process_memory(PerfMemorySnapshot *Out)
{
	if (!Out) return false;
	Out->WorkingSetBytes = 0;
	Out->PrivateBytes = 0;
	return false;
}

inline bool
perf_read_process_cpu(PerfCpuSnapshot *Out)
{
	if (!Out) return false;
	Out->UserMs = 0.0;
	Out->KernelMs = 0.0;
	return false;
}

inline void
perf_event(const char *, double = 0.0, double = 0.0)
{
}

struct PerfSpan
{
	PerfSpan(const char *)
	{
	}
};

inline void
perf_note_loop_tick(double)
{
}

inline bool
perf_get_tick_stats(PerfTickStats *)
{
	return false;
}

inline bool
perf_start(const char *, int)
{
	return false;
}

inline void
perf_finish()
{
}

inline uint64_t
perf_peak_private_bytes()
{
	return 0;
}

inline uint64_t
perf_peak_working_set_bytes()
{
	return 0;
}

#endif
