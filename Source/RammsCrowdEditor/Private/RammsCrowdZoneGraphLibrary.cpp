// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdZoneGraphLibrary.h"

#include "ZoneGraphDelegates.h"
#include "ZoneShapeComponent.h"

void URammsCrowdZoneGraphLibrary::UpdateZoneShape(UZoneShapeComponent* ShapeComponent)
{
	if (ShapeComponent != nullptr)
	{
		ShapeComponent->UpdateShape();
	}
}

void URammsCrowdZoneGraphLibrary::RebuildZoneGraph()
{
#if WITH_EDITOR
	UE::ZoneGraphDelegates::OnZoneGraphRequestRebuild.Broadcast();
#endif
}
