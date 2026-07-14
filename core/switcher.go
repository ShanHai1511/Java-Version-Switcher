package core

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

type SwitchResult struct {
	Success     bool   `json:"success"`
	OldHome     string `json:"old_home"`
	NewHome     string `json:"new_home"`
	PathCleaned int    `json:"path_cleaned"`
	BackupFile  string `json:"backup_file"`
	Error       string `json:"error,omitempty"`
}

func SwitchJDK(jdkPath string) *SwitchResult {
	result := &SwitchResult{
		NewHome:    jdkPath,
		BackupFile: BackupFilePath(),
	}

	oldHome, _ := ReadEnvVar("JAVA_HOME")
	result.OldHome = oldHome

	if err := BackupEnvVars(result.BackupFile); err != nil {
		result.Success = false
		result.Error = fmt.Sprintf("备份失败: %v", err)
		return result
	}

	javaExe := filepath.Join(jdkPath, "bin", "java.exe")
	if _, err := os.Stat(javaExe); os.IsNotExist(err) {
		result.Success = false
		result.Error = fmt.Sprintf("路径下未找到 bin\\java.exe: %s", jdkPath)
		return result
	}

	if err := WriteEnvVar("JAVA_HOME", jdkPath); err != nil {
		result.Error = fmt.Sprintf("写入 JAVA_HOME 失败: %v", err)
		restoreErr := RestoreEnvVars(result.BackupFile)
		if restoreErr != nil {
			result.Error += fmt.Sprintf("（回滚也失败: %v）", restoreErr)
		} else {
			result.Error += "（已自动回滚）"
		}
		result.Success = false
		return result
	}

	pathVal, err := ReadEnvVar("Path")
	if err != nil {
		result.Error = fmt.Sprintf("读取 Path 失败: %v", err)
		restoreErr := RestoreEnvVars(result.BackupFile)
		if restoreErr != nil {
			result.Error += fmt.Sprintf("（回滚也失败: %v）", restoreErr)
		} else {
			result.Error += "（已自动回滚）"
		}
		result.Success = false
		return result
	}

	cleanedPath, removed := CleanPath(pathVal, oldHome)
	result.PathCleaned = removed

	if len(cleanedPath) < 50 {
		restoreErr := RestoreEnvVars(result.BackupFile)
		result.Error = fmt.Sprintf("Path 清洗后过短(%d字符)，疑似异常，已回滚", len(cleanedPath))
		if restoreErr != nil {
			result.Error += fmt.Sprintf("（回滚也失败: %v）", restoreErr)
		}
		result.Success = false
		return result
	}

	if err := WriteEnvVar("Path", cleanedPath); err != nil {
		result.Error = fmt.Sprintf("写入 Path 失败: %v", err)
		restoreErr := RestoreEnvVars(result.BackupFile)
		if restoreErr != nil {
			result.Error += fmt.Sprintf("（回滚也失败: %v）", restoreErr)
		} else {
			result.Error += "（已自动回滚）"
		}
		result.Success = false
		return result
	}

	if err := BroadcastChanged(); err != nil {
		result.Error = fmt.Sprintf("广播失败(环境变量已修改但可能需重启): %v", err)
		result.Success = true
		return result
	}

	result.Success = true
	return result
}

func CleanPath(path string, oldJdkDir string) (string, int) {
	items := strings.Split(path, ";")
	var cleaned []string
	removed := 0

	oldJdkDirLower := strings.ToLower(oldJdkDir)

	for _, item := range items {
		trimmed := strings.TrimSpace(item)
		if trimmed == "" {
			continue
		}

		lower := strings.ToLower(trimmed)

		shouldRemove := strings.Contains(lower, `\java\`) ||
			strings.Contains(lower, `\jdk\`) ||
			strings.Contains(lower, `\jre\`) ||
			strings.Contains(lower, `%java_home%\bin`)

		if !shouldRemove && oldJdkDir != "" && strings.HasPrefix(lower, oldJdkDirLower) {
			shouldRemove = true
		}

		if shouldRemove {
			removed++
			continue
		}

		cleaned = append(cleaned, trimmed)
	}

	result := append([]string{"%JAVA_HOME%\\bin"}, cleaned...)
	return strings.Join(result, ";"), removed
}

func RestoreEnvVars(backupFile string) error {
	data, err := os.ReadFile(backupFile)
	if err != nil {
		return err
	}

	lines := strings.Split(string(data), "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, `"JAVA_HOME"=`) {
			val := extractRegValue(line)
			if val != "" {
				if err := WriteEnvVar("JAVA_HOME", val); err != nil {
					return err
				}
			}
		} else if strings.HasPrefix(line, `"Path"=`) {
			val := extractRegValue(line)
			if val != "" {
				if err := WriteEnvVar("Path", val); err != nil {
					return err
				}
			}
		}
	}
	return nil
}

func extractRegValue(line string) string {
	eqIdx := strings.IndexByte(line, '=')
	if eqIdx < 0 {
		return ""
	}

	val := line[eqIdx+1:]
	if len(val) >= 2 && val[0] == '"' && val[len(val)-1] == '"' {
		val = val[1 : len(val)-1]
	}

	val = strings.NewReplacer(`\\`, `\`, `\"`, `"`).Replace(val)
	return val
}

func WriteSwitchResult(result *SwitchResult) error {
	resultFile := ResultFilePath()
	data, err := json.Marshal(result)
	if err != nil {
		return err
	}
	return os.WriteFile(resultFile, data, 0644)
}

func ReadSwitchResult() (*SwitchResult, error) {
	data, err := os.ReadFile(ResultFilePath())
	if err != nil {
		return nil, err
	}
	var result SwitchResult
	if err := json.Unmarshal(data, &result); err != nil {
		return nil, err
	}
	return &result, nil
}

func ResultFilePath() string {
	return filepath.Join(os.Getenv("TEMP"), "jvs_switch_result.json")
}

func CleanResultFile() {
	os.Remove(ResultFilePath())
}
