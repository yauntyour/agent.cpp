# 更新内容

1. 修复todos.json匹配错误的问题。

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
