import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { once } from 'node:events';
import { test } from 'node:test';
import { createBridge, validateConfig } from '../bridge.mjs';
import { connectStreamerBot } from '../streamerbot.mjs';

const config = JSON.parse(await readFile(new URL('../bridge.config.json', import.meta.url), 'utf8'));
const keys = { gameToken: 'a'.repeat(64), webhookToken: 'b'.repeat(64) };
const game = { session: '01234567-4321-4321-4321-0123456789ab', ready: true, free: 4, active: 0, slow: false };
const backpackGame = { ...game, backpack: { ready: true, snapshot: false, recovery: false } };

async function start(t, customConfig = config, bridgeOptions) {
  const options = validateConfig(structuredClone(customConfig));
  let time = 50_000;
  const { server, trigger } = createBridge(options, keys, () => time, bridgeOptions);
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  options.port = server.address().port;
  t.after(() => {
    server.close();
    server.closeAllConnections();
  });
  async function call(path, data, token = keys.gameToken, method = 'POST') {
    const response = await fetch(`http://127.0.0.1:${options.port}/v1/${path}`, {
      method, headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
      body: data === undefined ? undefined : JSON.stringify(data)
    });
    return [response.status, await response.json()];
  }
  return { call, trigger, advance: ms => { time += ms; } };
}

test('webhooks require their own token and an armed game', async t => {
  const { call } = await start(t);
  const request = { preset: 'cluster-rain', eventId: 'test:1' };
  assert.equal((await call('arm'))[0], 409);
  assert.equal((await call('trigger', request))[0], 401);
  assert.equal((await call('arm', undefined, keys.webhookToken))[0], 401);
  assert.equal((await call('trigger', request, keys.webhookToken))[0], 409);
  await call('game', game);
  assert.equal((await call('arm'))[0], 200);
  assert.equal((await call('trigger', { preset: 'toString' }, keys.webhookToken))[0], 400);
  const [code, receipt] = await call('trigger', request, keys.webhookToken);
  assert.equal(code, 202);
  assert.equal(receipt.count, 10);
  assert.deepEqual(await call('trigger', request, keys.webhookToken), [200, { duplicate: true }]);
  assert.equal((await call('trigger', { ...request, eventId: 'test:2' }, keys.webhookToken))[0], 429);
});

test('random presets queue one available choice and share its cooldown and event deduplication', async t => {
  const { call, trigger } = await start(t);
  await call('game', game);
  assert.deepEqual(trigger('surprise-me'), [409, { error: 'game_not_armed_or_ready' }]);
  await call('arm');
  // This game advertises no Stingers or player actions, leaving two available roulette choices.
  assert.deepEqual(trigger('surprise-me'), [409, { error: 'random_choices_unavailable' }]);
  const [code, receipt] = trigger('creature-roulette', 'roulette:1');
  assert.equal(code, 202);
  assert.ok(['alpha-hogs', 'spitter-pack'].includes(receipt.selectedPreset));
  assert.equal(trigger(receipt.selectedPreset)[1].error, 'cooldown');
  assert.deepEqual(trigger('creature-roulette', 'roulette:1'), [200, { duplicate: true }]);
  assert.deepEqual(trigger('creature-roulette', 'roulette:2'), [429, { error: 'cooldown', retryAfterMs: 60000 }]);
  const [, delivery] = await call('game', game);
  assert.equal(delivery.job.id, receipt.id);
  assert.equal(delivery.job.creature, config.presets[receipt.selectedPreset].creature);
  assert.equal((await call('game', { ...game, ack: receipt.id }))[1].job, undefined);

  const invalid = structuredClone(config);
  for (const choice of ['creature-roulette', 'surprise-me', 'nuke-drop', 'missing']) {
    invalid.presets['creature-roulette'].choices[0] = choice;
    assert.throws(() => validateConfig(invalid), /Invalid random preset/);
  }
});

test('connection check exposes readiness to the webhook token and expires stale sessions', async t => {
  const { call, advance } = await start(t);
  const check = () => call('connection', undefined, keys.webhookToken, 'GET');
  assert.equal((await call('connection', undefined, keys.gameToken, 'GET'))[0], 401);
  assert.equal((await call('status', undefined, keys.webhookToken, 'GET'))[0], 401);
  assert.deepEqual(await check(), [200, { armed: false, connected: false, ready: false, slow: false }]);
  await call('game', game);
  assert.deepEqual(await check(), [200, { armed: false, connected: true, ready: true, slow: false }]);
  await call('arm');
  await call('game', { ...game, slow: true });
  assert.deepEqual(await check(), [200, { armed: true, connected: true, ready: true, slow: true }]);
  advance(3001);
  assert.deepEqual(await check(), [200, { armed: false, connected: false, ready: false, slow: false }]);
});

test('bundled bridge auto-arms once when each host world is ready', async t => {
  const { call, trigger, advance } = await start(t, config, { autoArmOnWorldReady: true });
  const sync = (state = game) => call('game', state);
  assert.equal((await sync({ ...game, ready: false }))[1].armed, false);
  assert.equal(trigger('hog-swarm')[0], 409);
  assert.equal((await sync())[1].armed, true);
  assert.equal(trigger('hog-swarm')[0], 202);
  assert.equal((await call('disarm'))[1].queued, 0);
  assert.equal((await sync({ ...game, ready: false, respawning: true }))[1].armed, false);
  assert.equal((await sync())[1].armed, false);
  advance(3001);
  assert.equal((await sync())[1].armed, false);

  advance(3001);
  const next = { ...game, session: '98765432-4321-4321-4321-0123456789ab' };
  assert.equal((await sync({ ...next, ready: false }))[1].armed, false);
  assert.equal((await sync(next))[1].armed, true);
});

