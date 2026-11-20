import { randomBytes, randomInt, randomUUID, timingSafeEqual } from 'node:crypto';
import { createServer } from 'node:http';
import { mkdir, readFile, rename, writeFile } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { setTimeout as delay } from 'node:timers/promises';
import { connectStreamerBot } from './streamerbot.mjs';

const root = fileURLToPath(new URL('.', import.meta.url));
const stateDir = process.env.FACTORYLIVE_CHAOS_HOME || (process.env.LOCALAPPDATA
  ? join(process.env.LOCALAPPDATA, 'FactoryLiveChaos')
  : join(root, 'Saved'));
const bodyLimit = 4096;
const heartbeatMs = 3000;
const deathAlertMs = 15_000;
const queueLifeMs = 120_000;
const creatureQueueLifeMs = 300_000;
const legacyCreatures = ['hog', 'alpha-hog', 'spitter'];
const hatcherType = 'crab-hatcher';
const entityVariants = {
  hog: ['hog', 'alpha-hog', 'cliff-hog', 'nuclear-hog', 'jetpack-hog'],
  spitter: ['spitter', 'spitter-red', 'spitter-desert', 'spitter-aquatic',
    'alpha-spitter', 'alpha-spitter-red', 'alpha-spitter-desert', 'alpha-spitter-aquatic'],
  stinger: ['stinger', 'big-stinger', 'elite-stinger'],
  hatcher: [hatcherType],
  nobelisk: ['standard', 'pulse', 'gas', 'cluster', 'nuke']
};
const creatures = new Set([...entityVariants.hog, ...entityVariants.spitter, ...entityVariants.stinger]);
const chaosCreatures = [hatcherType, 'spitter', 'alpha-spitter', 'jetpack-hog'];
const chaosMixes = {
  spitter: entityVariants.spitter.slice(0, 4),
  'alpha-spitter': entityVariants.spitter.slice(4),
  'jetpack-hog': [...entityVariants.hog, ...entityVariants.stinger]
};
const reportedCreatures = new Set([...creatures, hatcherType]);
const hatcherIntervalMs = 60_000;
const maxHatchers = 600;
const nobeliskTypes = new Set(entityVariants.nobelisk);
const fireworksType = 'fireworks';
const fireworksCount = 250;
const fireworksFlightLimit = 32;
const nukeIntervalMs = 15_000;
const nukeWaveIntervalMs = 30_000;
const maxNukesPerWave = 5;
const viewerActions = new Map([
  ['turn-around', { ready: 'turnReady', interval: 20_000, error: 'turn_unavailable' }],
  ['equipment-drop', { ready: 'equipmentDropReady', interval: 45_000, error: 'drop_unavailable' }]
]);
const backpackQueueMs = 8_000;
const operatorBackpackActions = new Set(['snapshot', 'restore', 'undo']);
const backpackSuccess = new Set(['saved', 'restored', 'undone', 'turned']);
const activeCreatureJobStates = new Set(['running', 'waiting_for_game', 'frame_time', 'mob_capacity', 'placement']);
const finalCreatureJobStates = new Set(['completed', 'placement_failed', 'timed_out']);
const jobIdPattern = /^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/i;
const tokenPattern = /^[a-f0-9]{64}$/;
const legacyNobeliskCapacity = 12;
const gameJobSlots = 4;
const limits = {
  creatures: { units: 2400, jobs: 8, burst: 4 },
  nobelisk: { units: 32, jobs: 4, burst: 2 }
};

function maxDirectQuantity(entityId, type) {
  return entityId === 'nobelisk' ? (type === 'nuke' ? 1 : limits.nobelisk.units) : 300;
}

export function getCatalog(config) {
  const presets = Object.entries(config.presets).filter(([, preset]) => !isDisabledPreset(preset))
    .map(([id, { effect, count, cooldownSeconds, creature, nobelisk, choices }]) =>
      ({ id, effect, count, cooldownSeconds, creature, nobelisk, choices }));
  const entities = Object.entries(entityVariants).map(([id, types]) => ({
    id,
    variants: types.map((name, variant) => ({ variant, name, maxQty: maxDirectQuantity(id, name) }))
  }));
  return { presets, entities };
}

function validTypes(types, allowed) {
  return Array.isArray(types) && types.length <= allowed.size &&
    new Set(types).size === types.length && types.every(type => allowed.has(type));
}

function isDisabledPreset(preset) {
  return preset?.effect === 'teleport' || preset?.effect === 'backpack-shuffle' ||
    (preset?.effect === 'creatures' && preset.creature === hatcherType);
}

function readCreatureJob(value, states) {
  const { id, creature, state, requested, spawned, remaining, consecutivePlacementFailures } = value ?? {};
  if (typeof id !== 'string' || !jobIdPattern.test(id) || !reportedCreatures.has(creature) || !states.has(state) ||
      ![requested, spawned, remaining, consecutivePlacementFailures].every(n => Number.isInteger(n) && n >= 0) ||
      requested < 1 || requested > limits.creatures.units || requested !== spawned + remaining ||
      (state === 'completed') !== (remaining === 0) ||
      consecutivePlacementFailures > 32) return null;
  return { id, creature, state, requested, spawned, remaining, consecutivePlacementFailures };
}

