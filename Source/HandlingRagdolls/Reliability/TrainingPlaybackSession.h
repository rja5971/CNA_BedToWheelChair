#pragma once

#include "CoreMinimal.h"

/** Decoder-independent playback/recovery policy, also exercised by automation tests. */
struct FTrainingPlaybackSession
{
	enum class EState { Idle, Opening, Playing, RetryPending, Failed, Completed };
	EState State = EState::Idle;
	int32 AutomaticRetries = 0;
	double StartedAt = 0;
	double LastProgressAt = 0;
	double LastMediaTime = 0;
	bool bHasProgress = false;

	void Start(double Now, bool bNewRequest)
	{
		if (bNewRequest) AutomaticRetries = 0;
		State = EState::Opening;
		StartedAt = LastProgressAt = Now;
		LastMediaTime = 0;
		bHasProgress = false;
	}
	bool Opened(double Now)
	{
		if (State != EState::Opening) return false;
		State = EState::Playing;
		LastProgressAt = Now;
		return true;
	}
	void Progress(double MediaTime, double Now)
	{
		if (State == EState::Playing && MediaTime > LastMediaTime + 0.001)
		{
			LastMediaTime = MediaTime;
			LastProgressAt = Now;
			bHasProgress = true;
		}
	}
	bool TimedOut(double Now) const
	{
		return (State == EState::Opening && Now - StartedAt >= 10.0)
			|| (State == EState::Playing && Now - LastProgressAt >= 5.0);
	}
	bool Fail()
	{
		if (State != EState::Opening && State != EState::Playing) return false;
		State = AutomaticRetries++ == 0 ? EState::RetryPending : EState::Failed;
		return true;
	}
	bool Complete()
	{
		if (State != EState::Playing || !bHasProgress) return false;
		State = EState::Completed;
		return true;
	}
	void Cancel() { State = EState::Idle; }
};