test('bundled bridge honors disarm before the first world heartbeat', async t => {
  const { call } = await start(t, config, { autoArmOnWorldReady: true });
  assert.equal((await call('disarm'))[1].armed, false);
  assert.equal((await call('game', game))[1].armed, false);
});

test('webhook accepts an entity variant and quantity with the same admission and idempotency as presets', async t => {
  const { call } = await start(t);
  const request = { entityId: 'spitter', variant: 7, qty: 24, eventId: 'stream-tip-1' };
  const send = data => call('trigger', data, keys.webhookToken);
  assert.deepEqual(await send(request), [409, { error: 'game_not_armed_or_ready' }]);
  await call('game', { ...game, creatureTypes: ['spitter'] });
  await call('arm');
  assert.deepEqual(await send(request), [409, { error: 'creature_unavailable' }]);
  const ready = { ...game, creatureTypes: ['alpha-spitter-aquatic'] };
  assert.equal((await call('game', ready))[0], 200);
  assert.deepEqual(await send({ ...request, preset: 'spitter-pack' }), [400, { error: 'ambiguous_trigger' }]);
  assert.deepEqual(await send({ ...request, variant: 8 }), [400, { error: 'unknown_entity_variant' }]);
  assert.deepEqual(await send({ ...request, qty: 301 }), [400, { error: 'invalid_qty' }]);
  assert.deepEqual(await send({ entityId: 'nobelisk', variant: 4, qty: 5 }), [400, { error: 'invalid_qty' }]);
  const [code, accepted] = await send(request);
  assert.equal(code, 202);
  assert.deepEqual(await send(request), [200, { duplicate: true }]);
  assert.equal((await send({ ...request, eventId: 'stream-tip-2' }))[1].error, 'cooldown');
  const [, { job }] = await call('game', ready);
  assert.deepEqual(job, { id: accepted.id, count: 24, effect: 'creatures', creature: 'alpha-spitter-aquatic' });
  const [catalogCode, catalog] = await call('presets', undefined, keys.webhookToken, 'GET');
  assert.equal(catalogCode, 200);
  assert.ok(catalog.presets.some(p => p.id === 'equipment-drop' && p.count === 1));
  assert.deepEqual(catalog.entities.find(e => e.id === 'spitter').variants[7],
    { variant: 7, name: 'alpha-spitter-aquatic', maxQty: 300 });
});

test('equipment drop is a single armed viewer action with its own shared cooldown', async t => {
  const options = structuredClone(config);
  options.presets['drop-alias'] = options.presets['equipment-drop'];
  const { call, trigger } = await start(t, options);
  const ready = { ...backpackGame, equipmentDropReady: true };
  await call('game', ready);
  assert.equal(trigger('equipment-drop')[0], 409);
  await call('arm');
  await call('game', { ...ready, equipmentDropReady: false });
  assert.deepEqual(trigger('equipment-drop'), [409, { error: 'drop_unavailable' }]);
  await call('game', ready);
  const [code, receipt] = trigger('equipment-drop');
  assert.equal(code, 202);
  assert.equal(receipt.count, 1);
  assert.deepEqual(trigger('drop-alias'), [429, { error: 'cooldown', retryAfterMs: 45000 }]);
  const [, delivery] = await call('game', ready);
  assert.deepEqual(delivery.backpackCommand, { id: receipt.id, action: 'equipment-drop' });
  const completed = { ...ready, backpack: { ...ready.backpack,
    last: { id: receipt.id, action: 'equipment-drop', code: 'dropped', stacks: 1 } } };
  assert.equal((await call('game', completed))[1].backpackCommand, undefined);
  const [resultCode, result] = await call(`inventory/result/${receipt.id}`, undefined, keys.gameToken, 'GET');
  assert.equal(resultCode, 200);
  assert.equal(result.code, 'dropped');
});

test('reports creature-wave delays and the last result, and resets them across saves', async t => {
  const { call, advance } = await start(t);
  const id = '39cf5f0e-ef5e-4da1-8dbf-40e33fe496d1';
  const wave = { id, creature: 'hog', requested: 100, spawned: 55, remaining: 45, consecutivePlacementFailures: 3 };
  const status = () => call('status', undefined, keys.gameToken, 'GET');
  for (const state of ['placement', 'mob_capacity', 'frame_time', 'waiting_for_game', 'running']) {
    const job = { ...wave, state };
    const current = { ...game, free: 3, pending: { creatures: 45, cluster: 0 }, creatureJobs: [job] };
    assert.equal((await call('game', current))[0], 200);
    assert.deepEqual((await status())[1].game.creatureJobs, [job]);
    assert.deepEqual(await call('game', { ...current, creatureJobs: [{ ...job, remaining: 46 }] }),
      [400, { error: 'invalid_creature_jobs' }]);
  }
  const lastCreatureJob = { ...wave, state: 'placement_failed', consecutivePlacementFailures: 32 };
  assert.equal((await call('game', { ...game, creatureJobs: [], lastCreatureJob }))[0], 200);
  assert.deepEqual((await status())[1].game.lastCreatureJob, lastCreatureJob);
  advance(3001);
  const saved = { ...game, session: '117d0154-34b2-4e5d-8ee2-51cf56195e8d' };
  assert.equal((await call('game', saved))[0], 200);
  assert.equal((await status())[1].game.lastCreatureJob, undefined);
  assert.equal((await status())[1].game.creatureJobs, undefined);
});

