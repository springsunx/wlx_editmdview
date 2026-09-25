#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

namespace {

std::filesystem::path replacement_path(const std::filesystem::path& target, unsigned int attempt) {
    return target.wstring() + L".editmdview.elevated." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(attempt) + L".tmp";
}

DWORD replace_from_staging(const std::filesystem::path& source,
    const std::filesystem::path& target) {
    if (source.empty() || target.empty() || source == target) return ERROR_INVALID_PARAMETER;

    std::filesystem::path replacement;
    DWORD failure = ERROR_FILE_EXISTS;
    bool copied = false;
    for (unsigned int attempt = 0; attempt < 32; ++attempt) {
        replacement = replacement_path(target, attempt);
        if (CopyFileW(source.c_str(), replacement.c_str(), TRUE)) {
            copied = true;
            failure = ERROR_SUCCESS;
            break;
        }
        failure = GetLastError();
        if (failure != ERROR_FILE_EXISTS && failure != ERROR_ALREADY_EXISTS) return failure;
    }
    if (!copied) return failure;

    SetFileAttributesW(replacement.c_str(), FILE_ATTRIBUTE_NORMAL);
    HANDLE file = CreateFileW(replacement.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        failure = GetLastError();
        DeleteFileW(replacement.c_str());
        return failure;
    }
    if (!FlushFileBuffers(file)) failure = GetLastError();
    CloseHandle(file);
    if (failure != ERROR_SUCCESS) {
        DeleteFileW(replacement.c_str());
        return failure;
    }

    if (!ReplaceFileW(target.c_str(), replacement.c_str(), nullptr,
            REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
        if (!MoveFileExW(replacement.c_str(), target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            failure = GetLastError();
            DeleteFileW(replacement.c_str());
            return failure;
        }
    }
    return ERROR_SUCCESS;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argumentCount = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments) return static_cast<int>(GetLastError());

    DWORD result = ERROR_INVALID_PARAMETER;
    if (argumentCount == 4 && wcscmp(arguments[1], L"--replace") == 0) {
        result = replace_from_staging(arguments[2], arguments[3]);
    }
    LocalFree(arguments);
    return static_cast<int>(result);
}
