const PORT_RISK_MAP = {
    21:    { label: "ftp",        severity: "critical", description: "unencrypted file transfer" },
    22:    { label: "ssh",        severity: "info",     description: "remote access" },
    23:    { label: "telnet",     severity: "critical", description: "unencrypted remote shell" },
    25:    { label: "smtp",       severity: "warning",  description: "unencrypted mail relay" },
    53:    { label: "dns",        severity: "info",     description: "domain name service" },
    80:    { label: "http",       severity: "info",     description: "unencrypted web traffic" },
    110:   { label: "pop3",       severity: "warning",  description: "unencrypted mail retrieval" },
    135:   { label: "rpc",        severity: "warning",  description: "windows rpc, often legacy" },
    139:   { label: "netbios",    severity: "warning",  description: "legacy protocol" },
    143:   { label: "imap",       severity: "warning",  description: "unencrypted mail access" },
    445:   { label: "smb",        severity: "warning",  description: "legacy protocol" },
    1433:  { label: "mssql",      severity: "warning",  description: "database (verify auth config)" },
    1521:  { label: "oracle",     severity: "warning",  description: "database (verify auth config)" },
    3306:  { label: "mysql",      severity: "warning",  description: "database (verify auth config)" },
    3389:  { label: "rdp",        severity: "warning",  description: "remote desktop access" },
    4444:  { label: "backdoor",   severity: "critical", description: "common backdoor port" },
    5432:  { label: "postgres",   severity: "warning",  description: "database (verify auth config)" },
    5900:  { label: "vnc",        severity: "warning",  description: "remote access, often unencrypted" },
    5901:  { label: "vnc",        severity: "warning",  description: "remote access, often unencrypted" },
    5902:  { label: "vnc",        severity: "warning",  description: "remote access, often unencrypted" },
    6379:  { label: "redis",      severity: "warning",  description: "database (often weak default auth)" },
    8080:  { label: "http-alt",   severity: "info",     description: "unencrypted web traffic" },
    8443:  { label: "https-alt",  severity: "info",     description: "alternative https port" },
    27017: { label: "mongodb",    severity: "warning",  description: "database (verify auth config)" },
};

export function getPortRisk(port) {
    return PORT_RISK_MAP[port] ?? null;
}