test('delivers each configured Nobelisk type and accepts the old cluster preset shape', async t => {
  const options = structuredClone(config);
  options.presets['legacy-cluster'] = { effect: 'cluster', count: 2, cooldownSeconds: 6 };
  options.presets['random-teleport'] = { effect: 'teleport', count: 1, cooldownSeconds: 90 };
  const variants = [
    ['nobelisk-rain', 'standard'], ['pulse-rain', 'pulse'], ['gas-rain', 'gas'],
    ['cluster-rain', 'cluster'], ['nuke-drop', 'nuke'], ['legacy-cluster', 'cluster']
  ];
  const { call, trigger, advance } = await start(t, options);
  const current = { ...game, nobeliskTypes: ['standard', 'pulse', 'gas', 'cluster', 'nuke'] };
  await call('game', current);
  await call('arm');
  assert.deepEqual(trigger('random-teleport'), [409, { error: 'effect_disabled' }]);
  for (const [preset, type] of variants) {
    const [code, receipt] = trigger(preset);
    assert.equal(code, 202);
    const [, delivery] = await call('game', current);
    assert.deepEqual(delivery.job, {
      id: receipt.id, effect: 'nobelisk', nobelisk: type, count: options.presets[preset].count
    });
    await call('game', { ...current, ack: receipt.id });
    advance(1000);
  }

  const invalid = structuredClone(options);
  invalid.presets['nuke-drop'].count = 2;
  assert.throws(() => validateConfig(invalid), /Invalid preset/);
  invalid.presets['nuke-drop'].nobelisk = 'other';
  assert.throws(() => validateConfig(invalid), /Invalid preset/);
});

test('Nobelisk variants share queue capacity and nuclear aliases share a cooldown', async t => {
  const options = structuredClone(config);
  options.presets['another-nuke'] = { ...options.presets['nuke-drop'] };
  const { call, trigger } = await start(t, options);
  const current = { ...game, nobeliskTypes: ['standard', 'cluster', 'nuke'],
    pending: { creatures: 0, cluster: 0 } };
  await call('game', current);
  await call('arm');
  assert.deepEqual(trigger('gas-rain'), [409, { error: 'nobelisk_unavailable' }]);
  assert.equal(trigger('nobelisk-rain')[0], 202);
  assert.deepEqual(trigger('cluster-rain'), [429, { error: 'queue_full' }]);
  await call('disarm');
  await call('arm');
  assert.equal(trigger('nuke-drop')[0], 202);
  assert.deepEqual(trigger('another-nuke'), [429, { error: 'cooldown', retryAfterMs: 15000 }]);
  await call('disarm');
  await call('arm');
  assert.deepEqual(trigger('another-nuke'), [429, { error: 'cooldown', retryAfterMs: 15000 }]);
});

test('Nuke Carpet Bomb needs wave support and holds the nuclear reservation until the wave clears', async t => {
  const { call, trigger, advance } = await start(t);
  const legacy = { ...game, nobeliskTypes: ['cluster', 'nuke'] };
  await call('game', legacy);
  await call('arm');
  assert.deepEqual(trigger('nuke-carpet'), [409, { error: 'nuke_wave_unavailable' }]);

  const ready = { ...legacy, pending: { creatures: 0, cluster: 0, nuke: 0 } };
  await call('game', ready);
  const [code, receipt] = trigger('nuke-carpet');
  assert.equal(code, 202);
  assert.equal(receipt.count, 5);
  assert.deepEqual(trigger('nuke-drop'), [429, { error: 'cooldown', retryAfterMs: 30000 }]);
  const [, delivery] = await call('game', ready);
  assert.deepEqual(delivery.job, { id: receipt.id, effect: 'nobelisk', nobelisk: 'nuke', count: 5 });

  const playing = { ...ready, ack: receipt.id, pending: { creatures: 0, cluster: 1, nuke: 1 } };
  for (let i = 0; i < 15; i++) {
    advance(2000);
    assert.equal((await call('game', playing))[0], 200);
  }
  assert.deepEqual(trigger('nuke-drop'), [429, { error: 'nuke_wave_busy' }]);
  await call('game', ready);
  assert.equal(trigger('nuke-drop')[0], 202);

  const invalid = structuredClone(config);
  invalid.presets['nuke-carpet'].count = 6;
  assert.throws(() => validateConfig(invalid), /Invalid preset/);
});

test('keeps the game connected and armed as tracked creatures exceed the old limit', async t => {
  const { call, advance } = await start(t);
  const heartbeat = active => ({ ...game, active, mobs: { spawned: active + 10, killed: 10, missing: 0 } });
  assert.equal((await call('game', heartbeat(32)))[0], 200);
  assert.equal((await call('arm'))[0], 200);
  advance(2000);
  const [code, reply] = await call('game', heartbeat(33));
  assert.equal(code, 200);
  assert.equal(reply.armed, true);
  advance(2000);
  assert.equal((await call('game', heartbeat(112)))[0], 200);
  assert.equal((await call('game', heartbeat(24000)))[0], 200);
  assert.equal((await call('game', { ...heartbeat(24000), active: 24001 }))[0], 400);
  assert.equal((await call('game', heartbeat(-1)))[0], 400);
  const [, state] = await call('status', undefined, keys.gameToken, 'GET');
  assert.equal(state.game.mobs.alive, 24000);
  assert.equal(state.armed, true);
});

