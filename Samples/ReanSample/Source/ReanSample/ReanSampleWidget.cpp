// Copyright (c) REAN Studios. MIT License.
#include "ReanSampleWidget.h"

#include "ReanBlueprintLibrary.h"
#include "ReanSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"

void UReanSampleInviteButton::OnClicked()
{
	if (Owner) Owner->InviteFriend(FriendId, FriendName);
}

// ─── Layout ─────────────────────────────────────────────────────────────────

TSharedRef<SWidget> UReanSampleWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget) BuildTree();
	return Super::RebuildWidget();
}

UTextBlock* UReanSampleWidget::MakeText(const FString& Text)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Block->SetText(FText::FromString(Text));
	Block->SetAutoWrapText(true);
	return Block;
}

UButton* UReanSampleWidget::MakeButton(const FString& Label)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Label));
	Text->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	Button->AddChild(Text);
	return Button;
}

void UReanSampleWidget::BuildTree()
{
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Background->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.92f));
	Background->SetPadding(FMargin(32.0f));
	WidgetTree->RootWidget = Background;

	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Background->SetContent(Root);
	auto Add = [](UVerticalBox* Box, UWidget* Child) { Box->AddChildToVerticalBox(Child)->SetPadding(FMargin(0.0f, 4.0f)); };

	Add(Root, MakeText(TEXT("REAN SDK sample")));
	StatusText = MakeText(TEXT("Not signed in."));
	Add(Root, StatusText);

	// Signed out: sign in, device code + QR code, cancel.
	SignedOutPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Add(Root, SignedOutPanel);
	SignInButton = MakeButton(TEXT("Sign in with REAN"));
	SignInButton->OnClicked.AddDynamic(this, &UReanSampleWidget::OnSignInClicked);
	Add(SignedOutPanel, SignInButton);
	CodeText = MakeText(FString());
	Add(SignedOutPanel, CodeText);
	QrImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	QrImage->SetVisibility(ESlateVisibility::Collapsed);
	Add(SignedOutPanel, QrImage);
	CancelButton = MakeButton(TEXT("Cancel"));
	CancelButton->OnClicked.AddDynamic(this, &UReanSampleWidget::OnCancelClicked);
	CancelButton->SetVisibility(ESlateVisibility::Collapsed);
	Add(SignedOutPanel, CancelButton);

	// Signed in: player, actions, friends, incoming invite.
	SignedInPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Add(Root, SignedInPanel);
	UserText = MakeText(FString());
	Add(SignedInPanel, UserText);
	UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Add(SignedInPanel, Actions);
	UButton* Refresh = MakeButton(TEXT("Refresh friends"));
	Refresh->OnClicked.AddDynamic(this, &UReanSampleWidget::OnRefreshClicked);
	Actions->AddChildToHorizontalBox(Refresh)->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	UButton* Activity = MakeButton(TEXT("Set activity"));
	ActivityLabel = Cast<UTextBlock>(Activity->GetChildAt(0));
	Activity->OnClicked.AddDynamic(this, &UReanSampleWidget::OnActivityClicked);
	Actions->AddChildToHorizontalBox(Activity)->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	UButton* SignOut = MakeButton(TEXT("Sign out"));
	SignOut->OnClicked.AddDynamic(this, &UReanSampleWidget::OnSignOutClicked);
	Actions->AddChildToHorizontalBox(SignOut);

	InvitePanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	InvitePanel->SetVisibility(ESlateVisibility::Collapsed);
	Add(SignedInPanel, InvitePanel);
	InviteText = MakeText(FString());
	Add(InvitePanel, InviteText);
	UHorizontalBox* InviteActions = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Add(InvitePanel, InviteActions);
	UButton* Accept = MakeButton(TEXT("Accept"));
	Accept->OnClicked.AddDynamic(this, &UReanSampleWidget::OnAcceptClicked);
	InviteActions->AddChildToHorizontalBox(Accept)->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	UButton* Decline = MakeButton(TEXT("Decline"));
	Decline->OnClicked.AddDynamic(this, &UReanSampleWidget::OnDeclineClicked);
	InviteActions->AddChildToHorizontalBox(Decline);

	Add(SignedInPanel, MakeText(TEXT("Friends")));
	FriendsBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	Add(SignedInPanel, FriendsBox);
}

// ─── Lifetime ───────────────────────────────────────────────────────────────

UReanSubsystem* UReanSampleWidget::Rean() const
{
	return UReanBlueprintLibrary::GetReanSubsystem(this);
}

void UReanSampleWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UReanSubsystem* Sdk = Rean();
	if (!Sdk)
	{
		SetStatus(TEXT("No REAN subsystem (is the ReanSDK plugin enabled?)."));
		return;
	}
	// Binding these also starts event polling (every EventPollSeconds while signed in).
	Sdk->OnInviteReceived.AddDynamic(this, &UReanSampleWidget::HandleInviteReceived);
	Sdk->OnInviteAnswered.AddDynamic(this, &UReanSampleWidget::HandleInviteAnswered);
	Sdk->OnSessionChanged.AddDynamic(this, &UReanSampleWidget::HandleSessionChanged);
	ShowSignedIn(Sdk->IsSignedIn());
	if (!Sdk->IsSignedIn()) StartSignIn(/*bSilent*/ true);  // launcher code / saved session, no code on screen yet
}

void UReanSampleWidget::NativeDestruct()
{
	if (UReanSubsystem* Sdk = Rean())
	{
		Sdk->OnInviteReceived.RemoveAll(this);
		Sdk->OnInviteAnswered.RemoveAll(this);
		Sdk->OnSessionChanged.RemoveAll(this);
	}
	Super::NativeDestruct();
}

void UReanSampleWidget::SetStatus(const FString& Text)
{
	if (StatusText) StatusText->SetText(FText::FromString(Text));
}

