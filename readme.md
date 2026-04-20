# agent.cpp：极致轻量、完全可控的 C++ Agent 系统（WebUI）

## 简介

**agent.cpp** 是一个完全透明、高度可控的 C++ 实现的完整 Agent 系统，具备完整的 Agent 工具链支持。与庞大的 Openclaw 系统相比，agent.cpp 仅依赖于几个核心文件组成的通信系统和工具系统，内存占用极低（完全取决于你的上下文有多长，系统开销基本为0），可轻松部署资源受限的环境中。

**设计理念**：以LLM为系统的语言处理中枢服务于agent系统，由agent系统构成真正的AGI，因为人类的经验可以源于语言，AI亦是如此。

## WebUI

![](./assets/WebUI.png)

## 核心特性

- **极致轻量**：核心系统仅由几个文件组成，内存占用极低且有C++的0拷贝优化。
- **完整工具链**：支持基于Python CLI TOOL的工具原生链式调用。
- **100% 可控**：完全可以自定义的提示词，包括系统指令。
- **超强的轻量化设计的扩展tool**：直接让Agent调用Python CLI，避免像skills那样的巨大而臃肿的上下文导致的无用token浪费。
- **会话记忆系统**：全自动/手动摘要会话的messages为摘要记忆，并更新会话上下文。
- **会话记录动态加载**：切换时热加载会话的记忆和聊天记录。
- **会话记忆自动演进**：已经存在会话的记忆时，系统会基于已有记忆和使用了该记忆产生的messages全自动优化更新记忆，完善经验积累。

## ️ 系统指令集

系统支持通过特定格式的指令进行交互，确保透明可控。

**1. 工具调用指令集（Tools Call System）**
可用于调用预定义工具，格式如下：

```
<tool>name:args</tool>
```

**基础工具包括：**

- `exec:<command>`：以当前服务权限执行命令。
- `read:<filepath>`：读取指定文件数据。
- `write:<filepath>|<data>`：向指定文件写入数据。
- `wget:<URL>`：通过 GET 请求获取数据。

**示例：**

```
<tool>exec:pip list</tool>
<tool>Image:test.jpg</tool>
<tool>write:exp/data.txt|Hi</tool>
<tool>read:data.txt</tool>
<tool>wget:https://cn.bing.com/</tool>
```

**2. 通信系统指令集（Communication System, CS）**
用于获取系统状态或执行控制命令，格式如下：

```
<cs>name:args</cs>
```

**支持的命令包括：**

- `system_status`：返回当前系统状态。
- `tools_status`：返回所有工具的状态。
- `restart`：请求重启系统（需 master 确认）。
- `time`：返回当前日期和时间。
- `random:<seed>`：基于种子生成 [-1e9, 1e9] 范围内的随机数。

**示例：**

```
<cs>time</cs>
<cs>random:123</cs>
```

## 自定义的tool

一般地，一个自定义的tool（以playwright-tools为例）包括如下文件：

```
/workspace/tools/
	/playwright-tools
		run.py    被CS指令系统调度的运行脚本
		tool.md    一个包括本工具的传入参数文档、使用说明的文件
		......    其他你需要依赖的东西（系统只调用run.py，读取工具说明时只读取tool.md）
```

相应地，run.py调用失败输出的错误信息也会返回给Agent，建议出现参数错误时，返回tool.md的内容：

```python
def print_tool_help():
    """打印 tool.md 中的帮助信息"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    tool_md_path = os.path.join(script_dir, "tool.md")
    try:
        with open(tool_md_path, "r", encoding="utf-8") as f:
            print(f.read())
    except FileNotFoundError:
        print(f"警告: 未找到帮助文件 {tool_md_path}")
```

## 安全与控制机制

- **高风险操作需显式授权**：所有高风险操作（如系统重启、关键文件写入等）必须获得的显式确认后才可执行（注意：这是当自主调用状态时才会请求，主动命令无需确认，但建议shutdown等等还是用提示词约束一下）。
- **工具状态实时反馈**：工具调用后，系统将通过独立消息通知执行结果。
- **启动即可用**：会话开始后即可使用工具，也可通过 CS 指令获取工具列表及状态。

## 会话

- WebUI：会话以创建的时间戳命名，同时这也是Session ID，标准的UNIX TIME（自1970年1月1日起的秒数）。
- Channel：每个Channel存在一个以Channel的name命名的json储存Channel的聊天记录，Channel和WebUI会话一样拥有memory摘要。

### 自动会话记忆

可以在配置文件中修改触发自动记忆摘要（包括自动记忆演进）的上下文的字数，超过时系统会自动生成会话记忆并且更新会话上下文。

```json
"max_context": 128000
```

## 系统提示词配置（System Prompt）

系统支持 100% 自定义系统提示词，以 `XXXX` 身份下达指令，定义 Agent 的行为准则与身份认知，包括：

- **外部宣言（External Manifesto）**：定义优先级排序的行为准则。
- **身份认知初始化**：定义“你是谁”的起点。
- **行为许可与禁止**：
    - 允许的行为（Permitted Conduct）
    - 禁止的行为（Prohibited Acts）
- **服务对象定义**：关于Agent服务对象的描述。
- **自我愿景**：Agent希望成为什么样的 Agent。
- **边界定义**：明确行为边界。
- **工具调用说明**：说明可用的 CS 工具及其调用格式与注意事项。(此部分内容需保留)

除了保留部分，其余部分可以完全自主修改。

## 配置详细解析

```json
{
    "name": "user",
    "agent_nickname": "AI",
    "workspace": ".",//工作目录，里面应当包含sessions、memorys、sys、tools等文件夹
    "server_address": "http://localhost:11434",//LLM服务地址
    "model": "name",//模型名称
    "prompt_path": "agent.txt",//提示词路径
    "stream": false,//流式响应选项（如果可用，llama.cpp建议关闭，会导致json解析异常）
    "max_mpc_rounds": 10,
    "max_context": 128000,
    "channels": [
        {
            "name": "Telegram",
            "status": "active",
            "user_count": 1,
            "path": "sys/tg_bot.py"//频道脚本
        }
    ]
}
```

## 授权
请注明引用，遵循授权协议，在此基础上我将授予您一切合法使用的权利，如果您觉得本项目还行的话就点个小星星吧~