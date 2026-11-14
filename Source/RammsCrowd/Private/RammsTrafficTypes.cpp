// Copyright (c) RAMMP. All rights reserved.

#include "RammsTrafficTypes.h"

#include "Math/UnrealMathUtility.h"

double FRammsSignalTiming::GetCycleSeconds() const
{
	const double Shared = 2.0 * (FMath::Max(AmberSeconds, 0.0f) + FMath::Max(AllRedSeconds, 0.0f));
	return FMath::Max(GreenSecondsX, 1.0f) + FMath::Max(GreenSecondsY, 1.0f) + Shared;
}

FRammsSignalState FRammsSignalCycle::Evaluate(const FRammsSignalTiming& Timing, const double TimeSeconds)
{
	const double Green[2] = { FMath::Max(Timing.GreenSecondsX, 1.0f), FMath::Max(Timing.GreenSecondsY, 1.0f) };
	const double Amber = FMath::Max(Timing.AmberSeconds, 0.0f);
	const double AllRed = FMath::Max(Timing.AllRedSeconds, 0.0f);
	const double Cycle = Timing.GetCycleSeconds();
	// X's green starts the cycle, Y's starts once X's green, amber and all red are over.
	const double Start[2] = { 0.0, Green[0] + Amber + AllRed };

	double T = FMath::Fmod(TimeSeconds + Timing.OffsetSeconds, Cycle);
	if (T < 0.0)
	{
		T += Cycle;
	}

	FRammsSignalState State;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		// Time since this axis' green began.
		double Local = T - Start[Axis];
		if (Local < 0.0)
		{
			Local += Cycle;
		}
		ERammsSignalLight Light;
		double ToChange;
		if (Local < Green[Axis])
		{
			Light = ERammsSignalLight::Green;
			ToChange = Green[Axis] - Local;
		}
		else if (Local < Green[Axis] + Amber)
		{
			Light = ERammsSignalLight::Amber;
			ToChange = Green[Axis] + Amber - Local;
		}
		else
		{
			Light = ERammsSignalLight::Red;
			ToChange = Cycle - Local;
		}
		// Pedestrians walking parallel to this axis' traffic: Walk, then the clearance before its amber.
		const double Clearance = FMath::Clamp(static_cast<double>(Timing.PedestrianClearanceSeconds), 0.0, Green[Axis]);
		const ERammsWalkSignal Walk = Local < Green[Axis] - Clearance ? ERammsWalkSignal::Walk
			: Local < Green[Axis] ? ERammsWalkSignal::FlashingDontWalk
			: ERammsWalkSignal::DontWalk;
		if (Axis == 0)
		{
			State.LightX = Light;
			State.SecondsToChangeX = static_cast<float>(ToChange);
			State.WalkAlongX = Walk;
		}
		else
		{
			State.LightY = Light;
			State.SecondsToChangeY = static_cast<float>(ToChange);
			State.WalkAlongY = Walk;
		}
	}
	return State;
}

namespace RammsTraffic
{
	double IdmFree(const double Speed, const FRammsIdm& Idm)
	{
		const double V0 = FMath::Max(Idm.DesiredSpeed, 1.0);
		const double Ratio = FMath::Max(Speed, 0.0) / V0;
		return Idm.MaxAcceleration * (1.0 - FMath::Pow(Ratio, Idm.Exponent));
	}

	double IdmFollow(const double Speed, const double Gap, const double LeaderSpeed, const FRammsIdm& Idm)
	{
		const double V = FMath::Max(Speed, 0.0);
		const double Free = IdmFree(V, Idm);
		if (Gap <= 1.0)
		{
			// Touching (or overlapping): brake as hard as the model allows; the caller clamps.
			return -1.0e6;
		}
		const double Ab = FMath::Max(Idm.MaxAcceleration * Idm.ComfortDeceleration, 1.0);
		const double Desired = Idm.MinGap + FMath::Max(0.0, V * Idm.TimeHeadway + V * (V - LeaderSpeed) / (2.0 * FMath::Sqrt(Ab)));
		const double Interaction = Desired / Gap;
		return Free - Idm.MaxAcceleration * Interaction * Interaction;
	}

