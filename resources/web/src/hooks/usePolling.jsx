import { useEffect, useRef } from "react";

export function usePolling(pollFn, onMount, onUnmount)
{
    const pollFnRef = useRef(pollFn);
    const onMountRef = useRef(onMount);
    const onUnmountRef = useRef(onUnmount);

    pollFnRef.current = pollFn;
    onMountRef.current = onMount;
    onUnmountRef.current = onUnmount;

    useEffect(() => {
        onMountRef.current();

        let active = true;
        let timeoutId = null;

        async function loop(delay)
        {
            timeoutId = setTimeout(async () => {
                const nextDelay = await pollFnRef.current();
                if (active)
                    loop(nextDelay);
            }, delay);
        }

        loop(250);

        return () => {
            active = false;
            if (timeoutId)
                clearTimeout(timeoutId);
            onUnmountRef.current();
        };
    }, []);
}
