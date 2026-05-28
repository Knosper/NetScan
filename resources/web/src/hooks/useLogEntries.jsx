import { useEffect, useRef, useState } from "react";
import { isTerminalScanState } from "../ui/helpers.js";

const MAX_LOG_ENTRIES = 40;

function buildLogEntry(kind, text, timestamp = Date.now())
{
    return {
        id: `${timestamp}-${kind}`,
        kind,
        text,
        timestamp
    };
}

function buildScanTerminalLogText(scanId, state, scan)
{
    const diagnostic = (scan?.stderrText || "").trim();
    if (diagnostic)
        return `scan #${scanId} ${state}: ${diagnostic}`;

    if (scan?.message)
        return `scan #${scanId} ${state}: ${scan.message}`;

    return `scan #${scanId} ${state}`;
}

function prependEntry(entry)
{
    return previous => [entry, ...previous].slice(0, MAX_LOG_ENTRIES);
}

export function useLogEntries(backendUnavailable, scanStatus)
{
    const [logEntries, setLogEntries] = useState([]);
    const previousBackendUnavailableRef = useRef(null);
    const lastLoggedStartedScanKeyRef = useRef("");
    const lastLoggedTerminalScanKeyRef = useRef("");
    const scanState = scanStatus?.status || "idle";
    const scanId = scanStatus?.scan?.id || "none";
    const scanMessage = scanStatus?.scan?.message || "";
    const scanStderrText = scanStatus?.scan?.stderrText || "";

    useEffect(() => {
        if (previousBackendUnavailableRef.current === null)
        {
            previousBackendUnavailableRef.current = backendUnavailable;
            return;
        }

        if (previousBackendUnavailableRef.current === backendUnavailable)
            return;

        previousBackendUnavailableRef.current = backendUnavailable;
        setLogEntries(prependEntry(buildLogEntry(
            backendUnavailable ? "offline" : "online",
            backendUnavailable ? "backend offline" : "backend online"
        )));
    }, [backendUnavailable]);

    useEffect(() => {
        const currentState = scanState;
        const currentScanId = scanId;

        if ((currentState === "queued" || currentState === "running") && currentScanId !== "none")
        {
            const startedKey = `${currentScanId}:started`;
            if (startedKey !== lastLoggedStartedScanKeyRef.current)
            {
                lastLoggedStartedScanKeyRef.current = startedKey;
                setLogEntries(prependEntry(buildLogEntry("started", `scan #${currentScanId} started`)));
            }
            return;
        }

        if (isTerminalScanState(currentState) && currentScanId !== "none")
        {
            const terminalKey = `${currentScanId}:${currentState}`;
            if (terminalKey !== lastLoggedTerminalScanKeyRef.current)
            {
                lastLoggedTerminalScanKeyRef.current = terminalKey;
                setLogEntries(prependEntry(buildLogEntry(
                    currentState,
                    buildScanTerminalLogText(currentScanId, currentState, {
                        message: scanMessage,
                        stderrText: scanStderrText
                    })
                )));
            }
        }
    }, [scanState, scanId, scanMessage, scanStderrText]);

    return { logEntries };
}
