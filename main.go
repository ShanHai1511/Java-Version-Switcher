package main

import (
	"fmt"
	"os"
	"strings"
	"syscall"
	"unsafe"

	"jvs/core"
	"jvs/gui"
)

var Version = "1.0.0"

func main() {
	if len(os.Args) > 1 && os.Args[1] == "--switch" {
		handleSubprocess()
		return
	}

	if len(os.Args) > 1 && os.Args[1] == "--version" {
		fmt.Println("Java Version Switcher", Version)
		os.Exit(0)
	}

	cfgPath := core.ConfigPath()
	cfg, err := core.LoadConfig(cfgPath)
	if err != nil {
		showFatalError(fmt.Sprintf("加载配置失败: %v", err))
		return
	}

	_ = cfg // unused for now, gui.Run will use it
	err = gui.Run(cfg)
	if err != nil {
		showFatalError(fmt.Sprintf("GUI 启动失败: %v", err))
	}
}

func handleSubprocess() {
	jdkPath := ""

	for i := 2; i < len(os.Args); i++ {
		arg := os.Args[i]
		if strings.HasPrefix(arg, "--") {
			if arg == "--backup" || arg == "--backup-file" {
				i++
			}
			continue
		}
		if jdkPath == "" {
			jdkPath = arg
		}
	}

	if jdkPath == "" {
		result := &core.SwitchResult{
			Success: false,
			Error:   "缺少 JDK 路径参数",
		}
		core.WriteSwitchResult(result)
		os.Exit(1)
	}

	result := core.SwitchJDK(jdkPath)

	if writeErr := core.WriteSwitchResult(result); writeErr != nil {
		result2 := &core.SwitchResult{
			Success: false,
			Error:   fmt.Sprintf("写入结果文件失败: %v", writeErr),
		}
		core.WriteSwitchResult(result2)
		os.Exit(1)
	}

	if result.Success {
		os.Exit(0)
	}
	os.Exit(1)
}

func showFatalError(msg string) {
	user32 := syscall.NewLazyDLL("user32.dll")
	procMessageBoxW := user32.NewProc("MessageBoxW")
	procMessageBoxW.Call(
		0,
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(msg))),
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr("Java Version Switcher - 错误"))),
		0x00000010,
	)
}
