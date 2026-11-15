// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassNavigationTypes.h"
#include "RammsTrafficSubsystem.h"
#include "RammsTrafficTypes.h"
#include "Templates/Function.h"
#include "ZoneGraphTypes.h"

/**
 * One frame of the Mass traffic as plain data and functions: URammsTrafficVehicleProcessor fills an FTrafficFrame from Mass
 * and the world (vehicles, walkers, robot footprints, signals) and runs it; the Ramms.Crowd.Traffic tests fill one by
 * hand over a hand-made FRammsTrafficNetwork and run the same code. See URammsTrafficVehicleProcessor for the rules.
 */
namespace RammsTraffic
{
	/** Below this a walker is standing; above MovingSpeed it is walking somewhere (and checked where it will be), cm/s. */
	constexpr float StandingSpeed = 25.0f;
	constexpr float MovingSpeed = 50.0f;
	/** A car stopped this long for a walker who stands (held up by others, not crossing) stops holding newcomers at that
	 *  crossing, so the crowd waiting there can move out of the walker's way, s. */
	constexpr float StuckWalkerReleaseSeconds = 2.0f;
	/** Walker radius when a crowd member has no FAgentRadiusFragment (URammsCrowdAgentTrait's default), cm. */
	constexpr float DefaultWalkerRadius = 25.0f;
	/** A car reaching a dead end starts again elsewhere at once when no player is this near (else after standing there
	 *  MaxWaitSeconds), and never closer than this to a player, cm. */
	constexpr double DeadEndRespawnDistance = 4000.0;

	/** One vehicle of this frame: pointers into its chunk (no entity is created or destroyed during the frame). */
	struct FVehicleRef
	{
		FRammsTrafficVehicleFragment* V = nullptr;
		FTransform* Transform = nullptr;
		/** Null in tests. */
		const FRammsTrafficVisualFragment* Visual = nullptr;
		const FRammsTrafficDrivingParams* P = nullptr;
	};

	struct FOccupant
	{
		float Distance = 0.0f;
		float Speed = 0.0f;
		float HalfLength = 0.0f;
		float HalfWidth = 0.0f;
		int32 Vehicle = INDEX_NONE;
	};

	/** A vehicle in an intersection (bInside), or let into it, on Lane. */
	struct FClaim
	{
		int32 Lane = INDEX_NONE;
		int32 Vehicle = INDEX_NONE;
		float HalfLength = 0.0f;
		bool bInside = false;
		/** In, or let in and no longer able to stop at its line (RammsTraffic::IsCommitted): always respected. */
		bool bCommitted = false;
		/** Only its rear is still in the intersection (its front is on the exit already). */
		bool bRear = false;
	};

	/** A vehicle standing at an all-way stop, for NextLane, since Arrival. */
	struct FWaiter
	{
		int32 NextLane = INDEX_NONE;
		float Arrival = 0.0f;
		int32 Vehicle = INDEX_NONE;
	};

	/** A vehicle on its way into an intersection. */
	struct FApproach
	{
		int32 NextLane = INDEX_NONE;
		float DistanceToLine = 0.0f;
		float Speed = 0.0f;
		int32 Vehicle = INDEX_NONE;
		/** It can no longer stop at its line (RammsTraffic::IsCommitted). */
		bool bCannotStop = false;
	};

	/** A crowd member this frame (for the corridors and ramms.Traffic.Dump). */
	struct FWalker
	{
		FMassEntityHandle Entity;
		FVector Location = FVector::ZeroVector;
		FVector2D Velocity = FVector2D::ZeroVector;
		float Radius = DefaultWalkerRadius;
		/** Its ZoneGraph lane (invalid: none), how far along it is, and the lane's length, cm. */
		FZoneGraphLaneHandle Lane;
		float LaneDistance = 0.0f;
		float LaneLength = 0.0f;
		/** Its lane as a street crossing (null on any other lane). */
		const FRammsStreetCrossing* Crossing = nullptr;
		/** Its move target's desired speed (-1: no move target) and action. */
		float DesiredSpeed = -1.0f;
		EMassMovementAction Action = EMassMovementAction::Move;
		/** Held at the start of its crossing (URammsCrossingGateProcessor), and when it came onto its lane. */
		bool bHeld = false;
		float LaneSince = 0.0f;
		/** Waiting, not crossing (RammsTraffic::IsWalkerWaiting): cars stop for it only when it is physically in their path. Set by AddWalker. */
		bool bWaiting = false;
	};

