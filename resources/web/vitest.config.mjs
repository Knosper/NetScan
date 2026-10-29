import { defineConfig } from "vitest/config";

export default defineConfig({
    test: {
        include: ["test/**/*.test.{js,jsx}"],
        environment: "jsdom",
        restoreMocks: true,
        unstubGlobals: true
    }
});
