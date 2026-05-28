#include "scan/nmap_xml_parser.hpp"
#include "test_output.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace lsm::scan;

namespace
{

bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

const char* LINUX_NMAP_HEADER =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE nmaprun>\n"
    "<?xml-stylesheet href=\"file:///usr/bin/../share/nmap/nmap.xsl\" type=\"text/xsl\"?>\n"
    "<!-- Nmap 7.94SVN scan initiated -->\n"
    "<nmaprun scanner=\"nmap\" args=\"nmap\" start=\"1000000000\""
    " version=\"7.94SVN\" xmloutputversion=\"1.05\">\n"
    "<scaninfo type=\"connect\" protocol=\"tcp\" numservices=\"1\" services=\"22\"/>\n"
    "<verbose level=\"1\"/>\n"
    "<debugging level=\"0\"/>\n";

const char* WINDOWS_NMAP_HEADER =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE nmaprun>\n"
    "<?xml-stylesheet href=\"file:///C:/Program Files (x86)/Nmap/nmap.xsl\" type=\"text/xsl\"?>\n"
    "<!-- Nmap 7.99 scan initiated -->\n"
    "<nmaprun scanner=\"nmap\" args=\"nmap\" start=\"1000000000\""
    " version=\"7.99\" xmloutputversion=\"1.05\">\n"
    "<scaninfo type=\"syn\" protocol=\"tcp\" numservices=\"1\" services=\"22\"/>\n"
    "<verbose level=\"1\"/>\n"
    "<debugging level=\"0\"/>\n";

const char* LINUX_NMAP_FOOTER =
    "<runstats><finished time=\"1000000001\" elapsed=\"0.01\" exit=\"success\"/>"
    "<hosts up=\"1\" down=\"0\" total=\"1\"/></runstats>\n"
    "</nmaprun>\n";

std::string read_nmap_fixture(const std::string& filename)
{
    const std::string path = "test/fixtures/nmap/" + filename;
    std::ifstream file(path);
    if (!file.is_open())
        return "";
    std::ostringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

static void test_empty_xml_returns_error(bool& all_ok)
{
    NmapXmlParseResult result = parse_nmap_xml("");
    all_ok = expect(!result.ok, "empty xml: ok should be false") && all_ok;
    all_ok = expect(!result.error.empty(), "empty xml: error should not be empty") && all_ok;
    all_ok = expect(result.snapshot.hosts.empty(), "empty xml: hosts should be empty") && all_ok;
}

static void test_invalid_xml_returns_error(bool& all_ok)
{
    NmapXmlParseResult result = parse_nmap_xml("<broken><xml><<<");
    all_ok = expect(!result.ok, "invalid xml: ok should be false") && all_ok;
    all_ok = expect(!result.error.empty(), "invalid xml: error should not be empty") && all_ok;
    all_ok = expect(result.snapshot.hosts.empty(), "invalid xml: hosts should be empty") && all_ok;
}

static void test_valid_xml_no_hosts_linux(bool& all_ok)
{
    const std::string xml = std::string(LINUX_NMAP_HEADER) + LINUX_NMAP_FOOTER;
    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "linux valid xml no hosts: ok should be true") && all_ok;
    all_ok = expect(result.error.empty(), "linux valid xml no hosts: error should be empty") && all_ok;
    all_ok = expect(result.snapshot.hosts.empty(), "linux valid xml no hosts: hosts should be empty") && all_ok;
}

static void test_valid_xml_no_hosts_windows(bool& all_ok)
{
    const std::string xml = std::string(WINDOWS_NMAP_HEADER) + LINUX_NMAP_FOOTER;
    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "windows valid xml no hosts: ok should be true") && all_ok;
    all_ok = expect(result.error.empty(), "windows valid xml no hosts: error should be empty") && all_ok;
    all_ok = expect(result.snapshot.hosts.empty(), "windows valid xml no hosts: hosts should be empty") && all_ok;
}

