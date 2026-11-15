// Copyright (c) RAMMP. All rights reserved.

#include "RammsTrafficProcessors.h"

#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassCrowdSettings.h"
#include "MassCrowdSubsystem.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "MassZoneGraphNavigationFragments.h"
#include "Misc/ScopeExit.h"
#include "RammsCrossingGateProcessor.h"
#include "RammsCrowdAgentTrait.h"
#include "RammsCrowdLog.h"
#include "RammsPlayerAvatarTrait.h"
#include "RammsTrafficRules.h"
#include "RammsTrafficSubsystem.h"
#include "RammsTrafficTypes.h"
#include "RammsTrafficVehicleActor.h"
#include "ZoneGraphSubsystem.h"

namespace
{
	TAutoConsoleVariable<int32> CVarTrafficDebug(
		TEXT("ramms.Traffic.Debug"),
		0,
		TEXT("Draw the Mass traffic: 1 = each vehicle's speed (km/h) and what limits it, 2 = also its corridor ahead."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarTrafficDumpInterval(
		TEXT("ramms.Traffic.DumpInterval"),
		0.0f,
		TEXT("Run ramms.Traffic.Dump every this many seconds of game time (0 = only when asked)."),
		ECVF_Default);

	using FTrafficFrame = RammsTraffic::FTrafficFrame;

	/** Ground height under P (the generated road, kerbs, humps: world static only), or P.Z when nothing is near. */
	float TraceGround(const UWorld* World, const FVector& P)
	{
		FHitResult Hit;
		const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(RammsTrafficGround), true);
		if (World->LineTraceSingleByObjectType(Hit, P + FVector(0.0, 0.0, 80.0), P - FVector(0.0, 0.0, 120.0), Objects, Params)
			&& FMath::Abs(Hit.ImpactPoint.Z - P.Z) < 60.0)
		{
			return static_cast<float>(Hit.ImpactPoint.Z);
		}
		return static_cast<float>(P.Z);
	}

	const TCHAR* YieldName(const ERammsTrafficYield Yield)
	{
		switch (Yield)
		{
		case ERammsTrafficYield::Leader: return TEXT("leader");
		case ERammsTrafficYield::Signal: return TEXT("signal");
		case ERammsTrafficYield::StopSign: return TEXT("stop sign");
		case ERammsTrafficYield::Claim: return TEXT("claim");
		case ERammsTrafficYield::Box: return TEXT("box");
		case ERammsTrafficYield::OncomingLeft: return TEXT("left-yield");
		case ERammsTrafficYield::Pedestrian: return TEXT("pedestrian");
		case ERammsTrafficYield::Robot: return TEXT("robot");
		case ERammsTrafficYield::Turn: return TEXT("turn");
		case ERammsTrafficYield::Queue: return TEXT("queue");
		case ERammsTrafficYield::DeadEnd: return TEXT("dead end");
		default: return TEXT("free");
		}
	}

	const TCHAR* LightName(const ERammsSignalLight Light)
	{
		return Light == ERammsSignalLight::Green ? TEXT("G") : Light == ERammsSignalLight::Amber ? TEXT("A") : TEXT("R");
	}

	const TCHAR* TurnName(const ERammsTrafficTurn Turn)
	{
		return Turn == ERammsTrafficTurn::Left ? TEXT("left") : Turn == ERammsTrafficTurn::Right ? TEXT("right") : TEXT("straight");
	}

	FString LaneName(const FTrafficFrame& F, const int32 Lane)
	{
		if (!F.Net->IsValidLane(Lane))
		{
			return TEXT("none");
		}
		const FRammsTrafficLane& L = F.Net->Lanes[Lane];
		if (L.Zone == INDEX_NONE)
		{
			return FString::Printf(TEXT("L%d road%s"), Lane, L.bApproach ? TEXT(" approach") : TEXT(""));
		}
		return FString::Printf(TEXT("L%d I%d %s axis %s%s"), Lane, L.Zone, TurnName(L.Turn), L.Axis == 0 ? TEXT("X") : TEXT("Y"), L.bSignalized ? TEXT(" signal") : TEXT(" stop"));
	}

	/** ramms.Traffic.Dump: every vehicle and every intersection's claims, to LogRammsCrowd. */
	void DumpTraffic(const FTrafficFrame& F)
	{
		UE_LOG(LogRammsCrowd, Display, TEXT("[Traffic dump] t=%.1f signals %s X=%s (%.1f s) Y=%s (%.1f s) walkAlongX=%d walkAlongY=%d; %d vehicles, %d held crossings"),
			F.Now, F.bHasSignals ? TEXT("on") : TEXT("off (all-way stops)"), LightName(F.Signals.LightX), F.Signals.SecondsToChangeX, LightName(F.Signals.LightY), F.Signals.SecondsToChangeY,
			static_cast<int32>(F.Signals.WalkAlongX), static_cast<int32>(F.Signals.WalkAlongY), F.Vehicles.Num(), F.HeldCrossings.Num());
		for (int32 i = 0; i < F.Vehicles.Num(); ++i)
		{
			const FRammsTrafficVehicleFragment& V = *F.Vehicles[i].V;
			if (!F.Net->IsValidLane(V.Lane))
			{
				UE_LOG(LogRammsCrowd, Display, TEXT("  %s: off the network"), *F.VehicleName(i));
				continue;
			}
			const FVector Location = F.Vehicles[i].Transform->GetLocation();
			const int32 Zone = F.ApproachZone(V);
			FString Waits = TEXT("-");
			if (Zone != INDEX_NONE)
			{
				const FRammsTrafficDrivingParams& P = *F.Vehicles[i].P;
				const FRammsTrafficLane& Into = F.Net->Lanes[V.NextLane];
				const FString Detail = V.Entry == ERammsTrafficYield::Signal
					? FString::Printf(TEXT(" (%s for axis %s)"), LightName(F.Signals.GetLight(Into.Axis)), Into.Axis == 0 ? TEXT("X") : TEXT("Y"))
					: V.EntryBlocker != INDEX_NONE ? FString::Printf(TEXT(" for %s"), *F.VehicleName(V.EntryBlocker)) : FString();
				Waits = FString::Printf(TEXT("I%d %s: %s%s, to line %.0f cm"), Zone, TurnName(Into.Turn),
					V.Entry == ERammsTrafficYield::None ? (V.bGranted ? TEXT("let in") : TEXT("approaching")) : YieldName(V.Entry),
					*Detail, F.DistanceToLine(V, P));
			}
			FString By = F.VehicleName(V.Blocker);
			if (V.Yield == ERammsTrafficYield::Pedestrian && F.Pedestrians.IsValidIndex(V.BlockerObstacle))
			{
				const RammsTraffic::FObstaclePoint& O = F.Pedestrians[V.BlockerObstacle];
				By = FString::Printf(TEXT("walker %s"), *F.WalkerText(O.Walker, O.Step));
			}
			else if (V.Yield == ERammsTrafficYield::Robot && F.Footprints.IsValidIndex(V.BlockerObstacle))
			{
				const RammsTraffic::FObstacleCapsule& C = F.Footprints[V.BlockerObstacle];
				By = FString::Printf(TEXT("robot footprint %d at (%.0f, %.0f) r %.0f"), V.BlockerObstacle, (C.A.X + C.B.X) * 0.5, (C.A.Y + C.B.Y) * 0.5, C.Radius);
			}
			const FString Walkers = V.WalkersSince >= 0.0f || V.PastStandingWalkers > 0.0f
				? FString::Printf(TEXT(" | for walkers only since t=%.1f, stood %.1f s (for a standing walker %.1f s), past standing walker E%d %.0f cm"),
					V.WalkersSince, V.WalkersWait, V.StuckWalkerWait, V.PastWalker.Index, V.PastStandingWalkers)
				: FString();
			UE_LOG(LogRammsCrowd, Display, TEXT("  %s at (%.0f, %.0f, %.0f) yaw %.0f: %s d=%.0f/%.0f v=%.1f km/h a=%.2f m/s2 | next %s | limit %s gap %.0f cm by %s | entry %s | granted %d leftWaiting %d blocked %.1f s stopped %.1f s arrival %.1f%s"),
				*F.VehicleName(i), Location.X, Location.Y, Location.Z, F.Vehicles[i].Transform->Rotator().Yaw,
				*LaneName(F, V.Lane), V.Distance, F.Net->Lanes[V.Lane].Length, V.Speed * 0.036f, V.Acceleration * 0.01f,
				*LaneName(F, V.NextLane), YieldName(V.Yield), V.LimitGap, *By, *Waits,
				V.bGranted ? 1 : 0, V.bLeftWaiting ? 1 : 0, V.BlockedTime, V.StoppedAtLine, V.ArrivalTime, *Walkers);
		}
		for (int32 Zone = 0; Zone < F.Claims.Num(); ++Zone)
		{
			if (F.Claims[Zone].IsEmpty() && F.Approaching[Zone].IsEmpty())
			{
				continue;
			}
			FString Claims, Approaching;
			for (const RammsTraffic::FClaim& C : F.Claims[Zone])
			{
				Claims += FString::Printf(TEXT(" L%d:%s%s"), C.Lane, *F.VehicleName(C.Vehicle), C.bInside ? (C.bRear ? TEXT("(rear in)") : TEXT("(in)")) : C.bCommitted ? TEXT("(committed)") : TEXT("(let in)"));
			}
			for (const RammsTraffic::FApproach& A : F.Approaching[Zone])
			{
				Approaching += FString::Printf(TEXT(" %s->L%d %s %.0fcm %.0fkm/h%s"), *F.VehicleName(A.Vehicle), A.NextLane, TurnName(F.Net->Lanes[A.NextLane].Turn),
					A.DistanceToLine, A.Speed * 0.036f, A.bCannotStop ? TEXT(" (cannot stop)") : TEXT(""));
			}
			const FVector C = F.Net->Zones[Zone].Center;
			UE_LOG(LogRammsCrowd, Display, TEXT("  I%d at (%.0f, %.0f) %s: claims%s | approaching%s"), Zone, C.X, C.Y,
				F.Net->Zones[Zone].bSignalized ? TEXT("signalized") : TEXT("all-way stop"), Claims.IsEmpty() ? TEXT(" none") : *Claims, Approaching.IsEmpty() ? TEXT(" none") : *Approaching);
		}

		// The street crossings walkers wait at, and the walkers on them who are held or stand still.
		FString Held;
		int32 Listed = 0;
		for (const TPair<FZoneGraphLaneHandle, float>& Pair : F.HeldCrossings)
		{
			if (++Listed > 40)
			{
				Held += TEXT(" ...");
				break;
			}
			Held += Pair.Value <= TNumericLimits<float>::Lowest() ? FString::Printf(TEXT(" %s:all"), *Pair.Key.ToString()) : FString::Printf(TEXT(" %s:after t=%.1f"), *Pair.Key.ToString(), Pair.Value);
		}
		UE_LOG(LogRammsCrowd, Display, TEXT("  held crossings (%d):%s"), F.HeldCrossings.Num(), Held.IsEmpty() ? TEXT(" none") : *Held);
		int32 OnCrossings = 0, HeldWalkers = 0, Standing = 0;
		Listed = 0;
		for (int32 w = 0; w < F.Walkers.Num(); ++w)
		{
			const RammsTraffic::FWalker& W = F.Walkers[w];
			if (W.Crossing == nullptr)
			{
				continue;
			}
			++OnCrossings;
			const bool bStanding = W.Velocity.SizeSquared() < FMath::Square(RammsTraffic::StandingSpeed);
			HeldWalkers += W.bHeld ? 1 : 0;
			Standing += bStanding && !W.bHeld ? 1 : 0;
			if ((W.bHeld || bStanding) && ++Listed <= 40)
			{
				UE_LOG(LogRammsCrowd, Display, TEXT("    walker %s"), *F.WalkerText(w, 0));
			}
		}
		UE_LOG(LogRammsCrowd, Display, TEXT("  walkers: %d, %d on street crossings (%d held at the start, %d standing)%s"),
			F.Walkers.Num(), OnCrossings, HeldWalkers, Standing, Listed > 40 ? TEXT(", first 40 of them listed above") : TEXT(""));
	}

	/** ramms.Traffic.Debug: what limits the car over it, and its corridor ahead (Level 2). */
	void DrawVehicleDebug(const UWorld* World, const FRammsTrafficVehicleFragment& V, const FRammsTrafficDrivingParams& P, const FTransform& Transform, const int32 Level)
	{
#if ENABLE_DRAW_DEBUG
		const FColor Color = V.Yield == ERammsTrafficYield::None ? FColor::Green
			: (V.Yield == ERammsTrafficYield::Pedestrian || V.Yield == ERammsTrafficYield::Robot) ? FColor::Red : FColor::Yellow;
		DrawDebugString(World, Transform.GetLocation() + FVector(0.0, 0.0, 260.0), FString::Printf(TEXT("%.0f km/h %s"), V.Speed * 0.036f, YieldName(V.Yield)), nullptr, Color, 0.0f, true);
		if (Level > 1)
		{
			const FVector Ahead = Transform.GetLocation() + Transform.GetRotation().GetForwardVector() * (V.HalfLength + FMath::Max(P.MinLookahead, V.Speed * P.LookaheadSeconds + 800.0f));
			DrawDebugLine(World, Transform.GetLocation() + FVector(0.0, 0.0, 50.0), Ahead + FVector(0.0, 0.0, 50.0), Color, false, -1.0f, 0, 4.0f);
		}
#endif
	}

	void GatherPlayers(const UWorld& World, TArray<FVector>& OutPlayers)
	{
		for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Controller = It->Get();
			if (Controller == nullptr)
			{
				continue;
			}
			if (const APawn* Pawn = Controller->GetPawn())
			{
				OutPlayers.Add(Pawn->GetActorLocation());
			}
			else if (Controller->PlayerCameraManager != nullptr)
			{
				OutPlayers.Add(Controller->PlayerCameraManager->GetCameraLocation());
			}
		}
	}
}

