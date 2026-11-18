// =====================================================================
//  PANDORA - консольная программа в стиле "Secure Device Manager"
//
//  Сборка (MinGW / g++):   g++ main.cpp -o main.exe -static -lsetupapi -liphlpapi
//  Сборка (Visual Studio): cl /EHsc main.cpp   (или просто Build в IDE)
//
//  Все спецсимволы (блоки, рамки) записаны байтами UTF-8 (\xE2\x96\x88 ...),
//  поэтому не важно, в какой кодировке сохранён этот файл.
// =====================================================================

#include <windows.h>
#include <setupapi.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <iomanip>
#include <sstream>
#include <conio.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

// ---------------------------------------------------------------------
//  Символы (UTF-8 байтами)
// ---------------------------------------------------------------------
static const char* BLOCK = "\xE2\x96\x88";  // █
static const char* SHADE = "\xE2\x96\x91";  // ░
static const char* DOT   = "\xE2\x97\x8F";  // ●

static const char* DH  = "\xE2\x95\x90";    // ═
static const char* DV  = "\xE2\x95\x91";    // ║
static const char* DTL = "\xE2\x95\x94";    // ╔
static const char* DTR = "\xE2\x95\x97";    // ╗
static const char* DBL = "\xE2\x95\x9A";    // ╚
static const char* DBR = "\xE2\x95\x9D";    // ╝
static const char* DML = "\xE2\x95\xA0";    // ╠
static const char* DMR = "\xE2\x95\xA3";    // ╣
static const char* LH  = "\xE2\x94\x80";    // ─

// ---------------------------------------------------------------------
//  Глобальное состояние
// ---------------------------------------------------------------------
static bool        g_vt = false;      // поддерживает ли консоль цвета (ANSI)
static std::string g_host;            // имя компьютера
static std::string g_session;         // номер сессии вида PAN-XXXXXXXX
static UINT        g_oldCP = 0;       // старая кодовая страница консоли

// ---------------------------------------------------------------------
//  Мелкие помощники
// ---------------------------------------------------------------------
static std::string fg(int n)  { return g_vt ? std::string("\x1b[38;5;") + std::to_string(n) + "m" : std::string(); }
static std::string rst()      { return g_vt ? "\x1b[0m" : ""; }
static std::string bld()      { return g_vt ? "\x1b[1m" : ""; }

static std::string rep(const char* s, int n)
{
    std::string r;
    for (int i = 0; i < n; ++i) r += s;
    return r;
}

static std::string pad(std::string s, size_t width)
{
    if (s.size() < width) s.append(width - s.size(), ' ');
    return s;
}

static void clearConsole()
{
    if (g_vt) std::cout << "\x1b[2J\x1b[3J\x1b[H" << std::flush;
    else      std::system("cls");
}

static void typeOut(const std::string& text, int delayMs)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        std::cout << text[i];
        Sleep(delayMs);
    }
}

// Читает строку; если ввод закончился (Ctrl+Z / закрытие) - выходит.
static std::string prompt(const std::string& label)
{
    std::cout << fg(51) << label << rst() << fg(255);
    std::string s;
    if (!std::getline(std::cin, s))
    {
        std::cout << rst() << "\n";
        std::exit(0);
    }
    std::cout << rst();
    return s;
}

// Ввод пароля со звёздочками.
static std::string readPassword(const std::string& label)
{
    std::cout << fg(51) << label << rst() << fg(255);
    std::string s;
    int ch;
    while ((ch = _getch()) != '\r' && ch != '\n' && ch != EOF)
    {
        if (ch == 8 || ch == 127)                    // Backspace
        {
            if (!s.empty()) { s.pop_back(); std::cout << "\b \b"; }
        }
        else if (ch == 0 || ch == 224)               // стрелки/F-клавиши - пропускаем
        {
            _getch();
        }
        else if (ch >= 32 && ch < 127)
        {
            s.push_back((char)ch);
            std::cout << '*';
        }
    }
    std::cout << rst() << "\n";
    return s;
}

static void pressAnyKey(const char* text = "Press any key to continue . . .")
{
    std::cout << "\n  " << fg(244) << text << rst();
    _getch();
    std::cout << "\n";
}

// ---------------------------------------------------------------------
//  Баннер PANDORA
//  В исходнике он нарисован обычными символами, при выводе превращается
//  в блоки и рамки:   # = █   = = ═   | = ║   r = ╔   7 = ╗   L = ╚   J = ╝
// ---------------------------------------------------------------------
static const char* LETTERS[7][6] =
{
    /* P */ { "######7 ", "##r==##7", "######rJ", "##r===J ", "##|     ", "L=J     " },
    /* A */ { " #####7 ", "##r==##7", "#######|", "##r==##|", "##|  ##|", "L=J  L=J" },
    /* N */ { "###7   ##7", "####7  ##|", "##r##7 ##|", "##|L##7##|", "##| L####|", "L=J  L===J" },
    /* D */ { "######7 ", "##r==##7", "##|  ##|", "##|  ##|", "######rJ", "L=====J " },
    /* O */ { " ######7 ", "##r===##7", "##|   ##|", "##|   ##|", "L######rJ", " L=====J " },
    /* R */ { "######7 ", "##r==##7", "######rJ", "##r==##7", "##|  ##|", "L=J  L=J" },
    /* A */ { " #####7 ", "##r==##7", "#######|", "##r==##|", "##|  ##|", "L=J  L=J" }
};

