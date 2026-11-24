// Copyright (c) REAN Studios. MIT License.
#include "ReanSampleGameMode.h"

#include "ReanSampleWidget.h"
#include "Blueprint/UserWidget.h"

AReanSampleGameMode::AReanSampleGameMode()
{
	PlayerControllerClass = AReanSamplePlayerController::StaticClass();
	DefaultPawnClass = nullptr;
}

void AReanSamplePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;
	Widget = CreateWidget<UReanSampleWidget>(this, UReanSampleWidget::StaticClass());
	if (!Widget) return;
	Widget->AddToViewport();
	bShowMouseCursor = true;
	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}