void UReanSampleWidget::ShowSignedIn(bool bSignedIn)
{
	if (!SignedOutPanel || !SignedInPanel) return;
	SignedOutPanel->SetVisibility(bSignedIn ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	SignedInPanel->SetVisibility(bSignedIn ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (!bSignedIn) return;
	const FReanUser Me = Rean()->GetCurrentUser();
	UserText->SetText(FText::FromString(FString::Printf(TEXT("Signed in as %s (@%s)"), *Me.DisplayName, *Me.Username)));
	ActivityLabel->SetText(FText::FromString(Rean()->HasActivity() ? TEXT("Clear activity") : TEXT("Set activity")));
	RefreshFriends();
}

// ─── Sign in ────────────────────────────────────────────────────────────────

void UReanSampleWidget::OnSignInClicked()
{
	StartSignIn(/*bSilent*/ false);
}

void UReanSampleWidget::StartSignIn(bool bSilent)
{
	UReanSubsystem* Sdk = Rean();
	if (!Sdk || Sdk->IsSigningIn()) return;
	TWeakObjectPtr<UReanSampleWidget> WeakThis(this);

	// 1. Started by the REAN Launcher: its one-time code signs the player in without any input.
	if (Sdk->WasStartedByLauncher())
	{
		SetStatus(TEXT("Signing in with the REAN Launcher..."));
		Sdk->SignInWithLauncher([WeakThis, bSilent](const FReanError& Error, const FReanUser& User) {
			if (UReanSampleWidget* Self = WeakThis.Get())
			{
				if (Error.IsOk()) Self->OnSignedIn(Error, User);
				else Self->StartSignIn(bSilent);  // the code is used now: falls back to the saved session / device code
			}
		});
		return;
	}

	// 2. A session saved by a previous launch.
	SetStatus(TEXT("Restoring your REAN session..."));
	Sdk->RestoreSession([WeakThis, bSilent](const FReanError& Error, const FReanUser& User) {
		UReanSampleWidget* Self = WeakThis.Get();
		if (!Self) return;
		if (Error.IsOk())
		{
			Self->OnSignedIn(Error, User);
		}
		else if (Error.Code == EReanErrorCode::NoSavedSession || Error.Code == EReanErrorCode::SessionEnded)
		{
			if (bSilent) Self->SetStatus(TEXT("Not signed in. Press \"Sign in with REAN\"."));
			else Self->SignInWithDeviceCode();  // 3. Device code
		}
		else
		{
			Self->SetStatus(FString::Printf(TEXT("Could not restore the session: %s"), *Error.Message));
		}
	});
}

void UReanSampleWidget::SignInWithDeviceCode()
{
	TWeakObjectPtr<UReanSampleWidget> WeakThis(this);
	SetStatus(TEXT("Getting a sign-in code..."));
	CancelButton->SetVisibility(ESlateVisibility::Visible);
	SignInButton->SetVisibility(ESlateVisibility::Collapsed);
	Rean()->SignInWithDevice(
		[WeakThis](const FReanDeviceCode& Code) {
			UReanSampleWidget* Self = WeakThis.Get();
			if (!Self) return;
			Self->SetStatus(TEXT("Waiting for you to approve on your phone or PC..."));
			Self->CodeText->SetText(FText::FromString(FString::Printf(TEXT("Go to %s and enter\n%s\nor scan the code:"), *Code.VerificationUri, *Code.UserCode)));
			if (UTexture2D* Qr = UReanBlueprintLibrary::MakeQrCodeTexture(Code.VerificationUriComplete, 6))
			{
				Self->QrImage->SetBrushFromTexture(Qr, true);
				Self->QrImage->SetVisibility(ESlateVisibility::Visible);
			}
		},
		[WeakThis](const FReanError& Error, const FReanUser& User) {
			if (UReanSampleWidget* Self = WeakThis.Get()) Self->OnSignedIn(Error, User);
		});
}

void UReanSampleWidget::OnSignedIn(const FReanError& Error, const FReanUser& User)
{
	CodeText->SetText(FText::GetEmpty());
	QrImage->SetVisibility(ESlateVisibility::Collapsed);
	CancelButton->SetVisibility(ESlateVisibility::Collapsed);
	SignInButton->SetVisibility(ESlateVisibility::Visible);
	if (!Error.IsOk())
	{
		switch (Error.Code)
		{
		case EReanErrorCode::Denied: SetStatus(TEXT("Sign-in was declined.")); break;
		case EReanErrorCode::Expired: SetStatus(TEXT("The code expired. Press Sign in to get a new one.")); break;
		case EReanErrorCode::Cancelled: SetStatus(TEXT("Sign-in cancelled.")); break;
		case EReanErrorCode::InvalidConfig:
		case EReanErrorCode::InvalidClient: SetStatus(TEXT("Set your app's client id in Project Settings > Plugins > REAN SDK.")); break;
		default: SetStatus(FString::Printf(TEXT("Sign-in failed: %s"), *Error.Message)); break;
		}
		return;
	}
	SetStatus(TEXT("Signed in."));
	ShowSignedIn(true);
}

void UReanSampleWidget::OnCancelClicked()
{
	if (UReanSubsystem* Sdk = Rean()) Sdk->CancelSignIn();
}

void UReanSampleWidget::OnSignOutClicked()
{
	if (UReanSubsystem* Sdk = Rean()) Sdk->SignOut();  // OnSessionChanged(SignedOut) updates the screen
}

void UReanSampleWidget::HandleSessionChanged(EReanSessionChange Change, const FReanError& Reason)
{
	if (Change == EReanSessionChange::SignedIn) return;
	PendingInvite = FReanInvite();
	if (InvitePanel) InvitePanel->SetVisibility(ESlateVisibility::Collapsed);
	ShowSignedIn(false);
	SetStatus(Change == EReanSessionChange::SessionEnded ? FString::Printf(TEXT("Your REAN session ended: %s"), *Reason.Message) : TEXT("Signed out."));
}

// ─── Friends and presence ───────────────────────────────────────────────────

void UReanSampleWidget::OnRefreshClicked()
{
	RefreshFriends();
}

void UReanSampleWidget::RefreshFriends()
{
	TWeakObjectPtr<UReanSampleWidget> WeakThis(this);
	Rean()->GetFriends([WeakThis](const FReanError& Error, const TArray<FReanFriend>& Friends) {
		UReanSampleWidget* Self = WeakThis.Get();
		if (!Self) return;
		Self->FriendsBox->ClearChildren();
		Self->InviteButtons.Reset();
		if (!Error.IsOk())
		{
			Self->FriendsBox->AddChild(Self->MakeText(FString::Printf(TEXT("Could not load friends: %s"), *Error.Message)));
			return;
		}
		if (Friends.Num() == 0) Self->FriendsBox->AddChild(Self->MakeText(TEXT("No REAN friends yet.")));
		for (const FReanFriend& Friend : Friends)
		{
			UHorizontalBox* Row = Self->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			const FString Line = Friend.User.DisplayName + TEXT("  ") + UReanBlueprintLibrary::DescribePresence(Friend.Presence).ToString();
			UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Self->MakeText(Line));
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TextSlot->SetVerticalAlignment(VAlign_Center);
			UButton* Invite = Self->MakeButton(TEXT("Invite"));
			UReanSampleInviteButton* Handler = NewObject<UReanSampleInviteButton>(Self);
			Handler->Owner = Self;
			Handler->FriendId = Friend.User.Id;
			Handler->FriendName = Friend.User.DisplayName;
			Invite->OnClicked.AddDynamic(Handler, &UReanSampleInviteButton::OnClicked);
			Self->InviteButtons.Add(Handler);
			Row->AddChildToHorizontalBox(Invite);
			Self->FriendsBox->AddChild(Row);
		}
	});
}

void UReanSampleWidget::OnActivityClicked()
{
	UReanSubsystem* Sdk = Rean();
	if (Sdk->HasActivity())
	{
		Sdk->ClearActivity();
		ActivityLabel->SetText(FText::FromString(TEXT("Set activity")));
		SetStatus(TEXT("Activity cleared."));
		return;
	}
	FReanActivity Activity;
	Activity.Details = TEXT("In the sample lobby");
	Activity.State = TEXT("Waiting for friends");
	Activity.PartyId = TEXT("sample-lobby-") + Sdk->GetCurrentUser().Id.Left(8);
	Activity.PartySize = 1;
	Activity.PartyMax = 4;
	Activity.bJoinable = true;
	Activity.StartedAt = UReanBlueprintLibrary::UnixSecondsNow();
	FReanError Error;
	if (!Sdk->SetActivity(Activity, Error))
	{
		SetStatus(FString::Printf(TEXT("Activity not set: %s"), *Error.Message));
		return;
	}
	ActivityLabel->SetText(FText::FromString(TEXT("Clear activity")));
	SetStatus(TEXT("Friends now see you \"In the sample lobby\" (kept alive every 60 s)."));
}

// ─── Invites ────────────────────────────────────────────────────────────────

void UReanSampleWidget::InviteFriend(const FString& FriendId, const FString& FriendName)
{
	TWeakObjectPtr<UReanSampleWidget> WeakThis(this);
	const FString JoinSecret = TEXT("sample-lobby:") + Rean()->GetCurrentUser().Id;  // whatever your game needs to join
	Rean()->SendInvite(FriendId, JoinSecret, TEXT("Join my lobby in the REAN sample!"), [WeakThis, FriendName](const FReanError& Error, const FReanSentInvite&) {
		if (UReanSampleWidget* Self = WeakThis.Get())
		{
			Self->SetStatus(Error.IsOk() ? FString::Printf(TEXT("Invite sent to %s (valid 10 minutes)."), *FriendName)
			                             : FString::Printf(TEXT("Invite not sent: %s"), *Error.Message));
		}
	});
}

void UReanSampleWidget::HandleInviteReceived(const FReanInvite& Invite)
{
	PendingInvite = Invite;
	InviteText->SetText(FText::FromString(Invite.Message.IsEmpty()
		? FString::Printf(TEXT("%s invited you to play."), *Invite.From.DisplayName)
		: FString::Printf(TEXT("%s invited you: \"%s\""), *Invite.From.DisplayName, *Invite.Message)));
	InvitePanel->SetVisibility(ESlateVisibility::Visible);
}

void UReanSampleWidget::HandleInviteAnswered(const FReanInviteAnswer& Answer)
{
	SetStatus(FString::Printf(TEXT("%s %s your invite."), *Answer.By.DisplayName, Answer.bAccepted ? TEXT("accepted") : TEXT("declined")));
}

void UReanSampleWidget::OnAcceptClicked()
{
	Respond(true);
}

void UReanSampleWidget::OnDeclineClicked()
{
	Respond(false);
}

void UReanSampleWidget::Respond(bool bAccept)
{
	if (PendingInvite.Id.IsEmpty()) return;
	const FReanInvite Invite = PendingInvite;
	PendingInvite = FReanInvite();
	InvitePanel->SetVisibility(ESlateVisibility::Collapsed);
	TWeakObjectPtr<UReanSampleWidget> WeakThis(this);
	Rean()->RespondToInvite(Invite.Id, bAccept, [WeakThis, bAccept, Invite](const FReanError& Error, const FString& JoinSecret) {
		UReanSampleWidget* Self = WeakThis.Get();
		if (!Self) return;
		if (!Error.IsOk())
		{
			Self->SetStatus(Error.Code == EReanErrorCode::Expired ? TEXT("That invite expired.") : FString::Printf(TEXT("Could not answer: %s"), *Error.Message));
			return;
		}
		// A real game would now join the lobby / server described by JoinSecret.
		Self->SetStatus(bAccept ? FString::Printf(TEXT("Joining %s: %s"), *Invite.From.DisplayName, *JoinSecret) : TEXT("Invite declined."));
	});
}
