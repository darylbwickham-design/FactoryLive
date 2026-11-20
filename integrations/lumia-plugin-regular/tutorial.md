# Factory Live for Lumia Stream

Run the Factory Live bridge on the Windows PC hosting Satisfactory. Use the Windows mod and bridge from the same Factory Live release.

1. Open PowerShell in the bridge folder and run `node bridge.mjs serve`. Leave it running.
2. In Lumia Stream 9.4, go to **Settings → Plugins → Installed → Install Manually** and select the Factory Live `.lumiaplugin` file. Enable Factory Live and connect it if Lumia prompts.
3. Add a **Factory Live → Check Bridge** action to a temporary command and use Lumia's **Test** button. You can do this with the game closed. The result tells you whether the bridge and the game are ready.
4. On a chat command, alert, or Twitch Points redemption, pick a direct action such as **Factory Live → Spawn Hog**, then choose the tier and quantity. Existing **Trigger Preset** and **Trigger Custom Preset** actions still work after upgrading the plugin.
5. Load a test save, use `node bridge.mjs status` to check the game, then run `node bridge.mjs arm` when you're ready for viewer effects.

The plugin finds the bridge's local link file automatically. **Trigger Custom Preset** accepts the name of a preset in your bridge config. Set a fixed name when creating a public command.

## Bridge controls and Pioneer Death

Open `Start Factory Live Bridge.cmd` from the Chaos bridge folder to start it disarmed. After loading the save, use **Arm Bridge** from a private button or broadcaster-only command. **Disarm Bridge** clears pending effects. Both actions show the outcome in Lumia.

With Chaos Bridge 0.9.0-chaos.4 or later, configure **Pioneer Death** in the plugin’s section under **Alerts**. The alert follows the host’s transition into respawn and can run while viewer effects are disarmed.

## Direct actions and multipliers

Use **Spawn Hog**, **Spawn Spitter**, **Spawn Stinger**, **Spawn Crab Hatcher**, or **Drop Nobelisk**. Direct actions require a bridge with the entity-variant webhook, such as Chaos bridge 0.9.0-chaos.3. The **Multiplier** accepts a positive number or a numeric Lumia variable from the alert. It defaults to 1, and the result is rounded to the nearest whole item. For example, quantity 20 with multiplier 1.5 queues 30. A final quantity above the limit is rejected. A direct Nuke request can contain one Nuke. For the fireworks show, choose **Nobelisk Chaos**.

Direct actions report the accepted count in `factory_live_quantity`. If a request was rejected, `factory_live_result` identifies the cause.

**Creature Roulette** picks an available creature preset. **Surprise Me** chooses Turn Around or an Elite Stinger. Each random preset queues one effect and respects the cooldowns of its choices.

The action result passes `factory_live_status`, `factory_live_result`, and `factory_live_selected_preset` to subsequent Lumia actions. A `queued` result confirms acceptance into the bridge queue. Check Bridge shows a popup and leaves the game alone.

[Full Lumia setup guide](https://lumi-con.com/factory-live/lumia/)
