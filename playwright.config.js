import { defineConfig } from '@playwright/test';
export default defineConfig({
  testDir: './tests', testMatch: '**/*.spec.js', timeout: 60000,
  fullyParallel: false, workers: 1, reporter: 'list',
  outputDir: 'test-results'
});
