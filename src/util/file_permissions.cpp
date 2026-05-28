#include "util/file_permissions.hpp"

#ifdef _WIN32

#include <windows.h>
#include <accctrl.h>
#include <aclapi.h>

namespace util
{
namespace
{
bool set_error(const std::string& message, std::string* error)
{
    if (error != nullptr)
        *error = message;
    return false;
}
}

bool restrict_file_to_owner(const std::string& path, std::string* error)
{
    PSECURITY_DESCRIPTOR security_descriptor = nullptr;
    PSID owner_sid = nullptr;
    DWORD result = GetNamedSecurityInfoA(path.c_str(),
                                         SE_FILE_OBJECT,
                                         OWNER_SECURITY_INFORMATION,
                                         &owner_sid,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         &security_descriptor);
    if (result != ERROR_SUCCESS)
        return set_error("failed to read file owner security info", error);

    EXPLICIT_ACCESSA entries[1];
    ZeroMemory(entries, sizeof(entries));

    entries[0].grfAccessPermissions = GENERIC_READ | GENERIC_WRITE | DELETE;
    entries[0].grfAccessMode = SET_ACCESS;
    entries[0].grfInheritance = NO_INHERITANCE;
    entries[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entries[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    entries[0].Trustee.ptstrName = static_cast<LPSTR>(owner_sid);

    PACL acl = nullptr;
    result = SetEntriesInAclA(1, entries, nullptr, &acl);
    if (result != ERROR_SUCCESS)
    {
        if (security_descriptor != nullptr)
            LocalFree(security_descriptor);
        return set_error("failed to build restricted ACL", error);
    }

    result = SetNamedSecurityInfoA(const_cast<char*>(path.c_str()),
                                   SE_FILE_OBJECT,
                                   DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                   nullptr,
                                   nullptr,
                                   acl,
                                   nullptr);

    if (acl != nullptr)
        LocalFree(acl);
    if (security_descriptor != nullptr)
        LocalFree(security_descriptor);

    if (result != ERROR_SUCCESS)
        return set_error("failed to apply restricted ACL", error);

    return true;
}
}

#else

#include <sys/stat.h>

namespace util
{
bool restrict_file_to_owner(const std::string& path, std::string* error)
{
    if (chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0)
    {
        if (error != nullptr)
            *error = "failed to chmod file to 0600";
        return false;
    }
    return true;
}
}

#endif
