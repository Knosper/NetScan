import { useEffect } from "react";
import { KEYBOARD_SEQUENCE_TIMEOUT_MS } from "../constants/timing.js";

export function useKeyboardShortcuts(go, commandInputRef)
{
    useEffect(() => {
        let seq = "";
        let timer = null;

        const NAV_MAP = { "gd": "dashboard", "gs": "hosts", "gh": "hosts", "gv": "services", "gc": "changes", "g,": "settings" };

        function onKey(e) {
            if (e.target.tagName === "INPUT" || e.target.tagName === "TEXTAREA" || e.target.tagName === "SELECT")
                return;

            if (e.key === "/" && !e.ctrlKey && !e.metaKey && !e.altKey) {
                e.preventDefault();
                commandInputRef.current?.focus();
                return;
            }

            seq += e.key.toLowerCase();
            clearTimeout(timer);
            timer = setTimeout(() => { seq = ""; }, KEYBOARD_SEQUENCE_TIMEOUT_MS);

            if (NAV_MAP[seq]) {
                go(NAV_MAP[seq]);
                seq = "";
            }
        }

        window.addEventListener("keydown", onKey);
        return () => {
            window.removeEventListener("keydown", onKey);
            clearTimeout(timer);
        };
    }, [go, commandInputRef]);
}
