# ESP32 Han Dictionary

这是面向 M5Stack Tab5 的儿童语音查字工具，基于 XiaoZhi ESP32 `v2.4.2`。

当前里程碑提供：

- 固定的 ESP-IDF 6.0.2 开发环境；
- Tab5 Legacy 与 P4X 两种芯片修订版的独立固件变体；
- `self.dictionary.lookup` 本地 MCP 工具；
- 内置“规”字演示数据；
- Windows 环境探测、构建、硬件识别与跨电脑导出脚本。

第一版 Tab5 主界面设计见 [`docs/ui/README.md`](docs/ui/README.md)。

## 公司电脑首次构建

```powershell
cd D:\ESP32Projects\ESP32-HanDictionary
. .\tools\setup.ps1
.\tools\build.ps1 -HardwareRevision Legacy
```

设备到手后，先识别 ESP32-P4 芯片修订版：

```powershell
.\tools\detect-hardware.ps1 -Port COM5
```

如果脚本提示 `P4X`，使用：

```powershell
.\tools\build.ps1 -HardwareRevision P4X
```

不要把 P4X 固件强制写入 Rev 1.x 芯片。

## 回家后复现环境

在家中电脑安装 **Espressif Installation Manager** 和 **ESP-IDF 6.0.2**，然后克隆自己的 Git 远程仓库并运行：

```powershell
cd ESP32-HanDictionary
. .\tools\setup.ps1
.\tools\build.ps1 -HardwareRevision Legacy
```

脚本不保存公司电脑的绝对 ESP-IDF 路径，会依次查找命令行环境、EIM 配置和常见安装目录。也可以显式传入：

```powershell
.\tools\build.ps1 -IdfPath 'C:\esp\v6.0.2\esp-idf'
```

如果暂时没有自己的 Git 远程仓库，可在提交代码后生成可迁移 bundle：

```powershell
.\tools\export-bundle.ps1
```

将 `dist\ESP32-HanDictionary.bundle` 复制到家中电脑，再执行：

```powershell
git clone .\ESP32-HanDictionary.bundle ESP32-HanDictionary
```

## 当前演示

连接小智后可以问：

> 规矩的规怎么写？

模型应调用：

```text
self.dictionary.lookup({"query":"规矩的规"})
```

工具返回拼音、部首、笔画数、结构、组词和笔顺。页码目前明确返回空值，避免模型编造；待确定《新华字典》版次与 ISBN 后再导入对应页码索引。

## 后续里程碑

1. 初始化 microSD，并定义可版本化的字典数据格式。
2. 导入可合法使用的字形、释义与笔顺数据。
3. 制作 1280×720 专用查字界面和笔顺动画。
4. 把语音识别结果中的目标字与本地 MCP 查询结果联动。
5. 加入拼音软键盘和摄像头拍字识别作为语音失败时的备用入口。
