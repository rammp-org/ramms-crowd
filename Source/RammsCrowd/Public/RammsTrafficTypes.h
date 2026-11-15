// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "RammsTrafficTypes.generated.h"

class ARammsTrafficVehicleActor;
class UStaticMesh;

/**
 * Lightweight Mass traffic for generated streets: kinematic vehicles that follow ZoneGraph vehicle lanes,
 * keep their distance (IDM), stop at signals and all-way stops, and yield to pedestrians and to the robot.
 * See URammsTrafficVehicleProcessor, ARammsTrafficSpawner, ARammsTrafficSignalClock.
 *
 * Axes: signals run one global phase per travel axis. Axis 0 (X) is traffic travelling along world X, axis 1
 * (Y) along world Y. Pedestrians "walking along X" cross the roads that run along Y.
 */

/** A vehicle signal's light for one travel axis. */
UENUM(BlueprintType)
enum class ERammsSignalLight : uint8
{
	Red,
	Amber,
	Green,
};

/** A pedestrian signal. */
UENUM(BlueprintType)
enum class ERammsWalkSignal : uint8
{
	DontWalk,
	/** Clearance: pedestrians already crossing finish, nobody starts. */
	FlashingDontWalk,
	Walk,
};

/** How an intersection lane turns (right-hand traffic: right turns are the short ones). */
UENUM(BlueprintType)
enum class ERammsTrafficTurn : uint8
{
	Straight,
	Right,
	Left,
};

/** What limits a vehicle's acceleration, or keeps it out of the intersection ahead (ramms.Traffic.Debug / Dump). */
UENUM()
enum class ERammsTrafficYield : uint8
{
	None,
	/** The car ahead (its lane, its next lanes, or a lane diverging from its own that it is still beside). */
	Leader,
	/** Red, or amber it can stop for. */
	Signal,
	/** All-way stop: standing its second at the line, or another car arrived first. */
	StopSign,
	/** A car on a crossing, merging or too-close lane is in the intersection or has been let in. */
	Claim,
	/** No room on its exit beyond the queue there (it would block the box). */
	Box,
	/** Permitted left turn: oncoming straight or right-turning traffic first. */
	OncomingLeft,
	Pedestrian,
	Robot,
	Turn,
};

/**
 * The signal cycle every signalized intersection shares (one global phase): X green, X amber, all red, Y green,
 * Y amber, all red. Pedestrians walking parallel to the green traffic get Walk for the start of that green and
 * Flashing Don't Walk for its last PedestrianClearanceSeconds; turning vehicles yield to them.
 */
USTRUCT(BlueprintType)
struct RAMMSCROWD_API FRammsSignalTiming
{
	GENERATED_BODY()

	/** Green for traffic travelling along world X, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "1.0", Units = "s"))
	float GreenSecondsX = 20.0f;

	/** Green for traffic travelling along world Y, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "1.0", Units = "s"))
	float GreenSecondsY = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "0.0", Units = "s"))
	float AmberSeconds = 3.5f;

	/** All red between the two axes' greens, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "0.0", Units = "s"))
	float AllRedSeconds = 1.5f;

	/** The last part of each green that shows Flashing Don't Walk to the parallel crossings, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "0.0", Units = "s"))
	float PedestrianClearanceSeconds = 8.0f;

	/** Shifts the whole cycle (world time + offset), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (Units = "s"))
	float OffsetSeconds = 0.0f;

	/** Length of one full cycle, s. */
	double GetCycleSeconds() const;
};

/** The signals at one moment. */
USTRUCT(BlueprintType)
struct RAMMSCROWD_API FRammsSignalState
{
	GENERATED_BODY()

