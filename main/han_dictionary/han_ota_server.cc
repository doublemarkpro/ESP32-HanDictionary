#include "han_ota_server.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "board.h"
#include "display.h"

namespace {

constexpr size_t kReceiveBufferSize = 16 * 1024;
constexpr int kMaximumReceiveTimeouts = 20;
constexpr size_t kImageMetadataSize =
    sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);

const char* TAG = "HanOtaServer";

esp_err_t SendText(httpd_req_t* request, const char* status, const char* text) {
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, text);
}

int ReceiveWithRetries(httpd_req_t* request, char* buffer, size_t length) {
    int timeouts = 0;
    while (true) {
        int received = httpd_req_recv(request, buffer, length);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && timeouts++ < kMaximumReceiveTimeouts) {
            continue;
        }
        return received;
    }
}

bool HeaderEndsWithBin(httpd_req_t* request) {
    const size_t length = httpd_req_get_hdr_value_len(request, "X-Firmware-Name");
    if (length < 5 || length > 160) {
        return false;
    }

    std::string filename(length + 1, '\0');
    if (httpd_req_get_hdr_value_str(request, "X-Firmware-Name", filename.data(),
                                    filename.size()) != ESP_OK) {
        return false;
    }
    filename.resize(length);
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return filename.size() >= 4 && filename.compare(filename.size() - 4, 4, ".bin") == 0;
}

}  // namespace

HanOtaServer& HanOtaServer::GetInstance() {
    static HanOtaServer instance;
    return instance;
}

bool HanOtaServer::Start() {
    if (server_ != nullptr) {
        return true;
    }

    std::array<char, 17> token{};
    snprintf(token.data(), token.size(), "%08lx%08lx", static_cast<unsigned long>(esp_random()),
             static_cast<unsigned long>(esp_random()));
    token_ = token.data();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 15;
    config.send_wait_timeout = 15;

    esp_err_t error = httpd_start(&server_, &config);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(error));
        server_ = nullptr;
        return false;
    }

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = RootHandler,
        .user_ctx = this,
    };
    const httpd_uri_t update = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = UpdateHandler,
        .user_ctx = this,
    };

    error = httpd_register_uri_handler(server_, &root);
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(server_, &update);
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register OTA handlers: %s", esp_err_to_name(error));
        httpd_stop(server_);
        server_ = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "Local OTA page started on port %u", config.server_port);
    return true;
}

esp_err_t HanOtaServer::RootHandler(httpd_req_t* request) {
    return static_cast<HanOtaServer*>(request->user_ctx)->HandleRoot(request);
}

esp_err_t HanOtaServer::UpdateHandler(httpd_req_t* request) {
    return static_cast<HanOtaServer*>(request->user_ctx)->HandleUpdate(request);
}