test('Nobelisk Chaos needs explicit game support and reserves 250 drops independently of normal bomb limits', async t => {
  const { call, trigger, advance } = await start(t);
  const current = { ...game, nobeliskTypes: ['standard', 'gas', 'pulse', 'cluster', 'nuke'], nobeliskCapacity: 32,
    pending: { creatures: 0, cluster: 0, nuke: 0 } };
  await call('game', current);
  await call('arm');
  assert.deepEqual(trigger('nobelisk-chaos'), [409, { error: 'nobelisk_chaos_unavailable' }]);
  const ready = { ...current, nobeliskChaos: true };
  assert.equal((await call('game', { ...ready, nobeliskTypes: ['cluster'] }))[0], 400);
  assert.equal((await call('game', { ...ready, pending: { ...ready.pending, cluster: 1 } }))[0], 200);
  assert.deepEqual(trigger('nobelisk-chaos'), [429, { error: 'nobelisk_lane_busy' }]);

  const slow = { ...ready, slow: true };
  assert.equal((await call('game', slow))[0], 200);
  const [code, receipt] = trigger('nobelisk-chaos', 'firework-redeem:1');
  assert.equal(code, 202);
  assert.equal(receipt.count, 250);
  assert.deepEqual(trigger('nobelisk-chaos', 'firework-redeem:1'), [200, { duplicate: true }]);
  const [, delivery] = await call('game', slow);
  assert.deepEqual(delivery.job, { id: receipt.id, effect: 'nobelisk', nobelisk: 'fireworks', count: 250 });
  const currentDrops = { launched: 32, impacted: 0, expired: 0, cancelled: 0, inFlight: 32 };
  const active = { ...ready, ack: receipt.id, free: 3,
    pending: { creatures: 0, cluster: 250, nuke: 1, fireworks: true }, nobelisks: currentDrops };
  assert.equal((await call('game', active))[0], 200);
  assert.equal((await call('game', { ...active, pending: { ...active.pending, fireworks: undefined } }))[0], 400);
  assert.equal((await call('game', { ...active, nobelisks: { ...currentDrops, launched: 33, inFlight: 33 } }))[0], 400);
  assert.deepEqual(trigger('cluster-rain'), [429, { error: 'nobelisk_lane_busy' }]);
  assert.equal(trigger('hog-swarm')[0], 202);
  assert.equal((await call('game', active))[1].job.creature, 'hog');
  advance(1000);
  assert.equal((await call('game', ready))[0], 200);
  assert.equal(trigger('cluster-rain')[0], 202);
});

test('expanded bomb waves use the reported game capacity and keep the old 12-drop limit', async t => {
  const options = structuredClone(config);
  options.presets['heavy-rain'] = { effect: 'nobelisk', nobelisk: 'standard', count: 20, cooldownSeconds: 5 };
  const { call, trigger } = await start(t, options);
  const current = { ...game, nobeliskTypes: ['standard', 'cluster'], pending: { creatures: 0, cluster: 12 } };
  await call('game', current);
  await call('arm');
  assert.deepEqual(trigger('heavy-rain'), [409, { error: 'nobelisk_capacity_unavailable' }]);
  assert.equal((await call('game', { ...current, nobeliskCapacity: 33 }))[0], 400);

  const expanded = { ...current, nobeliskCapacity: 32,
    nobelisks: { launched: 4, impacted: 0, expired: 0, cancelled: 0, inFlight: 4 } };
  assert.equal((await call('game', expanded))[0], 200);
  const [code, receipt] = trigger('heavy-rain');
  assert.equal(code, 202);
  assert.deepEqual(trigger('cluster-rain'), [429, { error: 'queue_full' }]);
  assert.deepEqual((await call('game', expanded))[1].job, {
    id: receipt.id, effect: 'nobelisk', nobelisk: 'standard', count: 20
  });
  assert.equal((await call('game', { ...expanded, pending: { creatures: 0, cluster: 33 } }))[0], 400);
});

test('reports actual Nobelisk impacts separately from launches and expired drops', async t => {
  const { call, trigger } = await start(t);
  const counters = { launched: 8, impacted: 4, expired: 1, cancelled: 1, inFlight: 2 };
  const current = { ...game, pending: { creatures: 0, cluster: 3 },
    nobelisks: counters, nobeliskTypes: ['standard', 'cluster'] };
  assert.equal((await call('game', current))[0], 200);
  const [, status] = await call('status', undefined, keys.gameToken, 'GET');
  assert.deepEqual(status.game.nobelisks, counters);
  assert.deepEqual(status.game.nobeliskTypes, current.nobeliskTypes);
  assert.equal((await call('game', { ...current, nobelisks: { ...counters, impacted: 5 } }))[0], 400);
  assert.equal((await call('game', { ...current, nobeliskTypes: ['cluster', 'cluster'] }))[0], 400);

  await call('game', game);
  await call('arm');
  assert.deepEqual(trigger('nobelisk-rain'), [409, { error: 'nobelisk_unavailable' }]);
  const [code, receipt] = trigger('cluster-rain');
  assert.equal(code, 202);
  assert.deepEqual((await call('game', game))[1].job, { id: receipt.id, effect: 'cluster', count: 10 });
});

