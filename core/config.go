package core

import (
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

type Config struct {
	ScanPaths      []string
	LastUsed       string
	Mirror         string
	AutoExtract    bool
	AlwaysOnTop    bool
	StartMinimized bool
}

func DefaultConfig() *Config {
	return &Config{
		ScanPaths:   []string{},
		LastUsed:    "",
		Mirror:      "https://repo.huaweicloud.com/java/jdk/",
		AutoExtract: true,
	}
}

func ConfigPath() string {
	appData := os.Getenv("APPDATA")
	if appData == "" {
		appData = filepath.Join(os.Getenv("USERPROFILE"), "AppData", "Roaming")
	}
	return filepath.Join(appData, "JVS", "jvs.ini")
}

func ConfigDir() string {
	return filepath.Dir(ConfigPath())
}

func LoadConfig(path string) (*Config, error) {
	cfg := DefaultConfig()
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return cfg, nil
		}
		return nil, err
	}
	parseINI(string(data), cfg)
	return cfg, nil
}

func SaveConfig(cfg *Config, path string) error {
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}
	return os.WriteFile(path, []byte(writeINI(cfg)), 0644)
}

func parseINI(data string, cfg *Config) {
	lines := strings.Split(data, "\n")
	var section string

	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, ";") || strings.HasPrefix(line, "#") {
			continue
		}
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			section = strings.ToLower(line[1 : len(line)-1])
			continue
		}

		eqIdx := strings.IndexByte(line, '=')
		if eqIdx < 0 {
			continue
		}
		key := strings.TrimSpace(strings.ToLower(line[:eqIdx]))
		val := strings.TrimSpace(line[eqIdx+1:])

		switch section {
		case "jdk":
			switch {
			case strings.HasPrefix(key, "scan_paths_"):
				cfg.ScanPaths = append(cfg.ScanPaths, val)
			case key == "last_used":
				cfg.LastUsed = val
			}
		case "download":
			switch key {
			case "mirror":
				cfg.Mirror = val
			case "auto_extract":
				cfg.AutoExtract = val == "true" || val == "1" || val == "yes"
			}
		case "ui":
			switch key {
			case "always_on_top":
				cfg.AlwaysOnTop = val == "true"
			case "start_minimized":
				cfg.StartMinimized = val == "true"
			}
		}
	}

	sort.Strings(cfg.ScanPaths)
}

func writeINI(cfg *Config) string {
	var b strings.Builder

	b.WriteString("[JDK]\n")
	for i, p := range cfg.ScanPaths {
		b.WriteString("scan_paths_")
		b.WriteString(strconv.Itoa(i))
		b.WriteString(" = ")
		b.WriteString(p)
		b.WriteByte('\n')
	}
	if cfg.LastUsed != "" {
		b.WriteString("last_used = ")
		b.WriteString(cfg.LastUsed)
		b.WriteByte('\n')
	}

	b.WriteString("\n[Download]\n")
	b.WriteString("mirror = ")
	b.WriteString(cfg.Mirror)
	b.WriteByte('\n')
	b.WriteString("auto_extract = ")
	b.WriteString(strconv.FormatBool(cfg.AutoExtract))
	b.WriteByte('\n')

	b.WriteString("\n[UI]\n")
	b.WriteString("always_on_top = ")
	b.WriteString(strconv.FormatBool(cfg.AlwaysOnTop))
	b.WriteByte('\n')
	b.WriteString("start_minimized = ")
	b.WriteString(strconv.FormatBool(cfg.StartMinimized))
	b.WriteByte('\n')

	return b.String()
}

func (c *Config) UpdateLastUsed(path string) error {
	c.LastUsed = path
	return SaveConfig(c, ConfigPath())
}

func (c *Config) AddScanPath(path string) error {
	path = filepath.Clean(path)
	for _, p := range c.ScanPaths {
		if strings.EqualFold(p, path) {
			return nil
		}
	}
	c.ScanPaths = append(c.ScanPaths, path)
	sort.Strings(c.ScanPaths)
	return SaveConfig(c, ConfigPath())
}

func (c *Config) RemoveScanPath(path string) error {
	path = filepath.Clean(path)
	filtered := make([]string, 0, len(c.ScanPaths))
	for _, p := range c.ScanPaths {
		if !strings.EqualFold(p, path) {
			filtered = append(filtered, p)
		}
	}
	c.ScanPaths = filtered
	return SaveConfig(c, ConfigPath())
}
