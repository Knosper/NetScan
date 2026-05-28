import { createContext, useContext } from "react";

export const ScanContext = createContext(null);

export function useScanContext()
{
    return useContext(ScanContext);
}