static void test_single_host_single_port_linux(bool& all_ok)
{
    const std::string xml = std::string(LINUX_NMAP_HEADER) +
        "<host>\n"
        "    <address addr=\"192.168.1.1\" addrtype=\"ipv4\"/>\n"
        "    <hostnames>\n"
        "        <hostname name=\"router.local\"/>\n"
        "    </hostnames>\n"
        "    <ports>\n"
        "        <port protocol=\"tcp\" portid=\"22\">\n"
        "            <state state=\"open\"/>\n"
        "            <service name=\"ssh\"/>\n"
        "        </port>\n"
        "    </ports>\n"
        "</host>\n" +
        LINUX_NMAP_FOOTER;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "linux single host/port: ok should be true") && all_ok;
    if (!result.ok) return;
    all_ok = expect(result.snapshot.hosts.size() == 1, "linux single host/port: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1) return;

    const HostSnapshot& host = result.snapshot.hosts[0];
    all_ok = expect(host.ip == "192.168.1.1", "linux single host/port: ip should match") && all_ok;
    all_ok = expect(host.hostname == "router.local", "linux single host/port: hostname should match") && all_ok;
    all_ok = expect(host.ports.size() == 1, "linux single host/port: should have 1 port") && all_ok;
    if (host.ports.size() != 1) return;

    const PortEntry& port = host.ports[0];
    all_ok = expect(port.protocol == "tcp", "linux single host/port: protocol should be tcp") && all_ok;
    all_ok = expect(port.port == 22, "linux single host/port: port should be 22") && all_ok;
    all_ok = expect(port.state == "open", "linux single host/port: state should be open") && all_ok;
    all_ok = expect(port.service == "ssh", "linux single host/port: service should be ssh") && all_ok;
}

static void test_single_host_single_port_windows(bool& all_ok)
{
    const std::string xml = std::string(WINDOWS_NMAP_HEADER) +
        "<host>\n"
        "    <address addr=\"192.168.1.1\" addrtype=\"ipv4\"/>\n"
        "    <hostnames>\n"
        "        <hostname name=\"router.local\"/>\n"
        "    </hostnames>\n"
        "    <ports>\n"
        "        <port protocol=\"tcp\" portid=\"22\">\n"
        "            <state state=\"open\"/>\n"
        "            <service name=\"ssh\"/>\n"
        "        </port>\n"
        "    </ports>\n"
        "</host>\n" +
        LINUX_NMAP_FOOTER;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "windows single host/port: ok should be true") && all_ok;
    if (!result.ok) return;
    all_ok = expect(result.snapshot.hosts.size() == 1, "windows single host/port: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1) return;

    const HostSnapshot& host = result.snapshot.hosts[0];
    all_ok = expect(host.ip == "192.168.1.1", "windows single host/port: ip should match") && all_ok;
    all_ok = expect(host.hostname == "router.local", "windows single host/port: hostname should match") && all_ok;
    all_ok = expect(host.ports.size() == 1, "windows single host/port: should have 1 port") && all_ok;
    if (host.ports.size() != 1) return;

    const PortEntry& port = host.ports[0];
    all_ok = expect(port.protocol == "tcp", "windows single host/port: protocol should be tcp") && all_ok;
    all_ok = expect(port.port == 22, "windows single host/port: port should be 22") && all_ok;
    all_ok = expect(port.state == "open", "windows single host/port: state should be open") && all_ok;
    all_ok = expect(port.service == "ssh", "windows single host/port: service should be ssh") && all_ok;
}

static void test_ipv4_ipv6_chooses_ipv4_linux(bool& all_ok)
{
    const std::string xml = std::string(LINUX_NMAP_HEADER) +
        "<host>\n"
        "    <address addr=\"fe80::1\" addrtype=\"ipv6\"/>\n"
        "    <address addr=\"192.168.1.1\" addrtype=\"ipv4\"/>\n"
        "</host>\n" +
        LINUX_NMAP_FOOTER;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "linux ipv4+ipv6: ok should be true") && all_ok;
    if (!result.ok) return;
    all_ok = expect(result.snapshot.hosts.size() == 1, "linux ipv4+ipv6: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1) return;
    all_ok = expect(result.snapshot.hosts[0].ip == "192.168.1.1", "linux ipv4+ipv6: should choose ipv4") && all_ok;
}

