#include "scan/process_renderer.hpp"

#include <cstdlib>
#include <iostream>
#include "test_output.hpp"

int main()
{
    bool all_ok = true;

    {
        ProcessSpec process;
        process.executable = "nmap";
        process.args.push_back("-sn");
        process.args.push_back("--");
        process.args.push_back("127.0.0.1");

        if (render_process_for_log(process) != "nmap -sn -- 127.0.0.1")
        {
            std::cerr << "FAIL: renderer should keep simple arguments unquoted\n";
            all_ok = false;
        }
    }

    {
        ProcessSpec process;
        process.executable = "/tmp/nmap binary";
        process.args.push_back("scan target");
        process.args.push_back("say\"hi");
        process.args.push_back("C:\\Program Files\\nmap");
        process.args.push_back("");

        if (render_process_for_log(process) !=
            "\"/tmp/nmap binary\" \"scan target\" \"say\\\"hi\" "
            "\"C:\\\\Program Files\\\\nmap\" \"\"")
        {
            std::cerr << "FAIL: renderer should quote and escape ambiguous arguments\n";
            all_ok = false;
        }
    }

    return finish_test("process_renderer_test", all_ok);
}
