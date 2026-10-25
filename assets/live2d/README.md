# assets/live2d — 自带的默认模型

第一次启动时，程序会把 `models/` 这一份解到数据根的 `.agent/live2d/models/`；
已经在用别的模型就一个字节都不动。解出来的文件可以随便改、随便换。

## 里面是什么

| 路径 | 说明 |
| --- | --- |
| `models/hiyori/` | Live2D 官方样例模型 Hiyori（`Hiyori.model3.json` + moc3 + 贴图 + 动作 + 物理） |

运行时核心（Pixi / Cubism Core / pixi-live2d-display）**不在这里**：Cubism Core
的许可不允许随程序再分发。界面的舞台面板里有「下载运行时」，点一下会取回
`.agent/live2d/runtime/`；不想下载也可以在面板里打开「运行时走 CDN」。

## 换默认模型

把新的模型目录放进 `models/`，然后改 `src/agent-core.hpp` 里的
`stage_default_model()`（默认值 `models/hiyori/Hiyori.model3.json`）。
`build_live2d.py` 会把整个 `assets/live2d/` 用 C++26 `#embed` 打进 exe，
所以不需要额外的文件依赖；换模型后重新编译即可。

## 许可

`models/hiyori/` 来自 Live2D 官方示例仓库
[Live2D/CubismWebSamples](https://github.com/Live2D/CubismWebSamples)，
受 [Live2D Free Material License](https://www.live2d.com/eula/live2d-free-material-license-agreement/)
约束，**不在本项目的 MIT 许可范围内**。项目其余部分仍是 MIT。
如果你要重新分发这个仓库，请自行确认该许可是否适合你的用途；
不需要自带模型的话，直接删掉 `models/` 目录即可（程序会退回「没有模型」状态）。