test('host inventory commands work disarmed, require operator auth and acknowledge each operation', async t => {
  const { call, trigger } = await start(t);
  await call('game', game);
  assert.deepEqual(await call('inventory', { action: 'snapshot' }), [409, { error: 'backpack_unavailable' }]);
  assert.deepEqual(await call('inventory', { action: 'restore' }, keys.webhookToken), [401, { error: 'unauthorized' }]);
  await call('game', backpackGame);
  assert.equal(trigger('pack-shuffle')[0], 400);
  assert.equal((await call('inventory', { action: 'shuffle' }))[0], 400);

  let current = backpackGame;
  for (const [action, code] of [['snapshot', 'saved'], ['restore', 'restored'], ['undo', 'undone']]) {
    const [accepted, { id }] = await call('inventory', { action });
    assert.equal(accepted, 202);
    const [, delivery] = await call('game', current);
    assert.equal(delivery.armed, false);
    assert.deepEqual(delivery.backpackCommand, { id, action });
    assert.deepEqual((await call('game', current))[1].backpackCommand, { id, action });
    assert.deepEqual(await call(`inventory/result/${id}`, undefined, keys.gameToken, 'GET'), [202, { pending: true }]);
    assert.equal((await call(`inventory/result/${id}`, undefined, keys.webhookToken, 'GET'))[0], 401);
    assert.deepEqual(await call('inventory', { action: 'snapshot' }), [429, { error: 'backpack_busy' }]);
    const last = { id, action, code, stacks: 17 };
    current = { ...current, backpack: { ...current.backpack, snapshot: true, recovery: action !== 'snapshot', last } };
    assert.equal((await call('game', current))[1].backpackCommand, undefined);
    assert.deepEqual(await call(`inventory/result/${id}`, undefined, keys.gameToken, 'GET'), [200, last]);
  }
});

test('old configs and heartbeats work while hatcher and shuffle aliases are disabled', async t => {
  const options = structuredClone(config);
  options.presets['old-shuffle'] = { effect: 'backpack-shuffle', count: 1, cooldownSeconds: 45 };
  options.presets['old-hatcher'] = { effect: 'creatures', creature: 'crab-hatcher', count: 3, cooldownSeconds: 60 };
  options.presets['old-surprise'] = { effect: 'random', choices: ['old-shuffle', 'old-hatcher', 'turn-around'], cooldownSeconds: 60 };
  const { call, trigger } = await start(t, options);
  const id = 'aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee';
  const current = { ...backpackGame, turnReady: true, creatureTypes: ['hog', 'crab-hatcher'],
    backpack: { ...backpackGame.backpack, last: { id, action: 'shuffle', code: 'shuffled', stacks: 2 } },
    hatchers: { alive: 0, pending: 0, capacity: 6 } };
  assert.equal((await call('game', current))[0], 200);
  await call('arm');
  assert.deepEqual(trigger('old-shuffle'), [409, { error: 'effect_disabled' }]);
  assert.deepEqual(trigger('old-hatcher'), [409, { error: 'effect_disabled' }]);
  const [code, receipt] = await call('trigger', { preset: 'old-surprise', action: 'restore' }, keys.webhookToken);
  assert.equal(code, 202);
  assert.equal(receipt.selectedPreset, 'turn-around');
  const [, response] = await call('game', current);
  assert.deepEqual(response.backpackCommand, { id: receipt.id, action: 'turn-around' });
});

test('the Hatcher trial requires the declared game capacity and caps the full wave', async t => {
  const options = structuredClone(config);
  options.presets['second-hatcher-redeem'] = options.presets['crab-hatcher'];
  const { call, trigger, advance } = await start(t, options);
  const base = { ...game, creatureTypes: ['hog', 'crab-hatcher'] };
  const ready = { ...base, hatcherTrial: { alive: 0, pending: 0, capacity: 600 } };
  const occupied = { ...ready, active: 590, hatcherTrial: { ...ready.hatcherTrial, alive: 590 } };
  await call('game', base);
  await call('arm');
  assert.deepEqual(trigger('crab-hatcher'), [409, { error: 'creature_unavailable' }]);
  await call('game', { ...base, hatcherTrial: { alive: 0, pending: 0 } });
  assert.deepEqual(trigger('crab-hatcher'), [409, { error: 'hatcher_wave_unavailable' }]);
  assert.equal((await call('game', { ...ready, hatcherTrial: { alive: 0, pending: 0, capacity: 10 } }))[0], 200);
  assert.deepEqual(await call('game', { ...ready, hatcherTrial: { alive: 0, pending: 0, capacity: 601 } }),
    [400, { error: 'invalid_hatcher_status' }]);
  assert.equal((await call('game', occupied))[0], 200);
  const [code, receipt] = trigger('crab-hatcher');
  assert.equal(code, 202);
  assert.equal(receipt.count, 10);
  assert.equal(trigger('second-hatcher-redeem')[1].error, 'cooldown');
  const [, delivery] = await call('game', occupied);
  assert.deepEqual(delivery.job, { id: receipt.id, effect: 'creatures', count: 10, creature: 'crab-hatcher' });
  const active = { ...ready, active: 593, ack: receipt.id, hatcherTrial: { alive: 593, pending: 7, capacity: 600 } };
  assert.equal((await call('game', active))[0], 200);
  for (let i = 0; i < 30; i++) {
    advance(2000);
    await call('game', active);
  }
  assert.deepEqual(trigger('second-hatcher-redeem'), [429, { error: 'hatcher_capacity' }]);
  await call('game', occupied);
  assert.equal(trigger('second-hatcher-redeem')[0], 202);
});

