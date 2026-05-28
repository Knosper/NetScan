import { createContext, useContext } from "react";

export const PresenceContext = createContext(null);

export function usePresenceContext()
{
    return useContext(PresenceContext);
}