static const char* glyph(char c)
{
    switch (c)
    {
        case '#': return BLOCK;
        case '=': return DH;
        case '|': return DV;
        case 'r': return DTL;
        case '7': return DTR;
        case 'L': return DBL;
        case 'J': return DBR;
        default:  return " ";
    }
}

static void printBanner(bool animate)
{
    static const int GRADIENT[6] = { 213, 177, 141, 105, 69, 39 };  // розовый -> голубой
    static const int SHADOW = 60;                                    // цвет "тени" букв

    for (int row = 0; row < 6; ++row)
    {
        std::string line = "  ";
        int cur = -1;
        for (int l = 0; l < 7; ++l)
        {
            for (const char* p = LETTERS[l][row]; *p; ++p)
            {
                if (*p != ' ')
                {
                    int color = (*p == '#') ? GRADIENT[row] : SHADOW;
                    if (color != cur) { line += fg(color); cur = color; }
                }
                line += glyph(*p);
            }
        }
        line += rst();
        std::cout << line << "\n";
        if (animate) Sleep(80);
    }
}

static void printTagline()
{
    std::cout << "\n  " << fg(244) << "SECURE DEVICE MANAGER"
              << fg(240) << "  |  "
              << fg(244) << "v2.4.1" << rst() << "\n";
    std::cout << "  " << fg(238) << rep(LH, 59) << rst() << "\n\n";
}

static void drawHeader()
{
    clearConsole();
    printBanner(false);
    printTagline();
}

static void sectionTitle(const std::string& title)
{
    std::cout << "  " << bld() << fg(51) << title << rst() << "\n\n";
}

// ---------------------------------------------------------------------
//  Анимации
// ---------------------------------------------------------------------
static void bootStep(const std::string& text, int ms)
{
    std::string line = text + " ";
    while (line.size() < 40) line += '.';

    std::cout << "  " << fg(99) << ">" << rst() << " " << fg(252) << line << rst() << " ";
    Sleep(ms);
    std::cout << fg(240) << "[ " << fg(46) << "OK" << fg(240) << " ]" << rst() << "\n";
}

static void progressBar(const std::string& label, int totalMs)
{
    const int W = 30;
    const int steps = 50;
    for (int i = 0; i <= steps; ++i)
    {
        int percent = i * 100 / steps;
        int filled  = i * W / steps;
        char num[16];
        std::snprintf(num, sizeof(num), "%3d%%", percent);

        std::cout << "\r  " << fg(252) << label << rst() << "  "
                  << fg(45)  << rep(BLOCK, filled)
                  << fg(238) << rep(SHADE, W - filled) << rst()
                  << " " << fg(255) << num << rst();
        Sleep(totalMs / steps);
    }
    std::cout << "\n";
}

// ---------------------------------------------------------------------
//  Системная информация
// ---------------------------------------------------------------------
static void printSystemInfo()
{
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);

    SYSTEM_INFO si;
    GetSystemInfo(&si);

    std::string ram = std::to_string((unsigned long long)(mem.ullTotalPhys / (1024ULL * 1024ULL))) + " MB";
    std::string cpu = std::to_string((unsigned long)si.dwNumberOfProcessors) + " logical cores";

    struct Row { const char* k; std::string v; };
    Row rows[] = { { "Host   ", g_host }, { "CPU    ", cpu }, { "RAM    ", ram }, { "Session", g_session } };

    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        std::cout << "  " << fg(240) << rows[i].k << " : " << fg(255) << rows[i].v << rst() << "\n";
    std::cout << "\n";
}

// ---------------------------------------------------------------------
//  Список устройств в рамке
// ---------------------------------------------------------------------
struct Device
{
    std::string name;
    std::string type;
};

