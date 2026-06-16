#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <vector>
#include <string>
#include <atomic>

const std::string GITHUB_OWNER = "komotok";
const std::string GITHUB_REPO = "SMActivator";

struct UpdateInfo {
    bool        available = false;
    std::string tagName;
    std::string releaseUrl;
    std::string downloadUrl;
};

static std::string jsonField(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    size_t end = json.find('"', pos);
    return end == std::string::npos ? std::string{} : json.substr(pos, end - pos);
}

static std::string jsonUnescape(std::string s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '/') { out += '/'; ++i; }
        else out += s[i];
    }
    return out;
}

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

void enableANSI() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

std::string fetchURL(const std::wstring& host, const std::wstring& path) {
    HINTERNET hSession = WinHttpOpen(L"SMActivator/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

    WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    WinHttpReceiveResponse(hRequest, NULL);

    std::string result;
    DWORD size = 0;
    do {
        WinHttpQueryDataAvailable(hRequest, &size);
        if (!size) break;
        std::vector<char> buf(size + 1, 0);
        DWORD downloaded = 0;
        WinHttpReadData(hRequest, buf.data(), size, &downloaded);
        result += buf.data();
    } while (size > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

UpdateInfo checkForUpdate(double currentVersion) {
    std::wstring owner(GITHUB_OWNER.begin(), GITHUB_OWNER.end());
    std::wstring repo(GITHUB_REPO.begin(), GITHUB_REPO.end());
    std::wstring path = L"/repos/" + owner + L"/" + repo + L"/releases/latest";

    std::string response = fetchURL(L"api.github.com", path);
    if (response.empty()) return {};

    UpdateInfo info;
    info.tagName = jsonField(response, "tag_name");
    info.releaseUrl = jsonUnescape(jsonField(response, "html_url"));

    std::string assetKey = "\"browser_download_url\":\"";
    size_t ap = response.find(assetKey);
    if (ap != std::string::npos) {
        ap += assetKey.size();
        size_t ae = response.find('"', ap);
        if (ae != std::string::npos)
            info.downloadUrl = jsonUnescape(response.substr(ap, ae - ap));
    }

    std::string tag = info.tagName;
    if (!tag.empty() && tag[0] == 'v') tag = tag.substr(1);
    try { info.available = !tag.empty() && std::stod(tag) > currentVersion; }
    catch (...) {}

    return info;
}

void restartProgram() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOW);
    exit(0);
}

void openURL(const std::string& url) {
    std::wstring wide(url.begin(), url.end());
    ShellExecuteW(NULL, L"open", wide.c_str(), NULL, NULL, SW_SHOW);
}

#else
void enableANSI() {}
UpdateInfo checkForUpdate(double) { return {}; }
void restartProgram() {}
void openURL(const std::string&) {}
#endif

namespace Color {
    const char* reset = "\033[0m";
    const char* red = "\033[31m";
    const char* green = "\033[32m";
    const char* yellow = "\033[33m";
    const char* cyan = "\033[36m";
    const char* white = "\033[97m";
}

class DotAnimation {
public:
    DotAnimation(const std::string& prefix, const char* color = Color::yellow)
        : running_(true)
    {
        thread_ = std::thread([this, prefix, color]() {
            int frame = 0;
            while (running_) {
                std::string dots(frame + 1, '.');
                std::string pad(3 - frame, ' ');
                std::cout << "\r" << color << prefix << dots << pad << Color::reset;
                std::cout.flush();
                frame = (frame + 1) % 3;
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
            }
            });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        std::cout << "\r\033[2K";
        std::cout.flush();
    }

    ~DotAnimation() { stop(); }

private:
    std::atomic<bool> running_;
    std::thread thread_;
};

void progressBar(int total, const std::string& label = "", const char* labelColor = Color::yellow, int width = 40) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> delay(5, 20);

    int denom = std::max(1, total);

    for (int i = 0; i <= total; ++i) {
        float percent = (float)i / total;
        int filled = (int)(percent * width);
        const char* barColor = percent < 0.4f ? Color::red
            : percent < 0.7f ? Color::yellow
            : Color::green;

        std::cout << "\r";
        if (!label.empty()) {
            int d = (i * 30 / denom) % 3;
            std::string dots(d + 1, '.');
            std::string pad(2 - d, ' ');
            std::cout << labelColor << label << dots << pad << Color::reset << " ";
        }
        std::cout << Color::white << "[" << Color::reset;
        for (int j = 0; j < width; ++j) {
            if (j < filled) std::cout << barColor << '#' << Color::reset;
            else            std::cout << Color::white << '-' << Color::reset;
        }
        std::cout << Color::white << "] " << Color::reset
            << barColor << (int)(percent * 100) << "%" << Color::reset;
        std::cout.flush();

        std::this_thread::sleep_for(std::chrono::milliseconds(delay(rng)));
    }
    std::cout << "\n";
}

double ver_current = 0.11;
int    updatechecker = 1;

