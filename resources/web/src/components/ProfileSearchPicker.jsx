import React from "react";
import { useProfilePickerInteraction } from "../hooks/useProfilePickerInteraction.jsx";

export function ProfileSearchPicker({
  options,
  onSelect,
  triggerLabel,
  placeholder = "search profiles",
  emptyLabel = "no matching profiles",
  renderItem = (profile) => (
    <>
      <span>{profile.name}</span>
      <span className="profile-search-picker-meta">
        [{profile.source === "default" ? "default" : "saved"}]
      </span>
    </>
  )
}) {
  const [query, setQuery] = React.useState("");
  const normalizedQuery = query.trim().toLowerCase();
  const filtered = options.filter(profile =>
    String(profile?.name || "").toLowerCase().includes(normalizedQuery)
  );

  function handleSelect(profile) {
    onSelect(profile);
    closePicker();
  }

  const {
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
  } = useProfilePickerInteraction({
    optionCount: filtered.length,
    onSelectActive: index => handleSelect(filtered[index]),
    onOpen: () => setQuery(""),
    onClose: () => setQuery("")
  });

  return (
    <div className="profile-search-picker" ref={pickerRef}>
      {!isOpen ? (
        <button
          type="button"
          className="button button-ghost profile-search-picker-trigger"
          aria-expanded={isOpen}
          aria-haspopup="listbox"
          onClick={handleTriggerClick}
          ref={triggerRef}
        >
          {triggerLabel}
        </button>
      ) : null}

      {isOpen && (
        <div className="profile-search-picker-popover">
          <input
            ref={inputRef}
            className="field-input profile-search-picker-input"
            type="text"
            value={query}
            placeholder={placeholder}
            onChange={event => {
              setQuery(event.target.value);
              handleQueryChange();
            }}
            onKeyDown={handleInputKeyDown}
          />

          <div className="profile-search-picker-list" role="listbox" aria-label="profiles">
            {filtered.length ? filtered.map((profile, index) => (
              <button
                key={profile.id}
                type="button"
                className={`button button-ghost profile-search-picker-item ${index === activeIndex ? "is-active" : ""}`}
                role="option"
                aria-selected={index === activeIndex}
                onMouseEnter={() => setActiveIndex(index)}
                onClick={() => handleSelect(profile)}
              >
                {renderItem(profile)}
              </button>
            )) : (
              <div className="profile-search-picker-empty">{emptyLabel}</div>
            )}
          </div>
        </div>
      )}
    </div>
  );
}