	/** Traffic travelling along world X. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ramms|Traffic")
	ERammsSignalLight LightX = ERammsSignalLight::Green;

	/** Traffic travelling along world Y. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ramms|Traffic")
	ERammsSignalLight LightY = ERammsSignalLight::Red;

	/** Pedestrians walking along world X (crossing the roads that run along Y). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ramms|Traffic")
	ERammsWalkSignal WalkAlongX = ERammsWalkSignal::Walk;

	/** Pedestrians walking along world Y (crossing the roads that run along X). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ramms|Traffic")
	ERammsWalkSignal WalkAlongY = ERammsWalkSignal::DontWalk;

	/** Seconds until LightX changes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ramms|Traffic")
	float SecondsToChangeX = 0.0f;

	/** Seconds until LightY changes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ramms|Traffic")
	float SecondsToChangeY = 0.0f;

	ERammsSignalLight GetLight(const int32 Axis) const { return Axis == 0 ? LightX : LightY; }
	ERammsWalkSignal GetWalk(const int32 Axis) const { return Axis == 0 ? WalkAlongX : WalkAlongY; }
};

/** Evaluates the shared signal cycle. Pure: no world needed (tested in Ramms.Crowd.Traffic). */
struct RAMMSCROWD_API FRammsSignalCycle
{
	static FRammsSignalState Evaluate(const FRammsSignalTiming& Timing, double TimeSeconds);
};

/** One car the spawner can use: a static mesh with the palette's placement (the level builder copies the palette's Car slot). */
USTRUCT(BlueprintType)
struct RAMMSCROWD_API FRammsTrafficCarMesh
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	/** Relative pick weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Length (X, along the car's front) and width (Y), cm, at Scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	FVector2D Footprint = FVector2D(450.0, 190.0);

	/** Height, cm, at Scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	float Height = 150.0f;

	/** The mesh's placement in the car's facing frame (front +X, base centre on the ground): the palette entry's
	 *  LocationOffset / RotationOffset / Scale, as RammsWorldGen places parked cars. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	FRotator RotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "0.01"))
	float Scale = 1.0f;
};

/**
 * How the traffic drives (shared by all vehicles of a spawner). Distances in cm, speeds in cm/s,
 * accelerations in cm/s^2. Car following is the Intelligent Driver Model.
 */
USTRUCT(BlueprintType)
struct RAMMSCROWD_API FRammsTrafficDrivingParams : public FMassConstSharedFragment
{
	GENERATED_BODY()