test('Chaos Mode accepts all four 300-creature waves together and delivers through slow frames', async t => {
  const { call, trigger } = await start(t);
  const creatureTypes = ['crab-hatcher', 'spitter', 'alpha-spitter', 'jetpack-hog'];
  const ready = { ...game, slow: true, creatureTypes, hatcherTrial: { alive: 0, pending: 0, capacity: 600 } };
  await call('game', { ...ready, creatureTypes: creatureTypes.slice(0, 3) });
  await call('arm');
  assert.deepEqual(trigger('chaos-mode'), [409, { error: 'creature_unavailable' }]);
  await call('game', { ...ready, pending: { creatures: 1201, cluster: 0 } });
  assert.deepEqual(trigger('chaos-mode'), [429, { error: 'queue_full' }]);
  assert.equal((await call('status', undefined, keys.gameToken, 'GET'))[1].queued, 0);
  await call('game', ready);
  const [code, receipt] = trigger('chaos-mode', 'donation:1');
  assert.equal(code, 202);
  assert.equal(receipt.count, 1200);
  assert.deepEqual(receipt.waves.map(({ creature, count }) => [creature, count]),
    creatureTypes.map(creature => [creature, 300]));
  assert.deepEqual(trigger('chaos-mode', 'donation:1'), [200, { duplicate: true }]);
  assert.deepEqual(trigger('cluster-rain'), [429, { error: 'game_throttling' }]);
  let ack;
  for (const wave of receipt.waves) {
    const [, reply] = await call('game', { ...ready, ack });
    assert.deepEqual(reply.job, { ...wave, effect: 'creatures' });
    ack = wave.id;
  }
  assert.equal((await call('game', { ...ready, ack }))[1].job, undefined);
});

test('Chaos Mode mixes the supported variants without adding extra jobs or changing totals', async t => {
  const { call, trigger } = await start(t);
  const ready = { ...game, mixedCreatures: true,
    creatureTypes: ['crab-hatcher', 'spitter', 'spitter-desert', 'alpha-spitter', 'alpha-spitter-red',
      'jetpack-hog', 'cliff-hog', 'nuclear-hog', 'big-stinger'],
    hatcherTrial: { alive: 0, pending: 0, capacity: 600 } };
  assert.equal((await call('game', ready))[0], 200);
  await call('arm');
  const [code, receipt] = trigger('chaos-mode');
  assert.equal(code, 202);
  assert.equal(receipt.count, 1200);
  assert.equal(receipt.waves.length, 4);
  assert.equal(receipt.waves[0].variants, undefined);
  assert.deepEqual(receipt.waves[1].variants, ['spitter', 'spitter-desert']);
  assert.deepEqual(receipt.waves[2].variants, ['alpha-spitter', 'alpha-spitter-red']);
  assert.deepEqual(receipt.waves[3].variants, ['cliff-hog', 'nuclear-hog', 'jetpack-hog', 'big-stinger']);
  let ack;
  for (const wave of receipt.waves) {
    const [, reply] = await call('game', { ...ready, ack });
    assert.deepEqual(reply.job, { ...wave, effect: 'creatures' });
    ack = wave.id;
  }
});

test('slow frames defer backpack delivery and stale commands expire without crossing worlds', async t => {
  const { call, advance } = await start(t);
  await call('game', backpackGame);
  const [, { id }] = await call('inventory', { action: 'restore' });
  const slow = { ...backpackGame, slow: true };
  for (let i = 0; i < 3; i++) {
    advance(2700);
    assert.equal((await call('game', slow))[1].backpackCommand, undefined);
  }
  const [, result] = await call(`inventory/result/${id}`, undefined, keys.gameToken, 'GET');
  assert.equal(result.code, 'expired');
  assert.equal((await call('game', backpackGame))[1].backpackCommand, undefined);
});

test('Stingers require a supported game and keep their spawn counts', async t => {
  const { call, trigger } = await start(t);
  await call('game', game);
  await call('arm');
  assert.deepEqual(trigger('stinger-pack'), [409, { error: 'creature_unavailable' }]);
  const current = { ...game, creatureTypes: ['stinger', 'elite-stinger'] };
  await call('game', current);
  for (const [preset, creature, count] of [['stinger-pack', 'stinger', 12], ['elite-stinger', 'elite-stinger', 1]]) {
    const [code, receipt] = trigger(preset);
    assert.equal(code, 202);
    assert.deepEqual((await call('game', current))[1].job, { id: receipt.id, effect: 'creatures', creature, count });
    await call('game', { ...current, ack: receipt.id });
  }
});


test('Turn Around requires the on-foot capability, stays in the viewer lane and shares a cooldown across aliases', async t => {
  const options = structuredClone(config);
  options.presets['another-turn'] = { ...options.presets['turn-around'] };
  const { call, trigger } = await start(t, options);
  await call('game', backpackGame);
  await call('arm');
  assert.deepEqual(trigger('turn-around'), [409, { error: 'turn_unavailable' }]);
  const current = { ...backpackGame, turnReady: true };
  await call('game', current);
  const [code, { id }] = trigger('turn-around');
  assert.equal(code, 202);
  assert.equal((await call('game', { ...current, turnReady: false }))[1].backpackCommand, undefined);
  assert.deepEqual((await call('game', current))[1].backpackCommand, { id, action: 'turn-around' });
  assert.deepEqual(trigger('another-turn'), [429, { error: 'cooldown', retryAfterMs: 20000 }]);
  const last = { id, action: 'turn-around', code: 'turned', stacks: 0 };
  assert.equal((await call('game', { ...current, backpack: { ...current.backpack, last } }))[1].backpackCommand, undefined);
  await call('disarm');
  assert.deepEqual(trigger('another-turn'), [409, { error: 'game_not_armed_or_ready' }]);
});

