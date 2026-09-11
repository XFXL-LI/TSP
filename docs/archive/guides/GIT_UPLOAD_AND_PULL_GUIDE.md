# TSP 项目 GitHub 上传与拉取指南

最后更新：2026-08-06

## 一、项目地址与当前分支

- GitHub 项目页面：<https://github.com/XFXL-LI/TSP>
- Git 克隆地址：`https://github.com/XFXL-LI/TSP.git`
- 当前 Firmware 2.0.4 开发分支：`agent/firmware-2.0.4-csq-led`
- 当前 Pull Request：<https://github.com/XFXL-LI/TSP/pull/1>
- 2026-08-06 最新提交：`b1231e9 Sync verified Firmware 2.0.4 fixes`

> 当前分支保留 Firmware 2.0.4 已验证修复。小时统计逻辑、HJ212 逐包
> 3000ms 间隔和 SHT30 代码均未修改，DEBUG 保持开启。

## 二、在其他电脑首次拉取项目

### 推荐方法：直接克隆当前开发分支

打开 PowerShell，进入准备存放项目的目录，然后执行：

```powershell
git clone --branch agent/firmware-2.0.4-csq-led --single-branch https://github.com/XFXL-LI/TSP.git
```

进入项目目录：

```powershell
Set-Location ".\TSP"
```

确认当前分支及最新提交：

```powershell
git branch --show-current
git log -1 --oneline
git status
```

正常情况下，当前分支应显示：

```text
agent/firmware-2.0.4-csq-led
```

### 已经克隆过仓库时切换到当前分支

```powershell
Set-Location "你的项目目录"
git fetch origin
git switch agent/firmware-2.0.4-csq-led
git pull --ff-only
```

如果本地还没有这个分支，可执行：

```powershell
git fetch origin
git switch --track origin/agent/firmware-2.0.4-csq-led
```

## 三、日常拉取远端最新修改

开始修改代码前，建议先执行：

```powershell
Set-Location "你的项目目录"
git switch agent/firmware-2.0.4-csq-led
git status
git pull --ff-only
```

`git pull --ff-only` 只允许安全的快进更新。当本地和远端产生不同提交时，
它会停止并提示处理分支差异，不会自动生成额外的合并提交。

如果 `git status` 显示存在尚未提交的修改，应先检查这些修改，不要直接覆盖。

## 四、本机项目目录说明

当前电脑有两个需要区分的目录：

### 实际 Arduino 源码目录

```text
D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\Firmware_2.0.3_original\TSP
```

这是当前正式测试和 Arduino IDE 使用的实际源码目录。

### Git 提交工作目录

```text
D:\ChatGPT-Pro\TSP-ESP32-S3\worktrees\firmware-2.0.4-csq-led
```

Git 提交和推送应在这个目录中执行。

如果先在实际 Arduino 源码目录中修改了文件，需要先核对修改，再把确定需要
提交的文件同步到 Git 工作目录中的相同相对路径。不要直接整目录覆盖，以免把
编译产物、临时文件或未经确认的改动一起带入 Git。

## 五、上传修改到 GitHub

### 1. 进入 Git 工作目录

```powershell
Set-Location "D:\ChatGPT-Pro\TSP-ESP32-S3\worktrees\firmware-2.0.4-csq-led"
```

### 2. 确认当前分支

```powershell
git branch --show-current
git status
```

必须确认当前分支是：

```text
agent/firmware-2.0.4-csq-led
```

### 3. 检查修改内容

查看全部未暂存差异：

```powershell
git diff
```

查看某个文件的差异：

```powershell
git diff -- "src\app\configManager\config.cpp"
```

### 4. 只添加确定需要提交的文件

示例：

```powershell
git add "src\app\configManager\config.cpp"
git add "CHANGELOG_2.0.4.md"
```

不建议直接执行：

```powershell
git add -A
```

因为它可能把编译产物、临时文件或无关修改一起加入提交。

### 5. 检查即将提交的内容

```powershell
git status
git diff --cached
```

只有确认文件范围和内容都正确后，才能继续提交。

### 6. 创建本地提交

```powershell
git commit -m "填写本次修改内容"
```

示例：

```powershell
git commit -m "Update Firmware 2.0.4 test documentation"
```

### 7. 推送到 GitHub

当前分支已经设置远端跟踪，因此直接执行：

```powershell
git push
```

如果是新建分支第一次推送，使用：

```powershell
git push -u origin 分支名称
```

### 8. 确认推送结果

```powershell
git status
git log -1 --oneline --decorate
```

还可以打开以下页面检查提交是否已更新：

- 分支页面：<https://github.com/XFXL-LI/TSP/tree/agent/firmware-2.0.4-csq-led>
- Pull Request：<https://github.com/XFXL-LI/TSP/pull/1>

## 六、一次完整上传示例

下面的文件名仅为示例，应替换为实际需要提交的文件：

```powershell
Set-Location "D:\ChatGPT-Pro\TSP-ESP32-S3\worktrees\firmware-2.0.4-csq-led"

git status
git branch --show-current
git diff

git add "CHANGELOG_2.0.4.md"
git add "src\module\file\file_storage.cpp"

git diff --cached
git commit -m "Update Firmware 2.0.4 storage fix"
git push

git status
git log -1 --oneline --decorate
```

## 七、常见情况

### `git push` 提示需要登录

先执行：

```powershell
gh auth login
```

按照提示选择 GitHub.com 和 HTTPS，并在浏览器中完成登录。登录完成后重新执行：

```powershell
git push
```

### `git pull --ff-only` 失败

这通常表示本地和远端各自存在不同提交。此时不要执行强制推送，也不要执行
`git reset --hard`。先运行：

```powershell
git status
git log --oneline --decorate --graph -10
```

保存输出并分析本地与远端差异后再处理。

### 修改了错误的分支

先不要提交或推送，执行：

```powershell
git status
git branch --show-current
```

确认修改文件和所在分支后再决定如何处理。

## 八、安全提醒

- Git 提交、拉取和推送不会烧录 ESP32-S3。
- 烧录固件是 Arduino IDE 或 `esptool` 的独立操作，不包含在本文步骤中。
- MCU 正在正式运行时，不要点击 Arduino IDE 的上传按钮，不要执行烧录命令。
- 不要删除现场遗留的 `E:\pending\0\20260805092020.pkt`。
- 正式测试期间继续保持 DEBUG 开启，不要自行切换测试固件基线。
- 双向交互屏统一称为“大彩HMI屏”，单向设备统一称为“LED灯珠灯箱”。