	/** Speed limit (1000 cm/s = 36 km/h); each driver keeps its own desired speed around it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "100.0", Units = "cm/s"))
	float SpeedLimit = 1000.0f;

	/** Drivers' desired speeds spread +-this share around the limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float SpeedVariation = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|IDM", meta = (ClampMin = "10.0"))
	float MaxAcceleration = 150.0f;

	/** IDM comfortable deceleration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|IDM", meta = (ClampMin = "10.0"))
	float ComfortDeceleration = 250.0f;

	/** Hardest braking the model ever applies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|IDM", meta = (ClampMin = "100.0"))
	float MaxDeceleration = 800.0f;

	/** IDM time headway, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|IDM", meta = (ClampMin = "0.1", Units = "s"))
	float TimeHeadway = 1.4f;

	/** Bumper-to-bumper gap kept to a stopped leader. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|IDM", meta = (ClampMin = "0.0", Units = "cm"))
	float MinGap = 250.0f;

	/** The front bumper stops this far short of a stop line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Rules", meta = (ClampMin = "0.0", Units = "cm"))
	float StopLineGap = 40.0f;

	/** On amber a vehicle stops when this deceleration (or less) does it; otherwise it goes through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Rules", meta = (ClampMin = "10.0"))
	float AmberStopDeceleration = 350.0f;

	/** Speed through a right / left turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Rules", meta = (ClampMin = "50.0", Units = "cm/s"))
	float RightTurnSpeed = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Rules", meta = (ClampMin = "50.0", Units = "cm/s"))
	float LeftTurnSpeed = 650.0f;

	/** All-way stop: how long a vehicle stands at the line before it may go (in arrival order). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Rules", meta = (ClampMin = "0.0", Units = "s"))
	float AllWayStopSeconds = 1.0f;

	/** Permitted left turn on green: oncoming vehicles closer than this many seconds to their line are waited for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Rules", meta = (ClampMin = "0.0", Units = "s"))
	float LeftTurnGapSeconds = 4.5f;

	/** Deadlock breaker: a vehicle that has stood this long in all at its stop line held by other vehicles' rules
	 *  (claims, arrival order, the box, oncoming traffic; time at a red light neither counts nor resets it) enters
	 *  as soon as no vehicle on a conflicting lane is physically in the intersection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Rules", meta = (ClampMin = "1.0", Units = "s"))
	float MaxWaitSeconds = 20.0f;

	/** Pedestrians do not step onto a crossing a vehicle will be over within this many seconds (it is in or let into
	 *  the intersection, or driving on), nor onto one under a vehicle; at least 3 m, at most 12 m ahead of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Yield", meta = (ClampMin = "0.0", Units = "s"))
	float CrossingHoldSeconds = 2.0f;

	/** Pedestrians and the robot count as in the vehicle's way within its half width plus this, either side of its path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Yield", meta = (ClampMin = "0.0", Units = "cm"))
	float CorridorMargin = 60.0f;

	/** Pedestrians and the robot are also checked where they will be this many seconds ahead (about to step in). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Yield", meta = (ClampMin = "0.0", Units = "s"))
	float PredictSeconds = 1.5f;

	/** Bumper gap kept to a pedestrian or the robot in the way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Yield", meta = (ClampMin = "0.0", Units = "cm"))
	float ObstacleGap = 250.0f;

	/** The path ahead is checked for pedestrians and the robot over speed x this, s ... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Yield", meta = (ClampMin = "0.5", Units = "s"))
	float LookaheadSeconds = 3.0f;

	/** ... and at least this far. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Yield", meta = (ClampMin = "100.0", Units = "cm"))
	float MinLookahead = 1500.0f;
};

/** A traffic vehicle's state on the lane network (URammsTrafficSubsystem lane indices). */
USTRUCT()
struct RAMMSCROWD_API FRammsTrafficVehicleFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Current lane and the one it will take next (network indices; INDEX_NONE = to be chosen / respawn). */
	int32 Lane = INDEX_NONE;
	int32 NextLane = INDEX_NONE;
	/** URammsTrafficSubsystem network version the indices belong to. */
	int32 NetworkVersion = INDEX_NONE;
	/** The vehicle centre's distance along Lane, cm. */
	float Distance = 0.0f;
	float Speed = 0.0f;
	float DesiredSpeed = 1000.0f;
	float Acceleration = 0.0f;
	float HalfLength = 225.0f;
	float HalfWidth = 95.0f;
	/** All-way stop: seconds stood at the line, and when it got there (world time; -1 = not yet). */
	float StoppedAtLine = 0.0f;
	float ArrivalTime = -1.0f;
	/** Seconds stood at the stop line held by other vehicles' rules (see MaxWaitSeconds). */
	float BlockedTime = 0.0f;
	/** Deterministic per-vehicle random stream (route choices). */
	uint32 RandomState = 1;
	/** Index into the spawner's car meshes. */
	int32 Visual = INDEX_NONE;
	/** The intersection lane it just left, while its rear is still in the intersection (it keeps that claim). */
	int32 PrevLane = INDEX_NONE;
	/** May enter the intersection ahead (all-way stop turn taken, or signal and gaps checked). */
	bool bGranted = false;
	/** A permitted left turn that waited at the line for oncoming traffic this green: it may go on the amber. */
	bool bLeftWaiting = false;
	/** Moved discontinuously this frame (spawned, respawned): the representation teleports. */
	bool bTeleport = true;
	/** What limited its acceleration this frame, and the entry decision for the intersection ahead. */
	ERammsTrafficYield Yield = ERammsTrafficYield::None;
	ERammsTrafficYield Entry = ERammsTrafficYield::None;
	/** The vehicle behind what limits it (this frame's traffic index; INDEX_NONE), the one it waits for to enter, and
	 *  the gap to what limits it, cm. */
	int32 Blocker = INDEX_NONE;
	int32 EntryBlocker = INDEX_NONE;
	float LimitGap = 0.0f;
	FVector Velocity = FVector::ZeroVector;
};

/** Where a vehicle is drawn: its representation actor, and when that actor collides. */
USTRUCT()
struct RAMMSCROWD_API FRammsTrafficVisualFragment : public FMassFragment
{
	GENERATED_BODY()