function tokenBucket(capacity, perSecond, now) {
  let tokens = capacity;
  let last = now();
  return () => {
    const t = now();
    tokens = Math.min(capacity, tokens + Math.max(0, t - last) * perSecond / 1000);
    last = t;
    if (tokens < 1) return false;
    tokens -= 1;
    return true;
  };
}

export function validateConfig(config) {
  if (!Number.isInteger(config.port) || config.port < 1024 || config.port > 65535) {
    throw new Error('port must be an integer from 1024 to 65535');
  }
  if (!config.presets || typeof config.presets !== 'object' || Array.isArray(config.presets)) {
    throw new Error('presets must be an object');
  }
  for (const [name, p] of Object.entries(config.presets)) {
    if (!/^[a-z0-9-]{1,40}$/.test(name)) throw new Error(`Invalid preset "${name}"`);
    // Keep older configs readable while disabling their retired presets.
    if (isDisabledPreset(p)) continue;
    if (p?.effect === 'nobelisk-chaos') {
      if (p.count !== fireworksCount || !Number.isInteger(p.cooldownSeconds) ||
          p.cooldownSeconds < 30 || p.cooldownSeconds > 600) {
        throw new Error(`Invalid Nobelisk Chaos preset "${name}"`);
      }
      continue;
    }
    if (p?.effect === 'hatcher-test' || p?.effect === 'chaos-mode') {
      const maxCount = p.effect === 'chaos-mode'
        ? Math.min(maxHatchers, Math.floor(limits.creatures.units / chaosCreatures.length)) : maxHatchers;
      if (!Number.isInteger(p.count) || p.count < 1 || p.count > maxCount || !Number.isInteger(p.cooldownSeconds) ||
          p.cooldownSeconds < hatcherIntervalMs / 1000 || p.cooldownSeconds > 600) {
        throw new Error(`Invalid ${p.effect} preset "${name}"`);
      }
      continue;
    }
    if (p?.effect === 'random') {
      if (!Array.isArray(p.choices) || p.choices.length < 2 || p.choices.length > 8 ||
          new Set(p.choices).size !== p.choices.length || !p.choices.every(choice => {
            if (typeof choice !== 'string' || !Object.hasOwn(config.presets, choice)) return false;
            const target = config.presets[choice];
            return target?.effect === 'creatures' || target?.effect === 'turn-around' ||
              target?.effect === 'backpack-shuffle';
          }) || !Number.isInteger(p.cooldownSeconds) || p.cooldownSeconds < 20 || p.cooldownSeconds > 600) {
        throw new Error(`Invalid random preset "${name}"`);
      }
      continue;
    }
    const isCreature = p?.effect === 'creatures' && creatures.has(p.creature);
    const isNukeWave = p?.effect === 'nuke-wave';
    const type = p?.effect === 'cluster' ? 'cluster' : p?.nobelisk;
    const isNobelisk = isNukeWave || p?.effect === 'cluster' || (p?.effect === 'nobelisk' && nobeliskTypes.has(type));
    const playerAction = viewerActions.get(p?.effect);
    const maxCount = isCreature ? limits.creatures.units
      : isNukeWave ? maxNukesPerWave : (type === 'nuke' || playerAction ? 1 : limits.nobelisk.units);
    const minCooldown = isNukeWave ? nukeWaveIntervalMs / 1000 : playerAction ? playerAction.interval / 1000 : 2;
    if ((!isCreature && !isNobelisk && !playerAction) || !Number.isInteger(p.count) ||
        p.count < (isNukeWave ? 2 : 1) || p.count > maxCount ||
        !Number.isInteger(p.cooldownSeconds) || p.cooldownSeconds < minCooldown || p.cooldownSeconds > 600) {
      throw new Error(`Invalid preset "${name}"`);
    }
  }
  const bot = config.streamerBot;
  if (bot?.enabled) {
    const url = new URL(bot.url);
    if (url.protocol !== 'ws:' || url.hostname !== '127.0.0.1' || url.username || url.password || url.hash) {
      throw new Error('streamerBot.url must be a loopback ws://127.0.0.1 address');
    }
    for (const group of [bot.commands, bot.rewards]) {
      if (!group || typeof group !== 'object' || Array.isArray(group)) {
        throw new Error('streamerBot commands and rewards must be objects');
      }
      for (const [key, preset] of Object.entries(group)) {
        if (!key || !Object.hasOwn(config.presets, preset)) {
          throw new Error(`Invalid Streamer.bot mapping "${key}"`);
        }
      }
    }
  }
  return config;
}

