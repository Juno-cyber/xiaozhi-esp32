# 适配墨水屏版本
# 终端手动进入环境：
`C:\WINDOWS/System32/WindowsPowerShell/v1.0/powershell.exe -ExecutionPolicy Bypass -NoExit -File D:\ProgramData\Espressif/Initialize-Idf.ps1 -IdfId esp-idf-a3f2773434c3337a74b2bc7fd15f2299`

# 编译配置命令

**配置编译目标为 ESP32S3：**

```bash
idf.py set-target esp32s3
```

**打开 menuconfig：**

```bash
idf.py menuconfig
```

**选择板子：**

```
Xiaozhi Assistant -> Board Type -> mu_epaper_esp32
```


**编译：**

```bash
idf.py build
```