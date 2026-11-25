import { expect, test } from "@playwright/test";

test("the complete original Web Audio synth remains available at /legacy/", async ({ page }) => {
  await page.addInitScript(() => {
    const nativeConnect = AudioNode.prototype.connect;
    let outputTapped = false;

    AudioNode.prototype.connect = function (destination, ...args) {
      if (!outputTapped && destination === this.context.destination) {
        outputTapped = true;
        const analyser = this.context.createAnalyser();
        analyser.fftSize = 2048;
        (window as Window & { synthOutputAnalyser?: AnalyserNode }).synthOutputAnalyser = analyser;
        Reflect.apply(nativeConnect, this, [analyser]);
        return Reflect.apply(nativeConnect, analyser, [destination]);
      }

      return Reflect.apply(nativeConnect, this, [destination, ...args]);
    };
  });

  await page.goto("/legacy/");
  await page.getByRole("button", { name: "POWER ON" }).click();

  const firstKey = page.locator(".piano .white-key").first();
  await firstKey.hover();
  await page.mouse.down();

  await page.waitForFunction(() => {
    const analyser = (window as Window & { synthOutputAnalyser?: AnalyserNode }).synthOutputAnalyser;
    if (!analyser) return false;

    const samples = new Float32Array(analyser.fftSize);
    analyser.getFloatTimeDomainData(samples);
    const rms = Math.sqrt(samples.reduce((sum, sample) => sum + sample * sample, 0) / samples.length);
    return rms > 0.005;
  }, undefined, { timeout: 5_000 });

  await page.mouse.up();

  await page.waitForFunction(() => {
    const analyser = (window as Window & { synthOutputAnalyser?: AnalyserNode }).synthOutputAnalyser;
    if (!analyser) return false;

    const samples = new Float32Array(analyser.fftSize);
    analyser.getFloatTimeDomainData(samples);
    const rms = Math.sqrt(samples.reduce((sum, sample) => sum + sample * sample, 0) / samples.length);
    return rms < 0.001;
  }, undefined, { timeout: 5_000 });

  await expect(page.getByText("STANDBY")).toBeVisible();
});
