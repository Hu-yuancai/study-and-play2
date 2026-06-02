Import("env")

# 工程路径含中文(非ASCII)时, Windows 下 ESP32 链接器(ld.exe)无法创建 firmware.map,
# 导致链接失败。这里在链接前移除 -Wl,-Map=... 选项(map 文件仅用于分析, 不影响固件)。
# 必须用 post: 时机加载本脚本, 因为 -Map 选项是 framework 构建脚本添加的,
# pre: 时机运行时该选项尚不存在。

def _strip(flags):
    cleaned = []
    skip_next = False
    for f in flags:
        s = str(f)
        if skip_next:
            skip_next = False
            continue
        if "-Map" in s:
            # 形如 -Wl,-Map=... 或单独的 -Map (后跟路径)
            if s in ("-Map", "-Wl,-Map") :
                skip_next = True
            continue
        cleaned.append(f)
    return cleaned

env.Replace(LINKFLAGS=_strip(env.get("LINKFLAGS", [])))
env.Replace(_LDFLAGS=env.get("_LDFLAGS", ""))
