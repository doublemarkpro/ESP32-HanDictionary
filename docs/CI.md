# GitHub Actions：项目构建策略

2026-09-11 更新。入口：[Build Boards](https://github.com/doublemarkpro/ESP32-HanDictionary/actions/workflows/build.yml)。

## 日常构建

推送 `main` / `ci/*` 或向 `main` 提交 PR 时，仅编译：

- `m5stack-tab5-han-dictionary`（Legacy）
- `m5stack-tab5-han-dictionary-p4x`（P4X）

两版固定 ESP-IDF 6.0.2，使用中文及 `nihaoxiaozhi`，与本地产品构建参数一致。
准备任务先执行 Python 代码 / 内容测试、Node.js 图形 / 字体资源测试，再生成两个板型的矩阵。
每版构建成功后运行 `tools/capacity_report.py`，应用至少保留 512 KiB；未通过不上传固件。
上传 `merged-binary.bin`，产物保留七天，仍需选择与真机修订匹配的版本。

只有 Markdown 或 `docs/ui/` 下 PNG / PPM 预览更新时不触发工作流。
不忽略整个 `docs/`：例如 `docs/examples/timetable.example.json` 会影响内容 / 字形验证，仍触发检查。
混合提交只要含未忽略的文件也会触发；必要时可手动运行，覆盖纯文档过滤。

同一自动事件、分支 / PR 的新运行取消旧运行；手动诊断与自动构建分组隔离。
原来没有并发组的历史运行需要单独取消，修改配置不会自动清理它们。
失败通知保持启用，不使用 `continue-on-error` 隐藏真实错误。

注意：若以后启用 PR 分支保护，不要直接把会因路径过滤而完全不运行的工作流设为必需检查；
应先拆出始终运行的轻量检查，避免文档 PR 等待不存在的检查。

## 手动全量构建

在 Actions → Build Boards → Run workflow 中：

1. `scope=tab5`：默认，只运行产品两版。
2. `scope=all`：运行构建脚本为 IDF 6.0.2 列出的完整板型矩阵，最多四个同时编译，可能很久。
3. 只有 `scope=all` 且额外勾选 `include_s31`，才运行两块 ESP32-S31 / IDF 6.1 实验板。

S31 仍使用原来的 `release-v6.1` 开发镜像，兼容性未在本次修复；显式选择后若失败仍正常报错。
本次是收敛产品 CI，不是宣称修复了所有上游板型。其他板型继续沿用原构建默认参数，不套用 Tab5 专用参数。
Checkout、Setup Node、Upload Artifact 已统一到使用 Node.js 24 的 v6，避免旧 Actions 的 Node.js 20 弃用告警。

## 原报警原因

旧运行 [34566749551](https://github.com/doublemarkpro/ESP32-HanDictionary/actions/runs/34566749551)
对应提交 `1be95a9`，两种 Tab5 产品构建均成功。两块 S31 失败于第三方 `78/uart-uhci`：

```text
uart_uhci.cc:195: error: 'gdma_get_alignment_constraints' was not declared
```

旧配置每次推送都构建全开发板，包括不相关的 S31；纯 TODO 更新也排队，导致反复报警。
本次没有关闭仓库通知、删除历史运行、修改 ESP-IDF / vendor 源码或更改产品固件逻辑。

## 本地验证

```powershell
python -m unittest discover -s scripts/tests -v
node --test tools/ui-assets/*.test.cjs
python scripts/build.py --list-boards --json | python scripts/select_ci_variants.py --event push --scope tab5
```

板型选择器拒绝自动事件请求全量矩阵，以及产品板型缺失 / 重复等异常。工作流提交前另用 actionlint 检查 YAML 与表达式。
云端状态以实际运行结果为准；本地测试不代表已经通过云端编译，更不代表真机验收。
