import React from "react";

export function useProfilePickerInteraction({
  optionCount,
  onSelectActive,
  onOpen,
  onClose
}) {
  const [isOpen, setIsOpen] = React.useState(false);
  const [activeIndex, setActiveIndex] = React.useState(0);
  const pickerRef = React.useRef(null);
  const inputRef = React.useRef(null);
  const triggerRef = React.useRef(null);

  const closePicker = React.useCallback(({ returnFocus = false } = {}) => {
    setIsOpen(false);
    setActiveIndex(0);
    onClose?.();
    if (returnFocus)
      triggerRef.current?.focus();
  }, [onClose]);

  const openPicker = React.useCallback(() => {
    setIsOpen(true);
    setActiveIndex(0);
    onOpen?.();
  }, [onOpen]);

  const handleTriggerClick = React.useCallback(() => {
    if (isOpen) {
      closePicker({ returnFocus: true });
      return;
    }
    openPicker();
  }, [closePicker, isOpen, openPicker]);

  const handleQueryChange = React.useCallback(() => {
    setActiveIndex(0);
  }, []);

  const handleInputKeyDown = React.useCallback((event) => {
    if (event.key === "ArrowDown") {
      if (!optionCount)
        return;
      event.preventDefault();
      setActiveIndex(previous => Math.min(previous + 1, optionCount - 1));
      return;
    }
    if (event.key === "ArrowUp") {
      if (!optionCount)
        return;
      event.preventDefault();
      setActiveIndex(previous => Math.max(previous - 1, 0));
      return;
    }
    if (event.key === "Enter") {
      event.preventDefault();
      if (!optionCount)
        return;
      onSelectActive?.(activeIndex);
      return;
    }
    if (event.key === "Escape") {
      event.preventDefault();
      closePicker({ returnFocus: true });
    }
  }, [activeIndex, closePicker, onSelectActive, optionCount]);

  React.useEffect(() => {
    if (!isOpen)
      return;

    const frameId = window.requestAnimationFrame(() => {
      inputRef.current?.focus();
      inputRef.current?.select();
    });

    return () => window.cancelAnimationFrame(frameId);
  }, [isOpen]);

  React.useEffect(() => {
    if (!isOpen)
      return;

    function handlePointerDown(event) {
      if (!pickerRef.current?.contains(event.target))
        closePicker();
    }

    document.addEventListener("mousedown", handlePointerDown);
    document.addEventListener("touchstart", handlePointerDown);

    return () => {
      document.removeEventListener("mousedown", handlePointerDown);
      document.removeEventListener("touchstart", handlePointerDown);
    };
  }, [closePicker, isOpen]);

  React.useEffect(() => {
    if (!optionCount) {
      setActiveIndex(0);
      return;
    }
    if (activeIndex >= optionCount)
      setActiveIndex(optionCount - 1);
  }, [activeIndex, optionCount]);

  return {
    isOpen,
    activeIndex,
    pickerRef,
    inputRef,
    triggerRef,
    closePicker,
    handleTriggerClick,
    handleQueryChange,
    handleInputKeyDown,
    setActiveIndex
  };
}
