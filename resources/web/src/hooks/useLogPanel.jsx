import { useEffect, useRef, useState } from "react";

const LOG_PANEL_HEIGHT_STORAGE_KEY = "netscan.shellLogHeight";
const COLLAPSED_LOG_PANEL_HEIGHT = 30;
const DEFAULT_LOG_PANEL_HEIGHT = 176;
const DEFAULT_LOG_PANEL_HEIGHT_MOBILE = 148;
const MAX_LOG_PANEL_HEIGHT_RATIO = 0.75;

function getDefaultLogPanelHeight()
{
    if (typeof window === "undefined")
        return DEFAULT_LOG_PANEL_HEIGHT;

    return window.innerWidth <= 480 ? DEFAULT_LOG_PANEL_HEIGHT_MOBILE : DEFAULT_LOG_PANEL_HEIGHT;
}

function getMaxLogPanelHeight()
{
    if (typeof window === "undefined")
        return DEFAULT_LOG_PANEL_HEIGHT;

    return Math.max(getDefaultLogPanelHeight(), Math.floor(window.innerHeight * MAX_LOG_PANEL_HEIGHT_RATIO));
}

function clampLogPanelHeight(height)
{
    const minHeight = COLLAPSED_LOG_PANEL_HEIGHT;
    const maxHeight = getMaxLogPanelHeight();
    return Math.min(Math.max(height, minHeight), maxHeight);
}

function readStoredLogPanelHeight()
{
    if (typeof window === "undefined")
        return getDefaultLogPanelHeight();

    const raw = window.localStorage.getItem(LOG_PANEL_HEIGHT_STORAGE_KEY);
    const parsed = Number.parseInt(raw || "", 10);
    if (!Number.isFinite(parsed))
        return getDefaultLogPanelHeight();

    return clampLogPanelHeight(parsed);
}

export function useLogPanel()
{
    const [logExpanded, setLogExpanded] = useState(false);
    const [logPanelHeight, setLogPanelHeight] = useState(() => readStoredLogPanelHeight());
    const [logResizing, setLogResizing] = useState(false);

    const logResizeStartYRef = useRef(0);
    const logResizeStartHeightRef = useRef(getDefaultLogPanelHeight());
    const lastDraggedLogPanelHeightRef = useRef(null);

    useEffect(() => {
        function syncLogPanelHeight()
        {
            setLogPanelHeight(previous => clampLogPanelHeight(previous));
        }

        window.addEventListener("resize", syncLogPanelHeight);
        return () => window.removeEventListener("resize", syncLogPanelHeight);
    }, []);

    useEffect(() => {
        window.localStorage.setItem(LOG_PANEL_HEIGHT_STORAGE_KEY, String(logPanelHeight));
    }, [logPanelHeight]);

    useEffect(() => {
        if (!logResizing)
            return undefined;

        const originalCursor = document.body.style.cursor;
        const originalUserSelect = document.body.style.userSelect;
        document.body.style.cursor = "ns-resize";
        document.body.style.userSelect = "none";

        function handlePointerMove(event)
        {
            const deltaY = logResizeStartYRef.current - event.clientY;
            const nextHeight = clampLogPanelHeight(logResizeStartHeightRef.current + deltaY);

            if (nextHeight > COLLAPSED_LOG_PANEL_HEIGHT)
                lastDraggedLogPanelHeightRef.current = nextHeight;

            setLogPanelHeight(nextHeight);
        }

        function stopResize()
        {
            setLogResizing(false);
        }

        window.addEventListener("mousemove", handlePointerMove);
        window.addEventListener("mouseup", stopResize);

        return () => {
            document.body.style.cursor = originalCursor;
            document.body.style.userSelect = originalUserSelect;
            window.removeEventListener("mousemove", handlePointerMove);
            window.removeEventListener("mouseup", stopResize);
        };
    }, [logResizing]);

    function handleLogResizeStart(event)
    {
        if (!logExpanded || event.button !== 0)
            return;

        event.preventDefault();
        logResizeStartYRef.current = event.clientY;
        logResizeStartHeightRef.current = logPanelHeight;
        setLogResizing(true);
    }

    function handleLogPanelToggle()
    {
        if (logExpanded)
        {
            if (logPanelHeight > COLLAPSED_LOG_PANEL_HEIGHT)
                lastDraggedLogPanelHeightRef.current = logPanelHeight;

            setLogExpanded(false);
            return;
        }

        setLogPanelHeight(previous => {
            const restoredHeight = lastDraggedLogPanelHeightRef.current
                ?? (previous > COLLAPSED_LOG_PANEL_HEIGHT ? previous : getDefaultLogPanelHeight());
            return clampLogPanelHeight(restoredHeight);
        });
        setLogExpanded(true);
    }

    return {
        logExpanded,
        logPanelHeight,
        logResizing,
        handleLogResizeStart,
        handleLogPanelToggle
    };
}
