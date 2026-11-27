# Changelog

本项目所有重要变更均记录于此文件。
格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [Semantic Versioning](https://semver.org/lang/zh-CN/)。

## [Unreleased]

## [0.1.5] - 2026-11-27

### 🚀 Features
- 新增 Docker 容器化部署支持，提供零 QEMU 多架构发布镜像（`Dockerfile.release`）、本地开发构建镜像（`Dockerfile`）与 `compose.yaml` 编排示例
- 支持 SemVer 预发布版本号（如 `v0.2.0-rc.1`）在 CMake 构建、发布流水线与 `relaydx-update` 中的端到端校验

### 🐛 Bug Fixes
- 修复主线 Linux 内核未定义 `PACKET_RECV_TYPE` 时 IPv4 广播/DHCP 原始套接字误捕获单播及出站报文的问题，改为挂载内核 BPF 过滤器并校验链路帧类型
- 修复 DHCPv4 中继改写广播标志后重算 UDP 校验和的按位取反顺序错误，为 `--broadcast4` 转发补齐虚拟网卡/网桥 `CHECKSUM_PARTIAL` 场景下的 UDP 校验和重算，并过滤分片 IPv4 报文与非法默认网关地址
- 修复 IPv4 ARP 与邻居表学习未过滤 `0.0.0.0`、环回、组播/广播及 `NUD_NOARP` 伪邻居的问题，修复 `--local4` 未注册 `RT_TABLE_LOCAL` 本地路由导致内核拒绝 `RTA_PREFSRC`（`EINVAL`）与回包无法上送本机的问题，并支持对 `--local4` 地址的 RFC 5227 ARP Probe 冲突响应
- 修复 DHCPv6 中继开启 `--rewrite-dns6` 时误丢弃带认证（`AUTH`）应答与无全局地址下游接口应答/RA 的问题，增加 master 与 link-local DNS 地址回退
- 修复 DHCPv6 中继在上下游均无全局地址时丢弃请求的问题，遵循 RFC 8415 将 `link-address` 置为 `::` 并转发至链路范围中继/服务器组播组 `ff02::1:2`
- 修复 DHCPv6 服务器嵌套 `RELAY-FORW` 应答截断尾部选项、`--lease6` 静态租约重复插入、`--state6` 租约状态文件栈缓冲区溢出、以及 `IA_PD` 租约过期或仅 `SOLICIT` 时残留内核路由的问题
- 修复 `SIGTERM`/`SIGINT`/`SIGHUP` 直接异步调用 `uloop_end()` 导致的事件循环唤醒竞态，统一改为通过 `uloop_signal` 管道安全分发；在 NDP 邻居超时中改用 `CLOCK_MONOTONIC` 单调时钟

### 🧰 Maintenance
- 重构 GitHub Actions `release.yml` 与 `ci.yml`，全量升级至原生 Node 24 Action 基线，接入 GHCR 多架构镜像自动发布与零 Annotations 门禁

## [0.1.4] - 2026-09-07

### 🚀 Features
- 移除未使用的地址抑制命令行参数，并为 `relaydx-update` 增加 GitHub 下载加速镜像（`--mirror` / `RELAYDX_GITHUB_MIRROR`）支持

## [0.1.3] - 2026-09-07

### 🚀 Features
- 新增 `relaydx-update` 在线升级工具与端到端校验测试，支持按当前架构下载并校验 GitHub Release 静态包

## [0.1.2] - 2026-09-07

### 🚀 Features
- 支持在无地址（addressless）接口上运行 IPv4 与 IPv6 中继

## [0.1.1] - 2026-09-07

### 🧰 Maintenance
- 将归档打包流程收敛至 GitHub Actions 发布工作流

## [0.1.0] - 2026-09-07

### 🚀 Features
- 发布首个统一单进程双栈中继版本，提供 `x86_64` 与 `aarch64` 完全静态链接的 musl 二进制发布包、systemd 看门狗集成与 Linux `netns` 网络端到端测试
