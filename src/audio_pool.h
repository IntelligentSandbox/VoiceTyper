#pragma once

#include "build_time_constants.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>

// ---------------------------------------------------------------------------
// Audio input memory pool
//
// Capture audio accumulates into AudioClip block chains backed by a shared
// AudioBlockPool. Blocks are fixed-size (AUDIO_POOL_BLOCK_SAMPLES), acquired
// from a LIFO free list and returned via pool_release_chain / clip_release, so
// steady-state capture performs no heap allocations and never realloc-copies
// already-captured audio. Blocks are write-before-read: samples are never
// zeroed on reuse.
//
// Locking: the pool mutex is only ever allowed to nest inside the caller's
// audio-buffer mutex (clip_append path). clip_release / pool_release_chain
// take the pool mutex alone and must be called after dropping the audio
// buffer mutex.
// ---------------------------------------------------------------------------

struct AudioBlock
{
	AudioBlock *Next;
	int Used;
	float Samples[AUDIO_POOL_BLOCK_SAMPLES];
};

struct AudioBlockPool
{
	std::mutex Mutex;
	AudioBlock *FreeHead;
	int FreeCount;

	AudioBlockPool() : FreeHead(nullptr), FreeCount(0) {}

	AudioBlockPool(const AudioBlockPool &) = delete;
	AudioBlockPool &operator=(const AudioBlockPool &) = delete;
};

static AudioBlock *
pool_new_block()
{
	AudioBlock *Block = new (std::nothrow) AudioBlock;
	if (Block)
	{
		Block->Next = nullptr;
		Block->Used = 0;
	}
	else
	{
		printf("[audio_pool] ERROR: failed to allocate audio block (%lld bytes)\n",
			(long long)sizeof(AudioBlock));
	}
	return Block;
}

static void
pool_free_list_push(AudioBlockPool *Pool, AudioBlock *Block)
{
	Block->Next = Pool->FreeHead;
	Pool->FreeHead = Block;
	Pool->FreeCount++;
}

static void
pool_init(AudioBlockPool *Pool)
{
	std::lock_guard<std::mutex> Lock(Pool->Mutex);
	for (int i = 0; i < AUDIO_POOL_INITIAL_BLOCKS; i++)
	{
		AudioBlock *Block = pool_new_block();
		if (!Block) break;
		pool_free_list_push(Pool, Block);
	}
}

static AudioBlock *
pool_acquire(AudioBlockPool *Pool)
{
	std::lock_guard<std::mutex> Lock(Pool->Mutex);

	if (Pool->FreeCount <= AUDIO_POOL_REFILL_LOW_WATER)
	{
		for (int i = 0; i < AUDIO_POOL_REFILL_BATCH_BLOCKS; i++)
		{
			AudioBlock *Block = pool_new_block();
			if (!Block) break;
			pool_free_list_push(Pool, Block);
		}
	}

	AudioBlock *Block = Pool->FreeHead;
	if (Block)
	{
		Pool->FreeHead = Block->Next;
		Pool->FreeCount--;
		Block->Next = nullptr;
	}
	return Block;
}

static void
pool_release_chain(AudioBlockPool *Pool, AudioBlock *Head)
{
	if (!Head) return;

	std::lock_guard<std::mutex> Lock(Pool->Mutex);
	while (Head)
	{
		AudioBlock *Next = Head->Next;
		pool_free_list_push(Pool, Head);
		Head = Next;
	}
}

struct AudioClip
{
	AudioBlock *Head;
	AudioBlock *Tail;
	int TailUsed;
	int TotalSamples;

	AudioClip() : Head(nullptr), Tail(nullptr), TailUsed(0), TotalSamples(0) {}
};

static void
clip_append(AudioBlockPool *Pool, AudioClip *Clip, const float *Samples, int Count)
{
	while (Count > 0)
	{
		if (!Clip->Tail || Clip->TailUsed == AUDIO_POOL_BLOCK_SAMPLES)
		{
			AudioBlock *Block = pool_acquire(Pool);
			if (!Block) return;

			Block->Next = nullptr;
			Block->Used = 0;
			if (Clip->Tail) Clip->Tail->Next = Block;
			else Clip->Head = Block;
			Clip->Tail = Block;
			Clip->TailUsed = 0;
		}

		int Space = AUDIO_POOL_BLOCK_SAMPLES - Clip->TailUsed;
		int CopyCount = Count < Space ? Count : Space;
		memcpy(Clip->Tail->Samples + Clip->TailUsed, Samples, (size_t)CopyCount * sizeof(float));
		Clip->TailUsed += CopyCount;
		Clip->Tail->Used = Clip->TailUsed;
		Clip->TotalSamples += CopyCount;
		Samples += CopyCount;
		Count -= CopyCount;
	}
}

static int
clip_read_last_n(const AudioClip *Clip, float *Out, int MaxCount)
{
	int Want = MaxCount < Clip->TotalSamples ? MaxCount : Clip->TotalSamples;
	if (Want <= 0) return 0;

	int Skip = Clip->TotalSamples - Want;
	int Written = 0;
	for (AudioBlock *Block = Clip->Head; Block && Written < Want; Block = Block->Next)
	{
		int BlockCount = Block->Used;
		if (Skip >= BlockCount)
		{
			Skip -= BlockCount;
			continue;
		}

		int Take = BlockCount - Skip;
		if (Take > Want - Written) Take = Want - Written;
		memcpy(Out + Written, Block->Samples + Skip, (size_t)Take * sizeof(float));
		Written += Take;
		Skip = 0;
	}

	return Written;
}

static void
clip_release(AudioBlockPool *Pool, AudioClip *Clip)
{
	pool_release_chain(Pool, Clip->Head);
	Clip->Head = nullptr;
	Clip->Tail = nullptr;
	Clip->TailUsed = 0;
	Clip->TotalSamples = 0;
}

static int
clip_duration_ms(const AudioClip *Clip)
{
	return (int)((int64_t)Clip->TotalSamples * 1000 / AUDIO_CAPTURE_SAMPLE_RATE);
}

// Gather a clip into one contiguous buffer for whisper input. Staging is a
// persistent, grow-only buffer: capacity expands geometrically
// (max(2*cap, needed)) and never shrinks. Returns the sample count; samples
// live at Staging->data() and belong to the caller only until the next call.
static int
staging_gather(std::vector<float> *Staging, const AudioClip *Clip)
{
	int Needed = Clip->TotalSamples;
	if (Needed <= 0) return 0;

	size_t Capacity = Staging->capacity();
	if ((size_t)Needed > Capacity)
	{
		size_t Grow = Capacity * 2;
		if ((size_t)Needed > Grow) Grow = (size_t)Needed;
		Staging->reserve(Grow);
	}

	float *Out = Staging->data();
	int Written = 0;
	for (AudioBlock *Block = Clip->Head; Block && Written < Needed; Block = Block->Next)
	{
		int BlockCount = Block->Used;
		int Remaining = Needed - Written;
		if (BlockCount > Remaining) BlockCount = Remaining;
		memcpy(Out + Written, Block->Samples, (size_t)BlockCount * sizeof(float));
		Written += BlockCount;
	}

	return Written;
}
