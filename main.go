package main

import (
	"fmt"
	"os"
	"strings"

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
		fmt.Fprintf(os.Stderr, "加载配置失败: %v\n", err)
		os.Exit(1)
	}

	if err := gui.Run(cfg); err != nil {
		fmt.Fprintf(os.Stderr, "GUI 启动失败: %v\n", err)
		os.Exit(1)
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
