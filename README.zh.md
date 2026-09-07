# relaydx

[English](README.md) | [中文](README.zh.md)

`relaydx` 在同一个进程、同一条事件循环里，在接口之间中继 IPv4 ARP/DHCP
以及 IPv6 RA、DHCPv6 和 NDP。

没有配置文件：所有行为都通过命令行指定。systemd 单元通过
`RELAYDX_OPTIONS` 传入这些参数。

## 功能

- IPv4 ARP 代理与主机路由管理
- IPv4 DHCP 与广播转发
- IPv6 路由通告中继或服务器模式
- DHCPv6 中继或服务器模式，含 IA_NA 与前缀委托
- IPv6 邻居发现代理，以及可选的路由学习
- 内置接口热插拔、载波和 IPv4 地址监视

该守护进程仅支持 Linux，必须以 root 运行，因为它使用原始套接字和 rtnetlink。
默认会为每个已启用的地址族打开内核转发。IPv6 中继模式下，会在打开全局 IPv6
转发之前把上游接口的 `accept_ra` 设为 `2`，以便继续接受上游路由通告。

## 构建

仅支持 Linux。需要 CMake 和 C99 编译器，无额外依赖库。

```sh
cmake --preset linux
cmake --build --preset linux-release
sudo cmake --install cmake-build-linux --prefix /usr
```

## 发布包

下载与架构匹配的压缩包，校验、解压并运行包内安装脚本：

```sh
sha256sum -c relaydx-0.1.0-linux-amd64.tar.gz.sha256
tar -xzf relaydx-0.1.0-linux-amd64.tar.gz
cd relaydx-0.1.0-linux-amd64
sudo ./install.sh
sudo editor /etc/default/relaydx
sudo systemctl enable --now relaydx
```

每个压缩包只有一个顶层目录，内部为简单的平铺结构：

```text
relaydx-0.1.0-linux-amd64/
├── relaydx
├── relaydx.service
├── relaydx.default
├── install.sh
├── uninstall.sh
├── version.txt
├── README.md
├── README.zh.md
├── LICENSE
└── THIRD_PARTY_NOTICES.md
```

`install.sh` 将程序安装到 `/usr/sbin/relaydx`，将服务单元安装到
`/etc/systemd/system/relaydx.service`，并在首次安装时创建
`/etc/default/relaydx`。已有配置不会被覆盖。使用
`sudo ./uninstall.sh` 卸载程序；添加 `--purge` 可同时删除配置。

维护者可直接根据 `version.txt` 构建发布包：

```sh
./scripts/release.sh
```

推送与版本匹配的 `v*` tag 会触发 `.github/workflows/release.yml`，自动
构建 amd64 和 arm64 包、校验 SHA256 并创建 GitHub Release。

## 快速开始

在上游和下游接口之间同时中继 IPv4 与 IPv6：

```sh
relaydx -4 -6 -M wan0 -I wan0 -I lan0 --dhcp4
```

IPv4 中继，加上 IPv6 RA/DHCPv6 服务器模式：

```sh
relaydx -4 --server6 -M . -I lan0 -i guest0 --dhcp4
```

仅 IPv4 的双接口中继，并允许本机访问被中继网络：

```sh
relaydx -4 -I wlan0 -I br-lan --broadcast4 --dhcp4 --local4 192.168.1.2
```

IPv6 NDP 代理，下游为外部接口：

```sh
relaydx -6 -M wan0 -i wan0 -i '~lan0'
```

IPv6 服务器：改写 DNS、静态 IA_NA，以及租约钩子：

```sh
relaydx --server6 -M . -i lan0 \
    --rewrite-dns6=fd00:53::1 \
    --lease6 000100012b3c4d5e001122334455:00000001 \
    --state6 /var/lib/relaydx/leases,/usr/local/sbin/relaydx-lease-hook
```

前台调试：

```sh
relaydx -vv -4 -6 -M wan0 -I wan0 -I lan0 --dhcp4
```

## 命令行

```text
relaydx [options] {-i|-I} IFACE [{-i|-I} IFACE ...]
```

必须以 root 运行。至少启用一个地址族（`-4`、`-6`、`--server6`，或单独的
IPv6 功能选项）。至少需要一个接口。IPv4 中继需要两个或更多接口。