test('defers explosives during slow frames, retries unacknowledged jobs and reserves a cluster slot', async t => {
  const { call, trigger } = await start(t);
  await call('game', game);
  await call('arm');
  assert.equal(trigger('hog-swarm')[0], 202);
  assert.equal((await call('game', { ...game, slow: true, free: 1 }))[1].job, undefined);
  assert.deepEqual(trigger('cluster-rain'), [429, { error: 'game_throttling' }]);
  assert.equal((await call('game', { ...game, free: 1 }))[1].job, undefined);
  const [, clusterReceipt] = trigger('cluster-rain');
  const [, first] = await call('game', { ...game, free: 1 });
  assert.equal(first.job.id, clusterReceipt.id);
  assert.deepEqual((await call('game', game))[1], first);
  const [, second] = await call('game', { ...game, ack: first.job.id });
  assert.equal(second.job.creature, 'hog');
  assert.equal(second.job.count, 80);
  const [, empty] = await call('game', { ...game, ack: second.job.id });
  assert.equal(empty.job, undefined);
});

test('bomb spam leaves a game slot for creatures and delivery balances both lanes', async t => {
  const { call, trigger } = await start(t);
  const bombHeavy = { ...game, free: 1, nobeliskCapacity: 32,
    creatureJobs: [], pending: { creatures: 0, cluster: 14 } };
  await call('game', bombHeavy);
  await call('arm');
  const [, bomb] = trigger('cluster-rain');
  assert.equal((await call('game', bombHeavy))[1].job, undefined);
  const [, hogs] = trigger('hog-swarm');
  assert.equal((await call('game', bombHeavy))[1].job.id, hogs.id);

  const hogJob = { id: hogs.id, creature: 'hog', state: 'running', requested: 80, spawned: 0, remaining: 80,
    consecutivePlacementFailures: 0 };
  const split = { ...bombHeavy, free: 2, creatureJobs: [hogJob], ack: hogs.id,
    pending: { creatures: 80, cluster: 5 } };
  const [, alphas] = trigger('alpha-hogs');
  assert.equal((await call('game', split))[1].job.id, alphas.id);
  const alphaJob = { ...hogJob, id: alphas.id, creature: 'alpha-hog', requested: 12, remaining: 12 };
  const creatureHeavy = { ...split, free: 1, ack: alphas.id, creatureJobs: [hogJob, alphaJob],
    pending: { creatures: 92, cluster: 5 } };
  assert.equal((await call('game', creatureHeavy))[1].job.id, bomb.id);
});

test('respawn clears queued effects and leaves an armed game ready to resume after recovery', async t => {
  const { call, trigger } = await start(t);
  await call('game', game);
  await call('arm');
  assert.equal(trigger('hog-swarm')[0], 202);
  assert.ok((await call('game', game))[1].job);
  assert.equal(trigger('cluster-rain')[0], 202);
  assert.equal((await call('game', { ...game, respawning: true }))[0], 400);
  const [code, response] = await call('game', { ...game, ready: false, respawning: true });
  assert.equal(code, 200);
  assert.equal(response.armed, true);
  assert.equal(response.job, undefined);
  assert.deepEqual(trigger('hog-swarm'), [409, { error: 'game_not_armed_or_ready' }]);
  const [, after] = await call('game', game);
  assert.equal(after.armed, true);
  assert.equal(after.job, undefined);
  assert.equal(trigger('hog-swarm')[0], 202);
});

test('disarm, competing connections and stale heartbeats clear or protect the queue', async t => {
  const { call, trigger, advance } = await start(t);
  await call('game', game);
  await call('arm');
  trigger('hog-swarm');
  trigger('cluster-rain');
  const [, before] = await call('game', game);
  const rival = { ...game, session: 'aaaaaaaa-bbbb-cccc-dddd-0123456789ab' };
  assert.equal((await call('game', rival))[0], 409);
  await call('disarm');
  const [, after] = await call('game', game);
  assert.equal(after.armed, false);
  assert.notEqual(after.epoch, before.epoch);
  assert.equal(after.job, undefined);
  await call('arm');
  trigger('cluster-rain');
  advance(3_001);
  const [code, replacement] = await call('game', rival);
  assert.equal(code, 200);
  assert.equal(replacement.armed, false);
  assert.equal(replacement.job, undefined);
});

test('limits cover queued, in-flight and game work and reserve room for clusters', async t => {
  const options = structuredClone(config);
  options.presets.first = { effect: 'creatures', creature: 'hog', count: 2400, cooldownSeconds: 2 };
  options.presets.second = { ...options.presets.first };
  const { call, trigger } = await start(t, options);
  await call('game', game);
  await call('arm');
  const [response, receipt] = trigger('first');
  assert.equal(response, 202);
  assert.deepEqual(trigger('second'), [429, { error: 'queue_full' }]);
  const [, inFlight] = await call('game', game);
  assert.equal(inFlight.job.id, receipt.id);
  assert.deepEqual(trigger('second'), [429, { error: 'queue_full' }]);

  await call('game', { ...game, ack: receipt.id, pending: { creatures: 2400, cluster: 0 } });
  assert.deepEqual(trigger('second'), [429, { error: 'queue_full' }]);
  assert.equal(trigger('cluster-rain')[0], 202);
  await call('game', { ...game, pending: { creatures: 2320, cluster: 7 } });
  assert.equal(trigger('hog-swarm')[0], 202);
  assert.deepEqual(trigger('alpha-hogs'), [429, { error: 'queue_full' }]);
  assert.equal((await call('game', { ...game, pending: { creatures: 2401, cluster: 0 } }))[0], 400);
});

