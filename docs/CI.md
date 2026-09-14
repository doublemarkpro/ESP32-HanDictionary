# 本地构建与验证

2026-09-15 起，本仓库不再配置 GitHub Actions 自动构建。推送 `main` 或其他分支不会触发云端板型编译，也不会再因该构建失败发送通知邮件。

Tab5 汉字学习助手在连接实机的开发电脑上验证，只编译实际使用的产品变体：

```powershell
python scripts/build.py m5stack/tab5 --name m5stack-tab5-han-dictionary
idf.py -p COM18 flash
```

提交前可执行本地测试：

```powershell
python -m unittest discover -s scripts/tests -v
node --test tools/ui-assets/*.test.cjs
```

构建脚本会更新本地 `sdkconfig` 和 `build/` 状态。开始构建前应加载 ESP-IDF 6.0.2 环境；编译成功只代表固件和资源通过构建，最终 UI 与硬件功能仍以 Tab5 实机验收为准。
