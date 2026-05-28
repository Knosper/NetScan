import { useState } from "react";
import { serializeTags } from "../ui/tagUtils.js";

export function useMetaSave({ ip, tags, setTags, tagInput, tagColor, setTagInput, onPatch, onDeleteField })
{
    const [saving, setSaving] = useState(false);
    const [error, setError] = useState("");
    const [savedAt, setSavedAt] = useState(0);

    async function handleSave(event)
    {
        event.preventDefault();
        setSaving(true);
        setError("");
        let effectiveTags = tags;
        const pending = tagInput.trim();
        if (pending && !tags.some(t => t.name === pending)) {
            effectiveTags = [...tags, { name: pending, color: tagColor }];
            setTags(effectiveTags);
            setTagInput("");
        }
        const fields = { tags: serializeTags(effectiveTags) };
        const result = await onPatch(ip, fields);
        setSaving(false);
        if (!result.ok)
            setError(`save failed (${result.status || "network error"})`);
        else
            setSavedAt(Date.now());
    }

    async function handleClearField(field)
    {
        setSaving(true);
        setError("");
        const result = await onDeleteField(ip, field);
        setSaving(false);
        if (!result.ok)
            setError(`clear failed (${result.status || "network error"})`);
        else if (field === "tags")
            setTags([]);
    }

    return { saving, error, savedAt, handleSave, handleClearField };
}
