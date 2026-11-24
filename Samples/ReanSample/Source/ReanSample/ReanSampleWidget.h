// Copyright (c) REAN Studios. MIT License.
//
// The REAN SDK sample screen, built in C++ (no widget assets needed):
//   Sign in (launcher code > saved session > device code with QR code)
//   -> friends list with presence -> set activity -> invite a friend
//   -> accept / decline an incoming invite -> sign out.
// The same flow in Blueprints is described in Samples/ReanSample/BLUEPRINT_SAMPLE.md.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ReanTypes.h"
#include "ReanSampleWidget.generated.h"

class UButton;
class UImage;
class UReanSubsystem;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UReanSampleWidget;

/** Holds which friend an "Invite" button is for (dynamic delegates carry no parameters). */
UCLASS()
class UReanSampleInviteButton : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UReanSampleWidget> Owner;

	FString FriendId;
	FString FriendName;

	UFUNCTION()
	void OnClicked();
};

UCLASS(Blueprintable)
class UReanSampleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InviteFriend(const FString& FriendId, const FString& FriendName);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	// UI
	void BuildTree();
	UButton* MakeButton(const FString& Label);
	UTextBlock* MakeText(const FString& Text);
	void SetStatus(const FString& Text);
	void ShowSignedIn(bool bSignedIn);
	void RefreshFriends();
	UReanSubsystem* Rean() const;
	/** Launcher code, then saved session, then (unless bSilent) a device code. */
	void StartSignIn(bool bSilent);
	void SignInWithDeviceCode();
	void OnSignedIn(const FReanError& Error, const FReanUser& User);

	UFUNCTION()
	void OnSignInClicked();
	UFUNCTION()
	void OnCancelClicked();
	UFUNCTION()
	void OnSignOutClicked();
	UFUNCTION()
	void OnRefreshClicked();
	UFUNCTION()
	void OnActivityClicked();
	UFUNCTION()
	void OnAcceptClicked();
	UFUNCTION()
	void OnDeclineClicked();

	// SDK events
	UFUNCTION()
	void HandleInviteReceived(const FReanInvite& Invite);
	UFUNCTION()
	void HandleInviteAnswered(const FReanInviteAnswer& Answer);
	UFUNCTION()
	void HandleSessionChanged(EReanSessionChange Change, const FReanError& Reason);

	void Respond(bool bAccept);

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;
	UPROPERTY()
	TObjectPtr<UTextBlock> UserText;
	UPROPERTY()
	TObjectPtr<UTextBlock> CodeText;
	UPROPERTY()
	TObjectPtr<UImage> QrImage;
	UPROPERTY()
	TObjectPtr<UVerticalBox> SignedOutPanel;
	UPROPERTY()
	TObjectPtr<UVerticalBox> SignedInPanel;
	UPROPERTY()
	TObjectPtr<UVerticalBox> InvitePanel;
	UPROPERTY()
	TObjectPtr<UTextBlock> InviteText;
	UPROPERTY()
	TObjectPtr<UScrollBox> FriendsBox;
	UPROPERTY()
	TObjectPtr<UButton> SignInButton;
	UPROPERTY()
	TObjectPtr<UButton> CancelButton;
	UPROPERTY()
	TObjectPtr<UTextBlock> ActivityLabel;
	UPROPERTY()
	TArray<TObjectPtr<UReanSampleInviteButton>> InviteButtons;

	FReanInvite PendingInvite;
};
