const { Plugin } = require('@lumiastream/plugin');
const { readFile } = require('node:fs/promises');
const { join } = require('node:path');
const manifest = require('./manifest.json');

const presetPattern = /^[a-z0-9-]{1,40}$/;
const tokenPattern = /^[a-f0-9]{64}$/;
const eventIdPattern = /^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/i;
const actionDefinitions = new Map(manifest.config.actions.map(action => [action.type, action]));
const builtInPresets = new Set(actionDefinitions.get('trigger_preset').fields[0].options.map(option => option.value));
const chaosPlugin = manifest.id === 'factory_live_chaos';
const spawnActions = new Map([
  ['spawn_hog', 'hog'], ['spawn_spitter', 'spitter'], ['spawn_stinger', 'stinger'],
  ['spawn_crab_hatcher', 'hatcher'], ['drop_nobelisk', 'nobelisk']
]);
const fixedPresets = new Map([
  ['mob_chaos', 'chaos-mode'], ['nobelisk_chaos', 'nobelisk-chaos'],
  ['drop_random_equipment', 'equipment-drop'], ['turn_pioneer_around', 'turn-around']
]);
const bridgeControls = new Map([['arm_bridge', 'arm'], ['disarm_bridge', 'disarm']]);

function numericValue(value) {
  if (typeof value === 'number') return value;
  if (typeof value !== 'string' || !/^(?:\d+(?:\.\d+)?|\.\d+)$/.test(value.trim())) return NaN;
  return Number(value.trim());
}

function spawnRequest(type, value) {
  const entityId = spawnActions.get(type);
  const fields = actionDefinitions.get(type).fields;
  const variantField = fields.find(field => field.key === 'variant');
  const quantityField = fields.find(field => field.key === 'quantity');
  const variant = numericValue(value?.variant ?? 0);
  if (!Number.isInteger(variant) || (variantField
    ? !variantField.options.some(option => Number(option.value) === variant) : variant !== 0)) {
    return { error: 'invalid_variant' };
  }
  const quantity = numericValue(value?.quantity ?? quantityField.defaultValue);
  const multiplierInput = value?.multiplier;
  const multiplier = typeof multiplierInput === 'string' && multiplierInput.trim() === ''
    ? 1 : numericValue(multiplierInput ?? 1);
  const qty = Math.round(quantity * multiplier);
  const limit = entityId === 'nobelisk' && variant === 4 ? 1 : quantityField.max;
  if (!Number.isSafeInteger(quantity) || quantity < 1 || quantity > quantityField.max) return { error: 'invalid_quantity' };
  if (!Number.isFinite(multiplier) || multiplier <= 0) return { error: 'invalid_multiplier' };
  if (!Number.isSafeInteger(qty) || qty < 1 || qty > limit) return { error: 'quantity_out_of_range' };
  return { body: { entityId, variant, qty } };
}

const connectionMessages = {
  link_missing: 'Start the Factory Live bridge on this PC.',
  invalid_link: 'Restart the Factory Live bridge to check its local link file.',
  bridge_offline: 'Factory Live bridge is unreachable. Check that its PowerShell window is open.',
  bridge_busy: 'Factory Live bridge is busy. Try again shortly.',
  bridge_upgrade_needed: 'Use Bridge 0.8.0 or newer. If already updated, restart the bridge to refresh its local link.',
  game_disconnected: 'Bridge connected. Load a Satisfactory save as host.',
  game_not_ready: 'Load a Satisfactory save as host and regain control of your pioneer.',
  disarmed: 'Bridge connected and game ready. Arm Factory Live when you want viewer effects.',
  game_throttling: 'Bridge connected. The game is waiting for frame times to recover.',
  ready: 'Factory Live is armed and the game is ready.'
};

class FactoryLivePlugin extends Plugin {
  inFlight = 0;
  unload = new AbortController();
  deathTimer;
  lastDeathId = null;
  deathFeedReady = false;

  async onload() {
    this.unload.abort();
    clearTimeout(this.deathTimer);
    this.unload = new AbortController();
    this.lastDeathId = null;
    this.deathFeedReady = false;
    void this.pollDeaths(this.unload.signal);
  }

  async onunload() {
    this.unload.abort();
    clearTimeout(this.deathTimer);
  }

  async pollDeaths(signal) {
    const result = await this.request();
    if (signal.aborted) return;
    if (result.status === 200) {
      const { pioneerDeathId } = result;
      const isNewDeath = this.deathFeedReady && pioneerDeathId && pioneerDeathId !== this.lastDeathId;
      this.deathFeedReady = true;
      if (pioneerDeathId) this.lastDeathId = pioneerDeathId;
      if (isNewDeath) {
        try {
          await this.lumia.triggerAlert({ alert: 'pioneer_death', showInEventList: true });
        } catch (error) {
          console.warn('Factory Live could not deliver the Pioneer Death alert.', error.message);
        }
      }
    }
    if (!signal.aborted) {
      this.deathTimer = setTimeout(() => void this.pollDeaths(signal), result.status === 200 ? 1500 : 5000);
    }
  }

  async validateAuth() {
    const result = await this.request();
    return { ok: result.status === 200, message: connectionMessages[result.code] };
  }

