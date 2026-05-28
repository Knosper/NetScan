import { useState, useEffect, useRef } from "react";
import {
    CLOCK_TICK_MS,
    SECOND_MS,
    SECONDS_PER_DAY,
    SECONDS_PER_HOUR,
    SECONDS_PER_MINUTE
} from "../constants/timing.js";

export function useUtcClock() {
    const [time, setTime] = useState(() => new Date().toISOString().slice(11, 19));
    useEffect(() => {
        const id = setInterval(() => setTime(new Date().toISOString().slice(11, 19)), CLOCK_TICK_MS);
        return () => clearInterval(id);
    }, []);
    return time;
}

export function useSessionUptime()
{
    const startedAtRef = useRef(Date.now());
    const [uptime, setUptime] = useState("0m");

    useEffect(() => {
        function update()
        {
            const totalSeconds = Math.max(0, Math.floor((Date.now() - startedAtRef.current) / SECOND_MS));
            const days = Math.floor(totalSeconds / SECONDS_PER_DAY);
            const hours = Math.floor((totalSeconds % SECONDS_PER_DAY) / SECONDS_PER_HOUR);
            const minutes = Math.floor((totalSeconds % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE);

            if (days > 0)
            {
                setUptime(`${days}d ${hours}h`);
                return;
            }

            if (hours > 0)
            {
                setUptime(`${hours}h ${minutes}m`);
                return;
            }

            setUptime(`${minutes}m`);
        }

        update();
        const id = setInterval(update, CLOCK_TICK_MS);
        return () => clearInterval(id);
    }, []);

    return uptime;
}