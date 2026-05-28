#include "scan/process_renderer.hpp"

namespace
{
bool needs_log_quotes(const std::string& argument)
{
    if (argument.empty())
        return true;

    for (std::string::size_type i = 0; i < argument.size(); ++i)
    {
        const char c = argument[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"' || c == '\\')
            return true;
    }

    return false;
}

std::string quote_argument_for_log(const std::string& argument)
{
    if (!needs_log_quotes(argument))
        return argument;

    std::string quoted;
    quoted += '"';

    for (std::string::size_type i = 0; i < argument.size(); ++i)
    {
        const char c = argument[i];
        if (c == '"' || c == '\\')
            quoted += '\\';
        else if (c == '\n')
        {
            quoted += "\\n";
            continue;
        }
        else if (c == '\r')
        {
            quoted += "\\r";
            continue;
        }
        else if (c == '\t')
        {
            quoted += "\\t";
            continue;
        }

        quoted += c;
    }

    quoted += '"';
    return quoted;
}
} // namespace

std::string render_process_for_log(const ProcessSpec& process)
{
    std::string out = quote_argument_for_log(process.executable);

    for (std::vector<std::string>::const_iterator it = process.args.begin();
         it != process.args.end();
         ++it)
    {
        out += " ";
        out += quote_argument_for_log(*it);
    }

    return out;
}
