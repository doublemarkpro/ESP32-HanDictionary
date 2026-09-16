#include "mqtt_message_board.h"

#include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <algorithm>

#include "board.h"
#include "mqtt.h"

namespace {
constexpr const char* kConfigPath = "/sdcard/handict/mqtt.json";
constexpr size_t kMaximumConfigBytes = 4096;
constexpr size_t kMaximumPayloadBytes = 2048;
constexpr size_t kMaximumMessages = 20;

std::string JsonString(const cJSON* root, const char* first, const char* second = nullptr) {
    auto value = cJSON_GetObjectItemCaseSensitive(root, first);
    if (!cJSON_IsString(value) && second != nullptr)
        value = cJSON_GetObjectItemCaseSensitive(root, second);
    return cJSON_IsString(value) && value->valuestring != nullptr ? value->valuestring : "";
}

std::string TruncateUtf8(const std::string& text, size_t maximum_bytes) {
    if (text.size() <= maximum_bytes)
        return text;
    size_t end = maximum_bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
        --end;
    return text.substr(0, end) + "…";
}

std::string CurrentTime() {
    std::time_t now = std::time(nullptr);
    struct tm local{};
    localtime_r(&now, &local);
    char value[16];
    std::strftime(value, sizeof(value), "%H:%M", &local);
    return value;
}

std::string StripBrokerScheme(std::string server) {
    constexpr const char* schemes[] = {"mqtt://", "mqtts://", "tcp://", "ssl://"};
    for (auto scheme : schemes) {
        if (server.rfind(scheme, 0) == 0) {
            server.erase(0, std::strlen(scheme));
            break;
        }
    }
    while (!server.empty() && server.back() == '/')
        server.pop_back();
    return server;
}
}  // namespace

namespace han {

MqttMessageBoard& MqttMessageBoard::GetInstance() {
    static MqttMessageBoard instance;
    return instance;
}

MqttMessageBoard::~MqttMessageBoard() { Disconnect(); }

void MqttMessageBoard::SetChangedCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    changed_callback_ = std::move(callback);
}

bool MqttMessageBoard::LoadConfig(MqttBoardConfig& config, std::string& error) const {
    FILE* file = std::fopen(kConfigPath, "rb");
    if (file == nullptr) {
        error = "请在 microSD 配置 handict/mqtt.json";
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::rewind(file);
    if (size <= 0 || size > static_cast<long>(kMaximumConfigBytes)) {
        std::fclose(file);
        error = "MQTT 配置文件大小不正确";
        return false;
    }
    std::string json(static_cast<size_t>(size), '\0');
    const bool read = std::fread(json.data(), 1, json.size(), file) == json.size();
    std::fclose(file);
    if (!read) {
        error = "无法读取 MQTT 配置";
        return false;
    }

    cJSON* root = cJSON_ParseWithLength(json.data(), json.size());
    if (root == nullptr) {
        error = "MQTT 配置不是有效 JSON";
        return false;
    }
    auto enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    config.enabled = cJSON_IsTrue(enabled);
    config.server = StripBrokerScheme(JsonString(root, "server", "endpoint"));
    auto port = cJSON_GetObjectItemCaseSensitive(root, "port");
    config.port = cJSON_IsNumber(port) ? port->valueint : 1883;
    const auto client_id = JsonString(root, "client_id");
    if (!client_id.empty())
        config.client_id = client_id;
    config.username = JsonString(root, "username");
    config.password = JsonString(root, "password");
    const auto topic = JsonString(root, "topic", "subscribe_topic");
    if (!topic.empty())
        config.topic = topic;
    cJSON_Delete(root);

    if (config.server.empty() || config.server.size() > 160 || config.topic.empty() ||
        config.topic.size() > 160 || config.port <= 0 || config.port > 65535 ||
        config.client_id.size() > 128 || config.username.size() > 128 ||
        config.password.size() > 256) {
        error = "MQTT 服务器、端口或主题配置无效";
        return false;
    }
    return true;
}

bool MqttMessageBoard::ReloadAndConnect() {
    MqttBoardConfig config;
    std::string error;
    std::unique_ptr<Mqtt> previous;
    unsigned generation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation = ++generation_;
        previous = std::move(mqtt_);
    }
    // Disconnecting may synchronously invoke callbacks. Never do it while mutex_ is held.
    if (previous != nullptr)
        previous->Disconnect();

    if (!LoadConfig(config, error)) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation != generation_)
                return false;
            config_ = config;
            state_ = MqttBoardState::Unconfigured;
            error_ = std::move(error);
        }
        NotifyChanged();
        return false;
    }
    if (!config.enabled) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation != generation_)
                return false;
            config_ = std::move(config);
            state_ = MqttBoardState::Disabled;
            error_.clear();
        }
        NotifyChanged();
        return true;
    }

    auto fresh = Board::GetInstance().GetNetwork()->CreateMqtt(1);
    if (fresh == nullptr) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation != generation_)
                return false;
            config_ = std::move(config);
            state_ = MqttBoardState::Error;
            error_ = "当前网络无法创建 MQTT 连接";
        }
        NotifyChanged();
        return false;
    }

    fresh->SetKeepAlive(90);
    fresh->OnConnected([this, generation] { HandleConnected(generation); });
    fresh->OnDisconnected([this, generation] { HandleDisconnected(generation); });
    fresh->OnError(
        [this, generation](const std::string& value) { HandleError(generation, value); });
    fresh->OnMessage([this, generation](const std::string& topic, const std::string& payload) {
        HandleMessage(generation, topic, payload);
    });

    Mqtt* client = fresh.get();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_)
            return false;
        config_ = config;
        state_ = MqttBoardState::Connecting;
        error_.clear();
        mqtt_ = std::move(fresh);
    }
    NotifyChanged();
    if (client->Connect(config.server, config.port, config.client_id, config.username,
                        config.password))
        return true;
    HandleError(generation, "连接 MQTT 服务器失败");
    return false;
}

