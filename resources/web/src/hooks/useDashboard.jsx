import { useState, useCallback, useEffect, useRef, startTransition } from "react";
import { fetchWithTimeout, isAbortError } from "../api.js";
import { buildBanner } from "../ui/helpers.js";
import { useAppContext } from "../AppContext.jsx";
import { BANNER_AUTO_HIDE_MS } from "../constants/timing.js";

const EMPTY_DASHBOARD = {
    stats: {
        hosts: 0,
        ports: 0,
        services: 0,
        lastScan: ""
    },
    scans: [],
    hosts: [],
    ports: [],
    changes: {
        hasBaseline: false,
        addedHosts: 0,
        removedHosts: 0,
        addedPorts: 0,
        removedPorts: 0,
        changedPorts: 0,
        deltaServices: 0
    }
};

export function useDashboard() {
    const [dashboard, setDashboard] = useState(EMPTY_DASHBOARD);
    const { setBanner, markBackendUnavailable, clearBackendUnavailable } = useAppContext();
    const dashboardRequestRef = useRef(null);

    const loadDashboard = useCallback(async () => {
        dashboardRequestRef.current?.abort();
        const controller = new AbortController();
        dashboardRequestRef.current = controller;

        try {
            const response = await fetchWithTimeout("/api/dashboard", {signal: controller.signal});
            if (dashboardRequestRef.current !== controller)
                return;

            if (!response.ok) {
                setBanner("top", buildBanner("danger", `dashboard failed ${response.status}`), BANNER_AUTO_HIDE_MS);
                return;
            }

            const payload = await response.json();
            if (dashboardRequestRef.current !== controller)
                return;

            clearBackendUnavailable();
            startTransition(() => {
                setDashboard(payload);
            });
        } catch (error) {
            if (isAbortError(error))
                return;

            markBackendUnavailable();
        } finally {
            if (dashboardRequestRef.current === controller)
                dashboardRequestRef.current = null;
        }
    }, [setBanner, markBackendUnavailable, clearBackendUnavailable]);

    useEffect(() => () => {
        dashboardRequestRef.current?.abort();
        dashboardRequestRef.current = null;
    }, []);

    const refreshDashboard = useCallback(() => {
        return loadDashboard();
    }, [loadDashboard]);

    return {
        dashboard,
        setDashboard,
        loadDashboard,
        refreshDashboard
    };
}