//----------------------------------------------------------------------//
// URammsTrafficVehicleProcessor
//----------------------------------------------------------------------//

URammsTrafficVehicleProcessor::URammsTrafficVehicleProcessor()
	: VehicleQuery(*this)
	, AvatarQuery(*this)
	, CrowdQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	// After the crowd has moved this frame (pedestrian positions are current), before actors follow Mass.
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::UpdateWorldFromMass);
	bRequiresGameThreadExecution = true;
}

void URammsTrafficVehicleProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	VehicleQuery.AddTagRequirement<FRammsTrafficVehicleTag>(EMassFragmentPresence::All);
	VehicleQuery.AddRequirement<FRammsTrafficVehicleFragment>(EMassFragmentAccess::ReadWrite);
	VehicleQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	VehicleQuery.AddRequirement<FRammsTrafficVisualFragment>(EMassFragmentAccess::ReadOnly);
	VehicleQuery.AddConstSharedRequirement<FRammsTrafficDrivingParams>(EMassFragmentPresence::All);

	AvatarQuery.AddTagRequirement<FRammsPlayerAvatarTag>(EMassFragmentPresence::All);
	AvatarQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	AvatarQuery.AddRequirement<FMassAvoidanceColliderFragment>(EMassFragmentAccess::ReadOnly);
	AvatarQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);

	CrowdQuery.AddTagRequirement<FRammsCrowdMemberTag>(EMassFragmentPresence::All);
	CrowdQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	CrowdQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	CrowdQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	CrowdQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	CrowdQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	CrowdQuery.AddRequirement<FRammsCrossingWaitFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
}

void URammsTrafficVehicleProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	URammsTrafficSubsystem* Traffic = UWorld::GetSubsystem<URammsTrafficSubsystem>(World);
	if (World == nullptr || Traffic == nullptr)
	{
		return;
	}
	if (SeenDumpRequests == INDEX_NONE)
	{
		// Requests are counted globally: only the ones made from now on are this world's to serve.
		SeenDumpRequests = URammsTrafficSubsystem::GetDumpRequestSerial();
	}

	// Every frame publishes the crossings walkers wait at, an empty set when there is nothing to drive: a hold must
	// never outlive the traffic that made it (despawned, network gone).
	const double Now = World->GetTimeSeconds();
	TMap<FZoneGraphLaneHandle, float> Held;
	ON_SCOPE_EXIT
	{
		Traffic->SetHeldCrossings(MoveTemp(Held), Now);
	};

	const UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(World);
	const FRammsTrafficNetwork* Net = ZoneGraph != nullptr ? Traffic->GetNetwork() : nullptr;
	if (Net == nullptr)
	{
		return;
	}

	FTrafficFrame F;
	// A hitch (level streaming, a breakpoint) must not fling the traffic.
	F.Begin(*Net, Now, FMath::Min(Context.GetDeltaTimeSeconds(), 0.1f));
	F.bHasSignals = Traffic->GetSignalState(F.Signals);
	GatherPlayers(*World, F.Players);
	F.GroundHeight = [World](const FVector& P) { return TraceGround(World, P); };
	const UMassCrowdSubsystem* Crowd = UWorld::GetSubsystem<UMassCrowdSubsystem>(World);
	F.DescribeLaneSignal = [Crowd, ZoneGraph](const FZoneGraphLaneHandle& Lane) -> FString
	{
		const UMassCrowdSettings* CrowdSettings = GetDefault<UMassCrowdSettings>();
		FZoneGraphTagMask Tags;
		if (CrowdSettings != nullptr && CrowdSettings->CrossingTag.IsValid() && Crowd != nullptr && ZoneGraph->GetLaneTags(Lane, Tags)
			&& Tags.Contains(CrowdSettings->CrossingTag) && Crowd->HasCrowdDataForZoneGraph(Lane.DataHandle))
		{
			return Crowd->GetLaneState(Lane) == ECrowdLaneState::Closed ? TEXT(", signal closed") : TEXT(", signal open");
		}
		return FString();
	};

	// The vehicles (pointers into their chunks: nothing is created or destroyed until the frame is over).
	VehicleQuery.ForEachEntityChunk(Context, [&F](FMassExecutionContext& Ctx)
	{
		const FRammsTrafficDrivingParams& P = Ctx.GetConstSharedFragment<FRammsTrafficDrivingParams>();
		const TArrayView<FRammsTrafficVehicleFragment> VehicleList = Ctx.GetMutableFragmentView<FRammsTrafficVehicleFragment>();
		const TArrayView<FTransformFragment> TransformList = Ctx.GetMutableFragmentView<FTransformFragment>();
		const TConstArrayView<FRammsTrafficVisualFragment> VisualList = Ctx.GetFragmentView<FRammsTrafficVisualFragment>();
		for (FMassExecutionContext::FEntityIterator It = Ctx.CreateEntityIterator(); It; ++It)
		{
			F.AddVehicle(VehicleList[It], TransformList[It].GetMutableTransform(), &VisualList[It], P);
		}
	});
	if (F.Vehicles.IsEmpty())
	{
		return;
	}
	F.GatherTraffic();

	// Robot footprints: the player avatar entities (pills or circles around the robot), now and PredictSeconds ahead.
	AvatarQuery.ForEachEntityChunk(Context, [&F](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FTransformFragment> TransformList = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassAvoidanceColliderFragment> ColliderList = Ctx.GetFragmentView<FMassAvoidanceColliderFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocityList = Ctx.GetFragmentView<FMassVelocityFragment>();
		for (FMassExecutionContext::FEntityIterator It = Ctx.CreateEntityIterator(); It; ++It)
		{
			const FTransform& AvatarTransform = TransformList[It].GetTransform();
			const FVector2D Center(AvatarTransform.GetLocation());
			const FVector2D Forward = FVector2D(AvatarTransform.GetRotation().GetForwardVector()).GetSafeNormal();
			const FMassAvoidanceColliderFragment& Collider = ColliderList[It];
			RammsTraffic::FObstacleCapsule Capsule;
			double HalfLength = 0.0;
			if (Collider.Type == EMassColliderType::Pill)
			{
				const FMassPillCollider Pill = Collider.GetPillCollider();
				Capsule.Radius = Pill.Radius;
				HalfLength = Pill.HalfLength;
			}
			else
			{
				Capsule.Radius = Collider.GetCircleCollider().Radius;
			}
			Capsule.A = Center - Forward * HalfLength;
			Capsule.B = Center + Forward * HalfLength;
			Capsule.Z = static_cast<float>(AvatarTransform.GetLocation().Z);
			F.AddFootprint(Capsule, VelocityList.IsEmpty() ? FVector2D::ZeroVector : FVector2D(VelocityList[It].Value));
		}
	});
	if (F.Footprints.IsEmpty())
	{
		// No avatar entity (a robot without the RAMMS Player Avatar trait): the player pawns' collision bounds.
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APawn* Pawn = It->Get() != nullptr ? It->Get()->GetPawn() : nullptr;
			if (Pawn == nullptr)
			{
				continue;
			}
			FVector Origin, Extent;
			Pawn->GetActorBounds(true, Origin, Extent);
			if (Extent.IsNearlyZero())
			{
				continue;
			}
			RammsTraffic::FObstacleCapsule Capsule;
			const bool bAlongX = Extent.X >= Extent.Y;
			Capsule.Radius = static_cast<float>(FMath::Min(Extent.X, Extent.Y));
			const FVector2D Axis = bAlongX ? FVector2D(Extent.X - Extent.Y, 0.0) : FVector2D(0.0, Extent.Y - Extent.X);
			Capsule.A = FVector2D(Origin) - Axis;
			Capsule.B = FVector2D(Origin) + Axis;
			Capsule.Z = static_cast<float>(Origin.Z - Extent.Z);
			F.AddFootprint(Capsule, FVector2D(Pawn->GetVelocity()));
		}
	}

	// Pedestrians, now and (walking ones) where they will be shortly (about to step off the kerb). Walkers waiting at
	// the kerb (held at a crossing's start, standing off the road part of a street crossing or on any other lane) stop
	// a car only when they are physically in its path (RammsTraffic::IsWalkerWaiting, IsInTheWay).
	CrowdQuery.ForEachEntityChunk(Context, [&F, Net](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FTransformFragment> TransformList = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocityList = Ctx.GetFragmentView<FMassVelocityFragment>();
		const TConstArrayView<FAgentRadiusFragment> RadiusList = Ctx.GetFragmentView<FAgentRadiusFragment>();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneList = Ctx.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TConstArrayView<FMassMoveTargetFragment> MoveTargetList = Ctx.GetFragmentView<FMassMoveTargetFragment>();
		const TConstArrayView<FRammsCrossingWaitFragment> WaitList = Ctx.GetFragmentView<FRammsCrossingWaitFragment>();
		for (FMassExecutionContext::FEntityIterator It = Ctx.CreateEntityIterator(); It; ++It)
		{
			RammsTraffic::FWalker W;
			W.Entity = Ctx.GetEntity(It);
			W.Location = TransformList[It].GetTransform().GetLocation();
			W.Radius = RadiusList.IsEmpty() ? RammsTraffic::DefaultWalkerRadius : RadiusList[It].Radius;
			W.Velocity = VelocityList.IsEmpty() ? FVector2D::ZeroVector : FVector2D(VelocityList[It].Value);
			if (!LaneList.IsEmpty())
			{
				W.Lane = LaneList[It].LaneHandle;
				W.LaneDistance = LaneList[It].DistanceAlongLane;
				W.LaneLength = LaneList[It].LaneLength;
			}
			if (!MoveTargetList.IsEmpty())
			{
				W.DesiredSpeed = MoveTargetList[It].DesiredSpeed.Get();
				W.Action = MoveTargetList[It].GetCurrentAction();
			}
			if (!WaitList.IsEmpty())
			{
				W.bHeld = WaitList[It].bHolding;
				W.LaneSince = WaitList[It].LaneSince;
			}
			W.Crossing = W.Lane.IsValid() ? Net->StreetCrossings.Find(W.Lane) : nullptr;
			F.AddWalker(W);
		}
	});

	// Who may enter which intersection, then drive.
	F.DecideEntries();
	F.Drive();

	const int32 Debug = CVarTrafficDebug.GetValueOnGameThread();
	if (Debug > 0)
	{
		for (const RammsTraffic::FVehicleRef& Ref : F.Vehicles)
		{
			DrawVehicleDebug(World, *Ref.V, *Ref.P, *Ref.Transform, Debug);
		}
	}

	// ramms.Traffic.Dump (and ramms.Traffic.DumpInterval).
	const float Interval = CVarTrafficDumpInterval.GetValueOnGameThread();
	const int32 Requests = URammsTrafficSubsystem::GetDumpRequestSerial();
	if (Requests != SeenDumpRequests || (Interval > 0.0f && F.Now - LastDumpTime >= Interval))
	{
		SeenDumpRequests = Requests;
		LastDumpTime = F.Now;
		DumpTraffic(F);
	}

	Held = MoveTemp(F.HeldCrossings);
}

