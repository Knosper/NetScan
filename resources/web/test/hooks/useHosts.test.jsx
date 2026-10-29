import { act, renderHook, waitFor } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { AppContext } from "../../src/AppContext.jsx";
import { useHosts } from "../../src/hooks/useHosts.jsx";

function jsonResponse(payload, status = 200)
{
    return new Response(JSON.stringify(payload), { status, headers: { "Content-Type": "application/json" } });
}

// fetch stub whose responses are released by the test, so request order can be controlled.
// With honorAbort false a response still arrives after its request was aborted, as when
// the abort comes too late; the hook must then drop it on its own.
function controllableFetch({ honorAbort = true } = {})
{
    const pending = [];
    const fetchMock = vi.fn((url, { signal } = {}) => new Promise((resolve, reject) => {
        const entry = { url, resolve: payload => resolve(jsonResponse(payload)) };
        if (honorAbort)
            signal?.addEventListener("abort", () => reject(new DOMException("aborted", "AbortError")));
        pending.push(entry);
    }));
    vi.stubGlobal("fetch", fetchMock);
    return { fetchMock, pending };
}

function renderUseHosts()
{
    const app = {
        setBanner: vi.fn(),
        markBackendUnavailable: vi.fn(),
        clearBackendUnavailable: vi.fn()
    };
    const wrapper = ({ children }) => <AppContext.Provider value={app}>{children}</AppContext.Provider>;
    return { app, ...renderHook(() => useHosts(), { wrapper }) };
}

const hostsPage = (hosts, extra = {}) => ({ hosts, total: hosts.length, pageSize: 50, page: 1, ...extra });

describe("useHosts", () => {
    it("ignores a slower, superseded host list response", async () => {
        const { pending } = controllableFetch({ honorAbort: false });
        const { result } = renderUseHosts();

        let first;
        let second;
        act(() => {
            first = result.current.loadHosts({ q: "old" });
        });
        act(() => {
            second = result.current.loadHosts({ q: "new" });
        });
        expect(pending.map(entry => entry.url)).toEqual([
            "/api/hosts?q=old&limit=50&offset=0",
            "/api/hosts?q=new&limit=50&offset=0"
        ]);

        await act(async () => {
            pending[1].resolve(hostsPage([{ ip: "10.0.0.2", lastScanId: 2 }]));
            pending[0].resolve(hostsPage([{ ip: "10.0.0.1", lastScanId: 1 }]));
            await Promise.all([first, second]);
        });

        expect(result.current.hosts.map(host => host.ip)).toEqual(["10.0.0.2"]);
        expect(result.current.loading).toBe(false);
    });

    it("drops cached host details when the host list changes", async () => {
        const { pending } = controllableFetch();
        const { result } = renderUseHosts();

        const load = (hosts) => act(async () => {
            const done = result.current.loadHosts();
            pending.at(-1).resolve(hostsPage(hosts));
            await done;
        });

        await load([{ ip: "10.0.0.1", lastScanId: 1 }]);
        await act(async () => {
            const done = result.current.loadHostDetail("10.0.0.1");
            pending.at(-1).resolve({ ip: "10.0.0.1", meta: { owner: "a" } });
            await done;
        });
        expect(result.current.hostDetails["10.0.0.1"]).toBeDefined();

        await load([{ ip: "10.0.0.1", lastScanId: 1 }]);
        expect(result.current.hostDetails["10.0.0.1"]).toBeDefined();

        await load([{ ip: "10.0.0.1", lastScanId: 7 }]);
        expect(result.current.hostDetails).toEqual({});
    });

    it("does not let an aborted detail request write into the cleared cache", async () => {
        const { pending } = controllableFetch({ honorAbort: false });
        const { result } = renderUseHosts();

        await act(async () => {
            const done = result.current.loadHosts();
            pending.at(-1).resolve(hostsPage([{ ip: "10.0.0.1", lastScanId: 1 }]));
            await done;
        });

        let detail;
        act(() => {
            detail = result.current.loadHostDetail("10.0.0.1");
        });
        const detailRequest = pending.at(-1);

        await act(async () => {
            const done = result.current.loadHosts();
            pending.at(-1).resolve(hostsPage([{ ip: "10.0.0.9", lastScanId: 3 }]));
            await done;
            detailRequest.resolve({ ip: "10.0.0.1", meta: {} });
            await detail;
        });

        expect(result.current.hostDetails).toEqual({});
        expect(result.current.hostDetailLoading).toEqual({});
    });

    it("applies saved metadata to the host row without reloading", async () => {
        const { pending, fetchMock } = controllableFetch();
        const { result } = renderUseHosts();

        await act(async () => {
            const done = result.current.loadHosts();
            pending.at(-1).resolve(hostsPage([{ ip: "10.0.0.1", lastScanId: 1, meta: { owner: "a" } }]));
            await done;
        });

        let saved;
        await act(async () => {
            const done = result.current.patchHostMeta("10.0.0.1", { tags: "web" });
            pending.at(-1).resolve({ meta: { owner: "a", tags: "web" } });
            saved = await done;
        });

        expect(saved).toEqual({ ok: true });
        const [url, options] = fetchMock.mock.calls.at(-1);
        expect(url).toBe("/api/hosts/10.0.0.1/meta");
        expect(options.method).toBe("PATCH");
        await waitFor(() => expect(result.current.hosts[0].meta).toEqual({ owner: "a", tags: "web" }));
    });
});
