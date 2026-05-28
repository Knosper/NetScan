import { DEFAULT_FETCH_TIMEOUT_MS } from "./constants/timing.js";
import { getApiKey, clearApiKey, requestApiKeyPrompt } from "./apiKey.js";

function mergeApiKeyHeader(headers)
{
    const key = getApiKey();
    if (!key)
        return headers;

    const merged = new Headers(headers || {});
    merged.set("X-API-Key", key);
    return merged;
}

function isApiRequest(url)
{
    return typeof url === "string" && url.startsWith("/api/");
}

export function fetchWithTimeout(url, options = {}, timeoutMs = DEFAULT_FETCH_TIMEOUT_MS)
{
    const finalOptions = isApiRequest(url)
        ? {...options, headers: mergeApiKeyHeader(options.headers)}
        : options;

    if (typeof AbortController === "undefined")
        return fetch(url, finalOptions).then(handleAuthFailure);

    const {signal, ...fetchOptions} = finalOptions;
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), timeoutMs);
    const abortFromCaller = () => controller.abort();

    if (signal?.aborted)
        controller.abort();
    else
        signal?.addEventListener("abort", abortFromCaller, {once: true});

    return fetch(url, {...fetchOptions, signal: controller.signal})
        .then(handleAuthFailure)
        .finally(() => {
            clearTimeout(timeoutId);
            signal?.removeEventListener("abort", abortFromCaller);
        });
}

function handleAuthFailure(response)
{
    if (response?.status === 401)
    {
        clearApiKey();
        requestApiKeyPrompt();
    }
    return response;
}

export function isAbortError(error)
{
    return error?.name === "AbortError";
}

export async function readOptionalJson(response)
{
    try
    {
        return await response.json();
    }
    catch (_)
    {
        return null;
    }
}
