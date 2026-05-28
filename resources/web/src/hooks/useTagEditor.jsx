import { useState } from "react";
import { parseTags } from "../ui/tagUtils.js";

export function useTagEditor(initialTagsRaw)
{
    const [tagInput, setTagInput] = useState("");
    const [tagColor, setTagColor] = useState("");
    const [tags, setTags] = useState(parseTags(initialTagsRaw));

    function addTag(event)
    {
        if (event.key !== "Enter" && event.key !== ",")
            return;
        event.preventDefault();
        const next = tagInput.trim();
        if (next && !tags.some(t => t.name === next))
            setTags(previous => [...previous, { name: next, color: tagColor }]);
        setTagInput("");
    }

    function removeTag(name)
    {
        setTags(previous => previous.filter(t => t.name !== name));
    }

    return { tags, setTags, tagInput, setTagInput, tagColor, setTagColor, addTag, removeTag };
}
