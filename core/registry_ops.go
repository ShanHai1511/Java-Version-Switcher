package core

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
	"unsafe"

	"golang.org/x/sys/windows"
	"golang.org/x/sys/windows/registry"
)

const (
	EnvRegPath  = `SYSTEM\CurrentControlSet\Control\Session Manager\Environment`
	JDKRegPath64 = `SOFTWARE\JavaSoft\JDK`
	JDKRegPath32 = `SOFTWARE\WOW6432Node\JavaSoft\JDK`
)

func escapeRegStr(s string) string {
	return strings.NewReplacer(`\`, `\\`, `"`, `\"`).Replace(s)
}

func ReadEnvVar(name string) (string, error) {
	k, err := registry.OpenKey(registry.LOCAL_MACHINE, EnvRegPath, registry.QUERY_VALUE)
	if err != nil {
		return "", fmt.Errorf("打开注册表失败: %w", err)
	}
	defer k.Close()

	val, _, err := k.GetStringValue(name)
	if err != nil {
		if err == registry.ErrNotExist {
			return "", nil
		}
		return "", fmt.Errorf("读取 %s 失败: %w", name, err)
	}
	return val, nil
}

func WriteEnvVar(name, value string) error {
	k, err := registry.OpenKey(registry.LOCAL_MACHINE, EnvRegPath, registry.SET_VALUE)
	if err != nil {
		return fmt.Errorf("打开注册表写入失败: %w", err)
	}
	defer k.Close()

	if err := k.SetStringValue(name, value); err != nil {
		return fmt.Errorf("写入 %s 失败: %w", name, err)
	}
	return nil
}

func DeleteEnvVar(name string) error {
	k, err := registry.OpenKey(registry.LOCAL_MACHINE, EnvRegPath, registry.SET_VALUE)
	if err != nil {
		return fmt.Errorf("打开注册表失败: %w", err)
	}
	defer k.Close()

	if err := k.DeleteValue(name); err != nil && err != registry.ErrNotExist {
		return fmt.Errorf("删除 %s 失败: %w", name, err)
	}
	return nil
}

func ReadRegKey(root registry.Key, path, name string) (string, error) {
	k, err := registry.OpenKey(root, path, registry.QUERY_VALUE)
	if err != nil {
		return "", err
	}
	defer k.Close()

	val, _, err := k.GetStringValue(name)
	if err != nil {
		return "", err
	}
	return val, nil
}

func ListSubKeys(root registry.Key, path string) ([]string, error) {
	k, err := registry.OpenKey(root, path, registry.ENUMERATE_SUB_KEYS)
	if err != nil {
		return nil, err
	}
	defer k.Close()

	var names []string
	for i := 0; ; i++ {
		name, err := k.ReadSubKeyNames(i)
		if err != nil {
			break
		}
		if len(name) > 0 {
			names = append(names, name[0])
		}
	}
	return names, nil
}

func GetCurrentJAVA_HOME() string {
	val, _ := ReadEnvVar("JAVA_HOME")
	return val
}

func BroadcastChanged() error {
	mod := windows.NewLazySystemDLL("user32.dll")
	proc := mod.NewProc("SendMessageTimeoutW")

	HWND_BROADCAST := uintptr(0xFFFF)
	WM_SETTINGCHANGE := uint32(0x001A)
	SMTO_ABORTIFHUNG := uint32(0x0002)
	timeout := uint32(5000)

	envStr, _ := windows.UTF16PtrFromString("Environment")

	ret, _, err := proc.Call(
		HWND_BROADCAST,
		uintptr(WM_SETTINGCHANGE),
		0,
		uintptr(unsafe.Pointer(envStr)),
		uintptr(SMTO_ABORTIFHUNG),
		uintptr(timeout),
		0,
	)
	if ret == 0 {
		return fmt.Errorf("广播 WM_SETTINGCHANGE 失败: %v", err)
	}
	return nil
}

func BackupEnvVars(backupFile string) error {
	javaHome, _ := ReadEnvVar("JAVA_HOME")
	path, _ := ReadEnvVar("Path")

	var b strings.Builder
	b.WriteString("Windows Registry Editor Version 5.00\n\n")
	b.WriteString("[HKEY_LOCAL_MACHINE\\")
	b.WriteString(strings.ReplaceAll(EnvRegPath, "\\", "\\\\"))
	b.WriteString("]\n")

	if javaHome != "" {
		b.WriteString(fmt.Sprintf("\"JAVA_HOME\"=\"%s\"\n", escapeRegStr(javaHome)))
	}
	if path != "" {
		b.WriteString(fmt.Sprintf("\"Path\"=\"%s\"\n", escapeRegStr(path)))
	}

	if err := os.MkdirAll(filepath.Dir(backupFile), 0755); err != nil {
		return fmt.Errorf("创建备份目录失败: %w", err)
	}
	return os.WriteFile(backupFile, []byte(b.String()), 0644)
}

func BackupFilePath() string {
	appData := os.Getenv("APPDATA")
	if appData == "" {
		appData = filepath.Join(os.Getenv("USERPROFILE"), "AppData", "Roaming")
	}
	backupDir := filepath.Join(appData, "JVS", "backup")
	if err := os.MkdirAll(backupDir, 0755); err != nil {
		// 返回默认 TEMP 路径作为降级方案
		return filepath.Join(os.Getenv("TEMP"), "jvs_backup_"+time.Now().Format("20060102_150405")+".reg")
	}
	return filepath.Join(backupDir, time.Now().Format("20060102_150405")+".reg")
}
