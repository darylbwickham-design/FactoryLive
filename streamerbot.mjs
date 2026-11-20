import { createHash } from 'node:crypto';

const hash = value => createHash('sha256').update(value).digest('base64');

export function connectStreamerBot(config, trigger, password, openSocket = url => new WebSocket(url)) {
  let socket;
  let retryTimer;
  let handshakeTimer;
  let backoff = 1000;
  let stopped = false;
  const commands = new Map(Object.entries(config.commands).map(([name, preset]) => [name.toLowerCase(), preset]));
  const rewards = new Map(Object.entries(config.rewards));
  const events = {};
  if (commands.size) events.Command = ['Triggered'];
  if (rewards.size) events.Twitch = ['RewardRedemption'];
  if (Object.keys(events).length === 0) return () => {};

  function connect() {
    let phase = 'hello';
    const ws = openSocket(config.url);
    socket = ws;
    handshakeTimer = setTimeout(() => {
      console.warn('Streamer.bot handshake timed out');
      ws.close();
    }, 5000);

    function subscribe() {
      phase = 'subscribe';
      ws.send(JSON.stringify({ id: 'factorylive-subscribe', request: 'Subscribe', events }));
    }

    ws.addEventListener('message', ({ data }) => {
      if (typeof data !== 'string' || data.length > 65_536) return;
      let packet;
      try { packet = JSON.parse(data); } catch { return; }
      if (!packet || typeof packet !== 'object') return;
      if (phase === 'hello' && packet.request === 'Hello') {
        const auth = packet.authentication;
        if (!auth) return subscribe();
        if (!password || typeof auth.salt !== 'string' || typeof auth.challenge !== 'string') {
          console.warn('Streamer.bot authentication needs FACTORYLIVE_STREAMERBOT_PASSWORD');
          ws.close();
          return;
        }
        phase = 'authenticate';
        ws.send(JSON.stringify({
          id: 'factorylive-auth', request: 'Authenticate',
          authentication: hash(hash(password + auth.salt) + auth.challenge)
        }));
        return;
      }
      if (phase === 'authenticate' && packet.id === 'factorylive-auth') {
        if (packet.status === 'ok') return subscribe();
        console.warn('Streamer.bot rejected the WebSocket password');
        ws.close();
        return;
      }
      if (phase === 'subscribe' && packet.id === 'factorylive-subscribe') {
        if (packet.status !== 'ok') {
          console.warn('Streamer.bot rejected the WebSocket subscription');
          ws.close();
          return;
        }
        clearTimeout(handshakeTimer);
        backoff = 1000;
        phase = 'ready';
        console.log('Streamer.bot WebSocket subscribed');
        return;
      }
      if (phase !== 'ready' || !packet.event || !packet.data) return;
      const { event, data: eventData } = packet;
      if (event.source === 'Command' && event.type === 'Triggered') {
        if (typeof eventData.command !== 'string') return;
        const name = eventData.command.toLowerCase();
        const preset = commands.get(name);
        if (!preset) return;
        const id = packet.timeStamp ?? packet.timestamp;
        const viewer = typeof eventData.user?.id === 'string' ? eventData.user.id : '';
        trigger(preset, typeof id === 'string'
          ? `sb:command:${name}:${id}:${viewer}`.slice(0, 200)
          : undefined);
      }
      if (event.source === 'Twitch' && event.type === 'RewardRedemption') {
        const preset = rewards.get(eventData.reward?.id);
        if (!preset || eventData.isFromSharedChatGuest) return;
        const id = eventData.redemptionId;
        trigger(preset, typeof id === 'string' && id ? `sb:reward:${id}`.slice(0, 200) : undefined);
      }
    });
    ws.addEventListener('close', () => {
      clearTimeout(handshakeTimer);
      if (stopped) return;
      retryTimer = setTimeout(connect, backoff);
      backoff = Math.min(backoff * 2, 30_000);
    });
  }

  connect();
  return () => {
    stopped = true;
    clearTimeout(retryTimer);
    clearTimeout(handshakeTimer);
    socket?.close();
  };
}