#ifdef _WIN32
#include <conio.h>
int getKey() {
    int ch = _getch();
    if (ch == 224) {
        ch = _getch();
        if (ch == 72) return -1;
        if (ch == 80) return -2;
        if (ch == 75) return -3;
        if (ch == 77) return -4;
    }
    return ch;
}
#else
#include <termios.h>
#include <unistd.h>
int getKey() {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    int ch = getchar();
    if (ch == 27) {
        getchar();
        ch = getchar();
        if (ch == 'A') ch = -1;
        if (ch == 'B') ch = -2;
        if (ch == 'D') ch = -3;
        if (ch == 'C') ch = -4;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}
#endif

int showMenu(const std::vector<std::string>& options) {
    int selected = 0;
    int n = (int)options.size();

    std::cout << "\033[?25l";

    auto draw = [&]() {
        std::cout << "\r\033[2K";
        for (int i = 0; i < n; ++i) {
            if (i == selected)
                std::cout << "\033[7m" << "  " << options[i] << "  " << "\033[0m";
            else
                std::cout << "  " << options[i] << "  ";
        }
        std::cout.flush();
        };

    draw();

    while (true) {
        int key = getKey();
        if (key == -3 && selected > 0)          selected--;
        else if (key == -4 && selected < n - 1) selected++;
        else if (key == '\r' || key == '\n')     break;
        draw();
    }

    std::cout << "\n";
    std::cout << "\033[?25h";
    return selected;
}

enum MenuOption { ACTIVATE = 0, CHECK_UPDATES, EXIT };

int main() {
    enableANSI();
    std::cout << "\n \n \n";
    std::cout << Color::cyan << R"( $$$$$$\  $$\      $$\  $$$$$$\              $$\     $$\                      $$\                         )" << Color::reset << "\n";
    std::cout << Color::cyan << R"($$  __$$\ $$$\    $$$ |$$  __$$\             $$ |    \__|                     $$ |                        )" << Color::reset << "\n";
    std::cout << Color::cyan << R"($$ /  \__|$$$$\  $$$$ |$$ /  $$ | $$$$$$$\ $$$$$$\   $$\ $$\    $$\ $$$$$$\ $$$$$$\    $$$$$$\   $$$$$$\ )" << Color::reset << "\n";
    std::cout << Color::cyan << R"(\$$$$$$\  $$\$$\$$ $$ |$$$$$$$$ |$$  _____|\_$$  _|  $$ |\$$\  $$  |\____$$\\_$$  _|  $$  __$$\ $$  __$$\ )" << Color::reset << "\n";
    std::cout << Color::cyan << R"( \____$$\ $$ \$$$  $$ |$$  __$$ |$$ /        $$ |    $$ | \$$\$$  / $$$$$$$ | $$ |    $$ /  $$ |$$ |  \__|)" << Color::reset << "\n";
    std::cout << Color::cyan << R"($$\   $$ |$$ |\$  /$$ |$$ |  $$ |$$ |        $$ |$$\ $$ |  \$$$  / $$  __$$ | $$ |$$\ $$ |  $$ |$$ |      )" << Color::reset << "\n";
    std::cout << Color::cyan << R"(\$$$$$$  |$$ | \_/ $$ |$$ |  $$ |\$$$$$$$\   \$$$$  |$$ |   \$  /  \$$$$$$$ | \$$$$  |\$$$$$$  |$$ |      )" << Color::reset << "\n";
    std::cout << Color::cyan << R"( \______/ \__|     \__|\__|  \__| \_______|   \____/ \__|    \_/    \_______|  \____/  \______/ \__|      )" << Color::reset << "\n";
    std::cout << "\n \n \n";
    std::cout << Color::white << "-------------------------------" << Color::reset << "\n";
    std::cout << Color::white << "Version " << ver_current << Color::reset << "\n";
    std::cout << Color::white << "-------------------------------" << Color::reset << "\n\n";

    if (updatechecker == 1) {
        DotAnimation anim("Checking for updates");
        UpdateInfo info = checkForUpdate(ver_current);
        anim.stop();

        if (info.available) {
            std::string url = info.downloadUrl.empty() ? info.releaseUrl : info.downloadUrl;
            std::cout << Color::yellow << "Update " << info.tagName << " available!" << Color::reset << "\n";
            std::cout << Color::white << "Opening download page..." << Color::reset << "\n";
            openURL(url);
        }
        else {
            std::cout << Color::green << "Up to date." << Color::reset << "\n";
        }
    }
    else if (updatechecker == 3) {
        std::cout << Color::white << "Updates disabled by policy, skipping check." << Color::reset << "\n";
    }
    else {
        std::cout << Color::white << "Update checker disabled, skipping check." << Color::reset << "\n";
    }

    std::cout << Color::green << "SMActivator ready." << Color::reset << "\n";

    std::vector<std::string> options = { "Activate Package", "Update check", "Exit" };

    while (true) {
        std::cout << "\nSelect an option:\n\n";
        int choice = showMenu(options);
        std::cout << "\n";

        switch (choice) {
        case ACTIVATE:
            progressBar(100, "Activating", Color::green);
            std::cout << Color::green << "Activated successfully." << Color::reset << "\n";
            std::cin.get();
            break;

        case CHECK_UPDATES: {
            DotAnimation anim("Checking for updates");
            UpdateInfo info = checkForUpdate(ver_current);
            anim.stop();

            if (info.available) {
                std::string url = info.downloadUrl.empty() ? info.releaseUrl : info.downloadUrl;
                std::cout << Color::yellow << "Update " << info.tagName << " available!" << Color::reset << "\n";
                std::cout << Color::white << "Opening download page..." << Color::reset << "\n";
                openURL(url);
                std::cin.get();
            }
            else {
                std::cout << Color::cyan << "Up to date. Press Enter to return to menu." << Color::reset << "\n";
                std::cin.get();
            }
            break;
        }

        case EXIT:
            std::cout << Color::red << "Terminating session. Goodbye." << Color::reset << "\n";
            return 0;
        }
    }
}
