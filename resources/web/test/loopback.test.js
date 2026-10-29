import { describe, expect, it } from "vitest";
import { isLoopbackHost } from "../src/loopback.js";

// Must agree with is_loopback_request_host() in src/app/config.cpp.
describe("isLoopbackHost", () => {
    it.each([
        "localhost",
        "LOCALHOST",
        "127.0.0.1",
        "127.255.255.254",
        "::1",
        "[::1]",
        "0:0:0:0:0:0:0:1",
        "[0000::0001]"
    ])("accepts %s", host => {
        expect(isLoopbackHost(host)).toBe(true);
    });

    it.each([
        "",
        null,
        undefined,
        "0.0.0.0",
        "128.0.0.1",
        "127.0.0.256",
        "127.0.0.01",
        "127.1",
        "localhost.example.com",
        "evil-localhost",
        "192.168.1.10",
        "::",
        "::2",
        "[::ffff:127.0.0.1]",
        "[::1",
        "not:an:ip"
    ])("rejects %s", host => {
        expect(isLoopbackHost(host)).toBe(false);
    });
});
