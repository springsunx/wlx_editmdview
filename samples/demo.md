# EditMdView

这是一个面向 Total Commander 的轻量 Markdown 编辑与预览插件。

## 第一版能力

- [x] Scintilla 编辑器
- [x] Markdown 源码高亮
- [x] 左右分栏实时预览
- [x] UTF-8、UTF-16 和 ANSI 检测
- [x] 保留原换行格式
- [x] `Ctrl+S` 安全保存

> 左侧编辑文本，右侧预览会自动刷新。

| 快捷键 | 功能 |
|---|---|
| `Ctrl+S` | 保存 |
| `Ctrl+F` | 聚焦查找框 |
| `Ctrl+M` | 编辑/分栏切换 |
| `F3` | 查找下一个 |

```cpp
#include <iostream>

int main() {
    std::cout << "Hello, Total Commander!\n";
}
```
