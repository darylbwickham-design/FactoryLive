# Spawn a mob from chat

Factory Live **0.9.0-chaos.8** accepts a named webhook for each of the 24 creatures below. Load a save as host. The bundled bridge starts and arms automatically.

## Streamer.bot

1. Make an action and add an **Execute C# Code** sub-action.
2. Paste [the Factory Live script](integrations/StreamerBotWebhook.cs). Set `Mob` to the last part of the endpoint, such as `lizard-doggo` or `elite-stinger`. Set `Quantity` to the number you want.
3. Assign a chat command or channel reward to the action. The script reads your local port and token automatically.

## HTTP actions

Use the port and `webhookToken` in `%LOCALAPPDATA%\FactoryLiveChaos\link.json`. If you set `FACTORYLIVE_CHAOS_HOME`, the link file is in that folder. The default address is `http://127.0.0.1:37129`.

For example, to request three Doggos, send this request from the game PC.

```http
POST /v1/mobs/lizard-doggo
Authorization: Bearer YOUR_WEBHOOK_TOKEN
Content-Type: application/json

{"qty":3,"eventId":"your-redemption-id"}
```

Send `{}` for one mob. Quantities are 1–300, with one Manta per request. Supply the same optional `eventId` when retrying a redemption. HTTP 202 means queued. A duplicate returns 200. A 409 means the host or creature is unavailable. A 429 means the cooldown or a queue limit is active.

Send an authenticated **GET /v1/mobs** to list the names, endpoints, quantity limits, and availability on the connected host. The existing **POST /v1/trigger** format still accepts `{"entityId":"spitter","variant":7,"qty":3}`. Its numeric variants follow the table below.

## Hostile mobs

| Creature | POST endpoint | Entity ID | Variant |
| --- | --- | --- | ---: |
| Fluffy-tailed Hog | `/v1/mobs/hog` | hog | 0 |
| Alpha Hog | `/v1/mobs/alpha-hog` | hog | 1 |
| Cliff Hog | `/v1/mobs/cliff-hog` | hog | 2 |
| Nuclear Hog | `/v1/mobs/nuclear-hog` | hog | 3 |
| Jetpack Hog (Johnny) | `/v1/mobs/jetpack-hog` | hog | 4 |
| Forest Spitter | `/v1/mobs/spitter` | spitter | 0 |
| Red Forest Spitter | `/v1/mobs/spitter-red` | spitter | 1 |
| Desert Spitter | `/v1/mobs/spitter-desert` | spitter | 2 |
| Aquatic Spitter | `/v1/mobs/spitter-aquatic` | spitter | 3 |
| Alpha Forest Spitter | `/v1/mobs/alpha-spitter` | spitter | 4 |
| Alpha Red Forest Spitter | `/v1/mobs/alpha-spitter-red` | spitter | 5 |
| Alpha Desert Spitter | `/v1/mobs/alpha-spitter-desert` | spitter | 6 |
| Alpha Aquatic Spitter | `/v1/mobs/alpha-spitter-aquatic` | spitter | 7 |
| Small Stinger | `/v1/mobs/stinger` | stinger | 0 |
| Big Stinger | `/v1/mobs/big-stinger` | stinger | 1 |
| Elite Stinger | `/v1/mobs/elite-stinger` | stinger | 2 |
| Flying Crab Hatcher | `/v1/mobs/crab-hatcher` | hatcher | 0 |
| Elite Flying Crab Hatcher | `/v1/mobs/elite-hatcher` | hatcher | 1 |
| Flying Crab | `/v1/mobs/flying-crab` | crab | 0 |

Both hatchers share the hatcher cooldown and population cap. The flying-crab endpoint spawns the native airborne creature directly.

## Wildlife

| Creature | POST endpoint | Entity ID | Variant |
| --- | --- | --- | ---: |
| Lizard Doggo | `/v1/mobs/lizard-doggo` | wildlife | 0 |
| Space Giraffe-Tick-Penguin-Whale Thing (Bean) | `/v1/mobs/bean` | wildlife | 1 |
| Flightless Bird | `/v1/mobs/flightless-bird` | wildlife | 2 |
| Beetle | `/v1/mobs/beetle` | wildlife | 3 |
| Giant Flying Manta | `/v1/mobs/manta` | wildlife | 4 |

Wildlife is available for individual rewards. **!chaos only spawns hostile creatures.** A redeemed Manta follows its own loop above the area where you called it. Living flying crabs and Mantas are restored gradually on reload. Each restored Manta starts its loop again.
