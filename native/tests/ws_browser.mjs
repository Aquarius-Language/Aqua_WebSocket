import { chromium } from 'playwright';
const port = process.argv[2];
const browser = await chromium.launch();
const page = await browser.newPage();
page.on('console', m => console.log('PAGE', m.text()));
const result = await page.evaluate(async (port) => {
  const log = [];
  const ws = new WebSocket(`ws://127.0.0.1:${port}/any`);
  ws.binaryType = 'arraybuffer';
  const got = [];
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej('error'); });
  ws.onmessage = e => got.push(e.data);
  const big = 'é'.repeat(200000);
  ws.send('hello 世界 🎉'); ws.send(big); ws.send(new Uint8Array([1,2,3,250]));
  await new Promise(r => setTimeout(r, 2500)); // allow server pings
  log.push(got.length, got[0], got[1] === big, got[2] instanceof ArrayBuffer ? [...new Uint8Array(got[2])].join(',') : String(got[2]));
  const closed = new Promise(r => ws.onclose = e => r([e.code, e.reason, e.wasClean]));
  ws.close(4001, 'done');
  log.push(await closed);
  return log;
}, port);
console.log('BROWSER', JSON.stringify(result));
await browser.close();