bool MqttMessageBoard::Refresh() {
    Mqtt* client = nullptr;
    std::string topic;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (mqtt_ != nullptr && state_ == MqttBoardState::Connected) {
            client = mqtt_.get();
            topic = config_.topic;
        }
    }
    if (client != nullptr)
        return client->Subscribe(topic, 1);
    return ReloadAndConnect();
}

void MqttMessageBoard::Disconnect() {
    std::unique_ptr<Mqtt> mqtt;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        mqtt = std::move(mqtt_);
        if (config_.enabled)
            state_ = MqttBoardState::Disconnected;
    }
    if (mqtt != nullptr)
        mqtt->Disconnect();
}

void MqttMessageBoard::HandleConnected(unsigned generation) {
    Mqtt* client = nullptr;
    std::string topic;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_ || mqtt_ == nullptr)
            return;
        client = mqtt_.get();
        topic = config_.topic;
        state_ = MqttBoardState::Connected;
        error_.clear();
    }
    if (!client->Subscribe(topic, 1))
        HandleError(generation, "订阅留言主题失败");
    else
        NotifyChanged();
}

void MqttMessageBoard::HandleDisconnected(unsigned generation) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_)
            return;
        state_ = MqttBoardState::Disconnected;
        error_ = "MQTT 连接已断开";
    }
    NotifyChanged();
}

void MqttMessageBoard::HandleError(unsigned generation, const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_)
            return;
        state_ = MqttBoardState::Error;
        error_ = TruncateUtf8(error, 120);
    }
    NotifyChanged();
}

void MqttMessageBoard::HandleMessage(unsigned generation, const std::string& topic,
                                     const std::string& payload) {
    if (payload.empty() || payload.size() > kMaximumPayloadBytes)
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_ || topic != config_.topic)
            return;
    }

    MqttBoardMessage message;
    cJSON* root = cJSON_ParseWithLength(payload.data(), payload.size());
    if (root != nullptr && cJSON_IsObject(root)) {
        message.sender = JsonString(root, "sender", "from");
        if (message.sender.empty())
            message.sender = JsonString(root, "name");
        message.text = JsonString(root, "message", "text");
        if (message.text.empty())
            message.text = JsonString(root, "content");
        message.time = JsonString(root, "time", "timestamp");
        message.avatar = JsonString(root, "avatar", "role");
    } else {
        message.text = payload;
    }
    if (root != nullptr)
        cJSON_Delete(root);
    if (message.text.empty())
        return;
    if (message.sender.empty())
        message.sender = "新留言";
    if (message.time.empty())
        message.time = CurrentTime();
    message.sender = TruncateUtf8(message.sender, 36);
    message.text = TruncateUtf8(message.text, 240);
    message.time = TruncateUtf8(message.time, 32);
    message.avatar = TruncateUtf8(message.avatar, 24);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!messages_.empty() && messages_.front().sender == message.sender &&
            messages_.front().text == message.text && messages_.front().time == message.time)
            return;
        messages_.insert(messages_.begin(), std::move(message));
        if (messages_.size() > kMaximumMessages)
            messages_.resize(kMaximumMessages);
    }
    NotifyChanged();
}

void MqttMessageBoard::NotifyChanged() {
    std::function<void()> callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = changed_callback_;
    }
    if (callback)
        callback();
}

MqttBoardConfig MqttMessageBoard::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

MqttBoardState MqttMessageBoard::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string MqttMessageBoard::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

std::vector<MqttBoardMessage> MqttMessageBoard::messages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_;
}

}  // namespace han