esp_err_t HanOtaServer::HandleRoot(httpd_req_t* request) {
    const esp_app_desc_t* description = esp_app_get_description();
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* update = esp_ota_get_next_update_partition(nullptr);

    std::string page = R"HTML(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'">
<title>Tab5 固件升级</title><style>
body{margin:0;background:#f4f6f8;color:#18212b;font-family:system-ui,-apple-system,"Microsoft YaHei",sans-serif}
main{max-width:620px;margin:7vh auto;padding:28px;background:white;border-radius:18px;box-shadow:0 8px 30px #0002}
h1{margin-top:0}.info{background:#eef6ff;padding:14px 18px;border-radius:10px;line-height:1.8}
input{display:block;margin:22px 0;width:100%}button{border:0;border-radius:10px;padding:12px 24px;background:#1769e0;color:white;font-size:16px}
button:disabled{opacity:.45}progress{width:100%;height:20px;margin-top:22px}.warn{color:#9b4a00}#status{min-height:1.6em}
</style></head><body><main><h1>Tab5 固件升级</h1><div class="info">当前版本：)HTML";
    page += description != nullptr ? description->version : "未知";
    page += "<br>运行分区：";
    page += running != nullptr ? running->label : "未知";
    page += "<br>固件上限：";
    page += update != nullptr ? std::to_string(update->size / 1024 / 1024) : "0";
    page += R"HTML( MiB</div><p class="warn">仅上传为本机 Tab5 汉字学习机编译的 <b>xiaozhi.bin</b>。升级时不要断电或刷新页面。</p>
<input id="file" type="file" accept=".bin,application/octet-stream"><button id="go">上传并升级</button>
<progress id="progress" max="100" value="0"></progress><p id="status">请选择固件。</p>
<script>const token=")HTML";
    page += token_;
    page += R"HTML(";const file=document.getElementById('file'),button=document.getElementById('go'),bar=document.getElementById('progress'),status=document.getElementById('status');
button.onclick=()=>{const image=file.files[0];if(!image||!image.name.toLowerCase().endsWith('.bin')){status.textContent='请选择 .bin 固件文件。';return}
if(!confirm('确定升级此 Tab5？写入期间不能断电。'))return;button.disabled=true;file.disabled=true;status.textContent='正在上传并写入备用分区…';
const xhr=new XMLHttpRequest();xhr.open('POST','/update');xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.setRequestHeader('X-OTA-Token',token);xhr.setRequestHeader('X-Firmware-Name',image.name);
xhr.upload.onprogress=e=>{if(e.lengthComputable){bar.value=Math.round(e.loaded*100/e.total);status.textContent='正在上传并写入：'+bar.value+'%'}};
xhr.onload=()=>{if(xhr.status===200){bar.value=100;status.textContent='升级成功，Tab5 正在重启。约 20 秒后刷新页面。'}else{status.textContent='升级失败：'+xhr.responseText;button.disabled=false;file.disabled=false}};
xhr.onerror=()=>{status.textContent='连接中断。若设备正在重启，请等待约 20 秒后刷新；否则请重新尝试。';button.disabled=false;file.disabled=false};xhr.send(image)};</script></main></body></html>)HTML";

    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    return httpd_resp_send(request, page.data(), page.size());
}

bool HanOtaServer::HasValidToken(httpd_req_t* request) const {
    const size_t length = httpd_req_get_hdr_value_len(request, "X-OTA-Token");
    if (length != token_.size()) {
        return false;
    }

    std::string received(length + 1, '\0');
    if (httpd_req_get_hdr_value_str(request, "X-OTA-Token", received.data(), received.size()) !=
        ESP_OK) {
        return false;
    }
    received.resize(length);
    return received == token_;
}

void HanOtaServer::RestoreAfterFailure() {
    update_in_progress_.store(false);
    Application::GetInstance().Schedule([]() {
        auto& application = Application::GetInstance();
        application.GetAudioService().Start();
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        application.SetDeviceState(kDeviceStateIdle);
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification("OTA 升级失败，原固件继续运行");
    });
}

esp_err_t HanOtaServer::HandleUpdate(httpd_req_t* request) {
    if (!HasValidToken(request)) {
        return SendText(request, "403 Forbidden", "升级令牌无效，请刷新 Tab5 升级页面后重试。");
    }
    if (!HeaderEndsWithBin(request)) {
        return SendText(request, "400 Bad Request", "只接受文件名以 .bin 结尾的固件。");
    }
    bool expected = false;
    if (!update_in_progress_.compare_exchange_strong(expected, true)) {
        return SendText(request, "409 Conflict", "已有升级正在进行。");
    }

    auto& application = Application::GetInstance();
    if (application.GetDeviceState() != kDeviceStateIdle) {
        update_in_progress_.store(false);
        return SendText(request, "409 Conflict", "设备正忙，请结束语音对话并返回待机后重试。");
    }

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    const size_t image_size = request->content_len;
    if (update_partition == nullptr || image_size < kImageMetadataSize ||
        image_size > update_partition->size) {
        update_in_progress_.store(false);
        return SendText(request, "413 Payload Too Large", "固件为空、过小或超过 OTA 分区容量。");
    }

    application.Schedule([]() {
        auto& app = Application::GetInstance();
        app.SetDeviceState(kDeviceStateUpgrading);
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        app.GetAudioService().Stop();
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus("OTA 升级");
        display->SetChatMessage("system", "升级中，不能断电！");
    });

    for (int attempt = 0; attempt < 40 && application.GetDeviceState() != kDeviceStateUpgrading;
         ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    if (application.GetDeviceState() != kDeviceStateUpgrading) {
        update_in_progress_.store(false);
        return SendText(request, "409 Conflict", "设备无法进入升级状态，请稍后重试。");
    }

    std::array<uint8_t, kImageMetadataSize> metadata{};
    size_t metadata_received = 0;
    while (metadata_received < metadata.size()) {
        int received = ReceiveWithRetries(
            request, reinterpret_cast<char*>(metadata.data() + metadata_received),
            metadata.size() - metadata_received);
        if (received <= 0) {
            RestoreAfterFailure();
            return SendText(request, "400 Bad Request", "固件头接收失败。");
        }
        metadata_received += received;
    }

    const auto* image_header = reinterpret_cast<const esp_image_header_t*>(metadata.data());
    const auto* new_description = reinterpret_cast<const esp_app_desc_t*>(
        metadata.data() + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    const esp_app_desc_t* current_description = esp_app_get_description();
    if (image_header->magic != ESP_IMAGE_HEADER_MAGIC ||
        new_description->magic_word != ESP_APP_DESC_MAGIC_WORD || current_description == nullptr ||
        strncmp(new_description->project_name, current_description->project_name,
                sizeof(new_description->project_name)) != 0) {
        RestoreAfterFailure();
        return SendText(request, "400 Bad Request", "不是兼容的 xiaozhi 应用固件。");
    }

    ESP_LOGI(TAG, "Receiving version %s (%u bytes) into %s", new_description->version,
             static_cast<unsigned>(image_size), update_partition->label);

    esp_ota_handle_t update_handle = 0;
    esp_err_t error =
        esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(error));
        RestoreAfterFailure();
        return SendText(request, "500 Internal Server Error", "无法打开备用 OTA 分区。");
    }

    error = esp_ota_write(update_handle, metadata.data(), metadata.size());
    size_t total_received = metadata.size();
    auto* buffer = static_cast<char*>(heap_caps_malloc(kReceiveBufferSize, MALLOC_CAP_SPIRAM |
                                                                              MALLOC_CAP_8BIT));
    if (error != ESP_OK || buffer == nullptr) {
        ESP_LOGE(TAG, "Could not allocate OTA buffer or write header: %s", esp_err_to_name(error));
        esp_ota_abort(update_handle);
        RestoreAfterFailure();
        return SendText(request, "500 Internal Server Error", "设备内存不足或固件头写入失败。");
    }

    while (total_received < image_size) {
        const size_t wanted = std::min(kReceiveBufferSize, image_size - total_received);
        int received = ReceiveWithRetries(request, buffer, wanted);
        if (received <= 0) {
            error = ESP_FAIL;
            break;
        }
        error = esp_ota_write(update_handle, buffer, received);
        if (error != ESP_OK) {
            break;
        }
        total_received += received;
    }
    heap_caps_free(buffer);

    if (error != ESP_OK || total_received != image_size) {
        ESP_LOGE(TAG, "OTA receive/write failed after %u/%u bytes: %s",
                 static_cast<unsigned>(total_received), static_cast<unsigned>(image_size),
                 esp_err_to_name(error));
        esp_ota_abort(update_handle);
        RestoreAfterFailure();
        return SendText(request, "400 Bad Request", "固件上传中断或写入失败，原固件未切换。");
    }

    error = esp_ota_end(update_handle);
    if (error == ESP_OK) {
        error = esp_ota_set_boot_partition(update_partition);
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "OTA validation/activation failed: %s", esp_err_to_name(error));
        RestoreAfterFailure();
        return SendText(request, "400 Bad Request", "固件校验失败，原固件未切换。");
    }

    ESP_LOGI(TAG, "OTA upload complete; next boot partition is %s", update_partition->label);
    SendText(request, "200 OK", "升级成功，设备即将重启。");
    vTaskDelay(pdMS_TO_TICKS(700));
    application.Schedule([]() { Application::GetInstance().Reboot(); });
    return ESP_OK;
}
