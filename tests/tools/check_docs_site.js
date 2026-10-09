/* Serve docs/site on localhost:4173, then run with Playwright installed:
 * node tests/tools/check_docs_site.js
 * Optional: PLAYWRIGHT_MODULE=/absolute/path/to/playwright and CHROME_PATH.
 * The documentation itself has no npm dependencies.
 */
async function checkDocsSite(page) {
  const base = 'http://127.0.0.1:4173';
  const results = [];
  const check = (condition, message) => {
    if (!condition) throw new Error(message);
    results.push(message);
  };
  await page.setViewportSize({ width: 1440, height: 1000 });
  await page.emulateMedia({ colorScheme: 'light', reducedMotion: 'reduce' });
  await page.goto(base + '/index.html');
  await page.evaluate(() => localStorage.clear());
  await page.reload();
  const layout = await page.evaluate(() => ({
    sidebar: document.querySelector('#sidebar-nav').getBoundingClientRect().top,
    heading: document.querySelector('#intro h1').getBoundingClientRect().top,
    placeholder: !!document.querySelector('[data-logo-placeholder]'),
    theme: document.documentElement.dataset.theme,
    overflow: document.documentElement.scrollWidth > innerWidth
  }));
  check(layout.sidebar < 160 && layout.heading < 240, 'Docs navigation and article start above the fold');
  check(layout.placeholder, 'Replaceable logo placeholder is present');
  check(layout.theme === 'light' && !layout.overflow, 'Light system theme and desktop layout work');
  await page.locator('#theme-toggle-btn').click();
  await page.reload();
  check(await page.locator('html').getAttribute('data-theme') === 'dark', 'Theme choice survives reload');

  await page.locator('#search-trigger-btn').click();
  await page.locator('#search-input').fill('not-a-real-module-123');
  await page.locator('.search-empty').waitFor({ state: 'visible' });
  await page.locator('#search-input').fill('std::stdio');
  await page.locator('#search-input').press('Enter');
  await page.waitForURL('**/stdlib.html#module-stdio');
  check(new URL(page.url()).hash === '#module-stdio', 'Keyboard search navigates across pages to the correct anchor');
  await page.locator('#module-stdio').waitFor({ state: 'visible' });

  await page.goto(base + '/stdlib.html');
  await page.locator('#stdlib-search-input').fill('hashmap');
  check(await page.locator('.stdlib-module-link:visible').count() === 1, 'Module filter narrows navigation');
  await page.locator('#stdlib-search-input').fill('not-a-module');
  await page.locator('#stdlib-filter-empty').waitFor({ state: 'visible' });
  await page.locator('#stdlib-search-input').fill('');

  for (const file of ['index.html', 'stdlib.html']) {
    await page.setViewportSize({ width: 375, height: 812 });
    await page.goto(base + '/' + file);
    check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), file + ': no horizontal page overflow');
    await page.locator('#mobile-menu-btn').click();
    check(await page.locator('#mobile-menu-btn').getAttribute('aria-expanded') === 'true', file + ': mobile navigation opens');
    await page.keyboard.press('Escape');
    check(await page.locator('#mobile-menu-btn').getAttribute('aria-expanded') === 'false', file + ': Escape closes mobile navigation');
    await page.locator('#search-trigger-btn').click();
    await page.locator('#search-input').waitFor({ state: 'visible' });
    await page.keyboard.press('Escape');
    check(await page.locator('#search-trigger-btn').evaluate(el => el === document.activeElement), file + ': closing search restores focus');
  }

  await page.setViewportSize({ width: 1440, height: 1000 });
  await page.goto(base + '/index.html#quickstart');
  await page.waitForFunction(() => document.querySelector('#sidebar-nav a[href="#quickstart"]')?.getAttribute('aria-current') === 'location');
  check(true, 'Deep-link navigation highlights the correct topic');
  await page.evaluate(() => {
    window.__copied = '';
    Object.defineProperty(navigator, 'clipboard', { configurable: true, value: {
      writeText: async text => { window.__copied = text; }
    }});
  });
  await page.locator('#quickstart .copy-btn').click();
  check((await page.evaluate(() => window.__copied)).includes('finc program.fin'), 'Copy button copies the real code');
  await page.evaluate(() => {
    navigator.clipboard.writeText = async () => { throw new Error('Clipboard denied'); };
  });
  await page.locator('#quickstart .copy-btn').click();
  await page.waitForFunction(() => document.querySelector('#fin-toast')?.textContent.includes('select'));
  check(true, 'Clipboard denial gives actionable feedback');

  await page.goto(base + '/index.html');
  await page.emulateMedia({ reducedMotion: 'no-preference' });
  const mascot = page.locator('[data-logo-placeholder]').first();
  const neutralFace = await mascot.textContent();
  await mascot.hover();
  await page.waitForFunction(neutral => document.querySelector('[data-logo-placeholder]').textContent !== neutral, neutralFace);
  check(true, 'Mascot reacts on hover');
  await page.getByRole('button', { name: 'Say hi to Fin' }).click();
  await page.waitForFunction(() => document.querySelector('#mascot-message')?.textContent.includes('Hello, friend!'));
  check(true, 'Compiler companion responds with an accessible greeting');
  await page.mouse.move(700, 100);
  await page.emulateMedia({ reducedMotion: 'reduce' });
  await page.evaluate(() => {
    Object.defineProperty(navigator, 'clipboard', { configurable: true, value: { writeText: async () => {} } });
  });
  await page.locator('#intro .copy-btn').first().click();
  check(await page.locator('.copy-sparkle').count() === 0, 'Reduced motion suppresses copy particles');
  await page.emulateMedia({ reducedMotion: 'no-preference' });
  await page.locator('#intro .copy-btn').first().click();
  await page.waitForFunction(() => document.querySelectorAll('.copy-sparkle').length > 0);
  await page.waitForFunction(() => document.querySelectorAll('.copy-sparkle').length === 0);
  check(true, 'Copy celebration animates and cleans itself up');

  const context = await page.context().browser().newContext({ viewport: { width: 1440, height: 1000 }, colorScheme: 'dark' });
  await context.addInitScript(() => {
    Object.defineProperty(window, 'localStorage', { get() { throw new Error('Storage blocked'); } });
  });
  const isolated = await context.newPage();
  await isolated.goto(base + '/index.html');
  await isolated.locator('#search-trigger-btn').click();
  await isolated.locator('#search-input').waitFor({ state: 'visible' });
  check(await isolated.locator('html').getAttribute('data-theme') === 'dark', 'Blocked storage does not break theme or search');
  await context.close();
  return results;
}

if (require.main === module) {
  const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
  (async () => {
    const browser = await chromium.launch({ executablePath: process.env.CHROME_PATH });
    try {
      const page = await browser.newPage();
      const results = await checkDocsSite(page);
      results.forEach(result => console.log('PASS ' + result));
    } finally {
      await browser.close();
    }
  })().catch(error => { console.error(error); process.exitCode = 1; });
}
module.exports = checkDocsSite;
