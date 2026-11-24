// Copyright (c) REAN Studios. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ReanSecretSave.generated.h"

/** One value of the SaveGame token store (not encrypted). */
UCLASS()
class UReanSecretSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString Value;
};
