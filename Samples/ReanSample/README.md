# ReanSample

A small Unreal Engine 5.8 project that uses the REAN SDK plugin from this repository (found through `AdditionalPluginDirectories` in `ReanSample.uproject`, so nothing needs copying).

**Preview: not yet compiled in UE 5.8.**

## Run it

1. Put your app's client id in `Config/DefaultGame.ini` (`ClientId=`), or open the project and set it in *Project Settings › Plugins › REAN SDK*. In development builds you can also pass `-ReanClientId=rean_…` on the command line. The placeholder `rean_xxxxxxxxxxxxxxxxxxxxxxxx` fails on purpose.
2. Right-click `ReanSample.uproject` › *Generate Visual Studio project files*, open the solution, build *Development Editor | Win64*.
3. Open the project and press *Play* (the sample runs in the engine's empty map, so it has no assets).
4. Press **Sign in with REAN**, enter the code at https://reanstudios.com/activate (or scan the QR code), then try *Refresh friends*, *Set activity*, *Invite* next to a friend, and accept an invite sent from another account.

To test invites with two players, run two clients (*Play › Number of Players: 2*, *Net Mode: Play Standalone*, with *Run Under One Process* on); each PIE client keeps its own saved session, so sign them in with two REAN accounts that are friends. Separate processes would share one saved session, which does not work (a refresh token works once).

## Files

| | |
|---|---|
| `Source/ReanSample/ReanSampleWidget.cpp` | The whole flow in C++: launcher code → saved session → device code with QR code → friends with presence → rich presence → invites → sign out. |
| `Source/ReanSample/ReanSampleGameMode.cpp` | Shows the widget. |
| `BLUEPRINT_SAMPLE.md` | The same flow as a Widget Blueprint, node by node. |
