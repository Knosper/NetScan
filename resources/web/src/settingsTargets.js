export const CIDR_PREFIX_OPTIONS = [
  "1", "2", "3", "4", "5", "6", "7", "8",
  "9", "10", "11", "12", "13", "14", "15", "16",
  "17", "18", "19", "20", "21", "22", "23", "24",
  "25", "26", "27", "28", "29", "30", "31", "32"
];

function trimValue(value) {
  return String(value || "").trim();
}

export function createIpTargetRow(value = "") {
  return { kind: "single", value: trimValue(value) };
}

export function createSubnetTargetRow(base = "", prefix = "24") {
  return {
    kind: "cidr",
    base: trimValue(base),
    prefix: CIDR_PREFIX_OPTIONS.includes(String(prefix)) ? String(prefix) : "24"
  };
}

export function createCustomTargetRow(value = "") {
  return { kind: "custom", value: trimValue(value) };
}

export function toTargetRows(values) {
  if (!Array.isArray(values) || !values.length)
    return [];

  return values.map(value => {
    const trimmed = trimValue(value);
    const slash = trimmed.lastIndexOf("/");
    if (slash > 0 && slash === trimmed.indexOf("/") && !trimmed.includes(":")) {
      const base = trimValue(trimmed.slice(0, slash));
      const prefix = trimmed.slice(slash + 1);
      if (CIDR_PREFIX_OPTIONS.includes(prefix))
        return createSubnetTargetRow(base, prefix);
    }

    if (trimmed.includes(":"))
      return createCustomTargetRow(trimmed);

    return createIpTargetRow(trimmed);
  });
}

export function normalizeTargetRowsForApi(rows) {
  if (!Array.isArray(rows))
    return [];

  return rows.map(row => {
    if (!row || typeof row !== "object")
      return "";

    if (row.kind === "cidr") {
      const base = trimValue(row.base);
      return `${base}/${row.prefix || "24"}`;
    }

    return trimValue(row.value);
  });
}

export function createSettingsModel(rawSettings = {}) {
  return {
    log_level: rawSettings?.log_level || "info",
    scan_cooldown_seconds: Number.isInteger(rawSettings?.scan_cooldown_seconds)
      ? rawSettings.scan_cooldown_seconds
      : 0,
    user_allowed_targets: toTargetRows(rawSettings?.user_allowed_targets || [])
  };
}

export function normalizeSettingsForApi(settings = {}) {
  return {
    log_level: settings?.log_level || "info",
    scan_cooldown_seconds: Number.isInteger(settings?.scan_cooldown_seconds)
      ? settings.scan_cooldown_seconds
      : 0,
    user_allowed_targets: normalizeTargetRowsForApi(settings?.user_allowed_targets)
  };
}