//----------------------------------------------------------------------//
// URammsTrafficVisualProcessor
//----------------------------------------------------------------------//

URammsTrafficVisualProcessor::URammsTrafficVisualProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::UpdateWorldFromMass;
	bRequiresGameThreadExecution = true;
}

void URammsTrafficVisualProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddTagRequirement<FRammsTrafficVehicleTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FRammsTrafficVehicleFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FRammsTrafficVisualFragment>(EMassFragmentAccess::ReadWrite);
}

void URammsTrafficVisualProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const UWorld* World = Context.GetWorld();
	if (World == nullptr)
	{
		return;
	}
	TArray<FVector> Players;
	GatherPlayers(*World, Players);
	EntityQuery.ForEachEntityChunk(Context, [&Players](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FTransformFragment> TransformList = Ctx.GetFragmentView<FTransformFragment>();
		const TArrayView<FRammsTrafficVehicleFragment> VehicleList = Ctx.GetMutableFragmentView<FRammsTrafficVehicleFragment>();
		const TArrayView<FRammsTrafficVisualFragment> VisualList = Ctx.GetMutableFragmentView<FRammsTrafficVisualFragment>();
		for (FMassExecutionContext::FEntityIterator It = Ctx.CreateEntityIterator(); It; ++It)
		{
			FRammsTrafficVisualFragment& Visual = VisualList[It];
			ARammsTrafficVehicleActor* Actor = Visual.Actor.Get();
			if (Actor == nullptr)
			{
				continue;
			}
			FRammsTrafficVehicleFragment& V = VehicleList[It];
			const FTransform& Transform = TransformList[It].GetTransform();
			if (Visual.CollisionRadius > 0.0f)
			{
				double Nearest = TNumericLimits<double>::Max();
				for (const FVector& Player : Players)
				{
					Nearest = FMath::Min(Nearest, FVector::DistSquared2D(Transform.GetLocation(), Player));
				}
				// 10% hysteresis so a car at the edge does not flicker its physics state.
				const double Radius = Visual.CollisionRadius * (Visual.bCollisionOn ? 1.1 : 1.0);
				Visual.bCollisionOn = Players.IsEmpty() || Nearest < Radius * Radius;
				Actor->SetCollisionActive(Visual.bCollisionOn);
			}
			// Kinematic move: the collision box gets a kinematic target (pushes with the car's real velocity).
			Actor->SetActorLocationAndRotation(Transform.GetLocation(), Transform.GetRotation(), false, nullptr,
				V.bTeleport ? ETeleportType::TeleportPhysics : ETeleportType::None);
			Actor->SetReportedVelocity(V.Velocity);
			V.bTeleport = false;
		}
	});
}
