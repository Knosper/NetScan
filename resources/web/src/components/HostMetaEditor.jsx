import React from "react";
import { useTagEditor } from "../hooks/useTagEditor.jsx";
import { useMetaSave } from "../hooks/useMetaSave.jsx";
import { Button, Chip } from "../atoms.jsx";
import { TAG_COLORS, parseTags } from "../ui/tagUtils.js";

export function HostMetaBadges({meta})
{
    if (!meta)
        return null;

    const displayName = meta.displayName || "";
    const role = meta.role || "";
    const tags = parseTags(meta.tags);
    const hasAny = displayName || role || tags.length > 0;

    if (!hasAny)
        return null;

    return (
        <span className="host-meta-badges">
            {role ? <Chip tone="info">{role}</Chip> : null}
            {tags.map(tag => (
                <span
                    key={tag.name}
                    className={`chip host-tag-chip${tag.color ? ` host-tag-color-${tag.color}` : ""}`}
                >
                    {tag.name}
                </span>
            ))}
        </span>
    );
}

function HostMetaTagField({ state, handlers, saving })
{
    const { tags, tagInput, tagColor, existingTags } = state;
    const { onRemoveTag, onTagInputChange, onTagInputKeyDown, onColorChange, onClearTags } = handlers;
    return (
        <div className="field-row">
            <label className="field-label mono-label">tags</label>
            <div className="host-meta-tags-input-wrap">
                {tags.map(tag => (
                    <span
                        key={tag.name}
                        className={`host-meta-tag-chip${tag.color ? ` host-tag-color-${tag.color}` : ""}`}
                    >
                        {tag.name}
                        <button type="button" className="host-meta-tag-remove" onClick={() => onRemoveTag(tag.name)} aria-label={`remove tag ${tag.name}`}>
                            ×
                        </button>
                    </span>
                ))}
                <input
                    type="text"
                    className="host-meta-tag-input"
                    value={tagInput}
                    placeholder="add tag, press Enter"
                    onChange={onTagInputChange}
                    onKeyDown={onTagInputKeyDown}
                />
            </div>
            <div className="host-meta-tag-color-picker" role="group" aria-label="tag color">
                <button
                    type="button"
                    className={`host-meta-tag-swatch host-meta-tag-swatch-default${!tagColor ? " selected" : ""}`}
                    onClick={() => onColorChange("")}
                    aria-label="no color"
                    title="no color"
                />
                {TAG_COLORS.map(c => (
                    <button
                        key={c}
                        type="button"
                        className={`host-meta-tag-swatch host-tag-color-${c}${tagColor === c ? " selected" : ""}`}
                        onClick={() => onColorChange(c)}
                        aria-label={`color ${c}`}
                        title={c}
                    />
                ))}
            </div>
            {existingTags.length > 0 ? (
                <Button type="button" variant="ghost" size="sm" onClick={onClearTags} disabled={saving}>
                    clear all
                </Button>
            ) : null}
        </div>
    );
}

export function HostMetaEditor({ip, meta, onPatch, onDeleteField})
{
    const existing = meta || {};
    const existingTags = parseTags(existing.tags);
    const { tags, setTags, tagInput, setTagInput, tagColor, setTagColor, addTag, removeTag } = useTagEditor(existing.tags);
    const { saving, error, savedAt, handleSave, handleClearField } = useMetaSave({ ip, tags, setTags, tagInput, tagColor, setTagInput, onPatch, onDeleteField });

    return (
        <form className="host-meta-editor" onSubmit={handleSave}>
            <div className="mono-label host-meta-editor-title">edit metadata</div>
            <HostMetaTagField
                state={{ tags, tagInput, tagColor, existingTags }}
                handlers={{ onRemoveTag: removeTag, onTagInputChange: e => setTagInput(e.target.value), onTagInputKeyDown: addTag, onColorChange: setTagColor, onClearTags: () => handleClearField("tags") }}
                saving={saving}
            />
            {error ? <div className="host-meta-editor-error">{error}</div> : null}
            <div className="form-actions">
                <Button type="submit" variant="primary" disabled={saving}>
                    {saving ? "saving…" : "save"}
                </Button>
                {!error && savedAt ? (
                    <span className="host-meta-editor-saved">saved</span>
                ) : null}
            </div>
        </form>
    );
}
