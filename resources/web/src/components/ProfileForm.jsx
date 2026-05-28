import React from "react";
import { Button } from "../atoms.jsx";

export function HostDiscoveryOnlyField({ checked, disabled, onChange }) {
  return (
    <div className="profile-form-field profile-form-field-inline">
      <label className="profile-form-checkbox-label">
        <input
          type="checkbox"
          checked={checked}
          disabled={disabled}
          onChange={onChange}
        />
        <span>host discovery only</span>
      </label>
    </div>
  );
}

export function ProfileForm({
  profileForm,
  setProfileForm,
  onSubmit,
  onCancel
}) {
  return (
    <form className="profile-form profile-form-grid" onSubmit={onSubmit}>
      <div className="profile-form-field">
        <label className="profile-form-label">name</label>
        <input
          type="text"
          className="field-input"
          value={profileForm.name}
          placeholder="profile name"
          onChange={event => setProfileForm(previous => ({ ...previous, name: event.target.value }))}
        />
      </div>
      <div className="profile-form-field">
        <label className="profile-form-label">target</label>
        <input
          type="text"
          className="field-input"
          value={profileForm.target}
          placeholder="192.168.1.0/24"
          onChange={event => setProfileForm(previous => ({ ...previous, target: event.target.value }))}
        />
      </div>
      <div className="profile-form-field">
        <label className="profile-form-label">ports</label>
        <input
          type="text"
          className="field-input"
          value={profileForm.ports}
          disabled={profileForm.hostDiscoveryOnly}
          placeholder="22,80,443"
          onChange={event => setProfileForm(previous => ({ ...previous, ports: event.target.value }))}
        />
      </div>
      <HostDiscoveryOnlyField
        checked={profileForm.hostDiscoveryOnly}
        onChange={event => setProfileForm(previous => ({
          ...previous,
          hostDiscoveryOnly: event.target.checked,
          ports: event.target.checked ? "" : previous.ports
        }))}
      />
      <div className="profile-form-spacer" />
      <div className="profile-form-actions">
        <Button type="submit" variant="primary" size="sm">
          {profileForm.id ? "update profile" : "save profile"}
        </Button>
        {profileForm.id ? (
          <Button type="button" variant="text" size="sm" onClick={onCancel}>
            cancel
          </Button>
        ) : null}
      </div>
    </form>
  );
}