```sh
relaydx --help
```

### 调用规则

- 未知选项、多余的非选项参数，或缺少地址族/接口时，打印帮助并以状态码 `1` 退出。
- 接口名是 Linux 设备名，最长 15 个字符（`IFNAMSIZ - 1`）。
- 同一接口可以出现多次，相关标志会合并。
- GNU getopt 会重排参数，选项和接口可以交错书写。
- 可选参数必须使用 `=` 形式（`--rewrite-dns6=ADDR`）。空格分隔的值会被当成独立参数并拒绝。
- 选项按命令行出现顺序生效。同时给出 `-6` 与 `--server6` 时，重叠项以后出现的为准。

```sh
# NDP + RA 中继，但关闭 DHCPv6
relaydx -6 --dhcp6 off -M wan0 -i wan0 -i lan0

# IPv6 服务器，再对真实上游打开 NDP
relaydx --server6 --ndp6 -M wan0 -I wan0 -I lan0
```

### 退出状态

| 状态码 | 含义 |
|--------|------|
| `0` | 正常退出（`SIGINT` / `SIGTERM`），或 `--help` |
| `1` | 选项无效，或在 `--no-interface-watch` 下事件循环停止 |
| `2` | 非 root，或 uloop / 核心 / 链路监视初始化失败 |
| `4` | 接口看起来已就绪，但中继模块启动失败 |

### 地址族与接口

#### `-4`, `--ipv4`

启用 IPv4 ARP 代理与主机跟踪。`--broadcast4`、`--dhcp4`、`--gateway4`、
`--route4`、`--local4` 会隐含打开本选项。

IPv4 把列出的每个接口都当作对等端，`-M` 不影响 IPv4 拓扑。

#### `-6`, `--ipv6`

启用 IPv6 **中继**预设：

| 功能 | 设置 |
|------|------|
| 路由通告 (RA) | 中继 |
| DHCPv6 | 中继 |
| NDP 代理 | 开 |
| 初始路由请求 (RS) | 开 |
| NDP 路由学习 | 开 |

后续选项可以覆盖其中单项。

#### `--server6`

启用 IPv6 **服务器**预设：

| 功能 | 设置 |
|------|------|
| 路由通告 (RA) | 服务器 |
| DHCPv6 | 服务器（IA_NA + 前缀委托） |
| NDP 代理 | 关 |
| 初始路由请求 (RS) | 关 |
| NDP 路由学习 | 关 |

若省略 `-M`，IPv6 上游默认是 `.`（没有真实上游接口）。

服务器模式是轻量 RA/DHCPv6 服务。它通告下游接口上**已经配置好**的前缀。这些接口上的地址配置仍由 NetworkManager、systemd-networkd 或静态配置负责。

#### `-i`, `--interface IFACE`

添加一个**不带** IPv4 主机路由管理的中继接口。

该接口仍参与 ARP 代理、DHCP/广播转发和 IPv6 服务。在其上学到的 IPv4 主机在 `--timeout4` 后直接过期，不会做 ARP 探测，也不会为它们安装 connected 主机路由。

名称前加 `~` 可将其标为 NDP 外部接口。

#### `-I`, `--managed-interface IFACE`

添加一个**带** IPv4 ARP 缓存和主机路由管理的中继接口。

对托管接口上学到的每个 IPv4 主机，`relaydx` 会在其他接口的策略路由表中安装主机路由，并在过期前发送 ARP 探测。双接口中继里，WAN 和 LAN 通常都用 `-I`。这里同样接受 `~` 前缀。

#### `-M`, `--master6 IFACE`

指定 IPv6 上游（master）接口。

| 取值 | 含义 |
|------|------|
| 真实接口 | RA/DHCPv6 中继和 NDP 的上游。若尚未出现在接口列表中，会自动加入。 |
| `.` | 没有真实上游。IPv6 纯服务器模式需要这个值。 |

省略 `-M` 时的默认值：

- IPv6 服务器模式（`--server6`、`--ra6 server` 或 `--dhcp6 server`）：`.`
- 其他情况：`-i` / `-I` 给出的第一个接口

master 不会进入 IPv6 下游（slave）列表。IPv6 至少需要一个下游接口。NDP 代理要求**真实** master；`-M . --ndp6` 会在启动时被拒绝。

