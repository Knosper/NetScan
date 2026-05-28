export const TAG_COLORS = ["red", "orange", "yellow", "green", "cyan", "blue", "purple", "gray"];

export function parseTagToken(token)
{
    const trimmed = String(token).trim();
    if (!trimmed)
        return null;
    const idx = trimmed.lastIndexOf(":");
    if (idx <= 0)
        return { name: trimmed, color: "" };
    const name = trimmed.slice(0, idx).trim();
    const color = trimmed.slice(idx + 1).trim().toLowerCase();
    if (!name)
        return null;
    return { name, color: TAG_COLORS.includes(color) ? color : "" };
}

export function parseTags(raw)
{
    const tokens = Array.isArray(raw)
        ? raw.map(String)
        : (typeof raw === "string" && raw.trim() ? raw.split(",") : []);
    const result = [];
    for (const token of tokens) {
        const parsed = parseTagToken(token);
        if (parsed)
            result.push(parsed);
    }
    return result;
}

export function serializeTags(tags)
{
    return tags
        .map(t => t.color ? `${t.name}:${t.color}` : t.name)
        .join(",");
}
