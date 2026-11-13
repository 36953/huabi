# 36953画笔 · 上课全屏标注（C++ / Win32）

上课时临时在屏幕上画重点：程序常驻托盘，按 **Pause** 把当前画面定格下来涂画，再按 Pause 或 Esc 结束。笔迹只画在定格画面上，**不会写进 CAD**。

[![Release](https://img.shields.io/github/v/release/36953/huabi?label=%E4%B8%8B%E8%BD%BD&color=B82222)](https://github.com/36953/huabi/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%2010%20%2F%2011-lightgrey)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C)

- **托盘常驻**：启动后在系统托盘，不挡课
- **定格涂画**：Pause 截屏叠加标注，底层软件不受影响
- **常用工具**：笔、直线、矩形、荧光笔、橡皮；粗细与颜色可调
- **撤销 / 重做 / 清屏 / 保存**：标注可存成图片
- **无运行时依赖**：单个 EXE，双击即用

下载：[Releases 最新版](https://github.com/36953/huabi/releases/latest) → `HuaBi.exe`（v1.0，约 830 KB，单文件免安装）

---

## 一、使用

1. 双击 `HuaBi.exe`（或「36953画笔」快捷方式）启动，图标出现在托盘。
2. 讲到需要标注时按 **Pause**（可在托盘菜单改快捷键）。
3. 在定格画面上画笔迹；右键或工具栏可切换颜色、粗细、工具。
4. 再按 **Pause** 或 **Esc** 结束标注，回到正常桌面。
5. 托盘右键可退出、查看帮助、固定图钉等。

偏好（快捷键、颜色、工具、线宽）保存在程序目录旁的 `huabi.ini`。

---

## 二、从源码编译

环境任选其一：

- **MSVC**：已安装 Visual Studio C++ 工具链
- **MinGW / MSYS2**：`ucrt64` 的 `g++`（`build.bat` 会优先找 `C:\msys64\ucrt64\bin`）

在项目根目录双击或运行：

```bat
build.bat
```

成功后生成 `HuaBi.exe`，并自动创建「36953画笔」快捷方式。

手动编译（GCC 示例）：

```bat
g++ -std=c++17 -O2 -Wall -municode -mwindows -static -finput-charset=UTF-8 -fexec-charset=UTF-8 -o HuaBi.exe src\main.cpp -lgdiplus -lole32 -lshell32 -lshlwapi -luuid -lgdi32 -luser32
```

---

## 三、目录

| 路径 | 说明 |
| --- | --- |
| `src/main.cpp` | 全部源码（Win32 + GDI+） |
| `build.bat` | 一键编译（MSVC / g++） |
| `make_shortcut.ps1` | 生成桌面旁快捷方式 |
| `LICENSE` | MIT |

---

## License

MIT © 36953
