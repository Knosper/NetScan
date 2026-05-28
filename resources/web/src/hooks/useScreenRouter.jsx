import { startTransition, useEffect, useRef, useState } from "react";

const SCREEN_STORAGE_KEY = "netscan:screen";

function normalizeScreenTarget(id, extra = {})
{
    if (id === "hosts")
        return {screen: "hosts", extra};

    if (id === "scans")
        return {screen: "hosts", extra};

    if (id === "profiles")
        return {screen: "settings", extra};

    return {screen: id, extra};
}

function getInitialScreenTarget(validScreens)
{
    const stored = localStorage.getItem(SCREEN_STORAGE_KEY) || "dashboard";
    const normalized = normalizeScreenTarget(stored);
    return validScreens.has(normalized.screen) ? normalized : normalizeScreenTarget("dashboard");
}

export function useScreenRouter(validScreens)
{
    const initialTarget = getInitialScreenTarget(validScreens);
    const [screen, setScreen] = useState(initialTarget.screen);
    const [screenExtra, setScreenExtra] = useState(initialTarget.extra);

    useEffect(() => {
        if (!validScreens.has(screen))
            return;

        localStorage.setItem(SCREEN_STORAGE_KEY, screen);
    }, [screen, validScreens]);

    function go(id, extra = {}, pushHistory = true)
    {
        if (!id)
            return;

        const target = normalizeScreenTarget(id, extra);
        if (!validScreens.has(target.screen))
            return;

        startTransition(() => {
            setScreen(target.screen);
            setScreenExtra(target.extra);
            window.scrollTo(0, 0);

            if (pushHistory) {
                window.history.pushState({ screen: target.screen }, "", `#${target.screen}`);
            }
        });
    }

    const goRef = useRef(go);
    const screenRef = useRef(screen);
    useEffect(() => {
        goRef.current = go;
        screenRef.current = screen;
    });

    useEffect(() => {
        function onPopState(event) {
            const state = event.state || {};
            const screenId = state.screen || window.location.hash.slice(1) || "dashboard";
            goRef.current(screenId, {}, false);
        }

        window.addEventListener("popstate", onPopState);

        if (!window.history.state) {
            window.history.replaceState({ screen: screenRef.current }, "", `#${screenRef.current}`);
        }

        return () => window.removeEventListener("popstate", onPopState);
    }, []);

    return { screen, screenExtra, go };
}