  async actions({ actions }) {
    let newlyPassedVariables;
    for (const action of actions) {
      const preset = action.value?.preset;
      const isSpawn = spawnActions.has(action.type);
      const control = bridgeControls.get(action.type);
      let result;
      if (control || action.type === 'check_bridge') {
        result = await this.request(undefined, control);
        const message = control && result.status === 200
          ? result.code === 'ready' ? 'Factory Live bridge armed.' : 'Factory Live bridge disarmed. Queued effects cleared.'
          : connectionMessages[result.code];
        await this.lumia.showToast({
          message: message ?? 'Factory Live could not update the bridge.',
          type: result.code === 'ready' ? 'success' : result.status === 200 ? 'info' : 'warning'
        });
      } else if (action.type === 'trigger_preset' || action.type === 'trigger_custom_preset') {
        const valid = typeof preset === 'string' && presetPattern.test(preset) &&
          (action.type === 'trigger_custom_preset' || builtInPresets.has(preset));
        result = valid ? await this.request({ preset }) : { status: 0, code: 'invalid_preset' };
      } else if (fixedPresets.has(action.type)) {
        result = await this.request({ preset: fixedPresets.get(action.type) });
      } else if (isSpawn) {
        const { body, error } = spawnRequest(action.type, action.value);
        result = body ? await this.request(body) : { status: 0, code: error };
      } else {
        continue;
      }
      newlyPassedVariables = {
        factory_live_status: result.status,
        factory_live_result: result.code,
        factory_live_selected_preset: result.selectedPreset ?? '',
        ...(isSpawn && { factory_live_quantity: result.quantity ?? 0 })
      };
      if (chaosPlugin) {
        for (const [key, value] of Object.entries(newlyPassedVariables)) {
          newlyPassedVariables[key.replace(/^factory_live_/, 'factory_live_chaos_')] = value;
        }
      }
    }
    return { newlyPassedVariables };
  }

  async request(body, control) {
    if (this.inFlight >= 4) return { status: 0, code: 'bridge_busy' };
    this.inFlight++;
    try {
      const signal = AbortSignal.any([this.unload.signal, AbortSignal.timeout(2500)]);
      const directory = process.env[chaosPlugin ? 'FACTORYLIVE_CHAOS_HOME' : 'FACTORYLIVE_HOME'] ||
        (process.env.LOCALAPPDATA && join(process.env.LOCALAPPDATA, chaosPlugin ? 'FactoryLiveChaos' : 'FactoryLive'));
      if (!directory) return { status: 0, code: 'link_missing' };
      let link;
      try {
        link = JSON.parse(await readFile(join(directory, 'link.json'), { encoding: 'utf8', signal }));
      } catch (error) {
        const code = error.code === 'ENOENT' ? 'link_missing'
          : error.name === 'AbortError' ? 'bridge_offline' : 'invalid_link';
        return { status: 0, code };
      }
      const token = control ? link?.gameToken : link?.webhookToken;
      if (!Number.isInteger(link?.port) || link.port < 1024 || link.port > 65535 ||
          typeof token !== 'string' || !tokenPattern.test(token)) {
        return { status: 0, code: 'invalid_link' };
      }
      const endpoint = control ?? (body ? 'trigger' : 'connection');
      const response = await fetch('http://127.0.0.1:' + link.port + '/v1/' + endpoint, {
        signal,
        redirect: 'error',
        method: body || control ? 'POST' : 'GET',
        headers: {
          Authorization: 'Bearer ' + token,
          ...(body && { 'Content-Type': 'application/json' })
        },
        body: body ? JSON.stringify(body) : undefined
      });
      const status = response.status;
      // Older bridges authenticate unknown routes with the operator token.
      if (!body && !control && (status === 401 || status === 404)) {
        return { status, code: 'bridge_upgrade_needed' };
      }
      const data = await response.json();
      if (control) {
        if (status === 401) return { status, code: 'invalid_link' };
        const shouldBeArmed = control === 'arm';
        if (status === 200 && data.armed === shouldBeArmed) {
          return { status, code: shouldBeArmed ? 'ready' : 'disarmed' };
        }
        return { status, code: data.error ?? 'bad_bridge_response' };
      }
      if (body) {
        if (status === 202 && data.accepted) {
          return { status, code: 'queued', selectedPreset: data.selectedPreset ?? body.preset, quantity: data.count };
        }
        if (status === 200 && data.duplicate) return { status, code: 'duplicate' };
        if (body.entityId && data.error === 'unknown_preset') return { status, code: 'variant_bridge_upgrade_needed' };
        return { status, code: data.error ?? 'bad_bridge_response' };
      }
      if (status === 429) return { status, code: 'bridge_busy' };
      if (status !== 200 || typeof data.connected !== 'boolean') {
        return { status: 0, code: 'bridge_offline' };
      }
      const code = !data.connected ? 'game_disconnected' : !data.ready ? 'game_not_ready'
        : !data.armed ? 'disarmed' : data.slow ? 'game_throttling' : 'ready';
      const pioneerDeathId = typeof data.pioneerDeathId === 'string' && eventIdPattern.test(data.pioneerDeathId)
        ? data.pioneerDeathId : null;
      return { status, code, pioneerDeathId };
    } catch {
      return { status: 0, code: 'bridge_offline' };
    } finally {
      this.inFlight--;
    }
  }
}

module.exports = FactoryLivePlugin;
