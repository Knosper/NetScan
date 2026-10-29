import js from "@eslint/js";
import globals from "globals";
import react from "eslint-plugin-react";
import reactHooks from "eslint-plugin-react-hooks";

export default [
    { ignores: ["app.js", "style.css", "node_modules/", ".build/"] },
    js.configs.recommended,
    {
        files: ["**/*.{js,jsx,mjs}"],
        languageOptions: {
            ecmaVersion: 2022,
            sourceType: "module",
            parserOptions: { ecmaFeatures: { jsx: true } },
            globals: { ...globals.browser }
        },
        plugins: { react, "react-hooks": reactHooks },
        settings: { react: { version: "18.3" } },
        rules: {
            "react/jsx-uses-vars": "error",
            "react/jsx-uses-react": "error",
            "react-hooks/rules-of-hooks": "error",
            "react-hooks/exhaustive-deps": "warn",
            "no-unused-vars": ["error", { argsIgnorePattern: "^_", caughtErrorsIgnorePattern: "^_" }]
        }
    },
    {
        files: ["*.mjs"],
        languageOptions: { globals: { ...globals.node } }
    }
];