static void test_ipv4_ipv6_chooses_ipv4_windows(bool& all_ok)
{
    const std::string xml = std::string(WINDOWS_NMAP_HEADER) +
        "<host>\n"
        "    <address addr=\"fe80::1\" addrtype=\"ipv6\"/>\n"
        "    <address addr=\"192.168.1.1\" addrtype=\"ipv4\"/>\n"
        "</host>\n" +
        LINUX_NMAP_FOOTER;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "windows ipv4+ipv6: ok should be true") && all_ok;
    if (!result.ok) return;
    all_ok = expect(result.snapshot.hosts.size() == 1, "windows ipv4+ipv6: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1) return;
    all_ok = expect(result.snapshot.hosts[0].ip == "192.168.1.1", "windows ipv4+ipv6: should choose ipv4") && all_ok;
}

static void test_host_without_hostname_linux(bool& all_ok)
{
    const std::string xml = std::string(LINUX_NMAP_HEADER) +
        "<host>\n"
        "    <address addr=\"192.168.1.2\" addrtype=\"ipv4\"/>\n"
        "</host>\n" +
        LINUX_NMAP_FOOTER;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "linux no hostname: ok should be true") && all_ok;
    if (!result.ok) return;
    all_ok = expect(result.snapshot.hosts.size() == 1, "linux no hostname: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1) return;
    all_ok = expect(result.snapshot.hosts[0].hostname == "", "linux no hostname: hostname should be empty") && all_ok;
}

static void test_host_without_hostname_windows(bool& all_ok)
{
    const std::string xml = std::string(WINDOWS_NMAP_HEADER) +
        "<host>\n"
        "    <address addr=\"192.168.1.2\" addrtype=\"ipv4\"/>\n"
        "</host>\n" +
        LINUX_NMAP_FOOTER;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "windows no hostname: ok should be true") && all_ok;
    if (!result.ok) return;
    all_ok = expect(result.snapshot.hosts.size() == 1, "windows no hostname: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1) return;
    all_ok = expect(result.snapshot.hosts[0].hostname == "", "windows no hostname: hostname should be empty") && all_ok;
}

static void test_invalid_port_id_skipped_linux(bool& all_ok)
{
    const std::string xml = std::string(LINUX_NMAP_HEADER) +
        "<host>\n"
        "    <address addr=\"192.168.1.1\" addrtype=\"ipv4\"/>\n"
        "    <ports>\n"
        "        <port protocol=\"tcp\" portid=\"abc\">\n"
        "            <state state=\"open\"/>\n"
        "        </port>\n"
        "        <port protocol=\"tcp\" portid=\"0\">\n"
        "            <state state=\"open\"/>\n"
        "        </port>\n"
        "        <port protocol=\"tcp\" portid=\"65536\">\n"
        "            <state state=\"open\"/>\n"
        "        </port>\n"
        "    </ports>\n"
        "</host>\n" +
        LINUX_NMAP_FOOTER;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "linux invalid port ids: ok should be true") && all_ok;
    if (!result.ok) return;
    all_ok = expect(result.snapshot.hosts.size() == 1, "linux invalid port ids: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1) return;
    all_ok = expect(result.snapshot.hosts[0].ports.size() == 0, "linux invalid port ids: ports should be empty") && all_ok;
}

static void test_invalid_port_id_skipped_windows(bool& all_ok)
{
    const std::string xml = std::string(WINDOWS_NMAP_HEADER) +
        "<host>\n"
        "    <address addr=\"192.168.1.1\" addrtype=\"ipv4\"/>\n"
        "    <ports>\n"
        "        <port protocol=\"tcp\" portid=\"abc\">\n"
        "            <state state=\"open\"/>\n"
        "        </port>\n"
        "        <port protocol=\"tcp\" portid=\"0\">\n"
        "            <state state=\"open\"/>\n"
        "        </port>\n"
        "        <port protocol=\"tcp\" portid=\"65536\">\n"
        "            <state state=\"open\"/>\n"
        "        </port>\n"
        "    </ports>\n"
        "</host>\n" +
        LINUX_NMAP_FOOTER;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "windows invalid port ids: ok should be true") && all_ok;
    if (!result.ok) return;
    all_ok = expect(result.snapshot.hosts.size() == 1, "windows invalid port ids: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1) return;
    all_ok = expect(result.snapshot.hosts[0].ports.size() == 0, "windows invalid port ids: ports should be empty") && all_ok;
}

