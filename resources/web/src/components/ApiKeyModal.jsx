import React, {useEffect, useRef, useState} from "react";
import {Button} from "../atoms.jsx";
import {setApiKey} from "../apiKey.js";

export function ApiKeyModal({open, onClose, retry})
{
    const [value, setValue] = useState("");
    const inputRef = useRef(null);

    useEffect(() => {
        if (!open)
        {
            setValue("");
            return;
        }
        inputRef.current?.focus();
    }, [open]);

    if (!open)
        return null;

    function handleSubmit(event)
    {
        event.preventDefault();
        const trimmed = value.trim();
        if (!trimmed)
            return;
        setApiKey(trimmed);
        setValue("");
        onClose?.();
        retry?.();
    }

    return (
        <div className="api-key-modal-backdrop" role="dialog" aria-modal="true" aria-label="api key required">
            <div className="api-key-modal">
                <div className="api-key-modal-head">
                    <div className="api-key-modal-title">api key required</div>
                    <div className="api-key-modal-sub">enter the api key to access this instance</div>
                </div>
                <form className="api-key-modal-body" onSubmit={handleSubmit}>
                    <label className="api-key-modal-label" htmlFor="api-key-input">api key</label>
                    <input
                        id="api-key-input"
                        ref={inputRef}
                        type="password"
                        className="field-input"
                        autoComplete="off"
                        spellCheck={false}
                        value={value}
                        onChange={event => setValue(event.target.value)}
                    />
                    <div className="api-key-modal-actions">
                        <Button type="submit" variant="primary" size="sm" disabled={!value.trim()}>
                            save key
                        </Button>
                    </div>
                </form>
            </div>
        </div>
    );
}
