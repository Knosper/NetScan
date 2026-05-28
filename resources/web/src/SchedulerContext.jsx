import { createContext, useContext } from "react";

export const SchedulerContext = createContext(null);

export function useSchedulerContext()
{
    return useContext(SchedulerContext);
}
