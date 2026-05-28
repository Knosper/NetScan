#ifndef APP_WINDOWS_SINGLE_INSTANCE_HPP
#define APP_WINDOWS_SINGLE_INSTANCE_HPP

#include <string>

class WindowsSingleInstanceGuard
{
public:
    WindowsSingleInstanceGuard();
    ~WindowsSingleInstanceGuard();

    WindowsSingleInstanceGuard(const WindowsSingleInstanceGuard&) = delete;
    WindowsSingleInstanceGuard& operator=(const WindowsSingleInstanceGuard&) = delete;

    bool acquire_for_install_root(const std::string& install_root);
    bool acquired() const;

private:
    void* handle_;
};

std::string build_windows_single_instance_name(const std::string& install_root);
bool is_windows_single_instance_active(const std::string& install_root);

#endif
