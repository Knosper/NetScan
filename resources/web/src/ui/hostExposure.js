export function formatValue(value)
{
    if (value === null || value === undefined || value === "")
        return "-";
    return String(value);
}

export function historyWhen(scan)
{
    return formatValue(scan.finishedAt || scan.startedAt || scan.createdAt);
}

export function hostExposure(host)
{
    const scanCount = host.scanCount ?? 0;
    const openPortCount = host.openPortCount ?? 0;

    if (scanCount === 0)
        return {tone: "muted", label: "new", title: "New host, no scan performed yet"};

    if (openPortCount > 0)
        return {tone: "warning", label: "exposed", title: "Offene Ports erkannt"};

    return {tone: "success", label: "quiet", title: "Keine offenen Ports gefunden"};
}