#### NDP 外部接口

在 `-i` / `-I` 的接口名前加 `~`，将其标为 NDP 外部接口：

```sh
relaydx -6 -M wan0 -i wan0 -i '~lan0'
```

在外部接口上，`relaydx` **不会**为其他主机代理邻居请求 (NS)，只处理重复地址检测 (DAD) 以及发往本机的流量。建议配合额外的防火墙规则。

`~` 会在使用内核接口名之前被去掉。

#### 约束

- IPv4（`-4` 或任一 IPv4 选项）：至少两个接口。
- IPv6：去掉 master 后至少还要有一个下游接口。
- NDP（`-6`、`--ndp6`、`--learn-routes6`、`--static-ndp6`）：master 不能是 `.`。
- 等待链路就绪时会跳过占位 master `.`。

### IPv4 选项

其中多项会隐含 `-4`。

#### `--broadcast4`

在列出的接口之间转发 IPv4 广播。隐含 `-4`。

不加此选项时，只中继 ARP（以及可选的 DHCP）。

#### `--dhcp4`

在列出的接口之间转发 DHCPv4（UDP 67）。隐含 `-4`。

中继的 DHCP 应答会置上广播标志，这样客户端不必具备经过中继的单播二层路径也能接收。

#### `--no-dhcp4-parse`

不要从 DHCPv4 选项学习路由。

默认会检查 DHCP 应答中的 option 3（路由器）。若路由器地址就是 DHCP 服务器主机本身，则经该主机添加默认路由；否则排队一条 pending 默认路由，等该网关被学习到后再安装。option 121（无类静态路由）会被识别，但目前不会安装。

#### `--gateway4 IP`

为客户端排队默认路由 `0.0.0.0/0`，下一跳为 `IP`。隐含 `-4`。可重复。

等该网关主机通过 ARP/DHCP 被学习到后才会安装。

#### `--route4 GW:NET/MASK`

排队一条静态 IPv4 路由。隐含 `-4`。可重复。

```text
--route4 192.0.2.1:10.0.0.0/8
```

`GW` 和 `NET` 是点分十进制地址，`MASK` 是 `0..32` 的前缀长度。等 `GW` 被学习为主机后安装。

#### `--timeout4 SECONDS`

IPv4 主机条目过期时间，单位秒。默认 `30`，最小 `1`。

在**非托管**（`-i`）接口上，定时器到期即删除主机。在**托管**（`-I`）接口上会先发送 ARP 探测。

#### `--arp-tries4 COUNT`

删除托管主机前，从每个中继接口发送的 ARP 探测次数。默认 `5`，最小 `1`。每秒一次。

#### `--table4 NUMBER`

IPv4 策略路由表号的起始值。默认 `16800`，最小 `1`。

每个 IPv4 接口占用一个连续表号，并添加一条匹配该接口入方向的 `ip rule`。某个已学习主机的主机路由安装在**其他**接口的表中，从而从一侧进入的流量能被路由到该主机所在接口。

若同时使用 `--local4`，本机表占用第一个表号，接口从表 `NUMBER + 1` 开始。

#### `--local4 IP`

允许中继主机自身访问被中继的 IPv4 网络，并以 `IP` 作为源地址。隐含 `-4`。

`relaydx` 会在中继接口上对 `IP` 应答 ARP，额外分配一张策略表，并在其中安装 `prefsrc = IP` 的主机路由。`IP` 应是专门用于此目的的地址，不必配置在某个接口上。

### IPv6 选项

预设（`-6`、`--server6`）可以用这些选项细化或替换。`--ra6` 与 `--dhcp6` 接受 `relay`、`server` 或 `off`。

#### `--ra6 relay|server|off`

路由通告模式。

- `relay` — 在 master 与下游之间代理 RS/RA。
- `server` — 根据下游接口上已有前缀、MTU 和默认路由，在下游生成 RA。
- `off` — 关闭 RA。

服务器 RA 会为下游接口上每个合适的 `/64`（或更短）地址附带 Prefix Information。首选/有效寿命上限为 3600 秒 / 7200 秒。仅当存在默认路由**并且**存在非 ULA 前缀时，才通告默认路由器寿命（除非设置了 `--always-default6`）。

