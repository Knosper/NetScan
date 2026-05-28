import { useState, useRef, useCallback } from "react";
import { SHORT_BANNER_AUTO_HIDE_MS } from "../constants/timing.js";
import { buildBanner as buildUiBanner } from "../ui/helpers.js";

export function useAppStatus() {
    const [banners, setBanners] = useState({
        top: null,
        scans: null,
        hosts: null,
        presence: null,
        settings: null,
        changes: null,
        services: null
    });

    const [backendUnavailable, setBackendUnavailable] = useState(false);
    const bannerTimersRef = useRef({});
    const backendUnavailableRef = useRef(false);

    backendUnavailableRef.current = backendUnavailable;

    const setBanner = useCallback((name, banner, autoHideMs = 0) => {
        if (bannerTimersRef.current[name]) {
            clearTimeout(bannerTimersRef.current[name]);
            delete bannerTimersRef.current[name];
        }

        setBanners(previous => ({...previous, [name]: banner}));

        if (banner && autoHideMs > 0) {
            bannerTimersRef.current[name] = setTimeout(() => {
                setBanners(previous => ({...previous, [name]: null}));
                delete bannerTimersRef.current[name];
            }, autoHideMs);
        }
    }, []);

    const markBackendUnavailable = useCallback((message = "backend not reachable") => {
        setBackendUnavailable(true);
        setBanner("top", buildUiBanner("danger", message));
    }, [setBanner]);

    const clearBackendUnavailable = useCallback(() => {
        if (!backendUnavailableRef.current)
            return;

        setBackendUnavailable(false);
        setBanner("top", buildUiBanner("accent", "backend restored"), SHORT_BANNER_AUTO_HIDE_MS);
    }, [setBanner]);

    const cleanupBannerTimers = useCallback(() => {
        Object.values(bannerTimersRef.current).forEach(timer => clearTimeout(timer));
    }, []);

    return {
        banners,
        setBanners,
        backendUnavailable,
        setBackendUnavailable,
        setBanner,
        markBackendUnavailable,
        clearBackendUnavailable,
        cleanupBannerTimers
    };
}
