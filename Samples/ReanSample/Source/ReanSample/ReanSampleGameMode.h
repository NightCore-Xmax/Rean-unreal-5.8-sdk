// Copyright (c) REAN Studios. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "ReanSampleGameMode.generated.h"

class UReanSampleWidget;

/** Shows the sample UI and gives it the mouse. */
UCLASS()
class AReanSamplePlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

	UPROPERTY()
	TObjectPtr<UReanSampleWidget> Widget;
};

UCLASS()
class AReanSampleGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AReanSampleGameMode();
};
