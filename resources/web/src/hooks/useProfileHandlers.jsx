import { useCallback } from "react";
import { buildBanner } from "../ui/helpers.js";
import { useAppContext } from "../AppContext.jsx";
import { EMPTY_PROFILE_FORM } from "./useSettingsHandlers.jsx";
import { BANNER_AUTO_HIDE_MS } from "../constants/timing.js";

export function useProfileHandlers({
    profileForm,
    setProfileForm,
    createProfile,
    updateProfile
})
{
    const { setBanner } = useAppContext();

    const handleCreateProfile = useCallback(async (event) => {
        event.preventDefault();

        const name = profileForm.name.trim();
        const target = profileForm.target.trim();

        if (!name)
        {
            setBanner("profiles", buildBanner("danger", "profile name required"), BANNER_AUTO_HIDE_MS);
            return;
        }

        if (!target)
        {
            setBanner("profiles", buildBanner("danger", "target required"), BANNER_AUTO_HIDE_MS);
            return;
        }

        const ok = profileForm.id
            ? await updateProfile(profileForm.id, name, target, profileForm.ports,
                profileForm.hostDiscoveryOnly)
            : await createProfile({ name, target, ports: profileForm.ports, hostDiscoveryOnly: profileForm.hostDiscoveryOnly });

        if (ok)
            setProfileForm(EMPTY_PROFILE_FORM);
    }, [profileForm, setProfileForm, createProfile, updateProfile, setBanner]);

    const handleCancelProfileEdit = useCallback(() => {
        setProfileForm(EMPTY_PROFILE_FORM);
    }, [setProfileForm]);

    const startProfileEdit = useCallback((profile) => {
        setProfileForm({
            id: profile.id,
            name: profile.name || "",
            target: profile.target || "",
            ports: profile.ports || "",
            hostDiscoveryOnly: Boolean(profile.hostDiscoveryOnly)
        });
    }, [setProfileForm]);

    return { handleCreateProfile, handleCancelProfileEdit, startProfileEdit };
}