static void printDevices(const std::vector<Device>& devices)
{
    const int NAME_W = 24;                    // ширина колонки с именем
    const int TYPE_W = 12;                    // ширина колонки с типом
    const int VISIBLE = 2 + 3 + 2 + NAME_W + TYPE_W + 8;   // "  [n]  имя тип ● online"
    const int INNER = VISIBLE + 2;            // внутренняя ширина рамки
    const int B = 99;                         // цвет рамки

    std::cout << "  " << fg(B) << DTL << rep(DH, INNER) << DTR << rst() << "\n";

    std::string title = "  CONNECTED DEVICES";
    std::string count = std::to_string(devices.size()) + " found  ";
    std::cout << "  " << fg(B) << DV << rst()
              << bld() << fg(255) << title << rst()
              << std::string(INNER - title.size() - count.size(), ' ')
              << fg(244) << count << rst()
              << fg(B) << DV << rst() << "\n";

    std::cout << "  " << fg(B) << DML << rep(DH, INNER) << DMR << rst() << "\n";

    for (size_t i = 0; i < devices.size(); ++i)
    {
        std::cout << "  " << fg(B) << DV << rst()
                  << "  " << fg(51) << "[" << (i + 1) << "]" << rst()
                  << "  " << fg(255) << pad(devices[i].name, NAME_W) << rst()
                  << fg(244) << pad(devices[i].type, TYPE_W) << rst()
                  << fg(46) << DOT << " online" << rst()
                  << std::string(INNER - VISIBLE, ' ')
                  << fg(B) << DV << rst() << "\n";
    }

    std::cout << "  " << fg(B) << DBL << rep(DH, INNER) << DBR << rst() << "\n";
}

// ---------------------------------------------------------------------
//  main
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
// Журнал событий: реальные действия программы + явно помеченные демо-события
// ---------------------------------------------------------------------
static std::vector<std::string> g_logs;

static void logEvent(const std::string& text)
{
    SYSTEMTIME t{};
    GetLocalTime(&t);
    char stamp[24];
    std::snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u", t.wHour, t.wMinute, t.wSecond);
    g_logs.push_back(std::string("[") + stamp + "] " + text);
    if (g_logs.size() > 80) g_logs.erase(g_logs.begin());
}

static std::string wideToUtf8(const wchar_t* value)
{
    if (!value || !*value) return "";
    int bytes = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return "";
    std::string out((size_t)bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, &out[0], bytes, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

static void scanComponents()
{
    drawHeader();
    sectionTitle("COMPONENT DISCOVERY / WINDOWS DEVICE ENUMERATION");
    logEvent("Started local Plug-and-Play component scan");
    std::cout << "  Scanning devices currently reported by Windows...\n\n";

    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr,
                                        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE)
    {
        std::cout << "  " << fg(196) << "Device enumeration failed. Win32 error: " << GetLastError() << rst() << "\n";
        logEvent("Component scan failed: SetupAPI error");
        pressAnyKey();
        return;
    }

    int shown = 0;
    for (DWORD i = 0; i < 512; ++i)
    {
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInfo(set, i, &info))
        {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
                logEvent("Device enumeration ended with a Windows API warning");
            break;
        }
        wchar_t name[512]{};
        DWORD type = 0, needed = 0;
        BOOL ok = SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_FRIENDLYNAME,
                    &type, reinterpret_cast<PBYTE>(name), sizeof(name), &needed);
        if (!ok)
            ok = SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_DEVICEDESC,
                    &type, reinterpret_cast<PBYTE>(name), sizeof(name), &needed);
        if (!ok || !name[0]) wcscpy_s(name, L"Unnamed Windows device");

        std::cout << "  " << fg(51) << "[" << std::setw(3) << std::setfill('0') << (shown + 1)
                  << "] " << rst() << fg(255) << wideToUtf8(name) << rst() << "\n";
        ++shown;
    }
    SetupDiDestroyDeviceInfoList(set);
    std::cout << "\n  " << fg(46) << "Enumeration complete: " << shown << " present device entries." << rst() << "\n";
    logEvent("Component scan complete: " + std::to_string(shown) + " present device entries");
    pressAnyKey();
}

static void networkMonitor()
{
    drawHeader();
    sectionTitle("NETWORK MONITOR / LOCAL ADAPTER STATUS");
    logEvent("Opened local network adapter monitor");

    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer(size);
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr,
                reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &size);
    if (result == ERROR_BUFFER_OVERFLOW)
    {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr,
                reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &size);
    }
    if (result != NO_ERROR)
    {
        std::cout << "  Adapter query failed. Windows error: " << result << "\n";
        logEvent("Network monitor could not query adapter information");
        pressAnyKey();
        return;
    }

    int count = 0;
    for (PIP_ADAPTER_ADDRESSES a = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()); a; a = a->Next)
    {
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        ++count;
        std::cout << "  " << fg(51) << "ADAPTER " << count << rst() << "  " << fg(255)
                  << wideToUtf8(a->FriendlyName) << rst() << "\n";
        std::cout << "    Status : " << (a->OperStatus == IfOperStatusUp ? fg(46) + "UP" : fg(196) + "DOWN") << rst() << "\n";
        std::cout << "    Type   : " << a->IfType << "\n";
        for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next)
        {
            char ip[INET6_ADDRSTRLEN]{};
            DWORD ipSize = sizeof(ip);
            SOCKADDR* sa = u->Address.lpSockaddr;
            void* addr = nullptr;
            if (sa->sa_family == AF_INET) addr = &reinterpret_cast<SOCKADDR_IN*>(sa)->sin_addr;
            else if (sa->sa_family == AF_INET6) addr = &reinterpret_cast<SOCKADDR_IN6*>(sa)->sin6_addr;
            if (addr && InetNtopA(sa->sa_family, addr, ip, ipSize))
                std::cout << "    Address: " << ip << "\n";
        }
        std::cout << "\n";
    }
    std::cout << "  " << fg(244) << "Local adapter information only; no traffic is intercepted." << rst() << "\n";
    logEvent("Network adapter status query complete: " + std::to_string(count) + " adapters");
    pressAnyKey();
}