	bool MustStopForSignal(const ERammsSignalLight Light, const double Speed, const double DistanceToLine, const double StopDeceleration, const double MaxDeceleration)
	{
		if (Light == ERammsSignalLight::Green || DistanceToLine < 0.0)
		{
			return false;
		}
		const double V = FMath::Max(Speed, 0.0);
		const double Decel = Light == ERammsSignalLight::Amber ? StopDeceleration : MaxDeceleration;
		const double StoppingDistance = V * V / (2.0 * FMath::Max(Decel, 1.0));
		return StoppingDistance <= DistanceToLine;
	}

	double SlowToCap(const double Speed, const double CapSpeed, const double Distance)
	{
		if (Speed <= CapSpeed)
		{
			return TNumericLimits<double>::Max();
		}
		return (CapSpeed * CapSpeed - Speed * Speed) / (2.0 * FMath::Max(Distance, 50.0));
	}

	double Advance(double& InOutSpeed, const double Accel, const double DeltaSeconds, const double MaxSpeed, const double StopDistance)
	{
		const double Dt = FMath::Max(DeltaSeconds, 0.0);
		double Speed = FMath::Clamp(InOutSpeed + Accel * Dt, 0.0, FMath::Max(MaxSpeed, 0.0));
		double Travel = Speed * Dt;
		if (StopDistance >= 0.0 && Travel >= StopDistance)
		{
			Travel = StopDistance;
			// At the stop: stand. Short of it (a long frame): arrive at the speed that covers it.
			Speed = (StopDistance < 1.0 || Dt <= UE_SMALL_NUMBER) ? 0.0 : FMath::Min(Speed, StopDistance / Dt);
		}
		InOutSpeed = Speed;
		return Travel;
	}

	ERammsTrafficTurn ClassifyTurn(const FVector2D& StartDirection, const FVector2D& EndDirection)
	{
		const FVector2D A = StartDirection.GetSafeNormal();
		const FVector2D B = EndDirection.GetSafeNormal();
		const double Cross = A.X * B.Y - A.Y * B.X;
		const double Dot = FVector2D::DotProduct(A, B);
		const double Degrees = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
		// Unreal is left-handed with Z up: turning from +X toward +Y is a right turn.
		if (Degrees > 30.0)
		{
			return ERammsTrafficTurn::Right;
		}
		if (Degrees < -30.0)
		{
			return ERammsTrafficTurn::Left;
		}
		return ERammsTrafficTurn::Straight;
	}

	int32 TravelAxis(const FVector2D& Direction)
	{
		return FMath::Abs(Direction.X) >= FMath::Abs(Direction.Y) ? 0 : 1;
	}

	float RandomFloat(uint32& InOutState)
	{
		// xorshift32: never 0 once seeded with a non-zero state.
		uint32 X = InOutState != 0 ? InOutState : 0x9E3779B9u;
		X ^= X << 13;
		X ^= X >> 17;
		X ^= X << 5;
		InOutState = X;
		return static_cast<float>(X >> 8) / static_cast<float>(1u << 24);
	}

	TArray<TPair<FName, float>> GetSignalParameterDefaults()
	{
		TArray<TPair<FName, float>> Out;
		Out.Emplace(FName(TEXT("GreenX")), 1.0f);
		Out.Emplace(FName(TEXT("AmberX")), 0.0f);
		Out.Emplace(FName(TEXT("RedX")), 0.0f);
		Out.Emplace(FName(TEXT("GreenY")), 0.0f);
		Out.Emplace(FName(TEXT("AmberY")), 0.0f);
		Out.Emplace(FName(TEXT("RedY")), 1.0f);
		Out.Emplace(FName(TEXT("WalkX")), 0.0f);
		Out.Emplace(FName(TEXT("HandX")), 1.0f);
		Out.Emplace(FName(TEXT("WalkY")), 1.0f);
		Out.Emplace(FName(TEXT("HandY")), 0.0f);
		return Out;
	}
}
