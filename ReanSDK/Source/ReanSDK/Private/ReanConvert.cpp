// Copyright (c) REAN Studios. MIT License.
#include "ReanConvert.h"

namespace ReanConvert
{

static EReanErrorCode Code(rean::ErrorCode C)
{
	switch (C)
	{
	case rean::ErrorCode::None: return EReanErrorCode::None;
	case rean::ErrorCode::InvalidArgument: return EReanErrorCode::InvalidArgument;
	case rean::ErrorCode::InvalidConfig: return EReanErrorCode::InvalidConfig;
	case rean::ErrorCode::NotSignedIn: return EReanErrorCode::NotSignedIn;
	case rean::ErrorCode::NoSavedSession: return EReanErrorCode::NoSavedSession;
	case rean::ErrorCode::Busy: return EReanErrorCode::Busy;
	case rean::ErrorCode::Cancelled: return EReanErrorCode::Cancelled;
	case rean::ErrorCode::Network: return EReanErrorCode::Network;
	case rean::ErrorCode::RateLimited: return EReanErrorCode::RateLimited;
	case rean::ErrorCode::InvalidClient: return EReanErrorCode::InvalidClient;
	case rean::ErrorCode::InvalidGrant: return EReanErrorCode::InvalidGrant;
	case rean::ErrorCode::SessionEnded: return EReanErrorCode::SessionEnded;
	case rean::ErrorCode::Denied: return EReanErrorCode::Denied;
	case rean::ErrorCode::Expired: return EReanErrorCode::Expired;
	case rean::ErrorCode::MissingScope: return EReanErrorCode::MissingScope;
	case rean::ErrorCode::NotFriends: return EReanErrorCode::NotFriends;
	case rean::ErrorCode::NotFound: return EReanErrorCode::NotFound;
	case rean::ErrorCode::InvalidRequest: return EReanErrorCode::InvalidRequest;
	case rean::ErrorCode::Server: return EReanErrorCode::Server;
	case rean::ErrorCode::NotSupported: return EReanErrorCode::NotSupported;
	}
	return EReanErrorCode::Server;
}

static EReanPresenceStatus Status(rean::PresenceStatus S)
{
	switch (S)
	{
	case rean::PresenceStatus::Offline: return EReanPresenceStatus::Offline;
	case rean::PresenceStatus::Online: return EReanPresenceStatus::Online;
	case rean::PresenceStatus::Away: return EReanPresenceStatus::Away;
	case rean::PresenceStatus::DoNotDisturb: return EReanPresenceStatus::DoNotDisturb;
	case rean::PresenceStatus::InGame: return EReanPresenceStatus::InGame;
	case rean::PresenceStatus::Unknown: return EReanPresenceStatus::Unknown;
	}
	return EReanPresenceStatus::Unknown;
}

FReanError Error(const rean::Error& E)
{
	FReanError Out;
	Out.Code = Code(E.code);
	Out.HttpStatus = E.httpStatus;
	Out.ApiCode = ToF(E.apiCode);
	Out.Message = ToF(E.message);
	return Out;
}

FReanError LocalError(EReanErrorCode InCode, const FString& Message)
{
	FReanError Out;
	Out.Code = InCode;
	Out.Message = Message;
	return Out;
}

FReanUser User(const rean::User& U)
{
	FReanUser Out;
	Out.Id = ToF(U.id);
	Out.Username = ToF(U.username);
	Out.DisplayName = ToF(U.displayName);
	Out.AvatarUrl = ToF(U.avatarUrl);
	Out.Email = ToF(U.email);
	Out.bEmailVerified = U.emailVerified;
	return Out;
}

FReanPresence Presence(const rean::Presence& P)
{
	FReanPresence Out;
	Out.Status = Status(P.status);
	Out.StatusText = ToF(P.statusText);
	Out.bHasActivity = P.hasActivity;
	const rean::FriendActivity& A = P.activity;
	FReanFriendActivity& O = Out.Activity;
	O.AppClientId = ToF(A.app.clientId);
	O.AppName = ToF(A.app.name);
	O.AppIconUrl = ToF(A.app.iconUrl);
	O.Details = ToF(A.details);
	O.State = ToF(A.state);
	O.StartedAt = A.startedAtUnix;
	O.bInYourGame = A.inYourGame;
	O.bHasParty = A.hasParty;
	O.PartyId = ToF(A.partyId);
	O.PartySize = A.partySize;
	O.PartyMax = A.partyMax;
	O.bJoinable = A.joinable;
	return Out;
}

FReanFriend Friend(const rean::Friend& F)
{
	FReanFriend Out;
	Out.User = User(F.user);
	Out.FriendsSince = ToF(F.friendsSince);
	Out.bHasPresence = F.hasPresence;
	Out.Presence = Presence(F.presence);
	return Out;
}

FReanInvite Invite(const rean::Invite& I)
{
	FReanInvite Out;
	Out.Id = ToF(I.id);
	Out.From = User(I.from);
	Out.Message = ToF(I.message);
	Out.JoinSecret = ToF(I.joinSecret);
	Out.CreatedAt = ToF(I.createdAt);
	Out.ExpiresAt = ToF(I.expiresAt);
	return Out;
}

FReanEvent Event(const rean::Event& E)
{
	FReanEvent Out;
	Out.Id = E.id;
	Out.TypeName = ToF(E.typeName);
	Out.CreatedAt = ToF(E.createdAt);
	Out.RawData = ToF(E.rawData);
	switch (E.type)
	{
	case rean::EventType::InviteReceived:
		Out.Type = EReanEventType::InviteReceived;
		Out.Invite = Invite(E.invite);
		break;
	case rean::EventType::InviteAccepted:
	case rean::EventType::InviteDeclined:
		Out.Type = E.type == rean::EventType::InviteAccepted ? EReanEventType::InviteAccepted : EReanEventType::InviteDeclined;
		Out.Answer.InviteId = ToF(E.inviteId);
		Out.Answer.By = User(E.by);
		Out.Answer.bAccepted = E.type == rean::EventType::InviteAccepted;
		break;
	case rean::EventType::Unknown:
		Out.Type = EReanEventType::Unknown;
		break;
	}
	return Out;
}

FReanDeviceCode DeviceCode(const rean::DeviceCode& D)
{
	FReanDeviceCode Out;
	Out.UserCode = ToF(D.userCode);
	Out.VerificationUri = ToF(D.verificationUri);
	Out.VerificationUriComplete = ToF(D.verificationUriComplete);
	Out.ExpiresInSeconds = D.expiresInSeconds;
	return Out;
}

rean::Activity Activity(const FReanActivity& A)
{
	rean::Activity Out;
	switch (A.Status)
	{
	case EReanActivityStatus::InGame: Out.status = "in_game"; break;
	case EReanActivityStatus::Online: Out.status = "online"; break;
	case EReanActivityStatus::Away: Out.status = "away"; break;
	case EReanActivityStatus::DoNotDisturb: Out.status = "dnd"; break;
	}
	Out.details = ToStd(A.Details);
	Out.state = ToStd(A.State);
	Out.partyId = ToStd(A.PartyId);
	Out.partySize = A.PartySize;
	Out.partyMax = A.PartyMax;
	Out.joinable = A.bJoinable;
	Out.startedAt = A.StartedAt;
	return Out;
}

EReanSessionChange SessionChange(rean::SessionChange C)
{
	switch (C)
	{
	case rean::SessionChange::SignedIn: return EReanSessionChange::SignedIn;
	case rean::SessionChange::SignedOut: return EReanSessionChange::SignedOut;
	case rean::SessionChange::SessionEnded: return EReanSessionChange::SessionEnded;
	}
	return EReanSessionChange::SignedOut;
}

} // namespace ReanConvert
