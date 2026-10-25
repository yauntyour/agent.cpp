# playwright-tools：浏览器自动化命令行工具

## Args

链式执行命令（多个命令顺序执行）

```
<tool>playwright-tools:--chain "command1; command2; ..."</tool>
```

command1,command2.....等等是playwright-cli的命令行参数：

全局选项:
-s <name>              在指定会话中运行命令
--config <file>        使用配置文件（默认 .playwright/cli.config.json）
--help                 显示帮助信息

核心命令:
open [url]                         打开浏览器，可选导航到 url
goto <url>                         导航到 url
close                              关闭当前页面
type <text>                        在可编辑元素中输入文本
click <ref> [button]               点击元素（ref 为快照引用、CSS 选择器或 locator）
dblclick <ref> [button]            双击元素
fill <ref> <text>                  填充文本到可编辑元素
fill <ref> <text> --submit         填充文本并提交（按 Enter）
drag <startRef> <endRef>           在两个元素间拖放
hover <ref>                        悬停在元素上
select <ref> <val>                 在下拉框中选择选项
upload <file>                      上传一个或多个文件
check <ref>                        勾选复选框或单选按钮
uncheck <ref>                      取消勾选
snapshot                           捕获页面快照获取元素引用
snapshot --filename=<f>            保存快照到指定文件
snapshot <ref>                     捕获特定元素的快照
snapshot --depth=N                 限制快照深度
eval <func> [ref]                  在页面或元素上执行 JavaScript
dialog-accept [prompt]             接受对话框（可选填充提示文本）
dialog-dismiss                     关闭对话框
resize <w> <h>                     调整浏览器窗口大小

导航命令:
go-back                            后退
go-forward                         前进
reload                             重新加载当前页面

键盘命令:
press <key>                        按下按键（如 'a', 'arrowleft'）
keydown <key>                      按下键（不释放）
keyup <key>                        释放键

鼠标命令:
mousemove <x> <y>                  移动鼠标到指定坐标
mousedown [button]                 按下鼠标按钮（左/右/中）
mouseup [button]                   释放鼠标按钮
mousewheel <dx> <dy>               滚动鼠标滚轮

保存命令:
screenshot [ref]                   截取当前页面或元素的截图
screenshot --filename=<f>          保存截图到指定文件
pdf                                保存页面为 PDF
pdf --filename=<f>                 保存 PDF 到指定文件

标签页命令:
tab-list                           列出所有标签页
tab-new [url]                      创建新标签页（可选导航）
tab-close [index]                  关闭指定标签页（默认当前）
tab-select <index>                 切换到指定标签页

存储状态命令:
state-save [filename]              保存存储状态（cookies/localStorage等）
state-load <filename>              加载存储状态

Cookies:
cookie-list [--domain]             列出 cookies（可限制域名）
cookie-get <name>                  获取指定 cookie
cookie-set <name> <val>            设置 cookie
cookie-delete <name>               删除 cookie
cookie-clear                       清除所有 cookies

LocalStorage:
localstorage-list                  列出所有 localStorage 条目
localstorage-get <key>             获取指定 key 的值
localstorage-set <k> <v>           设置 localStorage 键值对
localstorage-delete <key>          删除指定 key
localstorage-clear                 清除所有 localStorage

SessionStorage:
sessionstorage-list                列出所有 sessionStorage 条目
sessionstorage-get <key>           获取指定 key 的值
sessionstorage-set <k> <v>         设置 sessionStorage 键值对
sessionstorage-delete <key>        删除指定 key
sessionstorage-clear               清除所有 sessionStorage

网络命令:
route <pattern> [opts]             模拟网络请求（拦截/修改）
route-list                         列出当前活跃的路由
unroute [pattern]                  移除路由（未指定 pattern 则移除所有）

DevTools 命令:
console [min-level]                列出控制台消息（级别：error/warning/info/debug）
network                            列出页面加载后的所有网络请求
run-code <code>                    执行 Playwright 代码片段
run-code --filename=<f>            从文件执行 Playwright 代码
tracing-start                      开始记录跟踪（trace）
tracing-stop                       停止记录跟踪
video-start [filename]             开始视频录制
video-chapter <title>              在视频中添加章节标记
video-stop                         停止视频录制

打开参数（用于 open/attach）:
--browser=chrome                   指定浏览器（chromium/firefox/webkit 或 chrome/msedge）
--headed                           有头模式（默认无头）
--persistent                       使用持久化 profile
--profile=<path>                   使用自定义 profile 目录
--extension                        通过浏览器扩展连接
--config=file.json                 使用配置文件
--isolated                         不保存 profile 到磁盘（内存中）

会话管理命令:
list                               列出所有活跃会话
close-all                          关闭所有浏览器
kill-all                           强制终止所有浏览器进程
-s=<name> close                    停止指定会话的浏览器
-s=<name> delete-data              删除指定会话的用户数据

其他选项:
--output-dir <dir>                 设置输出文件目录（截图、快照等）
--output-mode <mode>               输出模式：file 或 stdout（默认 stdout）
--timeouts.action <ms>             动作超时（默认 5000ms）
--timeouts.navigation <ms>         导航超时（默认 60000ms）
--test-id-attribute <attr>         指定 test id 属性（默认 data-testid）
--save-video <WxH>                 保存视频，如 --save-video=800x600
--save-trace                       保存 Playwright trace
--ignore-https-errors              忽略 HTTPS 错误
--proxy-server <url>               设置代理服务器
--proxy-bypass <domains>           绕过代理的域名列表
--user-agent <ua>                  设置用户代理字符串
--viewport-size <WxH>              设置视口大小，如 1280x720
--device <name>                    模拟设备（如 "iPhone 15"）
--grant-permissions <perms>        授予权限（geolocation, clipboard-read 等）
--block-service-workers            阻止 service workers
--init-script <file.js>            注入初始化脚本
--init-page <file.ts>              注入页面初始化脚本（TypeScript）
--cdp-endpoint <url>               连接现有浏览器的 CDP 端点
--remote-endpoint <url>            连接 Playwright 服务器远程端点
--allow-unrestricted-file-access   允许任意文件系统访问（默认仅限工作区）

环境变量（配置替代）:
PLAYWRIGHT_CLI_SESSION             设置默认会话名称
PLAYWRIGHT_MCP_*                   系列环境变量（详见 README）

使用示例:
playwright-cli open <https://example.com> --headed
playwright-cli -s=mytest open --persistent
playwright-cli snapshot --depth=2
playwright-cli click e15
playwright-cli cookie-set token abc123