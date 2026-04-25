# image-drawer：调用sd-cli服务生成图像的命令行工具
## Tool Command
```
<tool>image-drawer:--prompt "<Your Prompt>" --output <filename> --steps 9 --height <H> --width <W> --negative-prompt "<Negative Prompt>"</tool>
```
## Parameters
- `--steps`: 较高的数字通常意味着更好的质量，但生成时间更长（默认 9）。推荐 9~12，不超过 20。
- `--output`: 图像将保存在当前目录下的 `assets/` 文件夹中，例如 `--output cat.png` → `./assets/cat.png`。默认 `output.png`。
- `--height` / `--width`: 图像尺寸（默认使用服务端设定）。
- `--negative-prompt`: 负提示词，会被追加到服务端默认负提示词后。
图像数据由服务端生成后以 base64 返回，工具直接解码保存，不再依赖服务端文件路径。