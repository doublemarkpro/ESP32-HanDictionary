#ifndef HAN_OTA_SERVER_H_
#define HAN_OTA_SERVER_H_

#include <atomic>
#include <string>

#include <esp_http_server.h>

class HanOtaServer {
public:
    static HanOtaServer& GetInstance();

    bool Start();

private:
    HanOtaServer() = default;
    HanOtaServer(const HanOtaServer&) = delete;
    HanOtaServer& operator=(const HanOtaServer&) = delete;

    static esp_err_t RootHandler(httpd_req_t* request);
    static esp_err_t UpdateHandler(httpd_req_t* request);

    esp_err_t HandleRoot(httpd_req_t* request);
    esp_err_t HandleUpdate(httpd_req_t* request);
    bool HasValidToken(httpd_req_t* request) const;
    void RestoreAfterFailure();

    httpd_handle_t server_ = nullptr;
    std::string token_;
    std::atomic<bool> update_in_progress_{false};
};

#endif  // HAN_OTA_SERVER_H_