static void test_host_without_address_skipped_linux(bool& all_ok)
{
    const std::string xml = std::string(LINUX_NMAP_HEADER) +
        "<host>\n"
        "    <hostnames>\n"
        "        <hostname name=\"nohost.local\"/>\n"
        "    </hostnames>\n"
        "</host>\n" +
        LINUX_NMAP_FOOTER;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "linux host without address: ok should be true") && all_ok;
    all_ok = expect(result.snapshot.hosts.empty(), "linux host without address: hosts should be empty") && all_ok;
}

static void test_host_without_address_skipped_windows(bool& all_ok)
{
    const std::string xml = std::string(WINDOWS_NMAP_HEADER) +
        "<host>\n"
        "    <hostnames>\n"
        "        <hostname name=\"nohost.local\"/>\n"
        "    </hostnames>\n"
        "</host>\n" +
        LINUX_NMAP_FOOTER;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "windows host without address: ok should be true") && all_ok;
    all_ok = expect(result.snapshot.hosts.empty(), "windows host without address: hosts should be empty") && all_ok;
}

static void test_port_without_service_has_empty_name_linux(bool& all_ok)
{
    const std::string xml = std::string(LINUX_NMAP_HEADER) +
        "<host>\n"
        "    <address addr=\"192.168.1.1\" addrtype=\"ipv4\"/>\n"
        "    <ports>\n"
        "        <port protocol=\"tcp\" portid=\"1234\">\n"
        "            <state state=\"closed\"/>\n"
        "        </port>\n"
        "    </ports>\n"
        "</host>\n" +
        LINUX_NMAP_FOOTER;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "linux port without service: ok should be true") && all_ok;
    if (!result.ok) return;
    all_ok = expect(result.snapshot.hosts.size() == 1, "linux port without service: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1) return;
    all_ok = expect(result.snapshot.hosts[0].ports.size() == 1, "linux port without service: should have 1 port") && all_ok;
    if (result.snapshot.hosts[0].ports.size() != 1) return;
    all_ok = expect(result.snapshot.hosts[0].ports[0].service == "", "linux port without service: service should be empty") && all_ok;
}

static void test_port_without_service_has_empty_name_windows(bool& all_ok)
{
    const std::string xml = std::string(WINDOWS_NMAP_HEADER) +
        "<host>\n"
        "    <address addr=\"192.168.1.1\" addrtype=\"ipv4\"/>\n"
        "    <ports>\n"
        "        <port protocol=\"tcp\" portid=\"1234\">\n"
        "            <state state=\"closed\"/>\n"
        "        </port>\n"
        "    </ports>\n"
        "</host>\n" +
        LINUX_NMAP_FOOTER;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "windows port without service: ok should be true") && all_ok;
    if (!result.ok) return;
    all_ok = expect(result.snapshot.hosts.size() == 1, "windows port without service: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1) return;
    all_ok = expect(result.snapshot.hosts[0].ports.size() == 1, "windows port without service: should have 1 port") && all_ok;
    if (result.snapshot.hosts[0].ports.size() != 1) return;
    all_ok = expect(result.snapshot.hosts[0].ports[0].service == "", "windows port without service: service should be empty") && all_ok;
}

static void test_windows_stylesheet_pi_stripped(bool& all_ok)
{
    const char* xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE nmaprun>\n"
        "<?xml-stylesheet href=\"file:///C:/Program Files (x86)/Nmap/nmap.xsl\" type=\"text/xsl\"?>\n"
        "<nmaprun>\n"
        "<host>\n"
        "    <address addr=\"127.0.0.1\" addrtype=\"ipv4\"/>\n"
        "</host>\n"
        "</nmaprun>\n";

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "windows stylesheet pi: ok should be true") && all_ok;
    if (!result.ok) return;
    all_ok = expect(result.snapshot.hosts.size() == 1, "windows stylesheet pi: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1) return;
    all_ok = expect(result.snapshot.hosts[0].ip == "127.0.0.1", "windows stylesheet pi: ip should be 127.0.0.1") && all_ok;
}

