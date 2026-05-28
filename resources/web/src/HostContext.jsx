import { createContext, useContext } from "react";

export const HostContext = createContext(null);

export function useHostContext()
{
    return useContext(HostContext);
}