	struct FObstaclePoint
	{
		FVector2D Position = FVector2D::ZeroVector;
		float Radius = 0.0f;
		float Z = 0.0f;
		/** The walker (FTrafficFrame::Walkers), and 0 where it is, else where it will be in Step / 2 of PredictSeconds. */
		int32 Walker = INDEX_NONE;
		uint8 Step = 0;
		bool bWaiting = false;
	};

	/** A robot footprint: a pill from A to B. */
	struct FObstacleCapsule
	{
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		float Radius = 0.0f;
		float Z = 0.0f;
	};

	/** Points on a coarse hash grid: "what is within R of here". */
	struct FPointGrid
	{
		static constexpr double CellSize = 400.0;
		TMap<FIntPoint, TArray<int32, TInlineAllocator<4>>> Cells;

		static FIntPoint CellOf(const FVector2D& P)
		{
			return FIntPoint(FMath::FloorToInt32(P.X / CellSize), FMath::FloorToInt32(P.Y / CellSize));
		}

		void Add(const int32 Index, const FVector2D& P)
		{
			Cells.FindOrAdd(CellOf(P)).Add(Index);
		}

		template <typename FunctionType>
		void ForEachNear(const FVector2D& P, const double Radius, FunctionType&& Function) const
		{
			const FIntPoint Lo = CellOf(P - FVector2D(Radius, Radius));
			const FIntPoint Hi = CellOf(P + FVector2D(Radius, Radius));
			for (int32 X = Lo.X; X <= Hi.X; ++X)
			{
				for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
				{
					if (const TArray<int32, TInlineAllocator<4>>* Cell = Cells.Find(FIntPoint(X, Y)))
					{
						for (const int32 Index : *Cell)
						{
							Function(Index);
						}
					}
				}
			}
		}
	};

	/** The next lane at random (deterministic per vehicle): at an intersection mostly straight on. INDEX_NONE at a dead end. */
	int32 ChooseNext(const FRammsTrafficNetwork& Net, int32 Lane, uint32& RandomState);

	/** The speed through Lane when it is an intersection's turn, else 0 (no cap). */
	float TurnSpeed(const FRammsTrafficLane& Lane, const FRammsTrafficDrivingParams& Params);

	/** Everything one frame of traffic needs to know, and the rules that move it. */
	struct FTrafficFrame
	{
		const FRammsTrafficNetwork* Net = nullptr;
		double Now = 0.0;
		float Dt = 0.0f;
		bool bHasSignals = false;
		FRammsSignalState Signals;

		TArray<FVehicleRef> Vehicles;
		TArray<TArray<FOccupant>> Occupancy;
		TArray<TArray<FClaim>> Claims;
		TArray<TArray<FWaiter>> Waiting;
		TArray<TArray<FApproach>> Approaching;

		TArray<FWalker> Walkers;
		TArray<FObstaclePoint> Pedestrians;
		FPointGrid PedestrianGrid;
		float MaxPedestrianRadius = 0.0f;
		/** The longest PredictSeconds among the vehicles (AddVehicle). */
		float Predict = 0.0f;
		TArray<FObstacleCapsule> Footprints;
		TArray<FVector> Players;
		/** Street crossings walkers wait at, each for those who come onto it at or after the time (see URammsTrafficSubsystem::SetHeldCrossings). */
		TMap<FZoneGraphLaneHandle, float> HeldCrossings;

