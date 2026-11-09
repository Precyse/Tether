// Warriors.exe: starts the built game engine (engine\ps2run.exe) with the settings in warriors.ini, writes its output to logs\warriors.log
// and shows a message if it stops with an error. Everything after the exe name on the command line is passed on to the engine.
// Built by warriors-setup on the player's PC; part of our MIT-licensed tooling.
#include <windows.h>

#include <string>

namespace {

constexpr wchar_t kIniName[] = L"warriors.ini";
constexpr DWORD kSettingMax = 2048;

std::wstring directory_of_exe() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring text = path;
    return text.substr(0, text.find_last_of(L'\\'));
}

std::wstring setting(const std::wstring &ini, const wchar_t *section, const wchar_t *key) {
    wchar_t value[kSettingMax];
    GetPrivateProfileStringW(section, key, L"", value, kSettingMax, ini.c_str());
    return value;
}

std::wstring quoted(const std::wstring &text) { return L"\"" + text + L"\""; }

std::wstring rest_of_command_line() {
    std::wstring line = GetCommandLineW();
    size_t at = 0;
    if (line[0] == L'"') {
        at = line.find(L'"', 1);
        at = at == std::wstring::npos ? line.size() : at + 1;
    } else {
        at = line.find(L' ');
        at = at == std::wstring::npos ? line.size() : at;
    }
    return line.substr(at);
}

void fail(const std::wstring &text) {
    MessageBoxW(nullptr, text.c_str(), L"The Warriors", MB_OK | MB_ICONERROR);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring home = directory_of_exe(), ini = home + L"\\" + kIniName;
    const std::wstring disc = setting(ini, L"game", L"disc"), elf = setting(ini, L"game", L"elf"), engine = setting(ini, L"game", L"engine");
    if (disc.empty() || elf.empty() || engine.empty()) {
        fail(L"warriors.ini is missing or incomplete. Run warriors-setup again.");
        return 2;
    }
    if (GetFileAttributesW(disc.c_str()) == INVALID_FILE_ATTRIBUTES) {
        fail(L"The game disc image was not found:\n" + disc + L"\n\nPut it back, or run warriors-setup with the new path.");
        return 2;
    }
    const std::wstring window = setting(ini, L"window", L"fullscreen") == L"1" ? L"--fullscreen" : L"--window";
    std::wstring command = quoted(home + L"\\" + engine) + L" " + quoted(elf) + L" --disc " + quoted(disc) + L" " + window;
    for (const auto &[key, flag] : {std::pair<const wchar_t *, const wchar_t *>{L"memcard", L" --memcard "}, {L"patches", L" --patches "}}) {
        const std::wstring value = setting(ini, L"game", key);
        if (!value.empty()) command += flag + quoted(value);
    }
    command += L" " + setting(ini, L"args", L"extra") + rest_of_command_line();

    CreateDirectoryW((home + L"\\logs").c_str(), nullptr);
    const std::wstring log_path = home + L"\\logs\\warriors.log";
    MoveFileExW(log_path.c_str(), (home + L"\\logs\\warriors.previous.log").c_str(), MOVEFILE_REPLACE_EXISTING);
    SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
    HANDLE log = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = log;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    std::wstring mutable_command = command;
    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, home.c_str(), &startup, &process)) {
        fail(L"The game engine could not be started:\n" + home + L"\\" + engine + L"\n\nRun warriors-setup again.");
        return 3;
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    AssignProcessToJobObject(job, process.hProcess);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    if (code != 0) fail(L"The game stopped with an error (code " + std::to_wstring(code) + L").\nThe log is at:\n" + log_path);
    return static_cast<int>(code);
}
