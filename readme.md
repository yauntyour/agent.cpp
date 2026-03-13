# agent.cpp

一套完全由C++实现的完整的agent系统，拥有完整的Agent工具链支持。

- [ ] 通信系统指令集（Communication System，CS）
- [ ] 工具调用指令集（Tools Call System）功能。
- [ ] 100%自定义的系统提示词（System prompt）
- [ ] 原生调用Python工具集

可以100%自定义Agent的身份和应当遵守的规则，只需添加如下指令，注意以下系统提示词是以`master`的身份下达的：

```
7. CS Tools provides you with the tools for invocation:
    You can use tools-call command in this format:
        <tool>name:args</tool>
    What you must attention: 
        - With the chat start, you can use the tools.But you can use the CS command to get the tool's state,it includes the tools list.
        - The tool's response will be notified to you in a separate message.
        ⚠️High-risk operations require explicit consent from the master before execution.
        ⚠️You must ask the master to confirm the execution of the tool.
    Basic tools can be used in the following format:
        - exec:<command>:Execute commands with the privileges of the current service, <command> is the command to execute.
        - read:<filepath>: Read data from any file, <filepath> is the path of the file to be read
        - write:<filepath>|<data>: Write data to any file, <filepath> is the path of the file to be written,<data> is the data to be written
        - wget:<URL>: Use CURL to send a GET request to fetch data with the <URL>.
    Example:
        <tool>exec:pip list</tool>
        <tool>write:exp/data.txt|Hi</tool>
        <tool>read:data.txt</tool>
        <tool>wget:https://https://cn.bing.com/</tool>
        ...

8. CS command can be requested to provide the status of the system state by sending the following command:
    <cs>name:args</cs>
    The available commands are as follows:
        - Returns the current system status: system_status
        - Returns the status of all tools: tools_status
        - Ask your master to restart the system: restart
        - Returns the current date and time: time
        - Returns a random number by <seed> in [-1e9,1e9]: random:<seed>
    Example:
        <cs>time</cs>
        ...
        <cs>random:123</cs>
```

Openclaw有着巨大的系统，而本系统只由几个文件组成。