	TWeakObjectPtr<ARammsTrafficVehicleActor> Actor;
	/** The actor collides (Chaos, CPU traces) within this distance of a player pawn; 0 = always. */
	float CollisionRadius = 6000.0f;
	bool bCollisionOn = true;
	/** Ground traces for pitch and height within this distance of a player pawn (else lane height). */
	float GroundTraceRadius = 6000.0f;
};

USTRUCT()
struct RAMMSCROWD_API FRammsTrafficVehicleTag : public FMassTag
{
	GENERATED_BODY()
};

/** IDM parameters in the units the functions below use (cm, s). */
struct RAMMSCROWD_API FRammsIdm
{
	double DesiredSpeed = 1000.0;
	double MaxAcceleration = 150.0;
	double ComfortDeceleration = 250.0;
	double TimeHeadway = 1.4;
	double MinGap = 250.0;
	double Exponent = 4.0;
};

/** The traffic's driving rules as pure functions (tested in Ramms.Crowd.Traffic). */
namespace RammsTraffic
{
	/** IDM acceleration on a free road. */
	RAMMSCROWD_API double IdmFree(double Speed, const FRammsIdm& Idm);

	/** IDM acceleration behind a leader: Gap bumper to bumper (cm, <= 0 touching), LeaderSpeed cm/s. A stop line is a leader at speed 0. */
	RAMMSCROWD_API double IdmFollow(double Speed, double Gap, double LeaderSpeed, const FRammsIdm& Idm);

	/**
	 * Whether a vehicle must stop at a stop line for this light. DistanceToLine: front bumper to its stopping
	 * point (cm; < 0 = already past it: never). Green: no. Amber: when it can stop with StopDeceleration or less
	 * (else it goes through). Red: unless it can no longer stop even with MaxDeceleration (it is committed).
	 */
	RAMMSCROWD_API bool MustStopForSignal(ERammsSignalLight Light, double Speed, double DistanceToLine, double StopDeceleration, double MaxDeceleration);

	/** Deceleration (<= 0) that brings Speed down to CapSpeed over Distance, or +inf when it is already at or under the cap. */
	RAMMSCROWD_API double SlowToCap(double Speed, double CapSpeed, double Distance);

	/**
	 * One step: speed += Accel * Dt, clamped to [0, MaxSpeed]; returns the distance travelled (speed after the
	 * step x Dt). With StopDistance >= 0 the vehicle never travels past it, and stops there.
	 */
	RAMMSCROWD_API double Advance(double& InOutSpeed, double Accel, double DeltaSeconds, double MaxSpeed, double StopDistance);

	/** Straight / right / left from a lane's start and end directions (right-hand: +Y is right of +X). */
	RAMMSCROWD_API ERammsTrafficTurn ClassifyTurn(const FVector2D& StartDirection, const FVector2D& EndDirection);

	/** 0 when Direction runs nearer world X than world Y, else 1. */
	RAMMSCROWD_API int32 TravelAxis(const FVector2D& Direction);

	/** Deterministic random stream: next value in [0, 1). */
	RAMMSCROWD_API float RandomFloat(uint32& InOutState);

	/** The material parameter collection scalars the signal clock drives, with their resting values (the look of a
	 *  level without a clock: X traffic green, Y red). Green/Amber/RedX: vehicle heads facing along world X.
	 *  Walk/HandX: pedestrian heads on those poles (they face across the road their heads control, so they serve
	 *  people walking along Y). */
	RAMMSCROWD_API TArray<TPair<FName, float>> GetSignalParameterDefaults();
}