export function createBridge(config, keys, now = () => performance.now(), { autoArmOnWorldReady = false } = {}) {
  let session = null;
  let lastSeen = 0;
  let details = {};
  let pioneerWasReady = false;
  let pioneerDeath;
  let lastAutoArmedSession;
  let armed = false;
  let epoch = randomUUID();
  let queue = [];
  let delivery = null;
  let nextNukeAt = -Infinity;
  const lastViewerActionAt = new Map();
  let lastHatcherAt = -Infinity;
  let pendingBackpack = null;
  const backpackResults = new Map();
  let webhookReaders = 0;
  const admitWebhook = tokenBucket(16, 12, now);
  const admitEvent = {
    creatures: tokenBucket(limits.creatures.burst, 1, now),
    nobelisk: tokenBucket(limits.nobelisk.burst, 1, now)
  };
  const lastUsed = new Map();
  const seen = new Map();
  const gameAuth = Buffer.from(`Bearer ${keys.gameToken}`);
  const webhookAuth = Buffer.from(`Bearer ${keys.webhookToken}`);

  function finishBackpack(code, stacks = 0) {
    const { id, action } = pendingBackpack;
    backpackResults.set(id, { id, action, code, stacks });
    if (backpackResults.size > 16) backpackResults.delete(backpackResults.keys().next().value);
    pendingBackpack = null;
  }

  function queueBackpack(action) {
    if (!session) return [409, { error: 'game_not_ready' }];
    const viewerAction = viewerActions.get(action);
    if (viewerAction ? !details[viewerAction.ready] : !details.backpack?.ready) {
      return [409, { error: viewerAction?.error ?? 'backpack_unavailable' }];
    }
    if (details.slow) return [429, { error: 'game_throttling' }];
    if (pendingBackpack) return [429, { error: 'backpack_busy' }];
    pendingBackpack = { id: randomUUID(), action, expires: now() + backpackQueueMs, delivered: false };
    return [202, { accepted: true, id: pendingBackpack.id, count: 1 }];
  }

  function stop() {
    armed = false;
    epoch = randomUUID();
    queue = [];
    delivery = null;
    if (pendingBackpack) finishBackpack(pendingBackpack.delivered ? 'unconfirmed' : 'cancelled');
    lastUsed.clear();
  }

  function expire() {
    const t = now();
    if (session && t - lastSeen > heartbeatMs) {
      stop();
      session = null;
      details = {};
      pioneerWasReady = false;
    }
    if (queue.some(job => job.expires <= t)) queue = queue.filter(job => job.expires > t);
    if (delivery && delivery.expires <= t) delivery = null;
    if (pendingBackpack && pendingBackpack.expires <= t) {
      finishBackpack(pendingBackpack.delivered ? 'unconfirmed' : 'expired');
    }
    for (const [id, expires] of seen) {
      if (expires > t) break;
      seen.delete(id);
    }
  }

  function recordPreset(preset, eventId, time) {
    lastUsed.set(preset, time);
    if (eventId) {
      seen.set(eventId, time + 600_000);
      if (seen.size > 4096) seen.delete(seen.keys().next().value);
    }
  }

  function trigger(preset, eventId) {
    if (typeof preset !== 'string' || !Object.hasOwn(config.presets, preset)) {
      return [400, { error: 'unknown_preset' }];
    }
    return triggerEffect(preset, config.presets[preset], eventId);
  }

  function triggerWebhook(data) {
    const hasEntityFields = ['entityId', 'variant', 'qty'].some(key => Object.hasOwn(data, key));
    if (!hasEntityFields) return trigger(data.preset, data.eventId);
    if (Object.hasOwn(data, 'preset')) return [400, { error: 'ambiguous_trigger' }];
    const { entityId, variant, qty, eventId } = data;
    if (typeof entityId !== 'string' || !Object.hasOwn(entityVariants, entityId) || !Number.isInteger(variant)) {
      return [400, { error: 'unknown_entity_variant' }];
    }
    const type = entityVariants[entityId][variant];
    if (type === undefined) return [400, { error: 'unknown_entity_variant' }];
    if (!Number.isInteger(qty) || qty < 1 || qty > maxDirectQuantity(entityId, type)) {
      return [400, { error: 'invalid_qty' }];
    }
    const effect = entityId === 'nobelisk' ? { effect: 'nobelisk', nobelisk: type, cooldownSeconds: 10 }
      : entityId === 'hatcher' ? { effect: 'hatcher-test', cooldownSeconds: hatcherIntervalMs / 1000 }
      : { effect: 'creatures', creature: type, cooldownSeconds: 10 };
    return triggerEffect(`entity:${entityId}:${variant}`, { ...effect, count: qty }, eventId);
  }

  function triggerEffect(preset, p, eventId) {
    expire();
    if (eventId !== undefined && (typeof eventId !== 'string' || !eventId || eventId.length > 200)) {
      return [400, { error: 'invalid_event_id' }];
    }
    if (isDisabledPreset(p)) return [409, { error: 'effect_disabled' }];
    if (seen.has(eventId)) return [200, { duplicate: true }];
    if (!armed || !details.ready) return [409, { error: 'game_not_armed_or_ready' }];
    const t = now();
    if (p.effect !== 'random') return enqueuePreset(preset, p, eventId, t);

    const waitMs = p.cooldownSeconds * 1000 - (t - (lastUsed.get(preset) ?? -Infinity));
    if (waitMs > 0) return [429, { error: 'cooldown', retryAfterMs: waitMs }];
    const choices = p.choices.filter(choice => !isDisabledPreset(config.presets[choice]));
    let busy = false;
    while (choices.length) {
      const [selected] = choices.splice(randomInt(choices.length), 1);
      const [code, result] = enqueuePreset(selected, config.presets[selected], eventId, t);
      if (code === 202) {
        lastUsed.set(preset, t);
        return [code, { ...result, selectedPreset: selected }];
      }
      busy ||= code === 429;
    }
    return busy ? [429, { error: 'random_choices_busy' }] : [409, { error: 'random_choices_unavailable' }];
  }

  function enqueuePreset(preset, p, eventId, t) {
    const isNukeWave = p.effect === 'nuke-wave';
    const isHatcher = p.effect === 'hatcher-test';
    const isChaos = p.effect === 'chaos-mode';
    const isFireworks = p.effect === 'nobelisk-chaos';
    const effect = isHatcher || isChaos ? 'creatures'
      : p.effect === 'cluster' || isNukeWave || isFireworks ? 'nobelisk' : p.effect;
    const waveCreatures = isChaos ? chaosCreatures : [isHatcher ? hatcherType : p.creature];
    if (details.slow && effect !== 'creatures' && !isFireworks) return [429, { error: 'game_throttling' }];
    const type = isFireworks ? fireworksType : isNukeWave ? 'nuke' : p.effect === 'cluster' ? 'cluster' : p.nobelisk;
    const waitMs = p.cooldownSeconds * 1000 - (t - (lastUsed.get(preset) ?? -Infinity));
    const viewerAction = viewerActions.get(effect);
    if (viewerAction) {
      const retryAfterMs = Math.max(waitMs, viewerAction.interval - (t - (lastViewerActionAt.get(effect) ?? -Infinity)));
      if (retryAfterMs > 0) return [429, { error: 'cooldown', retryAfterMs }];
      const result = queueBackpack(effect);
      if (result[0] === 202) {
        lastViewerActionAt.set(effect, t);
        recordPreset(preset, eventId, t);
      }
      return result;
    }
    const availableCreatures = details.creatureTypes ?? legacyCreatures;
    if (effect === 'creatures' && waveCreatures.some(creature => !availableCreatures.includes(creature))) {
      return [409, { error: 'creature_unavailable' }];
    }
    if (isHatcher || isChaos) {
      const retryAfterMs = hatcherIntervalMs - (t - lastHatcherAt);
      if (retryAfterMs > 0) return [429, { error: 'cooldown', retryAfterMs }];
      const { alive, pending, capacity } = details.hatcherTrial;
      if (p.count > capacity) return [409, { error: 'hatcher_wave_unavailable' }];
      const waiting = queue.reduce((count, job) => count + (job.creature === hatcherType ? job.count : 0),
        delivery?.creature === hatcherType ? delivery.count : 0);
      if (alive + pending + waiting + p.count > capacity) {
        return [429, { error: 'hatcher_capacity' }];
      }
    }
    if (isFireworks && !details.nobeliskChaos) return [409, { error: 'nobelisk_chaos_unavailable' }];
    if (effect === 'nobelisk' && !isFireworks && !(details.nobeliskTypes ?? ['cluster']).includes(type)) {
      return [409, { error: 'nobelisk_unavailable' }];
    }
    if (isNukeWave && details.pending?.nuke === undefined) {
      return [409, { error: 'nuke_wave_unavailable' }];
    }
    if (type === 'nuke') {
      const retryAfterMs = nextNukeAt - t;
      if (retryAfterMs > 0) return [429, { error: 'cooldown', retryAfterMs }];
      if (details.pending?.nuke > 0 || delivery?.nobelisk === 'nuke' || queue.some(job => job.nobelisk === 'nuke')) {
        return [429, { error: 'nuke_wave_busy' }];
      }
    }
    if (waitMs > 0) return [429, { error: 'cooldown', retryAfterMs: waitMs }];
    const lane = limits[effect];
    const waiting = queue.filter(job => job.effect === effect);
    const delivering = delivery?.effect === effect;
    const queuedUnits = waiting.reduce((total, job) => total + job.count, delivering ? delivery.count : 0);
    const gameUnits = (effect === 'nobelisk' ? details.pending?.cluster : details.pending?.creatures) ?? 0;
    if (effect === 'nobelisk' && (isFireworks ? gameUnits + queuedUnits > 0
        : details.pending?.fireworks || delivery?.nobelisk === fireworksType || waiting.some(job => job.nobelisk === fireworksType))) {
      return [429, { error: 'nobelisk_lane_busy' }];
    }
    const unitLimit = isFireworks ? fireworksCount
      : effect === 'nobelisk' ? details.nobeliskCapacity ?? legacyNobeliskCapacity : lane.units;
    const units = p.count * waveCreatures.length;
    if (units > unitLimit) return [409, { error: 'nobelisk_capacity_unavailable' }];
    if (waiting.length + Number(delivering) + waveCreatures.length > lane.jobs || gameUnits + queuedUnits + units > unitLimit) {
      return [429, { error: 'queue_full' }];
    }
    if (!admitEvent[effect]()) return [429, { error: 'event_rate_limit' }];
    const jobs = waveCreatures.map(creature => {
      const variants = isChaos && details.mixedCreatures
        ? chaosMixes[creature]?.filter(variant => availableCreatures.includes(variant)) : undefined;
      return {
        id: randomUUID(), effect, count: p.count, expires: t + (effect === 'creatures' ? creatureQueueLifeMs : queueLifeMs),
        ...(effect === 'creatures' ? { creature, ...(variants?.length > 1 && { variants }) } : { nobelisk: type })
      };
    });
    queue.push(...jobs);
    recordPreset(preset, eventId, t);
    if (isHatcher || isChaos) lastHatcherAt = t;
    if (type === 'nuke') nextNukeAt = t + (isNukeWave ? nukeWaveIntervalMs : nukeIntervalMs);
    const receipt = { accepted: true, id: jobs[0].id, count: units };
    if (isChaos) receipt.waves = jobs.map(({ id, creature, count, variants }) =>
      ({ id, creature, count, ...(variants && { variants }) }));
    return [202, receipt];
  }

  function sync(data) {
    if (typeof data.session !== 'string' || !/^[a-f0-9-]{36}$/i.test(data.session) ||
        typeof data.ready !== 'boolean' || !Number.isInteger(data.free) || data.free < 0 || data.free > gameJobSlots ||
        !Number.isSafeInteger(data.active) || data.active < 0 ||
        typeof data.slow !== 'boolean' ||
        [data.turnReady, data.equipmentDropReady, data.mixedCreatures, data.respawning]
          .some(flag => flag !== undefined && typeof flag !== 'boolean') || (data.respawning && data.ready)) {
      return [400, { error: 'invalid_game_status' }];
    }
    let mobs;
    if (data.mobs !== undefined) {
      if (!data.mobs || typeof data.mobs !== 'object' || Array.isArray(data.mobs)) {
        return [400, { error: 'invalid_mob_counts' }];
      }
      const { spawned, killed, missing } = data.mobs;
      if (![spawned, killed, missing].every(count => Number.isSafeInteger(count) && count >= 0) ||
          spawned - killed - missing !== data.active) {
        return [400, { error: 'invalid_mob_counts' }];
      }
      mobs = { spawned, alive: data.active, killed, missing };
    }
    const capacity = data.nobeliskCapacity === undefined ? legacyNobeliskCapacity : data.nobeliskCapacity;
    if (!Number.isInteger(capacity) || capacity < legacyNobeliskCapacity || capacity > limits.nobelisk.units) {
      return [400, { error: 'invalid_nobelisk_capacity' }];
    }
    const types = data.nobeliskTypes;
    if (types !== undefined && !validTypes(types, nobeliskTypes)) {
      return [400, { error: 'invalid_nobelisk_types' }];
    }
    const supportsFireworks = data.nobeliskChaos === true;
    if (data.nobeliskChaos !== undefined &&
        (!supportsFireworks || types?.length !== nobeliskTypes.size || capacity !== limits.nobelisk.units)) {
      return [400, { error: 'invalid_nobelisk_chaos_capability' }];
    }
    let pending;
    if (data.pending !== undefined) {
      const { creatures: creatureCount, cluster, nuke, fireworks } = data.pending ?? {};
      if (!Number.isInteger(creatureCount) || creatureCount < 0 || creatureCount > limits.creatures.units ||
          !Number.isInteger(cluster) || cluster < 0 || cluster > (fireworks ? fireworksCount : capacity) ||
          (fireworks !== undefined && (fireworks !== true || !supportsFireworks || cluster === 0 || nuke > 1)) ||
          (nuke !== undefined && (!Number.isInteger(nuke) || nuke < 0 || nuke > Math.min(cluster, maxNukesPerWave)))) {
        return [400, { error: 'invalid_pending_counts' }];
      }
      pending = { creatures: creatureCount, cluster, ...(nuke !== undefined && { nuke }), ...(fireworks && { fireworks }) };
    }
    let creatureJobs;
    if (data.creatureJobs !== undefined) {
      if (!Array.isArray(data.creatureJobs) || data.creatureJobs.length > gameJobSlots - data.free) {
        return [400, { error: 'invalid_creature_jobs' }];
      }
      creatureJobs = data.creatureJobs.map(job => readCreatureJob(job, activeCreatureJobStates));
      if (creatureJobs.some(job => !job) || new Set(creatureJobs.map(job => job.id)).size !== creatureJobs.length ||
          (pending && creatureJobs.reduce((sum, job) => sum + job.remaining, 0) !== pending.creatures)) {
        return [400, { error: 'invalid_creature_jobs' }];
      }
    }
    let lastCreatureJob;
    if (data.lastCreatureJob !== undefined) {
      lastCreatureJob = readCreatureJob(data.lastCreatureJob, finalCreatureJobStates);
      if (!lastCreatureJob || creatureJobs?.some(job => job.id === lastCreatureJob.id)) {
        return [400, { error: 'invalid_creature_job_result' }];
      }
    }
    if (data.creatureTypes !== undefined && !validTypes(data.creatureTypes, reportedCreatures)) {
      return [400, { error: 'invalid_creature_types' }];
    }
    let hatcherTrial;
    if (data.hatcherTrial !== undefined) {
      const { alive, pending: waiting, capacity: hatcherCapacity = 1 } = data.hatcherTrial ?? {};
      if (!data.creatureTypes?.includes(hatcherType) ||
          !Number.isSafeInteger(alive) || alive < 0 || alive > data.active ||
          !Number.isInteger(waiting) || waiting < 0 ||
          !Number.isInteger(hatcherCapacity) || hatcherCapacity < 1 || hatcherCapacity > maxHatchers ||
          alive + waiting > hatcherCapacity ||
          (pending && waiting > pending.creatures)) {
        return [400, { error: 'invalid_hatcher_status' }];
      }
      hatcherTrial = { alive, pending: waiting, capacity: hatcherCapacity };
    }
    const creatureTypes = data.creatureTypes?.filter(type => creatures.has(type) || (type === hatcherType && hatcherTrial));
    let drops;
    if (data.nobelisks !== undefined) {
      const { launched, impacted, expired, cancelled, inFlight } = data.nobelisks ?? {};
      const flightLimit = pending?.fireworks ? fireworksFlightLimit : capacity > legacyNobeliskCapacity ? 4 : 3;
      if (![launched, impacted, expired, cancelled, inFlight].every(count => Number.isSafeInteger(count) && count >= 0) ||
          inFlight > flightLimit || launched - impacted - expired - cancelled !== inFlight ||
          (pending && inFlight > pending.cluster)) {
        return [400, { error: 'invalid_nobelisk_counts' }];
      }
      drops = { launched, impacted, expired, cancelled, inFlight };
    }
    let backpack;
    if (data.backpack !== undefined) {
      const { ready, snapshot, recovery, last } = data.backpack ?? {};
      if (![ready, snapshot, recovery].every(value => typeof value === 'boolean')) {
        return [400, { error: 'invalid_backpack_status' }];
      }
      backpack = { ready, snapshot, recovery };
      if (last !== undefined) {
        const { id, action, code, stacks } = last ?? {};
        if (typeof id !== 'string' || !jobIdPattern.test(id) ||
            (!operatorBackpackActions.has(action) && action !== 'shuffle' && !viewerActions.has(action)) ||
            typeof code !== 'string' || !/^[a-z_]{1,40}$/.test(code) ||
            !Number.isInteger(stacks) || stacks < 0 || stacks > 512) {
          return [400, { error: 'invalid_backpack_result' }];
        }
        backpack.last = { id, action, code, stacks };
      }
    }
    if (data.session !== session) {
      // A second game instance cannot take over while the first is connected.
      if (session) return [409, { error: 'session_in_use' }];
      session = data.session;
    }
    if (data.respawning && !details.respawning) {
      if (pioneerWasReady) {
        pioneerDeath = { id: randomUUID(), expires: now() + deathAlertMs };
        pioneerWasReady = false;
      }
      const wasArmed = armed;
      stop();
      armed = wasArmed;
    }
    if (data.ready) pioneerWasReady = true;
    if (autoArmOnWorldReady && data.ready && lastAutoArmedSession !== session) {
      if (lastAutoArmedSession !== null) armed = true;
      lastAutoArmedSession = session;
    }
    lastSeen = now();
    details = { ready: data.ready, free: data.free, active: data.active, slow: data.slow };
    if (data.respawning) details.respawning = true;
    if (mobs) details.mobs = mobs;
    if (pending) details.pending = pending;
    if (creatureJobs) details.creatureJobs = creatureJobs;
    if (lastCreatureJob) details.lastCreatureJob = lastCreatureJob;
    if (data.nobeliskCapacity !== undefined) details.nobeliskCapacity = capacity;
    if (creatureTypes) details.creatureTypes = creatureTypes;
    if (hatcherTrial) details.hatcherTrial = hatcherTrial;
    if (types !== undefined) details.nobeliskTypes = types;
    if (supportsFireworks) details.nobeliskChaos = true;
    if (drops) details.nobelisks = drops;
    if (data.turnReady !== undefined) details.turnReady = data.turnReady;
    if (data.equipmentDropReady !== undefined) details.equipmentDropReady = data.equipmentDropReady;
    if (data.mixedCreatures === true) details.mixedCreatures = true;
    if (backpack) {
      details.backpack = backpack;
      const last = backpack.last;
      if (pendingBackpack && last?.id === pendingBackpack.id && last.action === pendingBackpack.action) {
        finishBackpack(last.code, last.stacks);
      } else if (last && backpackResults.get(last.id)?.code === 'unconfirmed') {
        const prior = backpackResults.get(last.id);
        if (prior.action === last.action) backpackResults.set(last.id, last);
      }
    }
    if (delivery && delivery.id === data.ack) delivery = null;
    if (data.ready) {
      const available = job => {
        if (job.nobelisk === fireworksType) return supportsFireworks;
        if (job.effect === 'nobelisk') return (types ?? ['cluster']).includes(job.nobelisk) &&
          job.count <= capacity &&
          (job.nobelisk !== 'nuke' || job.count === 1 || pending?.nuke !== undefined);
        return (creatureTypes ?? legacyCreatures).includes(job.creature) &&
          (!job.variants || (data.mixedCreatures && job.variants.every(variant => creatureTypes?.includes(variant)))) &&
          (job.creature !== hatcherType || job.count <= hatcherTrial.capacity);
      };
      if (delivery && !available(delivery)) delivery = null;
      if (queue.length) queue = queue.filter(available);
    }
    if (armed && data.ready && !delivery && data.free > 0 && queue.length) {
      let index = queue.findIndex(job => job.effect === 'nobelisk' && (!data.slow || job.nobelisk === fireworksType));
      const creatureIndex = queue.findIndex(job => job.effect === 'creatures');
      if (creatureJobs) {
        // Reserve a game slot for each kind and give the less busy lane first use of free slots.
        const creatureSlots = creatureJobs.length;
        const nobeliskSlots = gameJobSlots - data.free - creatureSlots;
        if (nobeliskSlots === gameJobSlots - 1) index = -1;
        if (creatureIndex >= 0 && creatureSlots < gameJobSlots - 1 &&
            (index < 0 || creatureSlots <= nobeliskSlots)) index = creatureIndex;
      } else if (index < 0 && data.free > 1) {
        index = creatureIndex;
      }
      if (index >= 0) [delivery] = queue.splice(index, 1);
    }
    const response = { epoch, armed };
    const playerAction = viewerActions.get(pendingBackpack?.action);
    if (pendingBackpack && !data.slow && (armed || operatorBackpackActions.has(pendingBackpack.action)) &&
        (playerAction ? details[playerAction.ready] : backpack?.ready)) {
      const { id, action } = pendingBackpack;
      response.backpackCommand = { id, action };
      pendingBackpack.delivered = true;
    }
    if (armed && data.ready && delivery) {
      const { expires, ...job } = delivery;
      if (types === undefined && job.effect === 'nobelisk') {
        job.effect = 'cluster';
        delete job.nobelisk;
      }
      response.job = job;
    }
    return [200, response];
  }

  function status() {
    return { armed, connected: session !== null, game: details, queued: queue.length, delivering: !!delivery,
      ...(pendingBackpack ? { backpackPending: pendingBackpack.action } : {}) };
  }

  const server = createServer({
    requestTimeout: 5000, headersTimeout: 3000, connectionsCheckingInterval: 1000,
    keepAliveTimeout: 1000, maxHeaderSize: 8192
  },
    async (req, res) => {
      const send = (code, value, close = false) => {
        if (res.destroyed) return;
        const headers = { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' };
        if (close) headers.Connection = 'close';
        res.writeHead(code, headers);
        res.end(JSON.stringify(value));
      };
      if (req.headers.host !== `127.0.0.1:${config.port}`) return send(421, { error: 'invalid_host' });
      const path = req.url;
      const auth = Buffer.from(req.headers.authorization ?? '');
      const webhook = path === '/v1/trigger' || path === '/v1/connection' || path === '/v1/presets';
      const expected = webhook ? webhookAuth : gameAuth;
      if (auth.length !== expected.length || !timingSafeEqual(auth, expected)) {
        return send(401, { error: 'unauthorized' }, true);
      }
      if (webhook && (webhookReaders >= 4 || !admitWebhook())) {
        return send(429, { error: 'webhook_rate_limit' }, true);
      }
      if (webhook) webhookReaders++;
      try {
        if (!webhook) expire();
        if (req.method === 'GET' && path === '/v1/connection') {
          expire();
          const pioneerDeathId = pioneerDeath?.expires > now() ? pioneerDeath.id : undefined;
          return send(200, {
            armed, connected: session !== null, ready: details.ready === true, slow: details.slow === true,
            ...(pioneerDeathId && { pioneerDeathId })
          });
        }
        if (req.method === 'GET' && path === '/v1/presets') return send(200, getCatalog(config));
        if (req.method === 'GET' && path === '/v1/status') return send(200, status());
        if (req.method === 'GET' && path.startsWith('/v1/inventory/result/')) {
          const id = path.slice('/v1/inventory/result/'.length);
          if (!jobIdPattern.test(id)) return send(400, { error: 'invalid_command_id' });
          if (pendingBackpack?.id === id) return send(202, { pending: true });
          const result = backpackResults.get(id);
          return result ? send(200, result) : send(404, { error: 'unknown_command' });
        }
        if (req.method !== 'POST') return send(404, { error: 'unknown_route' });
        if (path === '/v1/disarm') {
          // A disarm during startup also applies to the first heartbeat for this host.
          if (autoArmOnWorldReady) lastAutoArmedSession = session;
          stop();
          return send(200, status());
        }
        if (path === '/v1/arm') {
          if (!session || !details.ready) return send(409, { error: 'game_not_ready' });
          armed = true;
          return send(200, status());
        }
        if (!['/v1/trigger', '/v1/game', '/v1/inventory'].includes(path)) return send(404, { error: 'unknown_route' });
        if (!/^application\/json(?:\s*;|$)/i.test(req.headers['content-type'] ?? '')) {
          return send(415, { error: 'expected_json' });
        }
        const data = await readBody(req);
        if (path === '/v1/game') {
          expire();
          return send(...sync(data));
        }
        if (path === '/v1/inventory') {
          if (!operatorBackpackActions.has(data.action)) return send(400, { error: 'invalid_inventory_action' });
          return send(...queueBackpack(data.action));
        }
        return send(...triggerWebhook(data));
      } catch (error) {
        return send(error.status ?? 400, { error: error.message }, true);
      } finally {
        if (webhook) webhookReaders--;
      }
    });
  server.maxConnections = 32;
  server.setTimeout(5000, socket => socket.destroy());
  return { server, trigger };
}

async function runBackpackCommand(endpoint, token, action) {
  if (!operatorBackpackActions.has(action) && action !== 'status') {
    throw new Error('Usage: node bridge.mjs inventory snapshot|restore|undo|status');
  }
  const headers = { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' };
  if (action === 'status') {
    const response = await fetch(`${endpoint}/status`, { headers, signal: AbortSignal.timeout(3000) });
    const state = await response.json();
    console.log(JSON.stringify(response.ok ? { connected: state.connected, ...state.game.backpack,
      ...(state.backpackPending ? { pending: state.backpackPending } : {}) } : state));
    if (!response.ok) process.exitCode = 1;
    return;
  }
  const response = await fetch(`${endpoint}/inventory`, {
    method: 'POST', headers, body: JSON.stringify({ action }), signal: AbortSignal.timeout(3000)
  });
  const receipt = await response.json();
  if (response.status !== 202) {
    console.log(JSON.stringify(receipt));
    process.exitCode = 1;
    return;
  }
  const deadline = performance.now() + backpackQueueMs + 1000;
  while (performance.now() < deadline) {
    await delay(250);
    const result = await fetch(`${endpoint}/inventory/result/${receipt.id}`, {
      headers, signal: AbortSignal.timeout(2500)
    });
    if (result.status === 202) continue;
    const outcome = await result.json();
    console.log(JSON.stringify(outcome));
    if (!result.ok || !backpackSuccess.has(outcome.code)) process.exitCode = 1;
    return;
  }
  console.log(JSON.stringify({ id: receipt.id, action, code: 'unconfirmed' }));
  process.exitCode = 1;
}

function readBody(req) {
  return new Promise((resolveBody, reject) => {
    const chunks = [];
    let size = 0;
    req.on('data', chunk => {
      size += chunk.length;
      if (size <= bodyLimit) chunks.push(chunk);
      else reject(Object.assign(new Error('body_too_large'), { status: 413 }));
    });
    req.on('error', reject);
    req.on('end', () => {
      try {
        const value = JSON.parse(Buffer.concat(chunks).toString('utf8'));
        if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error();
        resolveBody(value);
      } catch {
        reject(new Error('invalid_json_object'));
      }
    });
  });
}

async function readConfig(path) {
  return validateConfig(JSON.parse(await readFile(path, 'utf8')));
}

function watchHost(parentPid) {
  if (!Number.isInteger(parentPid) || parentPid < 1 || parentPid > 0x7fffffff || parentPid !== process.ppid) {
    throw new Error('The bundled bridge must be started by its host game process');
  }
  const check = () => {
    try {
      process.kill(parentPid, 0);
    } catch {
      process.exit(0);
    }
  };
  check();
  setInterval(check, 2000).unref();
}

async function main() {
  const path = join(stateDir, 'link.json');
  const action = process.argv[2] ?? 'serve';
  if (action === 'serve' || action === 'serve-host') {
    const bundled = action === 'serve-host';
    if (bundled) watchHost(Number(process.argv[3]));
    const defaultConfigPath = join(root, 'bridge.config.json');
    let config;
    if (bundled) {
      try {
        config = await readConfig(join(stateDir, 'bridge.local.json'));
      } catch (error) {
        if (error.code !== 'ENOENT') throw error;
      }
    }
    const usingDefaultConfig = !config;
    config ??= await readConfig(bundled ? defaultConfigPath : process.argv[3] ?? defaultConfigPath);
    await mkdir(stateDir, { recursive: true });
    let keys;
    try {
      keys = JSON.parse(await readFile(path, 'utf8'));
      if (!tokenPattern.test(keys.gameToken) || !tokenPattern.test(keys.webhookToken) ||
          keys.gameToken === keys.webhookToken) throw new Error(`Invalid tokens in ${path}`);
    } catch (error) {
      if (error.code !== 'ENOENT') throw error;
      keys = { gameToken: randomBytes(32).toString('hex'), webhookToken: randomBytes(32).toString('hex') };
    }
    if (bundled && usingDefaultConfig && Number.isInteger(keys.port) && keys.port >= 1024 && keys.port <= 65535) {
      config.port = keys.port;
    }
    const bridge = createBridge(config, keys, undefined, { autoArmOnWorldReady: bundled });
    await new Promise((ready, reject) => {
      bridge.server.once('error', reject);
      bridge.server.listen(config.port, '127.0.0.1', ready);
    });
    const temp = `${path}.${randomUUID()}.tmp`;
    await writeFile(temp, JSON.stringify({ port: config.port, ...keys }), { mode: 0o600 });
    await rename(temp, path);
    console.log(`Factory Live listening on http://127.0.0.1:${config.port} (disarmed)`);
    console.log(`Link file: ${path}`);
    const closeBot = config.streamerBot?.enabled
      ? connectStreamerBot(config.streamerBot, bridge.trigger, process.env.FACTORYLIVE_STREAMERBOT_PASSWORD)
      : () => {};
    const stop = () => {
      closeBot();
      bridge.server.close();
    };
    process.once('SIGINT', stop);
    process.once('SIGTERM', stop);
    return;
  }
  if (!['status', 'arm', 'disarm', 'trigger', 'inventory'].includes(action)) {
    throw new Error('Usage: node bridge.mjs [serve [config] | status | arm | disarm | trigger <preset> | inventory <action>]');
  }
  const keys = JSON.parse(await readFile(path, 'utf8'));
  if (!Number.isInteger(keys.port) || keys.port < 1024 || keys.port > 65535 ||
      !tokenPattern.test(keys.gameToken) || !tokenPattern.test(keys.webhookToken)) {
    throw new Error(`Invalid link file at ${path}`);
  }
  const endpoint = `http://127.0.0.1:${keys.port}/v1`;
  if (action === 'inventory') return runBackpackCommand(endpoint, keys.gameToken, process.argv[3]);
  const options = {
    method: action === 'status' ? 'GET' : 'POST',
    headers: { Authorization: `Bearer ${action === 'trigger' ? keys.webhookToken : keys.gameToken}` },
    signal: AbortSignal.timeout(3000)
  };
  if (action === 'trigger') {
    options.headers['Content-Type'] = 'application/json';
    options.body = JSON.stringify({ preset: process.argv[3] });
  }
  const response = await fetch(`${endpoint}/${action}`, options);
  console.log(await response.text());
  if (!response.ok) process.exitCode = 1;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(error => {
    console.error(error.message);
    process.exit(1);
  });
}
