// Copyright (c) RAMMP. All rights reserved.

#include "Modules/ModuleManager.h"
#include "RammsCrowdEditorLog.h"

DEFINE_LOG_CATEGORY(LogRammsCrowdEditor);

class FRammsCrowdEditorModule : public IModuleInterface
{
};

IMPLEMENT_MODULE(FRammsCrowdEditorModule, RammsCrowdEditor)