static void test_fixture_linux_host_discovery(bool& all_ok)
{
    const std::string xml = read_nmap_fixture("linux_nmap_127001_host_discovery.xml");
    all_ok = expect(!xml.empty(), "linux host discovery fixture: file should be readable") && all_ok;
    if (xml.empty()) return;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "linux host discovery fixture: ok should be true") && all_ok;
    all_ok = expect(result.error.empty(), "linux host discovery fixture: error should be empty") && all_ok;
    all_ok = expect(result.snapshot.hosts.size() == 1u, "linux host discovery fixture: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1u) return;

    const HostSnapshot& host = result.snapshot.hosts[0];
    all_ok = expect(host.ip == "127.0.0.1", "linux host discovery fixture: ip should be 127.0.0.1") && all_ok;
    all_ok = expect(host.hostname == "localhost", "linux host discovery fixture: hostname should be localhost") && all_ok;
    all_ok = expect(host.ports.empty(), "linux host discovery fixture: ports should be empty") && all_ok;
}

static void test_fixture_windows_host_discovery(bool& all_ok)
{
    const std::string xml = read_nmap_fixture("windows_nmap_127001_host_discovery.xml");
    all_ok = expect(!xml.empty(), "windows host discovery fixture: file should be readable") && all_ok;
    if (xml.empty()) return;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "windows host discovery fixture: ok should be true") && all_ok;
    all_ok = expect(result.error.empty(), "windows host discovery fixture: error should be empty") && all_ok;
    all_ok = expect(result.snapshot.hosts.size() == 1u, "windows host discovery fixture: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1u) return;

    const HostSnapshot& host = result.snapshot.hosts[0];
    all_ok = expect(host.ip == "127.0.0.1", "windows host discovery fixture: ip should be 127.0.0.1") && all_ok;
    all_ok = expect(host.hostname == "localhost", "windows host discovery fixture: hostname should be localhost") && all_ok;
    all_ok = expect(host.ports.empty(), "windows host discovery fixture: ports should be empty") && all_ok;
}

static void test_fixture_linux_port_80(bool& all_ok)
{
    const std::string xml = read_nmap_fixture("linux_nmap_127001_port_80.xml");
    all_ok = expect(!xml.empty(), "linux port 80 fixture: file should be readable") && all_ok;
    if (xml.empty()) return;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "linux port 80 fixture: ok should be true") && all_ok;
    all_ok = expect(result.error.empty(), "linux port 80 fixture: error should be empty") && all_ok;
    all_ok = expect(result.snapshot.hosts.size() == 1u, "linux port 80 fixture: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1u) return;

    const HostSnapshot& host = result.snapshot.hosts[0];
    all_ok = expect(host.ip == "127.0.0.1", "linux port 80 fixture: ip should be 127.0.0.1") && all_ok;
    all_ok = expect(host.hostname == "localhost", "linux port 80 fixture: hostname should be localhost") && all_ok;
    all_ok = expect(host.ports.size() == 1u, "linux port 80 fixture: should have 1 port") && all_ok;
    if (host.ports.size() != 1u) return;

    const PortEntry& port = host.ports[0];
    all_ok = expect(port.protocol == "tcp", "linux port 80 fixture: protocol should be tcp") && all_ok;
    all_ok = expect(port.port == 80, "linux port 80 fixture: port should be 80") && all_ok;
    all_ok = expect(port.state == "closed", "linux port 80 fixture: state should be closed") && all_ok;
    all_ok = expect(port.service == "http", "linux port 80 fixture: service should be http") && all_ok;
}

