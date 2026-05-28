// API key storage for X-API-Key header.

const STORAGE_KEY = "ns_api_key";

let promptListener = null;

export function getApiKey()
{
    try
    {
        return localStorage.getItem(STORAGE_KEY) || "";
    }
    catch (_)
    {
        return "";
    }
}

export function setApiKey(value)
{
    try
    {
        if (value)
            localStorage.setItem(STORAGE_KEY, value);
        else
            localStorage.removeItem(STORAGE_KEY);
    }
    catch (_)
    {
        // storage unavailable — silently ignore; next 401 will re-prompt
    }
}

export function clearApiKey()
{
    setApiKey("");
}

export function setApiKeyPromptListener(listener)
{
    promptListener = typeof listener === "function" ? listener : null;
}

export function requestApiKeyPrompt()
{
    if (promptListener)
        promptListener();
}
