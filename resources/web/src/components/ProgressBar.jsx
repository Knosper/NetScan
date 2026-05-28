import React from "react";
import {
  PROGRESS_ANIMATION_MS,
  PROGRESS_COMPLETION_FADE_MS,
  PROGRESS_COMPLETION_HOLD_MS,
  PROGRESS_ELAPSED_REFRESH_MS,
  PROGRESS_HEURISTIC_STEP_MS,
  PROGRESS_PHANTOM_INTERVAL_MS,
  SECOND_MS,
  SECONDS_PER_HOUR,
  SECONDS_PER_MINUTE
} from "../constants/timing.js";

const PHANTOM_PROGRESS_START = 1;
const PHANTOM_PROGRESS_MAX = 8;
const HEURISTIC_PROGRESS_STEPS = [1, 3, 5, 7, 8];

function clampProgress(progress) {
  if (!Number.isFinite(progress))
    return null;

  return Math.max(0, Math.min(100, Math.round(progress)));
}

function easeOutCubic(t) {
  return 1 - Math.pow(1 - t, 3);
}

function formatElapsedTime(ms) {
  if (!Number.isFinite(ms) || ms < 0)
    return null;

  const totalSeconds = Math.floor(ms / SECOND_MS);
  const hours = Math.floor(totalSeconds / SECONDS_PER_HOUR);
  const minutes = Math.floor((totalSeconds % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE);
  const seconds = totalSeconds % SECONDS_PER_MINUTE;

  if (hours > 0)
    return `${hours}h ${minutes}m ${seconds}s`;
  if (minutes > 0)
    return `${minutes}m ${seconds}s`;
  return `${seconds}s`;
}

function resolveHeuristicProgress(elapsedMs) {
  if (!Number.isFinite(elapsedMs) || elapsedMs <= 0)
    return PHANTOM_PROGRESS_START;

  if (elapsedMs < PROGRESS_HEURISTIC_STEP_MS)
    return 3;
  if (elapsedMs < PROGRESS_HEURISTIC_STEP_MS * 2)
    return 5;
  if (elapsedMs < PROGRESS_HEURISTIC_STEP_MS * 3)
    return 7;
  if (elapsedMs < PROGRESS_HEURISTIC_STEP_MS * 5)
    return 8;

  return PHANTOM_PROGRESS_MAX;
}

function resolveProgressMode(progress, isDeterminate, isActive) {
  const clampedProgress = typeof progress === "number" ? clampProgress(progress) : null;
  const hasReliableProgress = clampedProgress !== null && clampedProgress > 0;
  const showDeterminate = isDeterminate === undefined
    ? hasReliableProgress
    : Boolean(isDeterminate) && hasReliableProgress;
  const showPhantom = Boolean(isActive) && !showDeterminate;
  const showProgress = showDeterminate || showPhantom;

  return { clampedProgress, showDeterminate, showPhantom, showProgress };
}

function ProgressShell({ isCompleting, showProgress, renderedValue, elapsedTime }) {
  return (
    <div className={isCompleting ? "progress-shell progress-shell-completing" : "progress-shell"}>
      <div className="progress-meta">
        <span className="progress-label">{showProgress ? "progress" : "waiting"}</span>
        <span className="progress-value">
          {renderedValue}%
          {elapsedTime && <span className="progress-time">{elapsedTime}</span>}
        </span>
      </div>
      <div className="progress-bar-wrap">
        <div className="progress-bar" style={{ width: `${renderedValue}%` }} />
      </div>
    </div>
  );
}

export function ProgressBar({
  progress,
  isDeterminate,
  isActive,
  resetKey
}) {
  const { clampedProgress, showDeterminate, showPhantom, showProgress } = resolveProgressMode(
    progress,
    isDeterminate,
    isActive
  );
  const [displayValue, setDisplayValue] = React.useState(0);
  const [isCompleting, setIsCompleting] = React.useState(false);
  const [isCompleteDismissed, setIsCompleteDismissed] = React.useState(false);
  const [elapsedTime, setElapsedTime] = React.useState(null);
  const [phantomTarget, setPhantomTarget] = React.useState(null);
  const displayValueRef = React.useRef(0);
  const timerStartRef = React.useRef(null);
  const phantomQueueRef = React.useRef([]);

  function updateDisplayValue(nextValue) {
    const nextDisplayValue = clampProgress(nextValue);
    displayValueRef.current = nextDisplayValue;
    setDisplayValue(nextDisplayValue);
  }

  function schedulePhantomSteps(limit) {
    const clampedLimit = clampProgress(limit);
    if (clampedLimit === null)
      return;

    const highestScheduled = Math.max(
      displayValueRef.current,
      phantomTarget ?? 0,
      phantomQueueRef.current.length ? phantomQueueRef.current[phantomQueueRef.current.length - 1] : 0
    );

    HEURISTIC_PROGRESS_STEPS.forEach(step => {
      if (step > highestScheduled && step <= clampedLimit)
        phantomQueueRef.current.push(step);
    });

    if (phantomTarget === null && phantomQueueRef.current.length)
      setPhantomTarget(phantomQueueRef.current.shift());
  }

  React.useEffect(() => {
    updateDisplayValue(0);
    setIsCompleting(false);
    setIsCompleteDismissed(false);
    setElapsedTime(null);
    setPhantomTarget(null);
    phantomQueueRef.current = [];
    timerStartRef.current = null;
  }, [resetKey]);

  React.useEffect(() => {
    if (!showProgress) {
      updateDisplayValue(0);
      setIsCompleting(false);
      setIsCompleteDismissed(false);
      return undefined;
    }

    const targetValue = showDeterminate ? clampedProgress : phantomTarget;
    if (targetValue === null)
      return undefined;

    let frameId = 0;
    let startTime = 0;
    const startValue = displayValueRef.current;
    const delta = targetValue - startValue;

    if (delta === 0)
      return undefined;

    if (delta < 0) {
      updateDisplayValue(targetValue);
      return undefined;
    }

    function tick(timestamp) {
      if (!startTime)
        startTime = timestamp;

      const elapsed = timestamp - startTime;
      const progressRatio = Math.min(1, elapsed / PROGRESS_ANIMATION_MS);
      const nextValue = startValue + delta * easeOutCubic(progressRatio);

      updateDisplayValue(nextValue);

      if (progressRatio < 1)
        frameId = window.requestAnimationFrame(tick);
    }

    frameId = window.requestAnimationFrame(tick);

    return () => window.cancelAnimationFrame(frameId);
  }, [clampedProgress, phantomTarget, showDeterminate, showProgress]);

  React.useEffect(() => {
    if (!showPhantom) {
      setPhantomTarget(null);
      phantomQueueRef.current = [];
      return undefined;
    }

    function updateHeuristicValue() {
      const elapsed = timerStartRef.current ? Date.now() - timerStartRef.current : 0;
      const nextValue = resolveHeuristicProgress(elapsed);
      schedulePhantomSteps(Math.min(PHANTOM_PROGRESS_MAX, nextValue));
    }

    updateHeuristicValue();

    const intervalId = window.setInterval(() => {
      if (displayValueRef.current >= PHANTOM_PROGRESS_MAX)
        return;
      updateHeuristicValue();
    }, PROGRESS_PHANTOM_INTERVAL_MS);

    return () => window.clearInterval(intervalId);
  }, [phantomTarget, resetKey, showPhantom]);

  React.useEffect(() => {
    if (!showPhantom || phantomTarget === null || displayValue !== phantomTarget)
      return;

    if (!phantomQueueRef.current.length) {
      setPhantomTarget(null);
      return;
    }

    setPhantomTarget(phantomQueueRef.current.shift());
  }, [displayValue, phantomTarget, showPhantom]);

  React.useEffect(() => {
    if (!isActive) {
      timerStartRef.current = null;
      return undefined;
    }

    if (!timerStartRef.current)
      timerStartRef.current = Date.now();

    function updateElapsed() {
      const elapsed = Date.now() - timerStartRef.current;
      setElapsedTime(formatElapsedTime(elapsed));
    }

    updateElapsed();
    const intervalId = window.setInterval(updateElapsed, PROGRESS_ELAPSED_REFRESH_MS);
    return () => window.clearInterval(intervalId);
  }, [isActive]);

  React.useEffect(() => {
    if (!showDeterminate || clampedProgress !== 100 || displayValue !== 100) {
      setIsCompleting(false);
      setIsCompleteDismissed(false);
      return undefined;
    }

    const holdId = window.setTimeout(() => {
      setIsCompleting(true);
    }, PROGRESS_COMPLETION_HOLD_MS);
    const dismissId = window.setTimeout(() => {
      setIsCompleteDismissed(true);
    }, PROGRESS_COMPLETION_HOLD_MS + PROGRESS_COMPLETION_FADE_MS);

    return () => {
      window.clearTimeout(holdId);
      window.clearTimeout(dismissId);
    };
  }, [clampedProgress, displayValue, showDeterminate]);

  if (isCompleteDismissed)
    return null;

  const renderedValue = showProgress
    ? displayValue
    : PHANTOM_PROGRESS_START;

  return (
    <ProgressShell
      isCompleting={isCompleting}
      showProgress={showProgress}
      renderedValue={renderedValue}
      elapsedTime={elapsedTime}
    />
  );
}