static void test_fixture_windows_port_80(bool& all_ok)
{
    const std::string xml = read_nmap_fixture("windows_nmap_127001_port_80.xml");
    all_ok = expect(!xml.empty(), "windows port 80 fixture: file should be readable") && all_ok;
    if (xml.empty()) return;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "windows port 80 fixture: ok should be true") && all_ok;
    all_ok = expect(result.error.empty(), "windows port 80 fixture: error should be empty") && all_ok;
    all_ok = expect(result.snapshot.hosts.size() == 1u, "windows port 80 fixture: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1u) return;

    const HostSnapshot& host = result.snapshot.hosts[0];
    all_ok = expect(host.ip == "127.0.0.1", "windows port 80 fixture: ip should be 127.0.0.1") && all_ok;
    all_ok = expect(host.hostname == "localhost", "windows port 80 fixture: hostname should be localhost") && all_ok;
    all_ok = expect(host.ports.size() == 1u, "windows port 80 fixture: should have 1 port") && all_ok;
    if (host.ports.size() != 1u) return;

    const PortEntry& port = host.ports[0];
    all_ok = expect(port.protocol == "tcp", "windows port 80 fixture: protocol should be tcp") && all_ok;
    all_ok = expect(port.port == 80, "windows port 80 fixture: port should be 80") && all_ok;
    all_ok = expect(port.state == "closed", "windows port 80 fixture: state should be closed") && all_ok;
    all_ok = expect(port.service == "http", "windows port 80 fixture: service should be http") && all_ok;
}

static void test_fixture_linux_default_ports(bool& all_ok)
{
    const std::string xml = read_nmap_fixture("linux_nmap_127001_default_ports.xml");
    all_ok = expect(!xml.empty(), "linux default ports fixture: file should be readable") && all_ok;
    if (xml.empty()) return;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "linux default ports fixture: ok should be true") && all_ok;
    all_ok = expect(result.error.empty(), "linux default ports fixture: error should be empty") && all_ok;
    all_ok = expect(result.snapshot.hosts.size() == 1u, "linux default ports fixture: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1u) return;

    const HostSnapshot& host = result.snapshot.hosts[0];
    all_ok = expect(host.ip == "127.0.0.1", "linux default ports fixture: ip should be 127.0.0.1") && all_ok;
    all_ok = expect(host.hostname == "localhost", "linux default ports fixture: hostname should be localhost") && all_ok;
    // Parser ignores <extraports> and only collects explicit <port> elements.
    // This fixture has one open port (33354) and 999 closed ports in <extraports>.
    all_ok = expect(host.ports.size() == 1u, "linux default ports fixture: should have 1 explicit port") && all_ok;
    if (host.ports.size() != 1u) return;
    all_ok = expect(host.ports[0].protocol == "tcp", "linux default ports fixture: protocol should be tcp") && all_ok;
    all_ok = expect(host.ports[0].port == 33354, "linux default ports fixture: port should be 33354") && all_ok;
    all_ok = expect(host.ports[0].state == "open", "linux default ports fixture: state should be open") && all_ok;
    all_ok = expect(host.ports[0].service == "", "linux default ports fixture: service should be empty") && all_ok;
}

