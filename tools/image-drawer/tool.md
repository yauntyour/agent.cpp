# image-drawer：调用sd-cli服务生成图像的命令行工具
## Tool Command
```
<tool>image-drawer:--prompt "<Your Prompt>" --output <filename> --steps 9 --height <H> --width <W> --negative-prompt "<Negative Prompt>"</tool>
```
## Parameters
`--steps`: 较高的数字通常意味着更好的质量，但生成时间更长（默认值为服务默认），由于Z-image模型的强大功能，9-12即可，不超过20
`--output`: 系统会把文件输出到assets目录中，请注意
