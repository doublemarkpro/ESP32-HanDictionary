# MQTT 留言板配置

编辑同目录的 `mqtt.json`，填入自己的 MQTT 服务器信息，并把 `enabled` 改为
`true`。端口 `1883` 使用普通 TCP，端口 `8883` 使用 TLS。设备以 QoS 1 订阅
`topic`，设置页面可重新读取配置并重新连接。

建议发送 JSON 消息：

```json
{
  "sender": "妈妈",
  "message": "放学后记得带雨伞",
  "time": "09:20",
  "avatar": "mom"
}
```

`avatar` 支持 `mom`、`dad` 和 `teacher`。也可以直接向主题发送纯文本；设备会把它
显示为“新留言”。单条消息不应超过 2 KiB，设备在内存中保留最近 20 条留言。