同时开启 DHCPv6 服务器时，RA 会置上 Other-Config 标志。

#### `--dhcp6 relay|server|off`

DHCPv6 模式。

- `relay` — 在下游客户端与 master 之间做 DHCPv6 中继。
- `server` — 迷你服务器：无状态信息、有状态 IA_NA，以及 IA_PD。
- `off` — 关闭 DHCPv6。

服务器模式下，下游接口上已有的前缀作为 IA_NA 提供。长于 `/64` 的前缀可用于委托：除第一个 `/64` 外的部分可通过 IA_PD 提供给下游路由器。

#### `--ndp6`

在 master 与下游之间启用邻居发现代理。`-6`、`--learn-routes6`、`--static-ndp6` 会隐含打开。

要求真实的 IPv6 master。

#### `--rs6`

在 master 上发送一次初始路由请求，以便尽快拿到 RA。同时启用 RA 中继。`-6` 会隐含打开。

#### `--learn-routes6`

从 NDP 学习到邻居的 IPv6 路由，并写入本机路由表。同时启用 NDP 代理。`-6` 会隐含打开。

#### `--rewrite-dns6[=ADDR]`

始终改写 RA（RDNSS）和 DHCPv6 中通告的 DNS 服务器。

```sh
--rewrite-dns6                 # 使用出接口上的本地地址
--rewrite-dns6=2001:db8::53    # 使用指定地址（注意 '='）
```

无参数形式通常配合本机 DNS 代理使用。带认证的 DHCPv6 应答不会被改写。

#### `--always-default6`

即使接口上只有 ULA 前缀（`fc00::/7`），服务器 RA 也通告默认路由。

不加此选项时，若没有公网前缀，即使本机有默认路由也不会通告，并记录一条警告。

#### `--deprecate-ula6`

当存在公网前缀时，RA 里 ULA 前缀的首选寿命设为 0，DHCPv6 IA_NA 中跳过 ULA。

#### `--ra-managed6 0|1|2`

服务器 RA 的 SLAAC / Managed-Config 模式。默认 `0`。

| 取值 | SLAAC（A 标志） | Managed-Config（M 标志） |
|------|-----------------|--------------------------|
| `0` | 是 | 否 |
| `1` | 是 | 是 |
| `2` | 否 | 是 |

#### `--ra-not-onlink6`

清除 Prefix Information 的 on-link（L）标志。主机不再把该前缀视为链路本地，离链路流量会发给路由器。

#### `--ra-preference6 LEVEL`

默认路由器与路由信息优先级：`low`、`medium`（默认）或 `high`。

#### `--state6 FILE[,CMD]`

DHCPv6 服务器：租约状态文件，以及可选的更新回调。

```sh
--state6 /var/lib/relaydx/leases
--state6 /var/lib/relaydx/leases,/usr/local/sbin/relaydx-lease-hook
```

逗号两侧不要加空格。`CMD` 通过 `execv` 执行，必须是绝对路径，且不接收参数。

文件内容包括：

- 带主机名的 IA_NA：`IPv6<TAB>hostname` 这种 hosts 风格行
- 注释行 `# iface DUID iaid hostname lifetime assigned length addrs...`

#### `--lease6 DUID:VALUE`

静态 DHCPv6 IA_NA 分配。可重复。

- `DUID` 是客户端 DUID 的偶数长度十六进制字符串（最多 260 个十六进制数字，即 130 字节）。
- `VALUE` 是 32 位十六进制主机标识，会成为分配地址的低 32 位（`prefix + VALUE`）。

```sh
--lease6 000100012b3c4d5e001122334455:00000001
```

#### `--static-ndp6 PREFIX/LEN:IFACE`

静态 NDP「前缀 → 接口」绑定。可重复。同时启用 NDP 代理。

```sh
--static-ndp6 2001:db8:1::/64:lan0
```

针对该前缀内地址的邻居请求会被视为位于 `IFACE`，无需等待学习邻居。

### 进程选项

#### `-v`, `--verbose`

增加日志详细程度。可重复（`-v`、`-vv`、…）。

| 次数 | syslog 掩码 | `DPRINTF` |
|------|-------------|-----------|
| 无 | 至 `LOG_WARNING` | 关 |
| `-v` | 至 `LOG_INFO` | 级别 1 |
| `-vv` 及以上 | 全部级别 | 对应调试级别 |

