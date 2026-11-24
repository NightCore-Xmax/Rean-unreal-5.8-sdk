# The REAN sample in Blueprints

The C++ sample (`Source/ReanSample/ReanSampleWidget.cpp`) builds its screen in code so it needs no binary assets. This page builds the same flow as a **Widget Blueprint**, node by node. It takes about 20 minutes.

Flow: **sign in → show code → friends list with presence → set activity → invite a friend → accept an invite**.

## 1. The widget

Create *Content Browser › Add › User Interface › Widget Blueprint* named `WBP_Rean`, and in the Designer add:

| Widget | Name | Notes |
|---|---|---|
| Text | `StatusText` | "Not signed in." |
| Button + Text | `SignInButton` | "Sign in with REAN" |
| Text | `CodeText` | empty, hidden |
| Image | `QrImage` | hidden |
| Button + Text | `CancelButton` | "Cancel", hidden |
| Vertical Box | `SignedInPanel` | hidden. Inside: `UserText`, buttons `RefreshButton`, `ActivityButton`, `SignOutButton`, a `FriendsList` (Scroll Box), and an `InvitePanel` (hidden) with `InviteText`, `AcceptButton`, `DeclineButton` |

Mark the ones you use in the Graph as *Is Variable*. Add a variable `PendingInvite` of type **Rean Invite**.

Show it from your Player Controller's *BeginPlay*: `Create Widget (WBP_Rean)` → `Add to Viewport` → `Set Show Mouse Cursor (true)` → `Set Input Mode UI Only`.

## 2. Event Construct: listen, then sign in quietly

```
Event Construct
 ├─ Get REAN Subsystem → Bind Event to On Invite Received  (→ custom event OnInvite)
 ├─ Get REAN Subsystem → Bind Event to On Invite Answered  (→ custom event OnAnswered)
 ├─ Get REAN Subsystem → Bind Event to On Session Changed  (→ custom event OnSession)
 └─ Get REAN Subsystem → Is Signed In?
       true  → ShowSignedIn (custom event, step 4)
       false → Branch on "Was Started By Launcher"
                 true  → Sign In With Launcher
                           On Success → ShowSignedIn
                           On Failure → RestoreQuietly
                 false → RestoreQuietly

RestoreQuietly (custom event)
 └─ Restore Session
       On Success → ShowSignedIn
       On Failure → Set Text (StatusText) = "Not signed in. Press Sign in with REAN."
```

Binding *On Invite Received* / *On Invite Answered* is what turns invite polling on. Unbind them (`Unbind all Events from …`) in *Event Destruct*.

## 3. Sign in with a device code

```
SignInButton.OnClicked
 └─ Sign In With Device
       On Code(Code, QrCode)
          → Set Text (CodeText) = Format "Go to {0} and enter\n{1}"  ({0} = Code.Verification Uri, {1} = Code.User Code)
          → Set Brush from Texture (QrImage, QrCode, Match Size = true) → Set Visibility (QrImage, Visible)
          → Set Visibility (CancelButton, Visible)
       On Success(User) → hide CodeText / QrImage / CancelButton → ShowSignedIn
       On Failure(User, Error)
          → Switch on Error.Code:
               Denied    → "Sign-in was declined."
               Expired   → "The code expired. Press Sign in again."
               Cancelled → "Sign-in cancelled."
               Invalid Config / Invalid Client → "Set your client id in Project Settings › Plugins › REAN SDK."
               Default   → Error.Message

CancelButton.OnClicked → Get REAN Subsystem → Cancel Sign In
```

The QR code encodes `Verification Uri Complete`, so players can scan it instead of typing the code.

## 4. Friends with presence

```
ShowSignedIn (custom event)
 ├─ Set Visibility (SignedInPanel, Visible), hide SignInButton
 ├─ Set Text (UserText) = Format "Signed in as {0}" (Get REAN Subsystem → Get Current User → Display Name)
 └─ RefreshFriends

RefreshFriends (custom event; also RefreshButton.OnClicked)
 └─ Get Friends
       On Success(Friends)
          → Clear Children (FriendsList)
          → For Each Friends:
               Create Widget (WBP_FriendRow) with Friend = Array Element → Add Child (FriendsList)
       On Failure(Friends, Error) → Set Text (StatusText) = Error.Message
```

`WBP_FriendRow` has a Text and an *Invite* button. Its Text is `Friend.User.Display Name` + `Describe Presence (Friend.Presence)` (e.g. "In game: Your Game - Ranked (Round 3) [2/5]"). `Friend.Presence.Activity.In Your Game` tells you whether party / join information is available.

## 5. Set activity (rich presence)

```
ActivityButton.OnClicked
 └─ Branch (Get REAN Subsystem → Has Activity)
       true  → Clear Activity → button text "Set activity"
       false → Make Rean Activity
                  Status     = In Game
                  Details    = "In the sample lobby"
                  State      = "Waiting for friends"
                  Party Id   = your lobby id
                  Party Size = 1, Party Max = 4, Joinable = true
                  Started At = Unix Seconds Now
               → Set Activity (Activity) → returns Valid + Error
                  Valid false → Set Text (StatusText) = Error.Message
                  Valid true  → button text "Clear activity"
```

The SDK re-sends it every 60 seconds (REAN forgets presence after 120 s) until you call Clear Activity or Sign Out.

## 6. Invite a friend

In `WBP_FriendRow`, *Invite*.OnClicked:

```
Send Invite (Friend Id = Friend.User.Id, Join Secret = your lobby id or server ticket, Message = "Join my lobby!")
   On Success → "Invite sent (valid 10 minutes)."
   On Failure → Error.Code: Not Friends / Rate Limited / Missing Scope → Error.Message
```

## 7. Receive and accept an invite

```
OnInvite (Invite)                         ← bound to On Invite Received
 ├─ Set PendingInvite = Invite
 ├─ Set Text (InviteText) = Format "{0} invited you: {1}" (Invite.From.Display Name, Invite.Message)
 └─ Set Visibility (InvitePanel, Visible)

AcceptButton.OnClicked
 └─ Respond To Invite (Invite Id = PendingInvite.Id, Accept = true)
       On Success(Join Secret) → join the lobby / server described by Join Secret
       On Failure(Error)       → Expired: "That invite expired."

DeclineButton.OnClicked → Respond To Invite (PendingInvite.Id, Accept = false)

OnAnswered (Answer)                       ← bound to On Invite Answered
 └─ Set Text (StatusText) = Format "{0} {1} your invite" (Answer.By.Display Name, Answer.Accepted ? "accepted" : "declined")
```

## 8. Sign out and ended sessions

```
SignOutButton.OnClicked → Get REAN Subsystem → Sign Out

OnSession (Change, Reason)               ← bound to On Session Changed
 └─ Switch on Change
       Signed Out    → hide SignedInPanel, show SignInButton, "Signed out."
       Session Ended → same, plus "Your REAN session ended: {Reason.Message}"
```

*Session Ended* happens when the player removes your game in *Connected apps* on reanstudios.com, or when the saved session expires (60 days without playing). Show sign-in again.