		/** Optional: the ground height under a point near a player (line traces); unset, cars follow the lanes' heights. */
		TFunction<float(const FVector&)> GroundHeight;
		/** Optional: a vehicle's name for the log (default: its actor's name). */
		TFunction<FString(int32)> NameVehicle;
		/** Optional: a walker's lane as the crowd's signal sees it, for the log (", signal open"). */
		TFunction<FString(const FZoneGraphLaneHandle&)> DescribeLaneSignal;

		/** Starts a frame over Network at world time InNow, stepping InDt seconds. */
		void Begin(const FRammsTrafficNetwork& Network, double InNow, float InDt);
		int32 AddVehicle(FRammsTrafficVehicleFragment& Vehicle, FTransform& Transform, const FRammsTrafficVisualFragment* Visual, const FRammsTrafficDrivingParams& Params);
		/** Lane occupancy, intersection claims, all-way stop arrivals and approaches, from the vehicles added. */
		void GatherTraffic();
		/** A robot footprint, and (moving) where it will be in Predict seconds. Add the vehicles first. */
		void AddFootprint(const FObstacleCapsule& Capsule, const FVector2D& Velocity);
		/** A crowd member (classified waiting or not), with its obstacle points. Returns its index. */
		int32 AddWalker(const FWalker& Walker);
		/** Every approaching vehicle's entry decision: per intersection, protected movements first (nearest the line first), then left turns. */
		void DecideEntries();
		/** Moves every vehicle and collects the crossings walkers wait at (HeldCrossings). */
		void Drive();

		/** Whether vehicle Self may enter the intersection ahead (its NextLane's zone). Returns why not, or None (and it holds a claim). */
		ERammsTrafficYield DecideEntry(int32 Self, int32& OutBlocker);
		void UpdateVehicle(int32 Self);
		void HoldCrossings(int32 Self);

		bool Sample(int32 Lane, float Distance, FVector& OutPosition, FVector& OutDirection) const;
		/** Distance along Lane, carried on into Next and then Next's only exit; OutLane is the lane sampled. */
		bool SampleAhead(int32 Lane, int32 Next, float Distance, FVector& OutPosition, FVector& OutDirection, int32& OutLane) const;
		/** The intersection the vehicle's lane leads into (its next lane's zone, when that is not its own lane's), else INDEX_NONE. */
		int32 ApproachZone(const FRammsTrafficVehicleFragment& V) const;
		/** Front bumper to the stopping point at the end of the vehicle's lane. */
		float DistanceToLine(const FRammsTrafficVehicleFragment& V, const FRammsTrafficDrivingParams& P) const;
		/** The vehicle ahead on Self's lane that has not been let in, or stands (Self queues behind it), else INDEX_NONE. */
		int32 QueuedBehind(int32 Self) const;
		/** The nearest vehicle ahead: on the vehicle's lane, else its next two lanes, or one diverging from them it is still beside. */
		bool FindLeader(const FRammsTrafficVehicleFragment& V, int32 Self, float& OutGap, float& OutSpeed, int32& OutLeader) const;
		/** Distance from the front bumper to the first pedestrian or robot in the vehicle's way ahead, or +inf; and which. */
		float ScanCorridor(const FRammsTrafficVehicleFragment& V, const FRammsTrafficDrivingParams& P, ERammsTrafficYield& OutReason, int32& OutObstacle) const;
		double NearestPlayerDistanceSquared(const FVector& Position) const;

		FString VehicleName(int32 Index) const;
		/** A walker for the log: who, where, its lane and what the crossing gate and its move target say. */
		FString WalkerText(int32 Index, int32 Step) const;

	private:
		void SiblingLeaders(const FRammsTrafficVehicleFragment& V, int32 Self, int32 OnLane, float OffsetToStart, float& InOutGap, float& InOutSpeed, int32& InOutLeader) const;
		void ComputePose(FRammsTrafficVehicleFragment& V, bool bTraceGround, FTransform& OutTransform) const;
		bool Respawn(FRammsTrafficVehicleFragment& V, int32 Self);
		void AdvanceLanes(FRammsTrafficVehicleFragment& V, int32 Self);
		void Hold(const FZoneGraphLaneHandle& Lane, float Since);
	};
}
