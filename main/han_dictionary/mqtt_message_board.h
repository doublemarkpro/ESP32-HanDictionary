#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Mqtt;

namespace han {

struct MqttBoardConfig {
    bool enabled = false;
    std::string server;
    int port = 1883;
    std::string client_id = "xiaozhi-tab5-board";
    std::string username;
    std::string password;
    std::string topic = "xiaozhi/message";
};

struct MqttBoardMessage {
    std::string sender;
    std::string text;
    std::string time;
    std::string avatar;
};

enum class MqttBoardState {
    Disabled,
    Unconfigured,
    Connecting,
    Connected,
    Disconnected,
    Error,
};

class MqttMessageBoard {
public:
    static MqttMessageBoard& GetInstance();

    void SetChangedCallback(std::function<void()> callback);
    bool ReloadAndConnect();
    bool Refresh();
    void Disconnect();

    MqttBoardConfig config() const;
    MqttBoardState state() const;
    std::string error() const;
    std::vector<MqttBoardMessage> messages() const;

private:
    MqttMessageBoard() = default;
    ~MqttMessageBoard();
    MqttMessageBoard(const MqttMessageBoard&) = delete;
    MqttMessageBoard& operator=(const MqttMessageBoard&) = delete;

    bool LoadConfig(MqttBoardConfig& config, std::string& error) const;
    void HandleConnected(unsigned generation);
    void HandleDisconnected(unsigned generation);
    void HandleError(unsigned generation, const std::string& error);
    void HandleMessage(unsigned generation, const std::string& topic, const std::string& payload);
    void NotifyChanged();

    mutable std::mutex mutex_;
    std::unique_ptr<Mqtt> mqtt_;
    MqttBoardConfig config_;
    MqttBoardState state_ = MqttBoardState::Unconfigured;
    std::string error_;
    std::vector<MqttBoardMessage> messages_;
    std::function<void()> changed_callback_;
    unsigned generation_ = 0;
};

}  // namespace han