日志写入 syslog，标识 `relaydx`，设施 `LOG_DAEMON`。前台运行时同时打印到 stderr（`LOG_PERROR`）。

#### `-d`, `--daemon`

调用 `daemon(0, 0)` 转入后台，并写入 PID 文件。随仓库提供的 systemd 单元是 `Type=notify`，**不要**再加 `-d`。

#### `-p`, `--pidfile FILE`

PID 文件路径。默认 `/var/run/relaydx.pid`。仅在使用 `-d` 时写入，退出时删除。

#### `--no-interface-watch`

一次性模式。不等待接口出现或就绪，也不在链路或 IPv4 地址变化时重载。若事件循环因 `SIGINT`/`SIGTERM` 以外的原因停止，进程以 `1` 退出。

#### `--no-forwarding-setup`

不修改内核转发 sysctl。当转发开关由其他组件管理时使用。

默认会按已启用的地址族写入：

| 条件 | Sysctl | 值 |
|------|--------|----|
| 启用 IPv4 | `/proc/sys/net/ipv4/ip_forward` | `1` |
| IPv6 中继（RA 中继、DHCPv6 中继或 NDP）且 master 为真实接口 | `/proc/sys/net/ipv6/conf/<master>/accept_ra` | `2` |
| 任一 IPv6 功能 | `/proc/sys/net/ipv6/conf/all/forwarding` | `1` |

`accept_ra=2` 在打开全局 IPv6 转发**之前**设置，这样上游接口仍然接受路由通告。

`relaydx` 从不配置接口地址（包括静态 IPv6 地址）。

#### `-h`, `--help`

向 stdout 打印帮助并以 `0` 退出。非法用法把同一段文字打到 stderr，并以 `1` 退出。

## 接口监视

接口监视默认开启。`relaydx` 可以在配置的接口尚不存在或没有载波时启动。它会等到每个真实接口同时处于管理 up 且 running（`IFF_UP | IFF_RUNNING`），再启动中继模块。任一侧 down 或消失时，守护进程会清除已学习的路由和中继状态并等待；全部接口恢复后再重新初始化模块。IPv4 地址变化（`RTM_NEWADDR` / `RTM_DELADDR`）也会触发进程内重载。

若需要一次性行为，使用 `--no-interface-watch`。若 IPv4/IPv6 转发 sysctl 由其他组件管理，使用 `--no-forwarding-setup`。接口地址（包括静态 IPv6 地址）仍由 NetworkManager、systemd-networkd 或发行版网络配置负责。

| 信号 | 动作 |
|------|------|
| `SIGINT`、`SIGTERM` | 停止事件循环并以 `0` 退出 |
| `SIGHUP` | 丢弃中继状态并重载，效果与接口变化相同 |
| `SIGCHLD` | 回收子进程（DHCPv6 租约钩子） |

## systemd

发布压缩包已包含 `install.sh`、`relaydx.service` 和 `relaydx.default`。
按 **发布包** 一节安装后，先编辑 `/etc/default/relaydx` 中的
`RELAYDX_OPTIONS`，再启动服务。

若从源码目录安装，则先构建并安装程序，再安装两个集成文件：

```sh
sudo cmake --install cmake-build-linux --prefix /usr
sudo install -m 0644 contrib/relaydx.service /etc/systemd/system/relaydx.service
sudo install -m 0644 contrib/relaydx.default /etc/default/relaydx
sudo systemctl daemon-reload
sudo editor /etc/default/relaydx
sudo systemctl enable --now relaydx
sudo systemctl status relaydx
```

不要加 `-d`；该单元是 `Type=notify`，由 systemd 监控前台进程。不需要
`BindsTo=`、轮询接口的 `ExecStartPre`、NetworkManager dispatcher 或
`post-up` 重启钩子。服务使用 systemd watchdog，`Restart=on-failure` 只用于
真正的进程失败。

## 许可

GPL-2.0。详见 `LICENSE`。

## 参考项目

- `relayd`: <https://github.com/openwrt/relayd>
- `6relayd`: <https://github.com/sbyx/6relayd>
- `libubox`: <https://github.com/openwrt/libubox>
