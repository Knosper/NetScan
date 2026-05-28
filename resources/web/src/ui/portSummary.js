export function formatPortToken(port)
{
    return `${port?.port ?? "-"}/${port?.service || "unknown"}`;
}

export function portSummary(ports, maxVisible = 3)
{
    const labels = ports.map(formatPortToken);
    const visibleLabels = labels.slice(0, maxVisible);
    const remainingCount = Math.max(0, labels.length - visibleLabels.length);

    return {
        visibleText: visibleLabels.join(", "),
        fullText: labels.join(", "),
        remainingCount
    };
}
