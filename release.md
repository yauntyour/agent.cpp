# 更新内容

1. 修复WebUI的CSS兼容和显示错误，优化页面布局
2. 频道设置单独抽离作为子页面
3. 数据统计从usage中抽离
4. 修复渲染bug

# 从源码构建

依赖需求：

```bash
#Linux：
sudo apt-get install -y libcurl4-openssl-dev libboost-dev

#Windows （msys2）：
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-curl mingw-w64-x86_64-boost

#MacOS：
brew install cmake ninja curl boost
```

懒狗安装指令：

```bash
rm -rf ./* && git clone --recurse-submodules https://github.com/yauntyour/agent.cpp.git && cd agent.cpp && mkdir build && cd build && cmake .. && cmake --build build && cmake --install build --prefix install
```

# 从release下载
