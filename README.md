# Factory Live

Run Twitch chat commands, channel-point rewards, and alerts in Satisfactory through Lumia Stream or Streamer.bot.

- [Install Factory Live through Satisfactory Mod Manager](https://ficsit.app/mod/FactoryLive). Choose **0.9.0-chaos.6** for Windows Steam build **517554** or **520140**.
- [Set up Lumia Stream and the bundled commands](https://lumi-con.com/factory-live/lumia/).
- [Set up Streamer.bot](https://lumi-con.com/factory-live/streamerbot/).

Load a save as host. The mod starts and arms the bundled bridge when the world is ready. **Disarm Bridge** clears pending effects. Close any older standalone bridge before starting Satisfactory.

**Satisfactory Stream Integration 1.5.0** is in `integrations/lumia-plugin-regular`. It keeps the `factory_live` plugin ID and bundles `!chaos`, `!nukem`, and 16 Twitch reward templates.

## Build the bundled mod

Copy `bridge.mjs`, `bridge.config.json`, `streamerbot.mjs`, the official Windows x64 Node.js `node.exe`, and its license into `Mods/FactoryLive/Bridge` in the starter project. Alpakit stages this folder via `Config/PluginSettings.ini`. Use Alpakit Release for the SMM upload.

Overrides go in `%LOCALAPPDATA%\FactoryLiveChaos\bridge.local.json` or the folder specified by `FACTORYLIVE_CHAOS_HOME`.

## Set up a $10 alert in Streamer.bot

1. Create an action and add the supplied `integrations/StreamerBotWebhook.cs` as an **Execute C# Code** sub-action. It already selects `chaos-mode` and reads the Chaos bridge's link file.
2. Bind it to a successful tip or payment trigger and apply your amount and currency filter. You can also bind it to a dedicated Twitch reward.
3. For another preset, create another action and set the `Preset` constant in that copy of the sub-action.

## Wave presets

| Preset | Requested creatures | Default cooldown |
| --- | --- | --- |
| `chaos-mode` | 300 each of Hatchers, mixed small Spitters, mixed alpha Spitters, and mixed Hogs and Stingers | 60 seconds |
| `hatcher-surge` | 300 Crab Hatchers | 60 seconds |
| `spitter-surge` | 300 forest spitters | 30 seconds |
| `alpha-spitter-surge` | 300 alpha forest spitters | 30 seconds |
| `jetpack-hog-surge` | 300 Jetpack Hogs | 30 seconds |

The bridge accepts all four Chaos Mode waves together if the loaded game supports all four creatures and there is room. Hatcher presets share a 60-second cooldown and a cap of 600 alive or queued. The global pending limit is 2,400 creatures. The mod admits up to 24,000 living creature points, 32,000 points including corpses, and 32,000 active world creatures. One Hatcher uses 12 points, one forest spitter uses two, one alpha forest spitter uses four, and one Jetpack Hog uses three.

Chaos Mode rotates through available native variants, including Forest, Red Forest, Desert and Aquatic Spitters, five Hog types, and three Stinger sizes. It attempts at most one creature spawn per frame and spaces spawn work farther apart when frame times rise. A severe frame stall pauses new spawns briefly. Use open terrain to give the waves room. Hatchers can spread 6–66 m from the pioneer and the other mobs can spread 9–81 m. Placement failure, respawn, or a timeout can cut a wave short.

`node bridge.mjs status` shows the running wave counts. `game.creatureJobs` reports requested, spawned, and remaining creatures. In the host game console, `FactoryLive.Mobs` shows persistent mob history. Run `node bridge.mjs disarm` to clear pending waves. Spawned creatures stay in the world and persist when you save.

## Creature draw distance

Factory Live on the host raises finite mesh draw distances by 50% for its spawned creatures, including mobs restored from a save. Naturally spawned adult and elite Stingers also get the increase and a 200 m minimum finite draw distance. Occlusion, AI activation, and creature despawning use the game's own rules.

To adjust these defaults, close Satisfactory, add the following section to the profile's `Saved/Config/Windows/Game.ini`, and relaunch. A scale of `1.0` restores the original draw distances for all affected creatures. Values from `1.0` to `3.0` are supported.

```ini
[FactoryLive]
CreatureDrawDistanceScale=1.5
BigStingerMinDrawDistanceMeters=200
```

## Nobelisk Chaos

Add **Factory Live Chaos → Nobelisk Chaos** to a separate Lumia alert, reward, or moderator command. In Streamer.bot, set the action's `Preset` constant to `nobelisk-chaos`.

The show launches ten rotating stars of 25 primary Nobelisks. Standard, Pulse, Gas, and Cluster are chosen at random, with one Nuke as the final drop. The drops travel outward and detonate on impact. Stars have a short gap between them. The final Nuke waits for the other drops to resolve and respects any existing Nuke recovery timer.

The preset has a 60-second cooldown and reserves the Nobelisk queue for the show. Creature waves can run during it. The mod staggers launches and impacts and keeps at most 16 primaries in flight. In `node bridge.mjs status`, `game.nobelisks` reports launches and impacts and `game.pending.fireworks` indicates a show is active. A respawn clears queued effects and cancels undetonated drops one per frame. New effects wait for a five-second recovery window after the game's respawn transition.

## Drop Random Equipment

Bind **Drop Random Equipment** in the Chaos Lumia plugin, or use **Trigger Custom Preset** with `equipment-drop`. It drops one item from a random eligible equipment slot as a normal pickup near the pioneer. The pioneer must be alive and on foot on solid ground. The default cooldown is 60 seconds.

## Webhook

Send JSON to `POST http://127.0.0.1:37129/v1/trigger` with the webhook token as a bearer credential. Use either `{"preset":"chaos-mode","eventId":"your-payment-event-id"}` or `{"entityId":"spitter","variant":7,"qty":24}` for 24 Aquatic Alpha Spitters. Use the port in your local link file if you customized it. An authenticated `GET /v1/presets` returns the configured presets, variant numbers and quantities. Reuse the exact event ID when retrying one payment event. HTTP 202 means the bridge queued the request.

For StreamElements, receive the event on the game PC in Lumia Stream and add an outbound API POST action or a Factory Live preset action. The StreamElements cloud chatbot's `$(customapi)` sends GET requests and cannot set an authorization header. [StreamElements customapi documentation](https://docs.streamelements.com/chatbot/variables/customapi)

The other Factory Live creature, Nobelisk, and player-action presets remain in `bridge.config.json`. For source builds, copy the inner `FactoryLive` folder to the matching SML Starter Project's `Mods` folder and package Factory Live Chaos with Alpakit. [Satisfactory modding setup](https://docs.ficsit.app/satisfactory-modding/latest/Development/BeginnersGuide/dependencies.html)
