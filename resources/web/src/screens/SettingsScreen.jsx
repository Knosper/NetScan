import React from "react";
import { useSchedulerContext } from "../SchedulerContext.jsx";
import { PageShell, PageSection, ListRegion } from "../components/PageShell.jsx";
import { stateKindFromBanner } from "../ui/helpers.js";
import { InlineBanner, StateNotice } from "../ui/notices.jsx";
import {
  ApiKeySection,
  ProfilesSection,
  ReconfigureSection,
  ScheduledScansSection,
  SettingsActionRows,
  SettingsForm,
  ThemeSettingsSection
} from "./settingsScreenSections.jsx";

export function SettingsScreen({
  backendUnavailable,
  offlineMessage,
  loading,
  settings,
  setSettings,
  settingsRowErrors,
  setSettingsRowErrors,
  handleSaveSettings,
  profiles,
  profileForm,
  setProfileForm,
  handleCreateProfile,
  handleCancelProfileEdit,
  runProfile,
  startProfileEdit,
  deleteProfile,
  handleShutdown,
  handlePruneClosedPorts,
  banners
}) {
  const {
    schedulerJobs, schedulerLoading, createSchedulerJob, deleteSchedulerJob, setSchedulerJobEnabled
  } = useSchedulerContext();
  const settingsBanner = banners.settings || null;
  const settingsSaveKind = stateKindFromBanner(settingsBanner);

  const [savedSettings, setSavedSettings] = React.useState(null);

  React.useEffect(() => {
    if (settings && !savedSettings) {
      setSavedSettings(settings);
    }
  }, [settings]);

  React.useEffect(() => {
    if (settingsBanner?.tone === "success" || settingsSaveKind === "completed") {
      if (settings) setSavedSettings(settings);
    }
  }, [settingsBanner]);
  const [profileSearch, setProfileSearch] = React.useState("");
  const [profilePage, setProfilePage] = React.useState(1);

  const filteredProfiles = profileSearch.trim()
    ? profiles.filter(p => String(p.name || "").toLowerCase().includes(profileSearch.trim().toLowerCase()))
    : profiles;

  React.useEffect(() => { setProfilePage(1); }, [profileSearch]);

  return (
    <PageShell
      banner={!backendUnavailable && settingsBanner?.tone === "danger"
        ? <InlineBanner banner={settingsBanner} />
        : null}
    >
      <PageSection title="settings" flush>
        <ListRegion>
          {loading.settings ? <StateNotice kind="loading" message="loading settings" compact /> : null}

          <ThemeSettingsSection />
          <ApiKeySection />
          <SettingsForm
            loading={loading}
            settings={settings}
            setSettings={setSettings}
            settingsRowErrors={settingsRowErrors}
            setSettingsRowErrors={setSettingsRowErrors}
            handleSaveSettings={handleSaveSettings}
            backendUnavailable={backendUnavailable}
            settingsBanner={settingsBanner}
            settingsSaveKind={settingsSaveKind}
            savedSettings={savedSettings}
          />
          <SettingsActionRows
            handlePruneClosedPorts={handlePruneClosedPorts}
            handleShutdown={handleShutdown}
          />
        </ListRegion>
      </PageSection>

      <ProfilesSection
        banners={banners}
        profileForm={profileForm}
        setProfileForm={setProfileForm}
        handleCreateProfile={handleCreateProfile}
        handleCancelProfileEdit={handleCancelProfileEdit}
        profiles={profiles}
        profileSearch={profileSearch}
        setProfileSearch={setProfileSearch}
        filteredProfiles={filteredProfiles}
        profilePage={profilePage}
        setProfilePage={setProfilePage}
        runProfile={runProfile}
        startProfileEdit={startProfileEdit}
        deleteProfile={deleteProfile}
      />

      <ScheduledScansSection
        banners={banners}
        schedulerJobs={schedulerJobs || []}
        schedulerLoading={Boolean(schedulerLoading)}
        createSchedulerJob={createSchedulerJob}
        deleteSchedulerJob={deleteSchedulerJob}
        setSchedulerJobEnabled={setSchedulerJobEnabled}
      />

      <ReconfigureSection />
    </PageShell>
  );
}
