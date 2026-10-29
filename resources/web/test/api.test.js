import { afterEach, describe, expect, it, vi } from "vitest";
import { fetchWithTimeout } from "../src/api.js";
import { getApiKey, setApiKey, setApiKeyPromptListener } from "../src/apiKey.js";

function stubFetch(status = 200)
{
    const fetchMock = vi.fn(async () => new Response("{}", { status }));
    vi.stubGlobal("fetch", fetchMock);
    return fetchMock;
}

function sentHeaders(fetchMock)
{
    return new Headers(fetchMock.mock.calls[0][1].headers);
}

describe("fetchWithTimeout", () => {
    afterEach(() => {
        setApiKey("");
        setApiKeyPromptListener(null);
    });

    it("sends the stored API key to /api/ requests", async () => {
        setApiKey("secret");
        const fetchMock = stubFetch();
        await fetchWithTimeout("/api/hosts", { headers: { Accept: "application/json" } });
        const headers = sentHeaders(fetchMock);
        expect(headers.get("X-API-Key")).toBe("secret");
        expect(headers.get("Accept")).toBe("application/json");
    });

    it("never sends the API key to other URLs", async () => {
        setApiKey("secret");
        const fetchMock = stubFetch();
        await fetchWithTimeout("https://example.com/api/hosts");
        expect(sentHeaders(fetchMock).get("X-API-Key")).toBeNull();
    });

    it("clears the key and asks for a new one on 401", async () => {
        setApiKey("stale");
        const prompt = vi.fn();
        setApiKeyPromptListener(prompt);
        stubFetch(401);
        const response = await fetchWithTimeout("/api/hosts");
        expect(response.status).toBe(401);
        expect(getApiKey()).toBe("");
        expect(prompt).toHaveBeenCalledOnce();
    });

    it("aborts the request after the timeout", async () => {
        vi.useFakeTimers();
        try {
            vi.stubGlobal("fetch", vi.fn((_url, { signal }) => new Promise((_resolve, reject) => {
                signal.addEventListener("abort", () => reject(new DOMException("aborted", "AbortError")));
            })));
            const pending = fetchWithTimeout("/api/hosts", {}, 50);
            vi.advanceTimersByTime(50);
            await expect(pending).rejects.toMatchObject({ name: "AbortError" });
        } finally {
            vi.useRealTimers();
        }
    });
});
