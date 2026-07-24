// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"

// Shared log category for the RammsCrowdEditor module's scripting utilities
// (defined in RammsCrowdEditorModule.cpp). File-local STATIC definitions
// collide in unity builds, hence the module-level declaration.
DECLARE_LOG_CATEGORY_EXTERN(LogRammsCrowdEditor, Log, All);
