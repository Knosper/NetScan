// Mirrors is_loopback_request_host() on the server: "localhost" or a loopback
// IP literal (127.0.0.0/8, ::1). Accepts location.hostname ("[::1]") and bare
// config values ("::1").
export function isLoopbackHost(host) {
  const name = String(host || "").toLowerCase().replace(/^\[(.*)\]$/, "$1");
  if (name === "localhost") return true;
  if (/^127(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}$/.test(name)) return true;
  if (!name.includes(":")) return false;
  try {
    return new URL(`http://[${name}]/`).hostname === "[::1]";
  } catch {
    return false;
  }
}
