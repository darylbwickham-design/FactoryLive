# Factory Live Chaos for Lumia Stream

Start the separate Chaos bridge, load the test save, and arm the bridge. This plugin reads the local Chaos credentials from `%LOCALAPPDATA%\FactoryLiveChaos\link.json`.

Add **Factory Live Chaos → Check Bridge** to verify the game is ready. In your successful tip alert, set an amount filter such as USD 10 or more, then add **Factory Live Chaos → Trigger Preset → Chaos Mode (1,200 creatures)**. For separate 300-creature waves, choose Hatcher Surge, Spitter Surge, Alpha Spitter Surge, or Jetpack Hog Surge.

Each Chaos Mode event queues four waves. Terrain, jobs expiring, or game stalls can reduce how many creatures arrive.

For **Nobelisk Chaos**, add a separate action and choose **Trigger Preset → Nobelisk Chaos (250-drop firework show)**. The show contains ten rotating stars with randomized drops and a single nuclear finale.

To let viewers drop a random item from the pioneer's equipment slots, choose **Drop Random Equipment**. The pioneer can pick it back up from the ground. This action and the expanded creature variants require the Chaos mod and bridge from the same 0.9.0-chaos.3 release.

## Bridge controls and Pioneer Death

Open `Start Factory Live Bridge.cmd` from the Chaos bridge folder to start it disarmed. After loading the save, use **Arm Bridge** from a private button or broadcaster-only command. **Disarm Bridge** clears pending effects. Both actions show the outcome in Lumia.

With Chaos Bridge 0.9.0-chaos.4 or later, configure **Pioneer Death** in the plugin’s section under **Alerts**. The alert follows the host’s transition into respawn and can run while viewer effects are disarmed.

## Direct actions and multipliers

Use **Spawn Hog**, **Spawn Spitter**, **Spawn Stinger**, **Spawn Crab Hatcher**, or **Drop Nobelisk** and choose the tier or variant where available. Enter a base **Quantity** and a **Multiplier**. The multiplier accepts a positive number or a numeric Lumia variable from the alert and defaults to 1. The result is rounded to the nearest whole item. Quantity 20 with multiplier 1.5 queues 30. Requests above the bridge's limits are rejected, and a direct Nuke request can contain one Nuke.

The direct action reports the accepted count in `factory_live_chaos_quantity` and its result in `factory_live_chaos_result`. Existing **Trigger Preset** and **Trigger Custom Preset** actions still work after upgrading.
