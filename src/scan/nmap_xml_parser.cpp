#include "scan/nmap_xml_parser.hpp"
#include "third_party/tinyxml2/tinyxml2.h"
#include <algorithm>
#include <string>

using namespace tinyxml2;

namespace lsm {
namespace scan {

namespace
{

std::string strip_xml_stylesheet_pi(const std::string& xml)
{
    const std::string tag = "<?xml-stylesheet";
    const std::string end = "?>";

    std::string result = xml;
    auto start_pos = result.find(tag);
    if (start_pos == std::string::npos)
        return result;

    auto end_pos = result.find(end, start_pos);
    if (end_pos == std::string::npos)
        return result;

    end_pos += end.size();
    if (end_pos < result.size() && result[end_pos] == '\n')
        ++end_pos;

    result.erase(start_pos, end_pos - start_pos);
    return result;
}

bool parse_port_number(const XMLElement* portNode, PortEntry& port)
{
    const char* portIdStr = portNode->Attribute("portid");
    if (!portIdStr)
        return false;

    try {
        const int portNum = std::stoi(portIdStr);
        if (portNum < 1 || portNum > 65535)
            return false;
        port.port = portNum;
        return true;
    } catch (...) {
        return false;
    }
}

void parse_port_protocol(const XMLElement* portNode, PortEntry& port)
{
    const char* protocol = portNode->Attribute("protocol");
    if (protocol)
        port.protocol = protocol;
}

void parse_port_state(const XMLElement* portNode, PortEntry& port)
{
    const XMLElement* stateNode = portNode->FirstChildElement("state");
    if (!stateNode)
        return;

    const char* state = stateNode->Attribute("state");
    if (state)
        port.state = state;
}

void parse_port_service(const XMLElement* portNode, PortEntry& port)
{
    const XMLElement* serviceNode = portNode->FirstChildElement("service");
    if (!serviceNode)
        return;

    const char* service = serviceNode->Attribute("name");
    if (service)
        port.service = service;
}

bool parse_port_entry(const XMLElement* portNode, PortEntry& port)
{
    parse_port_protocol(portNode, port);
    if (!parse_port_number(portNode, port))
        return false;
    parse_port_state(portNode, port);
    parse_port_service(portNode, port);
    return true;
}

void parse_host_addresses(const XMLElement* hostNode, HostSnapshot& host)
{
    std::string ipv4Addr;
    std::string ipv6Addr;

    for (const XMLElement* addrNode = hostNode->FirstChildElement("address");
         addrNode != nullptr;
         addrNode = addrNode->NextSiblingElement("address"))
    {
        const char* addrType = addrNode->Attribute("addrtype");
        const char* addr = addrNode->Attribute("addr");

        if (!addrType || !addr) continue;

        if (std::strcmp(addrType, "ipv4") == 0) {
            ipv4Addr = addr;
        } else if (std::strcmp(addrType, "ipv6") == 0) {
            ipv6Addr = addr;
        }
    }

    if (!ipv4Addr.empty()) {
        host.ip = ipv4Addr;
    } else if (!ipv6Addr.empty()) {
        host.ip = ipv6Addr;
    }
}

void parse_host_hostname(const XMLElement* hostNode, HostSnapshot& host)
{
    const XMLElement* hostnamesNode = hostNode->FirstChildElement("hostnames");
    if (hostnamesNode) {
        const XMLElement* hostnameNode = hostnamesNode->FirstChildElement("hostname");
        if (hostnameNode) {
            const char* hostname = hostnameNode->Attribute("name");
            if (hostname) {
                host.hostname = hostname;
            }
        }
    }
}

void parse_host_ports(const XMLElement* hostNode, HostSnapshot& host)
{
    const XMLElement* portsNode = hostNode->FirstChildElement("ports");
    if (!portsNode) return;

    for (const XMLElement* portNode = portsNode->FirstChildElement("port");
         portNode != nullptr;
         portNode = portNode->NextSiblingElement("port"))
    {
        PortEntry port;
        if (parse_port_entry(portNode, port))
            host.ports.push_back(port);
    }
}

HostSnapshot parse_single_host(const XMLElement* hostNode)
{
    HostSnapshot host;

    parse_host_addresses(hostNode, host);
    
    if (host.ip.empty()) {
        return host;
    }

    parse_host_hostname(hostNode, host);
    parse_host_ports(hostNode, host);

    return host;
}

} // anonymous namespace

NmapXmlParseResult parse_nmap_xml(const std::string& xml)
{
    NmapXmlParseResult result;
    result.ok = false;

    if (xml.empty()) {
        result.error = "Empty XML input";
        return result;
    }

    const std::string sanitized = strip_xml_stylesheet_pi(xml);

    XMLDocument doc;
    XMLError parseError = doc.Parse(sanitized.c_str(), sanitized.size());

    if (parseError != XML_SUCCESS) {
        result.error = std::string("XML parse error: ") + doc.ErrorStr();
        return result;
    }

    const XMLElement* root = doc.FirstChildElement("nmaprun");
    if (!root) {
        result.ok = true;
        return result;
    }

    result.ok = true;

    for (const XMLElement* hostNode = root->FirstChildElement("host");
         hostNode != nullptr;
         hostNode = hostNode->NextSiblingElement("host"))
    {
        HostSnapshot host = parse_single_host(hostNode);
        
        if (!host.ip.empty()) {
            result.snapshot.hosts.push_back(host);
        }
    }

    return result;
}

} // namespace scan
} // namespace lsm
