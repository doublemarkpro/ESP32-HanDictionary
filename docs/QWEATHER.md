# 和风天气配置

天气页使用和风天气的实时天气 v1、每日预报 v1 和实时空气质量 v1 接口。四日预报
同时提供每天的温度范围、降水概率和日出日落。生活指数目前由和风天气仍在提供的
`/v7/indices/1d` 接口读取穿衣指数及详细说明。固件使用 `X-QW-Api-Key` 请求头认证，
并请求中文结果。

## 准备配置

1. 在和风天气控制台创建项目和 API KEY 凭据，并取得该项目专属的 API Host。
2. 复制 `qweather.example.json` 为 `qweather.json`，填写自己的 API Host、API KEY、
   显示城市名和经纬度。
3. 将整个 `handict` 文件夹复制到 FAT32 microSD 卡根目录，关机插卡后再开机。

天气页的青岛背景、指标图标和天气状态插画也随内容包放在
`SD:/handict/ui/graphics/weather-page/`。这些较大的图片不再编入固件，因此更新固件时请同时
保留或复制该目录；完整目录目前约 76KB。缺少它不会影响天气数据刷新，但对应插画将无法显示。

最终路径和格式：

```text
SD:/handict/qweather.json
```

```json
{
  "api_host": "abcxyz.qweatherapi.com",
  "api_key": "你的 API KEY",
  "city": "青岛",
  "latitude": 36.07,
  "longitude": 120.38
}
```

`api_host` 只填写主机名，不含 `https://`、路径和斜杠。纬度范围为 -90 到 90，
经度范围为 -180 到 180；实时天气 v1 坐标保留两位小数。固件不会把 API KEY 输出到串口日志，也不会把它写入仓库
或天气缓存。

进入天气页会依次请求实时天气、四日预报、空气质量和穿衣指数。实时天气是整页更新的
必要数据；其余接口若因套餐权限或临时网络问题不可用，只会让对应卡片显示“暂无数据”，
不会丢掉已经成功取得的天气。整理后的快照不含密钥，写入 `weather.json`；断网或主请求
失败时，页面保留上一次数据并明确标记“缓存”。

和风天气官方文档：

- [实时天气 v1](https://dev.qweather.com/docs/api/weather/weather-current/)
- [每日天气预报 v1](https://dev.qweather.com/docs/api/weather/weather-daily-forecast/)
- [实时空气质量 v1](https://dev.qweather.com/docs/api/air-quality/air-current/)
- [天气生活指数](https://dev.qweather.com/docs/api/indices/indices-forecast/)
- [API Host](https://dev.qweather.com/docs/configuration/api-host/)
- [API KEY 身份认证](https://dev.qweather.com/docs/configuration/authentication/)

API KEY 适合当前原型快速接入。量产或公开分发设备时，应改用自有后端代理或短期 JWT，
避免长期凭据保存在终端可移动存储中。