test('ten thousand distinct mapped chat events cannot fill more than one creature queue', async t => {
  const options = structuredClone(config);
  for (let i = 0; i < 40; i++) {
    options.presets[`swarm-${i}`] = { effect: 'creatures', creature: 'hog', count: 50, cooldownSeconds: 2 };
  }
  const { call, trigger, advance } = await start(t, options);
  await call('game', game);
  await call('arm');

  let accepted = 0;
  for (let i = 0; i < 10_000; i++) {
    const [code] = trigger(`swarm-${i % 40}`, `chat:${i}`);
    if (code === 202) accepted++;
    else assert.equal(code, 429);
  }
  assert.equal(accepted, 4);
  assert.equal(trigger('cluster-rain')[0], 202);
  advance(1000);
  assert.equal(trigger('swarm-10', 'chat:after-refill')[0], 202);
  assert.equal(trigger('swarm-11', 'chat:over-refill')[0], 429);
});

test('HTTP spam hits the webhook limiter while game heartbeat and disarm still respond', async t => {
  const { call } = await start(t);
  await call('game', game);
  await call('arm');
  for (let i = 0; i < 16; i++) {
    await call('trigger', { preset: 'hog-swarm', eventId: `hook:${i}` }, keys.webhookToken);
  }
  const [heartbeat, rejected, stopped] = await Promise.all([
    call('game', game),
    call('trigger', { preset: 'hog-swarm' }, keys.webhookToken),
    call('disarm')
  ]);
  assert.equal(heartbeat[0], 200);
  assert.deepEqual(rejected, [429, { error: 'webhook_rate_limit' }]);
  assert.equal(stopped[1].armed, false);
});

test('reports the loaded save’s mob ledger through disarm and a world reload', async t => {
  const { call, advance } = await start(t);
  const status = () => call('status', undefined, keys.gameToken, 'GET');
  const mobs = { spawned: 30, killed: 7, missing: 2 };
  assert.equal((await call('game', { ...game, active: 21, mobs }))[0], 200);
  assert.deepEqual((await status())[1].game.mobs, { ...mobs, alive: 21 });

  await call('arm');
  await call('disarm');
  assert.deepEqual((await status())[1].game.mobs, { ...mobs, alive: 21 });

  advance(3001);
  const reopened = { ...game, session: 'aaaaaaaa-bbbb-cccc-dddd-0123456789ab' };
  await call('game', { ...reopened, active: 21, mobs });
  assert.deepEqual((await status())[1].game.mobs, { ...mobs, alive: 21 });
  assert.equal((await status())[1].armed, false);
  assert.equal((await call('game', { ...reopened, active: 22, mobs }))[0], 400);

  advance(3001);
  await call('game', { ...game, mobs: { spawned: 0, killed: 0, missing: 0 } });
  assert.deepEqual((await status())[1].game.mobs, { spawned: 0, alive: 0, killed: 0, missing: 0 });
});

test('Streamer.bot authenticates, subscribes and maps configured commands and reward IDs', () => {
  class FakeSocket extends EventTarget {
    sent = [];
    send(packet) { this.sent.push(JSON.parse(packet)); }
    receive(packet) { this.dispatchEvent(new MessageEvent('message', { data: JSON.stringify(packet) })); }
    close() { this.dispatchEvent(new Event('close')); }
  }
  const socket = new FakeSocket();
  const triggers = [];
  const stop = connectStreamerBot({
    url: 'ws://127.0.0.1:8080/',
    commands: { '!hogs': 'hog-swarm' },
    rewards: { 'test-reward': 'cluster-rain' }
  }, (...args) => triggers.push(args), 'test-password', () => socket);

  try {
    socket.receive({ request: 'Hello', authentication: { salt: 'salt', challenge: 'challenge' } });
    assert.equal(socket.sent[0].request, 'Authenticate');
    assert.match(socket.sent[0].authentication, /^[A-Za-z0-9+/]{43}=$/);
    socket.receive({ id: 'factorylive-auth', status: 'ok' });
    assert.deepEqual(socket.sent[1].events, { Command: ['Triggered'], Twitch: ['RewardRedemption'] });
    socket.receive({ id: 'factorylive-subscribe', status: 'ok' });
    socket.receive({
      timeStamp: 'now', event: { source: 'Command', type: 'Triggered' },
      data: { command: '!HOGS', user: { id: 'viewer' } }
    });
    socket.receive({
      event: { source: 'Twitch', type: 'RewardRedemption' },
      data: { reward: { id: 'test-reward' }, redemptionId: 'r1' }
    });
    socket.receive({
      event: { source: 'Twitch', type: 'RewardRedemption' },
      data: { reward: { id: 'test-reward' }, redemptionId: 'r2', isFromSharedChatGuest: true }
    });
    assert.deepEqual(triggers, [['hog-swarm', 'sb:command:!hogs:now:viewer'], ['cluster-rain', 'sb:reward:r1']]);
  } finally {
    stop();
  }
});
