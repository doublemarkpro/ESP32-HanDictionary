# 和风天气配置

天气页使用和风天气当前的实时天气 v1 接口：
`GET https://{API Host}/weather/v1/current/{纬度}/{经度}`。固件使用
`X-QW-Api-Key` 请求头认证，并请求中文结果。旧的城市天气 `/v7/weather/now`
接口已进入弃用流程，因此本项目不再基于 v7 新增代码。

## 准备配置

1. 在和风天气控制台创建项目和 API KEY 凭据，并取得该项目专属的 API Host。
2. 复制 `qweather.example.json` 为 `qweather.json`，填写自己的 API Host、API KEY、
   显示城市名和经纬度。
3. 将整个 `handict` 文件夹复制到 FAT32 microSD 卡根目录，关机插卡后再开机。

最终路径和格式：

```text
SD:/handict/qweather.json
```

```json
{
  "api_host": "abcxyz.qweatherapi.com",
  "api_key": "你的 API KEY",
  "city": "青岛",
  "latitude": 36.0662,
  "longitude": 120.3826
}
```

`api_host` 只填写主机名，不含 `https://`、路径和斜杠。纬度范围为 -90 到 90，
经度范围为 -180 到 180。固件不会把 API KEY 输出到串口日志，也不会把它写入仓库
或天气缓存。

进入天气页或点击“刷新天气”会请求实时数据。成功后显示天气、温度、体感温度、湿度、
风向、风力、来源和更新时间，并将不含密钥的结果写入 `weather.json`。断网或请求失败时，
页面保留上一次数据并明确标记“缓存”。无卡、配置错误、认证失败和未联网会显示不同提示。

和风天气官方文档：

- [实时天气 v1](https://dev.qweather.com/docs/api/weather/weather-current/)
- [API Host](https://dev.qweather.com/docs/configuration/api-host/)
- [API KEY 身份认证](https://dev.qweather.com/docs/configuration/authentication/)

API KEY 适合当前原型快速接入。量产或公开分发设备时，应改用自有后端代理或短期 JWT，
避免长期凭据保存在终端可移动存储中。
