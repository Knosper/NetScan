export function historySignature(entry)
{
    const scan = entry?.scan || {};
    const ports = Array.isArray(entry?.ports) ? entry.ports : [];
    const normalizedPorts = ports
        .map(port => `${port?.port ?? ""}/${String(port?.service || "unknown").toLowerCase()}`)
        .sort();

    return JSON.stringify({
        state: scan.state || "unknown",
        deleted: Boolean(scan.deleted),
        ports: normalizedPorts
    });
}

export function filterRedundantHistory(history)
{
    const filtered = [];
    let hiddenCount = 0;
    const seenSignatures = new Set();

    for (const entry of history)
    {
        const signature = historySignature(entry);
        if (seenSignatures.has(signature))
        {
            hiddenCount += 1;
            continue;
        }

        seenSignatures.add(signature);
        filtered.push(entry);
    }

    return {entries: filtered, hiddenCount};
}