static void test_fixture_windows_default_ports(bool& all_ok)
{
    const std::string xml = read_nmap_fixture("windows_nmap_127001_default_ports.xml");
    all_ok = expect(!xml.empty(), "windows default ports fixture: file should be readable") && all_ok;
    if (xml.empty()) return;

    NmapXmlParseResult result = parse_nmap_xml(xml);
    all_ok = expect(result.ok, "windows default ports fixture: ok should be true") && all_ok;
    all_ok = expect(result.error.empty(), "windows default ports fixture: error should be empty") && all_ok;
    all_ok = expect(result.snapshot.hosts.size() == 1u, "windows default ports fixture: should have 1 host") && all_ok;
    if (result.snapshot.hosts.size() != 1u) return;

    const HostSnapshot& host = result.snapshot.hosts[0];
    all_ok = expect(host.ip == "127.0.0.1", "windows default ports fixture: ip should be 127.0.0.1") && all_ok;
    all_ok = expect(host.hostname == "localhost", "windows default ports fixture: hostname should be localhost") && all_ok;
    // 9 open ports explicitly listed; 991 closed ports are in <extraports> and ignored.
    all_ok = expect(host.ports.size() == 9u, "windows default ports fixture: should have 9 explicit ports") && all_ok;
    if (host.ports.size() != 9u) return;

    all_ok = expect(host.ports[0].port == 135,  "windows default ports: port[0] should be 135") && all_ok;
    all_ok = expect(host.ports[0].service == "msrpc", "windows default ports: service[0] should be msrpc") && all_ok;
    all_ok = expect(host.ports[1].port == 445,  "windows default ports: port[1] should be 445") && all_ok;
    all_ok = expect(host.ports[1].service == "microsoft-ds", "windows default ports: service[1] should be microsoft-ds") && all_ok;
    all_ok = expect(host.ports[2].port == 1042, "windows default ports: port[2] should be 1042") && all_ok;
    all_ok = expect(host.ports[2].service == "afrog", "windows default ports: service[2] should be afrog") && all_ok;
    all_ok = expect(host.ports[3].port == 1043, "windows default ports: port[3] should be 1043") && all_ok;
    all_ok = expect(host.ports[3].service == "boinc", "windows default ports: service[3] should be boinc") && all_ok;
    all_ok = expect(host.ports[4].port == 1455, "windows default ports: port[4] should be 1455") && all_ok;
    all_ok = expect(host.ports[4].service == "esl-lm", "windows default ports: service[4] should be esl-lm") && all_ok;
    all_ok = expect(host.ports[5].port == 2869, "windows default ports: port[5] should be 2869") && all_ok;
    all_ok = expect(host.ports[5].service == "icslap", "windows default ports: service[5] should be icslap") && all_ok;
    all_ok = expect(host.ports[6].port == 8080, "windows default ports: port[6] should be 8080") && all_ok;
    all_ok = expect(host.ports[6].service == "http-proxy", "windows default ports: service[6] should be http-proxy") && all_ok;
    all_ok = expect(host.ports[7].port == 8081, "windows default ports: port[7] should be 8081") && all_ok;
    all_ok = expect(host.ports[7].service == "blackice-icecap", "windows default ports: service[7] should be blackice-icecap") && all_ok;
    all_ok = expect(host.ports[8].port == 8082, "windows default ports: port[8] should be 8082") && all_ok;
    all_ok = expect(host.ports[8].service == "blackice-alerts", "windows default ports: service[8] should be blackice-alerts") && all_ok;

    for (const PortEntry& p : host.ports)
    {
        all_ok = expect(p.protocol == "tcp", "windows default ports: every port should be tcp") && all_ok;
        all_ok = expect(p.state == "open", "windows default ports: every port should be open") && all_ok;
    }
}

} // anonymous namespace

int main()
{
    bool all_ok = true;

    test_empty_xml_returns_error(all_ok);
    test_invalid_xml_returns_error(all_ok);
    test_valid_xml_no_hosts_linux(all_ok);
    test_valid_xml_no_hosts_windows(all_ok);
    test_single_host_single_port_linux(all_ok);
    test_single_host_single_port_windows(all_ok);
    test_ipv4_ipv6_chooses_ipv4_linux(all_ok);
    test_ipv4_ipv6_chooses_ipv4_windows(all_ok);
    test_host_without_hostname_linux(all_ok);
    test_host_without_hostname_windows(all_ok);
    test_invalid_port_id_skipped_linux(all_ok);
    test_invalid_port_id_skipped_windows(all_ok);
    test_host_without_address_skipped_linux(all_ok);
    test_host_without_address_skipped_windows(all_ok);
    test_port_without_service_has_empty_name_linux(all_ok);
    test_port_without_service_has_empty_name_windows(all_ok);
    test_windows_stylesheet_pi_stripped(all_ok);
    test_fixture_linux_host_discovery(all_ok);
    test_fixture_windows_host_discovery(all_ok);
    test_fixture_linux_port_80(all_ok);
    test_fixture_windows_port_80(all_ok);
    test_fixture_linux_default_ports(all_ok);
    test_fixture_windows_default_ports(all_ok);

    return finish_test("nmap_xml_parser_test", all_ok);
}