static void showLogs()
{
    drawHeader();
    sectionTitle("SYSTEM EVENT LOG / SESSION ACTIVITY");
    if (g_logs.empty()) std::cout << "  No events recorded yet.\n";
    for (const std::string& line : g_logs)
        std::cout << "  " << fg(244) << line.substr(0, 10) << rst() << fg(255) << line.substr(10) << rst() << "\n";
    std::cout << "\n  " << fg(240) << "Session-local application log. Not the Windows Event Log." << rst() << "\n";
    pressAnyKey();
}

static bool accessControl()
{
    drawHeader();
    sectionTitle("ACCESS CONTROL / DEMO SESSION GATE");
    std::cout << "  This is a local demo gate, not Windows account authentication.\n\n";
    std::string key = prompt("  Enter demo key (PANDORA-2026): ");
    if (key == "PANDORA-2026")
    {
        std::cout << "\n  " << fg(46) << "ACCESS GRANTED" << rst() << "\n";
        logEvent("Demo access gate accepted the local demonstration key");
        Sleep(500);
        return true;
    }
    std::cout << "\n  " << fg(196) << "ACCESS DENIED" << rst() << "\n";
    logEvent("Demo access gate rejected an entered key");
    pressAnyKey();
    return false;
}

static void mainMenu()
{
    bool running = true;
    while (running)
    {
        drawHeader();
        sectionTitle("CONTROL CENTER");
        std::cout << "  " << fg(51) << "[1] " << fg(255) << "System information\n"
                  << fg(51) << "  [2] " << fg(255) << "Discover Windows components\n"
                  << fg(51) << "  [3] " << fg(255) << "Network monitor\n"
                  << fg(51) << "  [4] " << fg(255) << "Access control demo\n"
                  << fg(51) << "  [5] " << fg(255) << "System event log\n"
                  << fg(51) << "  [Q] " << fg(255) << "Exit\n\n" << rst();
        std::string choice = prompt("  Select module: ");
        if (choice == "1") { drawHeader(); sectionTitle("SYSTEM INFORMATION"); printSystemInfo(); logEvent("Viewed local system information"); pressAnyKey(); }
        else if (choice == "2") scanComponents();
        else if (choice == "3") networkMonitor();
        else if (choice == "4") (void)accessControl();
        else if (choice == "5") showLogs();
        else if (choice == "q" || choice == "Q") running = false;
        else { std::cout << "  Invalid module.\n"; Sleep(600); }
    }
}

static void initConsole()
{
    g_oldCP = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleA("Pandora");

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode))
        g_vt = SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;

    char computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(computerName);
    g_host = GetComputerNameA(computerName, &size) ? computerName : "UNKNOWN";

    std::srand((unsigned)(GetTickCount() ^ (unsigned)std::time(NULL)));
    unsigned id = ((unsigned)std::rand() << 16) ^ (unsigned)std::rand();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "PAN-%08X", id);
    g_session = buf;

    std::cout << std::unitbuf;   // выводить сразу, без буферизации
}

int main()
{
    initConsole();
    clearConsole();
    std::cout << "\n";
    printBanner(true);                 // сохраняем оригинальный баннер PANDORA
    printTagline();
    std::cout << "  " << fg(255);
    typeOut("Hello " + g_host, 20);
    std::cout << rst() << "\n\n";
    printSystemInfo();

    bootStep("Initialising interface", 220);
    bootStep("Loading local device inventory", 260);
    bootStep("Preparing network monitor", 260);
    bootStep("Starting session event log", 220);
    logEvent("PANDORA interface initialised");
    std::cout << "\n  " << fg(46) << "PANDORA READY" << rst() << "\n";
    Sleep(650);

    if (accessControl()) mainMenu();
    logEvent("PANDORA session closed");
    drawHeader();
    std::cout << "  " << fg(46) << "Session closed. No files were modified." << rst() << "\n";
    pressAnyKey("Press any key to exit . . .");
    SetConsoleOutputCP(g_oldCP);
    return 0;
}
