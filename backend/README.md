# TinyWebChat Backend

本目录为后端分层结构入口。

## 构建

```bash
cd backend
cmake -S . -B build
cmake --build build -j
```

产物路径：

```bash
./build/server
```

当前为平滑迁移阶段：`backend/src` 使用符号链接映射现有稳定代码，后续可逐步迁移为真实文件。