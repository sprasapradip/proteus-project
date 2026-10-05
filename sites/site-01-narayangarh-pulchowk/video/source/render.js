// Renders engine.html to an MP4.
//   node render.js                 full video -> ../site01-narayangarh-pulchowk-demo.mp4
//   node render.js 2 10.5 45       preview stills at those seconds -> preview/
const { chromium } = require(process.env.PLAYWRIGHT || 'playwright');
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const FPS = 30;
(async () => {
  const stills = process.argv.slice(2).map(Number);
  const browser = await chromium.launch({ executablePath: process.env.CHROME || undefined });
  const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
  await page.goto('file://' + path.join(__dirname, 'engine.html'));
  await page.evaluate(() => document.fonts.ready);
  await page.waitForTimeout(300);
  if (stills.length) {
    fs.mkdirSync(path.join(__dirname, 'preview'), { recursive: true });
    for (const s of stills) {
      await page.evaluate((t) => window.render(t), s);
      await page.screenshot({ path: path.join(__dirname, 'preview', `t${s}.jpg`), type: 'jpeg', quality: 85 });
    }
    await browser.close();
    return;
  }
  const dur = await page.evaluate(() => window.DURATION);
  const dir = path.join(__dirname, 'frames');
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir);
  const n = Math.round(dur * FPS);
  for (let i = 0; i < n; i++) {
    await page.evaluate((t) => window.render(t), i / FPS);
    await page.screenshot({ path: path.join(dir, `${String(i).padStart(5, '0')}.jpg`), type: 'jpeg', quality: 93 });
    if (i % 300 === 0) console.log(`frame ${i}/${n}`);
  }
  await browser.close();
  const out = path.join(__dirname, '..', 'site01-narayangarh-pulchowk-demo.mp4');
  execFileSync('ffmpeg', ['-y', '-v', 'error', '-framerate', String(FPS), '-i', path.join(dir, '%05d.jpg'),
    '-c:v', 'libx264', '-preset', 'slow', '-crf', '20', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', out]);
  fs.rmSync(dir, { recursive: true, force: true });
  console.log('wrote', out);
})();
