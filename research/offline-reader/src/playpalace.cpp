#include "playpalace.h"
#include "unified_screen.h"
#include "playpalace_grid.h"
#include "playpalace_sound.h"
#include "playpalace_speech.h"
#include "key_learning.h"

#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <atomic>
#include <deque>
#include <esp_wifi.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern const uint8_t caBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t caBundleEnd[] asm("_binary_x509_crt_bundle_end");

namespace playpalace {
namespace {
constexpr const char* directory = "/PlayPalace";
constexpr const char* identityFile = "/PlayPalace/identities-export.json";
constexpr const char* pendingIdentityFile = "/PlayPalace/identities-export-incoming.json";
constexpr const char* oldIdentityFile = "/PlayPalace/identities-export.bak";
constexpr const char* profileFile = "/PlayPalace/profiles.json";
constexpr const char* backupFile = "/PlayPalace/profiles.bak";
constexpr const char* stagingFile = "/PlayPalace/profiles.tmp";
constexpr uint32_t retryWindow = 300000;

struct Endpoint {
    String host, path;
    uint16_t port = 0;
    bool secure = false;
};

Endpoint parseEndpoint(String address, unsigned defaultPort = 0) {
    address.trim();
    if (!address.startsWith("ws://") && !address.startsWith("wss://") &&
        address.indexOf("://") < 0) address = "wss://" + address;
    Endpoint result;
    result.secure = address.startsWith("wss://");
    if (!result.secure && !address.startsWith("ws://")) return {};
    const int start = result.secure ? 6 : 5;
    const int path = address.indexOf('/', start);
    String authority = path < 0 ? address.substring(start) : address.substring(start, path);
    if (!authority.length() || authority.indexOf('@') >= 0 ||
        authority.indexOf('?') >= 0 || authority.indexOf('#') >= 0) return {};
    for (unsigned n = 0; n < address.length(); ++n) {
        if (address[n] <= ' ' || address[n] == '\\' || address[n] == '#') return {};
    }
    const int colon = authority.indexOf(':');
    if (colon >= 0) {
        const String port = authority.substring(colon + 1);
        if (!port.length() || port.length() > 5) return {};
        for (unsigned n = 0; n < port.length(); ++n)
            if (port[n] < '0' || port[n] > '9') return {};
        defaultPort = port.toInt();
        if (!defaultPort) return {};
        authority = authority.substring(0, colon);
    }
    if (defaultPort > 65535 || !authority.length()) return {};
    result.host = authority;
    result.path = path < 0 ? "/" : address.substring(path);
    result.port = defaultPort ? defaultPort : result.secure ? 443 : 80;
    return result;
}

struct Account {
    String address, username, password, refresh, certificate, label;
    unsigned port = 0;
    String soundBase;
    int64_t refreshExpires = 0;
    bool allowUnencrypted = false;
};

enum class EventType { Connected, Closed, Text };
enum Trace : uint8_t {
    Opened=1, WaitWifi, StartSocket, Handshake, SendRefresh, SendPassword, LoginOk,
    RefreshRejected, ServerDisconnect, SocketClosed, TimedOut, JsonNoMemory,
    MenuReceived, SaveFailed, QueueOverflow, SignedOut, SendFailed
};
char trace[33]{};
size_t traceLength = 0;
void record(Trace event) {
    constexpr char digits[] = "0123456789abcdefghijklmnopqrstuv";
    if (traceLength == sizeof(trace) - 1) {
        memmove(trace, trace + 1, traceLength - 1);
        --traceLength;
    }
    trace[traceLength++] = digits[event];
    trace[traceLength] = 0;
}
struct Event { EventType type; const uint8_t* data; size_t size; };
struct Connection {
    Endpoint endpoint;
    String ca;
    QueueHandle_t incoming = nullptr, outgoing = nullptr;
    SemaphoreHandle_t received = nullptr;
    TaskHandle_t task = nullptr;
    std::atomic<bool> stop{false}, done{false}, overflow{false}, sendFailed{false};
    std::atomic<unsigned> stackFree{0};
    std::atomic<size_t> largestFrame{0};
};

void socketTask(void* argument) {
    auto& connection = *static_cast<Connection*>(argument);
    {
        WebSocketsClient socket;
        auto& endpoint = connection.endpoint;
        socket.setReconnectInterval(250);
        socket.setExtraHeaders("");
        socket.onEvent([&](WStype_t type, uint8_t* payload, size_t size) {
            EventType category;
            if (type == WStype_CONNECTED) category = EventType::Connected;
            else if (type == WStype_DISCONNECTED || type == WStype_ERROR) category = EventType::Closed;
            else if (type == WStype_TEXT) category = EventType::Text;
            else return;
            if (category != EventType::Text) socket.setReconnectInterval(30000);
            if (connection.stop || connection.overflow) return;
            if (category == EventType::Text && size > connection.largestFrame.load())
                connection.largestFrame = size;
            const Event event{category, payload, size};
            if (xQueueSend(connection.incoming, &event, 0) != pdTRUE) {
                connection.overflow = true;
                return;
            }
            // The WebSocket library owns this frame until the callback returns.
            // Wait for the UI to consume it so a fast game cannot queue copies.
            xSemaphoreTake(connection.received, portMAX_DELAY);
        });
        if (endpoint.secure && connection.ca.length())
            socket.beginSslWithCA(endpoint.host.c_str(), endpoint.port, endpoint.path.c_str(),
                                  connection.ca.c_str(), "");
        else if (endpoint.secure)
            socket.beginSslWithBundle(endpoint.host.c_str(), endpoint.port, endpoint.path.c_str(),
                                      caBundleStart, caBundleEnd - caBundleStart, "");
        else socket.begin(endpoint.host.c_str(), endpoint.port, endpoint.path.c_str(), "");
        uint32_t nextStackSample = millis();
        while (!connection.stop && !connection.overflow) {
            socket.loop();
            String* message;
            while (!connection.stop && xQueueReceive(connection.outgoing, &message, 0) == pdTRUE) {
                if (!socket.sendTXT(*message)) connection.sendFailed = true;
                delete message;
            }
            if (int32_t(millis() - nextStackSample) >= 0) {
                connection.stackFree = uxTaskGetStackHighWaterMark(nullptr);
                nextStackSample = millis() + 1000;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        socket.disconnect();
    }
    connection.done = true;
    vTaskDelete(nullptr);
}

enum class Screen { Home, Saved, Address, Username, Password, Confirm, Connecting, Game, SoundAddress };
enum class Pane { Game, Chat, History };
enum class OrderedRead { None, Live, Text };
struct MenuItem { String text, id; };
using SpeechText = std::shared_ptr<const String>;
struct Line { SpeechText text; String buffer; };

fs::FS* storage = nullptr;
Speak speak = nullptr;
ConnectWifi connectWifi = nullptr;
ReturnHome returnHome = nullptr;
Screen screen = Screen::Home;
Pane pane = Pane::Game;
std::unique_ptr<Connection> connection;
std::vector<Account> accounts;
Account active;
std::vector<MenuItem> items;
std::vector<int> rows;
std::deque<Line> history;
PendingSpeech<SpeechText> announcements;
std::vector<String> muted;
String menuSpeech;
wifi_ps_type_t previousWifiPowerSave = WIFI_PS_NONE;
bool restoreWifiPowerSave = false;
SpeechText orderedText;
OrderedRead orderedRead = OrderedRead::None;
size_t orderedOffset = 0;
bool orderedFirst = false;
String entry, chat, inputId, menuId, prompt, historyBuffer, escapeBehavior;
String tutorialId, tutorialText, documentId;
std::vector<MenuItem> tutorialActions;
int tutorialSelected = 0;
int selected = 0, option = 0, gridStart = 0, gridWidth = 1, historyIndex = 0;
int lastAccount = -1;
int effectVolume = 75;
unsigned mediaVolume = 75;
bool tableChatMuted = false, globalChatMuted = false;
bool grid = false, multiletter = true, signedIn = false, waitingForMenu = false;
bool inputReadOnly = false, usingRefresh = false, pendingConnection = false;
bool inputMultiline = false, inputCancelable = true;
bool recoveringTable = false, atTable = false, appOpen = false;
bool waitingForWifi = false;
bool welcomePending = false, menuWaitAnnounced = false;
bool logoutPending = false, exitRequested = false;
bool profilesDirty = false;
uint32_t saveProfilesAt = 0;
uint32_t logoutDeadline = 0;
uint32_t retryUntil = 0, nextConnection = 0, connectDeadline = 0;
uint32_t nextWifiAttempt = 0;
uint32_t resyncAt = 0;
unsigned resyncAttempts = 0;
bool requestedRaggedGrid = false;

bool reached(uint32_t deadline) { return int32_t(millis() - deadline) >= 0; }
void say(const String& message, bool interrupt = true) {
    if (speak && message.length()) speak(message, true, interrupt);
}

void stopOrderedRead() {
    orderedRead = OrderedRead::None;
    orderedText.reset();
}

void beginOrderedRead(OrderedRead kind, SpeechText text) {
    menuSpeech = "";
    orderedRead = kind;
    orderedText = std::move(text);
    orderedOffset = 0;
    orderedFirst = true;
}

void readNextPart(bool speechBusy) {
    if (speechBusy && !orderedFirst) return;
    const String& text = *orderedText;
    while (orderedOffset < text.length()) {
        const size_t end = speechPartEnd({text.c_str(), text.length()}, orderedOffset);
        String part = text.substring(orderedOffset, end);
        orderedOffset = end;
        part.trim();
        if (part.length()) {
            say(part, orderedFirst && orderedRead == OrderedRead::Text);
            orderedFirst = false;
            return;
        }
    }
    stopOrderedRead();
}

void readText(String text) {
    beginOrderedRead(OrderedRead::Text, std::make_shared<String>(std::move(text)));
}

bool readJson(const char* path, JsonDocument& data) {
    File file = storage->open(path);
    return file && file.size() <= 32768 && !deserializeJson(data, file);
}

class ProfileWriter {
public:
    explicit ProfileWriter(int file) : descriptor(file) {}
    size_t write(uint8_t byte) { return write(&byte, 1); }
    size_t write(const uint8_t* bytes, size_t count) {
        size_t done = 0;
        while (done < count && !failed) {
            const size_t part = std::min(count - done, sizeof(buffer) - used);
            memcpy(buffer + used, bytes + done, part);
            used += part;
            done += part;
            if (used == sizeof(buffer)) flush();
        }
        return failed ? 0 : done;
    }
    bool flush() {
        if (!failed && used && ::write(descriptor, buffer, used) != ssize_t(used)) failed = true;
        used = 0;
        return !failed;
    }
private:
    int descriptor;
    uint8_t buffer[256];
    size_t used = 0;
    bool failed = false;
};

bool saveProfiles() {
    if (!storage) return false;
    JsonDocument data;
    data["last"] = lastAccount;
    data["at_table"] = atTable;
    data["effect_volume"] = effectVolume;
    data["muted"].to<JsonArray>();
    for (const auto& buffer : muted) data["muted"].as<JsonArray>().add(buffer);
    data["table_chat_muted"] = tableChatMuted;
    data["global_chat_muted"] = globalChatMuted;
    JsonArray array = data["accounts"].to<JsonArray>();
    for (const Account& account : accounts) {
        JsonObject row = array.add<JsonObject>();
        row["address"] = account.address;
        row["username"] = account.username;
        row["password"] = account.password;
        row["refresh"] = account.refresh;
        row["certificate"] = account.certificate;
        row["label"] = account.label;
        row["port"] = account.port;
        row["sound_base"] = account.soundBase;
        row["refresh_expires_at"] = account.refreshExpires;
        row["allow_unencrypted"] = account.allowUnencrypted;
    }
    if (data.overflowed()) return false;
    const size_t expected = measureJson(data);
    const char* mount = storage->mountpoint();
    if (!mount || expected > 32768) return false;
    char current[96], backup[96], staging[96];
    if (snprintf(current, sizeof(current), "%s%s", mount, profileFile) >= int(sizeof(current)) ||
        snprintf(backup, sizeof(backup), "%s%s", mount, backupFile) >= int(sizeof(backup)) ||
        snprintf(staging, sizeof(staging), "%s%s", mount, stagingFile) >= int(sizeof(staging)))
        return false;
    const int file = ::open(staging, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (file < 0) return false;
    ProfileWriter writer(file);
    const bool written = serializeJson(data, writer) == expected && writer.flush() && fsync(file) == 0;
    const bool closed = ::close(file) == 0;
    if (!written || !closed) { ::unlink(staging); return false; }
    struct stat info{};
    if (::stat(current, &info) == 0) {
        ::unlink(backup);
        if (::rename(current, backup) != 0) { ::unlink(staging); return false; }
    }
    if (::rename(staging, current) != 0) {
        ::rename(backup, current);
        return false;
    }
    ::unlink(backup);
    profilesDirty = false;
    return true;
}

void scheduleProfileSave() {
    profilesDirty = true;
    saveProfilesAt = millis() + 500;
}

int accountIndex(const Account& account) {
    const auto endpoint = parseEndpoint(account.address, account.port);
    for (size_t i = 0; i < accounts.size(); ++i) {
        const auto other = parseEndpoint(accounts[i].address, accounts[i].port);
        if (account.username == accounts[i].username && endpoint.host.equalsIgnoreCase(other.host)
            && endpoint.port == other.port && endpoint.path == other.path && endpoint.secure == other.secure)
            return int(i);
    }
    return -1;
}

void loadProfiles() {
    JsonDocument data;
    if (!readJson(profileFile, data) && !readJson(backupFile, data)) return;
    effectVolume = std::clamp(data["effect_volume"] | 75, 0, 100);
    tableChatMuted = data["table_chat_muted"] | false;
    globalChatMuted = data["global_chat_muted"] | false;
    for (JsonVariantConst buffer : data["muted"].as<JsonArrayConst>())
        if (buffer.is<const char*>() && muted.size() < 40) muted.push_back(buffer.as<String>());
    for (JsonObjectConst row : data["accounts"].as<JsonArrayConst>()) {
        if (accounts.size() >= 24) break;
        Account account{row["address"] | "", row["username"] | "", row["password"] | "",
                        row["refresh"] | "", row["certificate"] | "", row["label"] | "",
                        row["port"] | 0u, row["sound_base"] | "", row["refresh_expires_at"] | int64_t{0},
                        row["allow_unencrypted"] | false};
        if (parseEndpoint(account.address, account.port).port && account.username.length())
            accounts.push_back(std::move(account));
    }
    const int index = data["last"] | -1;
    if (index >= 0 && index < int(accounts.size())) {
        lastAccount = index;
        recoveringTable = data["at_table"] | false;
        atTable = recoveringTable;
    }
}

bool importAccounts(const char* path = identityFile) {
    JsonDocument data;
    if (!readJson(path, data) || !data["servers"].is<JsonArray>()) return false;
    bool found = false;
    auto original = accounts;
    for (JsonObjectConst server : data["servers"].as<JsonArrayConst>()) {
        for (JsonPairConst pair : server["accounts"].as<JsonObjectConst>()) {
            JsonObjectConst row = pair.value().as<JsonObjectConst>();
            Account account{server["host"] | "", row["username"] | "", row["password"] | "",
                            row["refresh_token"] | "", server["trusted_certificate"]["pem"] | "",
                            server["name"] | "", server["port"] | 0u, {},
                            row["refresh_expires_at"] | int64_t{0}};
            if (!parseEndpoint(account.address, account.port).port || !account.username.length()) continue;
            const int index = accountIndex(account);
            if (index >= 0) {
                if (account.password.length()) accounts[index].password = account.password;
                if (account.refresh.length() && (accounts[index].refresh.isEmpty() ||
                    account.refreshExpires > accounts[index].refreshExpires))
                    accounts[index].refresh = account.refresh;
                if (accounts[index].refresh == account.refresh)
                    accounts[index].refreshExpires = account.refreshExpires;
                if (account.certificate.length()) accounts[index].certificate = account.certificate;
            } else if (accounts.size() < 24) accounts.push_back(std::move(account));
            else continue;
            found = true;
        }
    }
    if (found && saveProfiles()) return true;
    accounts = std::move(original);
    return false;
}

bool acceptPendingExport() {
    if (!importAccounts(pendingIdentityFile)) return false;
    if (storage->exists(identityFile)) {
        storage->remove(oldIdentityFile);
        if (!storage->rename(identityFile, oldIdentityFile)) return false;
    }
    if (!storage->rename(pendingIdentityFile, identityFile)) {
        if (storage->exists(oldIdentityFile)) storage->rename(oldIdentityFile, identityFile);
        return false;
    }
    storage->remove(oldIdentityFile);
    return true;
}

void stopConnection() {
    signedIn = waitingForMenu = false;
    welcomePending = false;
    stopOrderedRead();
    menuSpeech = "";
    if (restoreWifiPowerSave) {
        esp_wifi_set_ps(previousWifiPowerSave);
        restoreWifiPowerSave = false;
    }
    chat = entry = inputId = documentId = tutorialId = "";
    tutorialActions.clear();
    if (connection) connection->stop = true;
}

void cleanConnection() {
    if (!connection || !connection->done) return;
    String* outgoing;
    while (xQueueReceive(connection->outgoing, &outgoing, 0) == pdTRUE) delete outgoing;
    vQueueDelete(connection->incoming);
    vQueueDelete(connection->outgoing);
    vSemaphoreDelete(connection->received);
    connection.reset();
}

void send(const JsonDocument& packet) {
    if (!connection || connection->stop) return;
    auto* message = new (std::nothrow) String;
    if (!message || !serializeJson(packet, *message) ||
        xQueueSend(connection->outgoing, &message, 0) != pdTRUE) {
        delete message;
        say("Could not send command");
    }
}

void simple(const char* type) {
    JsonDocument packet;
    packet["type"] = type;
    send(packet);
}

constexpr const char* signInOptions[] = {"Sign in", "Saved accounts", "Import desktop identities"};
#ifdef UNIFIED_SCREEN
void drawSignInMenu(const char* help = nullptr) {
    using unifiedScreen::Icon;
    if (screen == Screen::Home)
        unifiedScreen::menu("PlayPalace", Icon::Menu, std::size(signInOptions), option,
                            [](int index) -> String { return signInOptions[index]; }, help);
    else if (screen == Screen::Saved)
        unifiedScreen::menu("Saved accounts", Icon::Menu, accounts.size(), option,
                            [](int index) -> String {
                                const auto& account = accounts[index];
                                return account.label.length() ? account.label : account.username;
                            }, help);
}
#endif

void home() {
    screen = Screen::Home;
    option = 0;
    say("PlayPalace. Sign in. Semicolon moves up. Period moves down. Enter selects");
#ifdef UNIFIED_SCREEN
    drawSignInMenu("; . Move   Enter Select   Alt+Q Home");
#endif
}

void scheduleConnection(uint32_t delay = 0) {
    stopConnection();
    pendingConnection = true;
    screen = Screen::Connecting;
    waitingForWifi = false;
    nextConnection = millis() + delay;
}

void requestConnection(bool newLogin, bool passwordFallback = false) {
    if (newLogin) {
        retryUntil = 0;
        pane = Pane::Game;
        items.clear();
        menuId = "";
    }
    if (!parseEndpoint(active.address, active.port).secure && !active.allowUnencrypted) {
        stopConnection();
        pendingConnection = false;
        screen = Screen::Confirm;
        say(passwordFallback ?
            "Saved session expired. Enter to sign in with your password over the unencrypted connection, or Escape to cancel" :
            "Unencrypted connection. Your login can be read on the network. Enter to allow this server and connect, Escape to cancel");
        return;
    }
    scheduleConnection();
}

void connectionLost(uint32_t delay = 2000) {
    const bool wasConnected = signedIn || waitingForMenu;
    if (wasConnected && !retryUntil) {
        recoveringTable = atTable;
        retryUntil = millis() + retryWindow;
        say("Connection lost. Reconnecting");
    }
    sound::cancel();
    stopConnection();
    if (!retryUntil || reached(retryUntil)) {
        pendingConnection = false;
        retryUntil = 0;
        home();
        say("Disconnected. Choose a saved account to reconnect");
    } else scheduleConnection(delay);
}

void startSocket() {
    auto candidate = std::make_unique<Connection>();
    candidate->endpoint = parseEndpoint(active.address, active.port);
    if (candidate->endpoint.secure) {
        if (active.certificate.startsWith("-----BEGIN CERTIFICATE-----"))
            candidate->ca = active.certificate;
    }
    candidate->incoming = xQueueCreate(1, sizeof(Event));
    candidate->outgoing = xQueueCreate(6, sizeof(String*));
    candidate->received = xSemaphoreCreateBinary();
    if (!candidate->incoming || !candidate->outgoing || !candidate->received ||
        xTaskCreatePinnedToCore(socketTask, "PlayPalace", candidate->endpoint.secure ? 9216 : 6144,
                                candidate.get(), 1, &candidate->task, 0) != pdPASS) {
        if (candidate->incoming) vQueueDelete(candidate->incoming);
        if (candidate->outgoing) vQueueDelete(candidate->outgoing);
        if (candidate->received) vSemaphoreDelete(candidate->received);
        pendingConnection = false;
        home();
        say("Could not start PlayPalace connection");
        return;
    }
    connection = std::move(candidate);
    if (!restoreWifiPowerSave && esp_wifi_get_ps(&previousWifiPowerSave) == ESP_OK &&
        previousWifiPowerSave != WIFI_PS_NONE)
        restoreWifiPowerSave = esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK;
    record(StartSocket);
    pendingConnection = false;
    connectDeadline = millis() + 20000;
}

void authenticate() {
    JsonDocument packet;
    const int64_t now = time(nullptr);
    usingRefresh = !active.refresh.isEmpty() &&
        (!active.refreshExpires || now < 1700000000 || now < active.refreshExpires - 5);
    if (!usingRefresh && active.password.isEmpty()) {
        stopConnection();
        home();
        say("Saved login requires a password. Choose Sign in");
        return;
    }
    record(usingRefresh ? SendRefresh : SendPassword);
    connectDeadline = millis() + (usingRefresh ? 8000 : 15000);
    packet["type"] = usingRefresh ? "refresh_session" : "authorize";
    packet["username"] = active.username;
    packet["client_type"] = "Desktop";
    packet["platform"] = "Cardputer ADV";
    if (usingRefresh) packet["refresh_token"] = active.refresh;
    else {
        packet["password"] = active.password;
        packet["major"] = 11;
        packet["minor"] = 0;
        packet["patch"] = 0;
    }
    send(packet);
}

void playEffect(const String& name, unsigned volume = 100) {
    if (!storage || !effectVolume || !mediaVolume) return;
    const auto endpoint = parseEndpoint(active.address, active.port);
    const String base = active.soundBase.length() ? active.soundBase :
        String(endpoint.secure ? "https://" : "http://") + endpoint.host + "/play/clients/desktop/sounds";
    const unsigned combined = unsigned(effectVolume) * mediaVolume * std::min(volume, 100u) / 10000;
    sound::request(*storage, base, name, combined);
}

void authenticated(JsonObjectConst packet) {
    record(LoginOk);
    signedIn = waitingForMenu = true;
    resyncAttempts = 0;
    requestedRaggedGrid = false;
    JsonDocument capabilities;
    capabilities["type"] = "client_capabilities";
    capabilities["capabilities"].to<JsonArray>().add("menu_grid_rows_v1");
    send(capabilities);
    menuWaitAnnounced = false;
    resyncAt = millis() + 750;
    connectDeadline = millis() + 15000;
    items.clear();
    menuId = "";
    screen = Screen::Game;
    active.refresh = packet["refresh_token"] | active.refresh;
    active.refreshExpires = packet["refresh_expires_at"] | active.refreshExpires;
    const int index = accountIndex(active);
    if (index >= 0) {
        lastAccount = index;
        if (active.label.isEmpty()) active.label = accounts[index].label;
        if (active.soundBase.isEmpty()) active.soundBase = accounts[index].soundBase;
        active.allowUnencrypted = active.allowUnencrypted || accounts[index].allowUnencrypted;
        accounts[index] = active;
    } else if (accounts.size() < 24) {
        lastAccount = int(accounts.size());
        accounts.push_back(active);
    } else lastAccount = -1;
    if (lastAccount >= 0) scheduleProfileSave();
    say(lastAccount >= 0 ? "Signed in" : "Signed in. No room for another saved account");
    welcomePending = true;
}

void fallbackToPassword() {
    active.refresh = "";
    active.refreshExpires = 0;
    if (active.password.length()) {
        requestConnection(false, true);
        if (active.allowUnencrypted) say("Trying the saved password");
    } else {
        pendingConnection = false;
        stopConnection();
        home();
        say("Saved session expired. Choose Sign in");
    }
}

void queueAnnouncement(SpeechText message) {
    announcements.push(std::move(message));
}

void queueAnnouncement(String message) {
    if (message.length()) queueAnnouncement(std::make_shared<String>(std::move(message)));
}

void line(String message, const String& buffer, bool silent = false) {
    if (!message.length()) return;
    const bool atEnd = historyIndex == int(history.size());
    size_t bytes = 0;
    for (const auto& item : history) bytes += item.text->length() + item.buffer.length();
    while (!history.empty() && (history.size() >= 24 || bytes + message.length() + buffer.length() > 1800)) {
        bytes -= history.front().text->length() + history.front().buffer.length();
        history.pop_front();
        historyIndex = std::max(-1, historyIndex - 1);
    }
    history.push_back({std::make_shared<String>(std::move(message)), buffer});
    if (atEnd) historyIndex = int(history.size());
    if (!silent && std::find(muted.begin(), muted.end(), buffer) == muted.end())
        queueAnnouncement(history.back().text);
}

String menuFocusLabel() {
    return items.empty() ? String("No game menu") :
        items[selected].text + ", " + String(selected + 1) + " of " + String(items.size());
}
#ifdef UNIFIED_SCREEN
void drawGameMenu() {
    unifiedScreen::menu("PlayPalace", unifiedScreen::Icon::Menu, items.size(), selected,
                        [](int index) -> String { return items[index].text; });
}
#endif
void focusMenu(bool immediate = false) {
    menuSpeech = menuFocusLabel();
    if (immediate) {
        say(menuSpeech);
#ifdef UNIFIED_SCREEN
        drawGameMenu();
#endif
        menuSpeech = "";
    }
}

int menuPosition(JsonArrayConst entries, const char* id) {
    if (!id || !*id) return -1;
    int index = 0;
    for (JsonVariantConst entry : entries) {
        if (index == 512) break;
        const char* candidate = entry["id"] | "";
        if (!strcmp(candidate, id)) return index;
        ++index;
    }
    return -1;
}

void updateMenu(JsonObjectConst packet) {
    if (!packet["items"].is<JsonArrayConst>()) return;
    record(MenuReceived);
    const String id = packet["menu_id"] | "";
    const bool same = id == menuId;
    const auto entries = packet["items"].as<JsonArrayConst>();
    // A restored table can send its first menu before processing our capabilities.
    if (!requestedRaggedGrid && id == "turn_menu" && packet["grid_rows"].isNull() &&
        menuPosition(entries, "skippo-empty:0:4") >= 0) {
        requestedRaggedGrid = true;
        simple("resync_ui");
    }
    // Removing a background menu leaves the foreground menu and its selection in place.
    if (!same && entries.size() == 0) return;
    // An active status box owns focus until the server removes it.
    if (!same && id == "turn_menu" && menuId == "status_box" && !items.empty()) return;
    if (!same || entries.size() == 0) menuSpeech = "";
    const bool startReading = id == "status_box" ||
        (id == "game_over" && (!same || items.empty()) && menuPosition(entries, "score_line") >= 0);
    menuId = id;
    grid = packet["grid_enabled"] | (same && grid);
    multiletter = packet["multiletter_enabled"] | (!same || multiletter);
    escapeBehavior = packet["escape_behavior"] | (same ? escapeBehavior : String("keybind"));
    gridWidth = packet["grid_width"] | (same ? gridWidth : 1);
    gridStart = packet["grid_start_index"] | (same ? gridStart : 0);
    if (!same || !packet["grid_rows"].isNull()) {
        rows.clear();
        for (int length : packet["grid_rows"].as<JsonArrayConst>()) rows.push_back(length);
    }
    prompt = packet["prompt"] | (same ? prompt : String());
    int position = packet["position"] | -1;
    if (position < 0 || size_t(position) >= std::min(entries.size(), size_t(512))) {
        position = packet["position"].isNull() ? menuPosition(entries, packet["selection_id"] | "") : -1;
    }
    if (position < 0 && same && !items.empty()) {
        // Keep the current item, or the next surviving item after a played card disappears.
        for (size_t i = selected; i < items.size() && position < 0; ++i) {
            const String& itemId = items[i].id;
            if (i == size_t(selected) && (itemId == "status_line" || itemId == "score_line") &&
                itemId == (entries[i]["id"] | "")) {
                position = selected;
            } else if (i == size_t(selected) || (itemId != "status_line" && itemId != "score_line")) {
                position = menuPosition(entries, itemId.c_str());
            }
        }
    }
    items.clear();
    for (JsonVariantConst item : entries) {
        if (items.size() >= 512) break;
        if (item.is<JsonObjectConst>()) items.push_back({item["text"] | "", item["id"] | ""});
        else items.push_back({item.as<String>(), {}});
    }
    selected = position >= 0 ? position : same ? selected : 0;
    selected = std::clamp(selected, 0, std::max(0, int(items.size()) - 1));
    const bool tableMenu = id == "turn_menu" || id == "game_over" || id == "actions_menu"
        || id == "action_input_menu";
    if (tableMenu || id == "main_menu") {
        if (recoveringTable) line(tableMenu ? "Returned to your table" : "Your previous seat is unavailable. Lobby",
                                  "system");
        recoveringTable = false;
        if (atTable != tableMenu) {
            atTable = tableMenu;
            scheduleProfileSave();
        }
    }
    waitingForMenu = false;
    retryUntil = 0;
    if (pane == Pane::Game && !items.empty()) {
#ifdef UNIFIED_SCREEN
        drawGameMenu();
#endif
        if (startReading) {
            String text = prompt;
            for (const auto& item : items) {
                if (id == "game_over" && item.id != "score_line") continue;
                if (text.length()) text += '\n';
                text += item.text;
            }
            if (orderedRead == OrderedRead::None) readText(std::move(text));
            else queueAnnouncement(std::move(text));
        }
        else if (!same) {
            const String message = prompt.length() ? prompt + ". " + items[selected].text : items[selected].text;
            line(message, "menu", true);
            if (std::find(muted.begin(), muted.end(), "menu") == muted.end())
                menuSpeech = message.substring(0, 480);
        }
    }
}

void receive(const uint8_t* text, size_t length) {
    JsonDocument shape;
    shape["type"] = true;
    JsonDocument header;
    DeserializationError error = deserializeJson(header, reinterpret_cast<const char*>(text), length,
                                                 DeserializationOption::Filter(shape),
                                                 DeserializationOption::NestingLimit(20));
    if (error) { if (error == DeserializationError::NoMemory) record(JsonNoMemory); return; }
    const String type = header["type"] | "";
    if (type != "authorize_success" && type != "refresh_session_success" &&
        type != "refresh_session_failure" && type != "disconnect" && type != "menu" &&
        type != "clear_ui" && type != "speak" && type != "chat" && type != "play_sound" &&
        type != "pong" && type != "request_input" && type != "document_editor" &&
        type != "tutorial_prompt") return;
    shape.clear();
    if (type == "menu") {
        for (const char* field : {"menu_id", "grid_enabled", "multiletter_enabled", "escape_behavior",
                                 "grid_width", "grid_start_index", "grid_rows", "prompt",
                                 "position", "selection_id"})
            shape[field] = true;
        // Legacy menus mix plain strings and objects in the same array.
        shape["items"] = true;
    } else shape.set(true);
    header.clear();
    JsonDocument data;
    error = deserializeJson(data, reinterpret_cast<const char*>(text), length,
                            DeserializationOption::Filter(shape),
                            DeserializationOption::NestingLimit(20));
    if (error) { if (error == DeserializationError::NoMemory) record(JsonNoMemory); return; }
    JsonObjectConst packet = data.as<JsonObjectConst>();
    if (type == "authorize_success" || type == "refresh_session_success") authenticated(packet);
    else if (type == "refresh_session_failure" && usingRefresh) {
        record(RefreshRejected);
        fallbackToPassword();
    } else if (type == "disconnect") {
        record(ServerDisconnect);
        if (logoutPending) { exitRequested = true; return; }
        const bool retry = packet["reconnect"] | false;
        if (retry && !(packet["session_replaced"] | false))
            connectionLost(std::clamp(packet["retry_after"] | 2, 1, 300) * 1000u);
        else {
            const bool previouslySignedIn = signedIn;
            retryUntil = 0;
            pendingConnection = false;
            stopConnection();
            home();
            const String message = packet["message"] | "";
            say(message.length() ? message : previouslySignedIn ? String("Signed out by the server") :
                String("The server did not accept the login"));
        }
    } else if (type == "menu" && signedIn) updateMenu(packet);
    else if (type == "clear_ui") {
        items.clear();
        menuId = "";
        selected = 0;
        menuSpeech = "";
    }
    else if (type == "speak") {
        String message = packet["text"] | "";
        const String buffer = packet["buffer"] | "game";
        line(std::move(message), buffer, (packet["muted"] | false) || (packet["replay"] | false));
    }
    else if (type == "chat") {
        const String convo = packet["convo"] | "chat";
        line(String(packet["sender"] | "") + ": " + (packet["message"] | ""), convo,
             (convo == "global" && globalChatMuted) || (convo == "local" && tableChatMuted));
    } else if (type == "play_sound" && signedIn && storage) {
        playEffect(packet["name"] | "", std::clamp(packet["volume"] | 100, 0, 100));
    }
    else if (type == "pong") queueAnnouncement("Server responded");
    else if (type == "request_input" && signedIn) {
        inputId = packet["input_id"] | "";
        entry = packet["default_value"] | "";
        inputReadOnly = packet["read_only"] | false;
        inputMultiline = packet["multiline"] | false;
        inputCancelable = packet["cancel_supported"] | true;
        waitingForMenu = false;
        queueAnnouncement(packet["prompt"] | "Input requested");
    } else if (type == "document_editor" && signedIn) {
        documentId = packet["dialog_id"] | "";
        entry = packet["content"] | "";
        queueAnnouncement(String(packet["prompt"] | "Edit document") +
            ". Option E reads the text, Shift Enter inserts a line, Enter saves, Escape cancels");
    } else if (type == "tutorial_prompt" && signedIn) {
        const String id = packet["prompt_id"] | "";
        if (id == tutorialId) return;
        tutorialId = id;
        tutorialActions.clear();
        if (!tutorialId.length()) { focusMenu(); return; }
        tutorialText = String(packet["title"] | "Tutorial") + ". " + (packet["content"] | "");
        for (JsonObjectConst action : packet["actions"].as<JsonArrayConst>())
            tutorialActions.push_back({action["label"] | "", action["id"] | ""});
        tutorialSelected = 0;
        queueAnnouncement(tutorialText + (tutorialActions.empty() ? String() : ". " + tutorialActions[0].text));
    }
}

void selectMenu() {
    if (items.empty()) return;
    menuSpeech = "";
    JsonDocument packet;
    packet["type"] = "menu";
    packet["menu_id"] = menuId;
    packet["selection"] = selected + 1;
    if (items[selected].id.length()) packet["selection_id"] = items[selected].id;
    send(packet);
    if (menuId == "logout_confirm_menu" && items[selected].id == "yes") {
        logoutPending = true;
        logoutDeadline = millis() + 2500;
        return;
    }
    // A keypress should not start a second network client while the reply arrives.
    sound::localCue(*storage, "menuenter.ogg", unsigned(effectVolume) * mediaVolume / 100);
}

void bindKey(const String& name, const Key& key) {
    JsonDocument packet;
    packet["type"] = "keybind";
    packet["key"] = name;
    packet["shift"] = key.shift;
    packet["control"] = key.ctrl;
    packet["alt"] = key.alt;
    if (menuId.length()) packet["menu_id"] = menuId;
    if (!items.empty()) {
        packet["menu_index"] = selected + 1;
        if (items[selected].id.length()) packet["menu_item_id"] = items[selected].id;
    }
    send(packet);
}

void typeCharacter(const Key& key, String& target, size_t limit, bool echo = true) {
    if (key.fn || key.ctrl || key.alt) return;
    if (key.base == 8) {
        if (target.length()) {
            unsigned pos = target.length() - 1;
            while (pos && (uint8_t(target[pos]) & 0xc0) == 0x80) --pos;
            target.remove(pos);
            if (echo) say("Backspace");
        }
    } else if (key.character >= 32 && target.length() < limit) {
        target += key.character;
        if (echo) {
            String name = keys::name(key.character).c_str();
            if (key.character >= 'A' && key.character <= 'Z') name = "Capital " + name;
            say(name);
        }
    }
}

void historyKey(const Key& key) {
    if (history.empty()) { say("History is empty"); return; }
    if (key.base == '[' || key.base == ']') {
        std::vector<String> buffers{String()};
        for (const auto& item : history)
            if (std::find(buffers.begin(), buffers.end(), item.buffer) == buffers.end())
                buffers.push_back(item.buffer);
        auto current = std::find(buffers.begin(), buffers.end(), historyBuffer) - buffers.begin();
        if (key.shift) current = key.base == '[' ? 0 : buffers.size() - 1;
        else current = std::clamp(int(current) + (key.base == '[' ? -1 : 1), 0, int(buffers.size()) - 1);
        historyBuffer = buffers[current];
        historyIndex = int(history.size());
        say(historyBuffer.length() ? historyBuffer : "All buffers");
        return;
    }
    if (key.function == 4 && historyBuffer.length()) {
        auto current = std::find(muted.begin(), muted.end(), historyBuffer);
        if (current == muted.end()) { muted.push_back(historyBuffer); say("Buffer muted"); }
        else { muted.erase(current); say("Buffer unmuted"); }
        if (!saveProfiles()) say("Could not save mute preference");
        return;
    }
    if (key.base != ',' && key.base != '.' && key.base != ';' && key.base != '/') return;
    const int step = key.base == ',' || key.base == ';' ? -1 : 1;
    const int count = int(history.size());
    for (int i = key.shift ? (step < 0 ? 0 : count - 1) : historyIndex + step;
         i >= 0 && i < count; i += key.shift ? -step : step) {
        if (!historyBuffer.length() || history[i].buffer == historyBuffer) {
            historyIndex = i;
            beginOrderedRead(OrderedRead::Text, history[i].text);
            return;
        }
    }
    say(step < 0 ? "Start of history" : "End of history");
}
}

void open(fs::FS& card, bool mounted, Speak callback, ConnectWifi connect, ReturnHome leave) {
    record(Opened);
    speak = callback;
    connectWifi = connect;
    returnHome = leave;
    appOpen = true;
    home();
    if (!mounted || (!card.exists(directory) && !card.mkdir(directory))) {
        say("PlayPalace needs a writable SD card");
        return;
    }
    storage = &card;
    loadProfiles();
    const bool pendingExport = card.exists(pendingIdentityFile);
    if (pendingExport && !acceptPendingExport()) say("Could not replace the desktop identities export");
    if (lastAccount >= 0) {
        active = accounts[lastAccount];
        requestConnection(true);
    } else if (accounts.empty() && card.exists(identityFile)) {
        say("PlayPalace. Sign in, or choose Import desktop identities to use the export on your card");
    } else if (pendingExport && !card.exists(pendingIdentityFile)) {
        option = 1;
        say("Fresh desktop identities imported. Saved accounts. Press Enter");
    }
    if (connectWifi) connectWifi();
    nextWifiAttempt = millis() + 12000;
#ifdef UNIFIED_SCREEN
    if (screen == Screen::Home) drawSignInMenu("; . Move   Enter Select   Alt+Q Home");
#endif
}

void close() {
    appOpen = false;
    logoutPending = exitRequested = false;
    pendingConnection = false;
    retryUntil = 0;
    stopConnection();
    sound::cancel();
    if (profilesDirty && !saveProfiles()) record(SaveFailed);
    profilesDirty = false;
    storage = nullptr;
    speak = nullptr;
    connectWifi = nullptr;
    returnHome = nullptr;
    lastAccount = -1;
    active = {};
    std::vector<Account>().swap(accounts);
    std::vector<MenuItem>().swap(items);
    std::vector<MenuItem>().swap(tutorialActions);
    std::vector<int>().swap(rows);
    history.clear();
    announcements.clear();
    menuSpeech = "";
    std::vector<String>().swap(muted);
    entry = chat = inputId = menuId = historyBuffer = "";
    recoveringTable = atTable = false;
}

bool busy(){return appOpen||connection||sound::busy();}

void tick(bool speechBusy) {
    const bool overflow = connection && connection->overflow;
    if (overflow && !connection->stop) { record(QueueOverflow); connectionLost(); }
    cleanConnection();
    if (connection && connection->stop) {
        Event discarded;
        if (xQueueReceive(connection->incoming, &discarded, 0) == pdTRUE)
            xSemaphoreGive(connection->received);
    }
    if (!appOpen) return;
    if (exitRequested || (logoutPending && reached(logoutDeadline))) {
        if (returnHome) returnHome();
        return;
    }
    if (connection && !connection->stop && connection->sendFailed.exchange(false)) {
        record(SendFailed);
        connectionLost();
    }
    if (connection && !connection->stop && WiFi.status() != WL_CONNECTED) connectionLost();
    if (pendingConnection && !connection && reached(nextConnection)) {
        if (retryUntil && reached(retryUntil)) connectionLost();
        else if (WiFi.status() != WL_CONNECTED) {
            if (!waitingForWifi) { record(WaitWifi); say("Waiting for Wi-Fi"); }
            waitingForWifi = true;
            if (!retryUntil) retryUntil = millis() + retryWindow;
            if (reached(nextWifiAttempt)) {
                if (!connectWifi || !connectWifi()) {
                    pendingConnection = false;
                    retryUntil = 0;
                    home();
                    say("Choose a Wi-Fi network in Cardputer Settings");
                    return;
                }
                nextWifiAttempt = millis() + 12000;
            }
            nextConnection = millis() + 1000;
        } else if (!active.allowUnencrypted && !parseEndpoint(active.address, active.port).secure) {
            pendingConnection = false;
            requestConnection(false);
        } else {
            say(waitingForWifi ? "Wi-Fi connected. Connecting to PlayPalace" : "Connecting to PlayPalace");
            startSocket();
        }
    }
    if (connection && !connection->stop) {
        Event event;
        if (xQueueReceive(connection->incoming, &event, 0) == pdTRUE) {
            if (event.type == EventType::Connected) { record(Handshake); authenticate(); }
            else if (event.type == EventType::Closed) {
                record(SocketClosed);
                if (logoutPending) exitRequested = true;
                else connectionLost();
            }
            else receive(event.data, event.size);
            xSemaphoreGive(connection->received);
        }
        if (!connection->stop && !signedIn && reached(connectDeadline)) {
            record(TimedOut);
            if (usingRefresh) fallbackToPassword();
            else connectionLost();
        } else if (!connection->stop && signedIn && waitingForMenu && reached(connectDeadline) &&
                   !menuWaitAnnounced) {
            menuWaitAnnounced = true;
            say("Signed in. Waiting for the menu. Enter requests it again. Escape cancels");
        } else if (!connection->stop && signedIn && waitingForMenu && resyncAttempts < 3 &&
                 reached(resyncAt)) {
            simple("resync_ui");
            ++resyncAttempts;
            resyncAt = millis() + 1500;
        }
    }
    if (orderedRead != OrderedRead::None) readNextPart(speechBusy);
    else if (!speechBusy && menuSpeech.length()) {
        if (pane == Pane::Game) {
            say(menuSpeech, false);
#ifdef UNIFIED_SCREEN
            if (!items.empty()) drawGameMenu();
#endif
        }
        menuSpeech = "";
    } else if (!speechBusy && !announcements.empty()) {
        beginOrderedRead(OrderedRead::Live, announcements.pop());
        readNextPart(false);
    }
    if (welcomePending && signedIn && !waitingForMenu) {
        welcomePending = false;
        playEffect("welcome.ogg");
    }
    if (profilesDirty && reached(saveProfilesAt) && ESP.getFreeHeap() >= 6500) {
        if (!saveProfiles()) {
            record(SaveFailed);
            profilesDirty = false;
            say("Could not save the login to the SD card");
        }
    }
}

bool canRepeatMenuArrow() {
    return signedIn && screen == Screen::Game && pane == Pane::Game && !waitingForMenu &&
        !logoutPending && tutorialId.isEmpty() && inputId.isEmpty() && documentId.isEmpty() &&
        !items.empty();
}

void key(const Key& key, bool repeated) {
    if (logoutPending) return;
    if (key.base >= 8 && orderedRead != OrderedRead::None) stopOrderedRead();
    if (!storage) { say("PlayPalace needs a writable SD card"); return; }
    const bool escape = key.base == '`' && (key.fn || (screen == Screen::Game &&
        pane == Pane::Game && inputId.isEmpty() && documentId.isEmpty()));
    const bool enter = !key.fn && key.base == 13;
    if (key.alt && key.base == 'k') {
        say("Tab cycles Game, Chat, and History. Semicolon up, period down, comma left, slash right. "
            "Hold an arrow to repeat. Control stops speech. "
            "Function and digits for F keys. Backtick is Escape in Game. Option M returns to Game. "
            "Tap Function to read game view information. Option U sets the sound server. "
            "Option S signs out. Alt Q returns to Cardputer Home");
        return;
    }
    if (screen == Screen::Confirm) {
        if (escape) { pendingConnection = false; retryUntil = 0; home(); }
        else if (enter) {
            active.allowUnencrypted = true;
            const int index = accountIndex(active);
            if (index >= 0) {
                accounts[index].allowUnencrypted = true;
                saveProfiles();
            }
            scheduleConnection();
        }
        return;
    }
    if (screen == Screen::Connecting) {
        if (escape) { pendingConnection = false; retryUntil = 0; stopConnection(); home(); }
        return;
    }
    if (screen == Screen::SoundAddress) {
        if (enter) {
            if (entry.length() && !entry.startsWith("https://") && !entry.startsWith("http://")) {
                say("Enter an HTTP or HTTPS sound address");
                return;
            }
            active.soundBase = entry;
            if (lastAccount >= 0) accounts[lastAccount].soundBase = entry;
            say(saveProfiles() ? "Sound address saved" : "Could not save sound address");
        } else if (!escape) {
            typeCharacter(key, entry, 255);
            return;
        }
        entry = "";
        screen = Screen::Game;
        return;
    }
    if (screen == Screen::Address || screen == Screen::Username || screen == Screen::Password) {
        if (escape) { entry = ""; active = {}; home(); }
        else if (!enter) typeCharacter(key, entry, 255, screen != Screen::Password);
        else if (screen == Screen::Address) {
            if (!parseEndpoint(entry).port) { say("Enter a valid WebSocket server address"); return; }
            active.address = entry;
            entry = "";
            screen = Screen::Username;
            say("Username");
        } else if (screen == Screen::Username) {
            if (!entry.length()) { say("Enter a username"); return; }
            active.username = entry;
            entry = "";
            screen = Screen::Password;
            say("Password. Typing is silent");
        } else {
            active.password = entry;
            entry = "";
            requestConnection(true);
        }
        return;
    }
    if (screen == Screen::Home || screen == Screen::Saved) {
        if (key.base == 8 && !key.fn && !key.ctrl && !key.alt) {
            if (screen == Screen::Home) exitRequested = true;
            else home();
            return;
        }
        if (escape) { home(); return; }
        const int count = screen == Screen::Home ? int(std::size(signInOptions)) : int(accounts.size());
        if (!count) return;
        if (key.base == '.' || key.base == ';') {
            option = std::clamp(option + (key.base == ';' ? -1 : 1), 0, count - 1);
            if (screen == Screen::Home) say(signInOptions[option]);
            else say(accounts[option].username + ". " + accounts[option].label);
#ifdef UNIFIED_SCREEN
            drawSignInMenu();
#endif
        } else if (screen == Screen::Saved && key.ctrl && key.base == 8) {
            accounts.erase(accounts.begin() + option);
            if (lastAccount == option) { lastAccount = -1; atTable = false; }
            else if (lastAccount > option) --lastAccount;
            const bool saved = saveProfiles();
            home();
            say(saved ? "Saved login removed" : "Could not save changes");
        } else if (enter) {
            if (screen == Screen::Saved) {
                recoveringTable = atTable && lastAccount == option;
                atTable = recoveringTable;
                active = accounts[option];
                requestConnection(true);
            } else if (option == 0) {
                active = {};
                entry = "";
                recoveringTable = atTable = false;
                screen = Screen::Address;
                say("Server address. Include W S or W S S, and port when needed");
            } else if (option == 1) {
                if (accounts.empty()) say("No saved accounts");
                else {
                    screen = Screen::Saved; option = 0; say(accounts[0].username + ". " + accounts[0].label);
#ifdef UNIFIED_SCREEN
                    drawSignInMenu("; . Move   Enter Connect   Backspace Back");
#endif
                }
            } else say(importAccounts() ? "Desktop identities imported" : "Could not import desktop identities");
        }
        return;
    }
    if (key.alt && key.base == 's') {
        record(SignedOut);
        exitRequested = true;
        return;
    }
    if (!signedIn) return;
    if (waitingForMenu) {
        if (escape) { stopConnection(); home(); }
        else if (enter) { simple("resync_ui"); say("Requested the menu"); }
        return;
    }
    if (tutorialId.length()) {
        if (key.base == '.' || key.base == ';' || key.base == 9) {
            if (tutorialActions.empty()) return;
            const int step = key.base == ';' || key.shift ? -1 : 1;
            tutorialSelected = std::clamp(tutorialSelected + step, 0, int(tutorialActions.size()) - 1);
            say(tutorialActions[tutorialSelected].text);
        } else if (enter && !tutorialActions.empty()) {
            JsonDocument packet;
            packet["type"] = "tutorial_response";
            packet["prompt_id"] = tutorialId;
            packet["action"] = tutorialActions[tutorialSelected].id;
            send(packet);
        } else if (key.alt && key.base == 'e') say(tutorialText);
        return;
    }
    if (documentId.length()) {
        if (key.alt && key.base == 'e') { readText(entry.isEmpty() ? String("Empty document") : entry); return; }
        if (enter && key.shift && entry.length() < 8192) { entry += '\n'; return; }
        if (escape || enter) {
            JsonDocument packet;
            packet["type"] = "document_editor";
            packet["dialog_id"] = documentId;
            packet["action"] = escape ? "cancel" : "save";
            packet["content"] = escape ? String() : entry;
            send(packet);
            entry = documentId = "";
        } else typeCharacter(key, entry, 8192);
        return;
    }
    if (key.alt && key.base == 'u' && inputId.isEmpty()) {
        entry = active.soundBase;
        screen = Screen::SoundAddress;
        say("Sound asset base address. Empty uses the PlayPalace default");
        return;
    }
    if (inputId.length()) {
        if (key.alt && key.base == 'e' && inputReadOnly) { readText(entry); return; }
        if (escape && !inputCancelable) { say("This input cannot be cancelled"); return; }
        if (enter && key.shift && inputMultiline && !inputReadOnly && entry.length() < 1024) {
            entry += '\n';
            return;
        }
        if (escape || enter) {
            JsonDocument packet;
            packet["type"] = "editbox";
            packet["input_id"] = inputId;
            packet["cancelled"] = escape;
            packet["text"] = escape ? String() : entry;
            send(packet);
            entry = inputId = "";
        } else if (!inputReadOnly) typeCharacter(key, entry, 1024, false);
        return;
    }
    if ((key.base == 9 && !key.ctrl && !key.alt) || (key.alt && key.base == 'm')) {
        pane = key.alt ? Pane::Game : Pane((int(pane) + (key.shift ? 2 : 1)) % 3);
        if (pane == Pane::Game) say(items.empty() ? String("Game") : "Game. " + items[selected].text);
        else if (pane == Pane::Chat) say("Chat");
        else { historyIndex = int(history.size()); say("History"); }
        return;
    }
    if (key.alt && key.base == 'p') { simple("ping"); return; }
    if (key.function == 2) { simple(key.shift ? "list_online_with_games" : "list_online"); return; }
    if (key.function == 6) {
        bool& mutedChat = key.shift ? globalChatMuted : tableChatMuted;
        mutedChat = !mutedChat;
        say(String(key.shift ? "Global" : "Table") + (mutedChat ? " chat muted" : " chat unmuted"));
        if (!saveProfiles()) say("Could not save mute preference");
        return;
    }
    if (key.function == 9 || key.function == 10) {
        adjustEffectVolume(key.function == 9 ? -5 : 5);
        say("Sound effects volume " + String(effectVolume) + " percent");
        return;
    }
    if (pane == Pane::Chat && !key.fn && !key.ctrl && !key.alt) {
        if (enter) {
            chat.trim();
            if (!chat.length()) return;
            JsonDocument packet;
            if (chat.startsWith("/")) {
                const int end = chat.indexOf(' ');
                packet["type"] = "slash_command";
                packet["command"] = end < 0 ? chat.substring(1) : chat.substring(1, end);
                packet["args"] = end < 0 ? String() : chat.substring(end + 1);
            } else {
                const bool global = chat.startsWith(".");
                const int end = global ? chat.indexOf(' ') : -1;
                if (global && end < 0) return;
                packet["type"] = "chat";
                packet["convo"] = global ? "global" : "local";
                packet["message"] = global ? chat.substring(end + 1) : chat;
                packet["language"] = "English";
            }
            send(packet);
            chat = "";
        } else typeCharacter(key, chat, 512);
        return;
    }
    if (pane == Pane::History) { historyKey(key); return; }
    if (escape || key.base == 8) {
        if (key.fn && !escape) bindKey("delete", key);
        else if (!escape && menuId == "main_menu") return;
        else if (escapeBehavior == "select_last_option" && !items.empty()) {
            const int old = selected;
            selected = int(items.size()) - 1;
            selectMenu();
            selected = old;
        } else if (escapeBehavior == "escape_event") {
            JsonDocument packet;
            packet["type"] = "escape";
            packet["menu_id"] = menuId;
            send(packet);
        } else bindKey(escape ? "escape" : "backspace", key);
        return;
    }
    if (key.function > 0) { bindKey("f" + String(key.function), key); return; }
    const char* directions = ";.,/";
    const char* match = key.base ? strchr(directions, key.base) : nullptr;
    if (match) {
        const auto direction = Direction(match - directions);
        static const char* names[] = {"up", "down", "left", "right"};
        if (key.ctrl || items.empty()) bindKey(names[int(direction)], key);
        else if (!items.empty()) {
            const int previous = selected;
            if (grid) selected = navigate(selected, int(items.size()), gridStart, gridWidth,
                                          rows, direction, key.alt);
            else if (direction == Direction::Up || direction == Direction::Down) {
                const int delta = direction == Direction::Up ? -1 : 1;
                selected = std::clamp(selected + delta, 0, int(items.size()) - 1);
            }
            if (!repeated || previous != selected) focusMenu(true);
        }
    } else if (enter && !key.shift && !key.ctrl && !key.alt) selectMenu();
    else if (key.base == ' ') bindKey("space", key);
    else if (enter) bindKey("enter", key);
    else if (key.base >= 32 && (!multiletter || key.ctrl || key.alt || key.shift ||
             (menuId == "status_box" && isalnum(uint8_t(key.base)))))
        bindKey(String(key.base), key);
    else if (multiletter && !items.empty() && key.base >= 'a' && key.base <= 'z') {
        for (size_t i = 1; i <= items.size(); ++i) {
            const int index = (selected + i) % items.size();
            if (tolower(uint8_t(items[index].text[0])) == key.base) {
                selected = index;
                focusMenu(true);
                break;
            }
        }
    }
}

void readGameView() {
    if (!signedIn || screen != Screen::Game || waitingForMenu || logoutPending) return;
    stopOrderedRead();
    if (menuId.isEmpty() || prompt.isEmpty()) say("No game view information");
    else readText(prompt);
}

#ifdef UNIFIED_DIAGNOSTICS
void status() {
    Serial.printf("PP s=%u w=%d h=%u l=%u t=%s i=%d/%u\n", unsigned(screen), int(WiFi.status()),
                  ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), trace,
                  selected + 1, unsigned(items.size()));
}

void memoryStatus() {
    Serial.printf("PPMEM h=%u m=%u l=%u s=%u media=%u effects=%d frame=%u\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  connection ? connection->stackFree.load() : 0, mediaVolume, effectVolume,
                  connection ? unsigned(connection->largestFrame.load()) : 0);
}
#endif

void adjustEffectVolume(int delta) {
    if (!appOpen || !storage) return;
    const unsigned previous = effectVolume;
    effectVolume = std::clamp(effectVolume + delta, 0, 100);
    sound::changeVolume(previous * mediaVolume, unsigned(effectVolume) * mediaVolume);
    scheduleProfileSave();
}

void setMediaVolume(unsigned percent) {
    const unsigned previous = mediaVolume;
    mediaVolume = std::min(percent, 100u);
    if (appOpen) sound::changeVolume(previous, mediaVolume);
}

void stopAnnouncements() {
    stopOrderedRead();
    announcements.clear();
    menuSpeech = "";
}

bool readingText() { return orderedRead == OrderedRead::Text; }
}
