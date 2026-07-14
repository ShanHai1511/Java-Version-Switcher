package core

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"

	"golang.org/x/sys/windows/registry"
)

type JDKInfo struct {
	Version    string
	Major      int
	Vendor     string
	Path       string
	IsCurrent  bool
	IsPortable bool
	Tag        string
}

func (j *JDKInfo) DisplayName() string {
	s := fmt.Sprintf("JDK %s  %s", j.Version, j.Vendor)
	if j.Tag != "" {
		s += "  " + j.Tag
	}
	return s
}

var javaVersionRegex = regexp.MustCompile(`(?:openjdk|java)\s+version\s+"([^"]+)"`)

var vendorRegexes = []struct {
	pattern *regexp.Regexp
	vendor  string
}{
	{regexp.MustCompile(`(?i)Zulu`), "Azul Zulu"},
	{regexp.MustCompile(`(?i)Temurin|Eclipse`), "Adoptium Temurin"},
	{regexp.MustCompile(`(?i)GraalVM`), "GraalVM"},
	{regexp.MustCompile(`(?i)OpenJDK`), "OpenJDK"},
	{regexp.MustCompile(`(?i)Java\s*TM`), "Oracle"},
	{regexp.MustCompile(`(?i)Microsoft`), "Microsoft"},
	{regexp.MustCompile(`(?i)SAP`), "SAP"},
}

func ScanAll(cfg *Config) []*JDKInfo {
	var (
		mu   sync.Mutex
		jdks []*JDKInfo
		wg   sync.WaitGroup
		dirs = scanDirs()
	)

	seen := make(map[string]bool)

	addJDK := func(j *JDKInfo) {
		if j == nil {
			return
		}
		mu.Lock()
		if !seen[j.Path] {
			seen[j.Path] = true
			jdks = append(jdks, j)
		}
		mu.Unlock()
	}

	for _, dir := range dirs {
		wg.Add(1)
		go func(d string) {
			defer wg.Done()
			for _, j := range ScanDirectory(d) {
				addJDK(j)
			}
		}(dir)
	}

	wg.Add(1)
	go func() {
		defer wg.Done()
		for _, j := range ScanRegistryPath(JDKRegPath64) {
			addJDK(j)
		}
		for _, j := range ScanRegistryPath(JDKRegPath32) {
			addJDK(j)
		}
	}()

	for _, customPath := range cfg.ScanPaths {
		wg.Add(1)
		go func(p string) {
			defer wg.Done()
			for _, j := range ScanDirectory(p) {
				j.IsPortable = true
				addJDK(j)
			}
		}(customPath)
	}

	wg.Wait()

	currentHome := GetCurrentJAVA_HOME()
	for _, j := range jdks {
		if strings.EqualFold(j.Path, currentHome) {
			j.IsCurrent = true
			break
		}
	}

	if lastUsed := cfg.LastUsed; lastUsed != "" {
		found := false
		for _, j := range jdks {
			if strings.EqualFold(j.Path, lastUsed) {
				found = true
				break
			}
		}
		if !found {
			jdks = append(jdks, ScanDirectory(lastUsed)...)
		}
	}

	for _, j := range jdks {
		j.Tag = DetermineTag(j.Major)
	}

	jdks = Deduplicate(jdks)
	sort.Slice(jdks, func(i, j int) bool {
		return jdks[i].Major > jdks[j].Major
	})

	return jdks
}

func scanDirs() []string {
	var dirs []string
	programFiles := os.Getenv("ProgramFiles")
	programFilesX86 := os.Getenv("ProgramFiles(x86)")

	javaDir := filepath.Join(programFiles, "Java")
	if _, err := os.Stat(javaDir); err == nil {
		dirs = append(dirs, javaDir)
	}
	if programFilesX86 != "" {
		javaDir32 := filepath.Join(programFilesX86, "Java")
		if _, err := os.Stat(javaDir32); err == nil {
			dirs = append(dirs, javaDir32)
		}
	}

	jvsDir := filepath.Join(os.Getenv("USERPROFILE"), ".jvs", "jdk")
	if _, err := os.Stat(jvsDir); err == nil {
		dirs = append(dirs, jvsDir)
	}

	return dirs
}

func ScanDirectory(root string) []*JDKInfo {
	entries, err := os.ReadDir(root)
	if err != nil {
		return nil
	}

	var jdks []*JDKInfo
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		jdkPath := filepath.Join(root, entry.Name())
		j := validateJDK(jdkPath)
		if j != nil {
			jdks = append(jdks, j)
		}
	}
	return jdks
}

func ScanRegistryPath(regPath string) []*JDKInfo {
	names, err := ListSubKeys(registry.LOCAL_MACHINE, regPath)
	if err != nil {
		return nil
	}

	var jdks []*JDKInfo
	for _, name := range names {
		subKey := regPath + `\` + name
		javaHome, err := ReadRegKey(registry.LOCAL_MACHINE, subKey, "JavaHome")
		if err != nil {
			continue
		}
		if j := validateJDK(javaHome); j != nil {
			j.Version = name
			jdks = append(jdks, j)
		}
	}
	return jdks
}

func validateJDK(jdkPath string) *JDKInfo {
	javaExe := filepath.Join(jdkPath, "bin", "java.exe")
	if _, err := os.Stat(javaExe); os.IsNotExist(err) {
		return nil
	}

	version, vendor := ResolveVersion(javaExe)
	if version == "" {
		return nil
	}

	major := ParseMajor(version)

	return &JDKInfo{
		Version: version,
		Major:   major,
		Vendor:  vendor,
		Path:    jdkPath,
	}
}

func ResolveVersion(javaExe string) (string, string) {
	cmd := exec.Command(javaExe, "-version")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", ""
	}

	outStr := string(output)

	matches := javaVersionRegex.FindStringSubmatch(outStr)
	if len(matches) < 2 {
		return "", ""
	}

	version := matches[1]

	vendor := "Unknown"
	for _, vr := range vendorRegexes {
		if vr.pattern.MatchString(outStr) {
			vendor = vr.vendor
			break
		}
	}

	return version, vendor
}

func ParseMajor(version string) int {
	parts := strings.Split(version, ".")
	if len(parts) == 0 {
		return 0
	}

	if parts[0] == "1" && len(parts) > 1 {
		major, err := strconv.Atoi(parts[1])
		if err == nil {
			return major
		}
		return 0
	}

	major, err := strconv.Atoi(parts[0])
	if err != nil {
		return 0
	}
	return major
}

func Deduplicate(list []*JDKInfo) []*JDKInfo {
	seen := make(map[string]*JDKInfo)
	for _, j := range list {
		key := strings.ToLower(j.Path)
		if existing, ok := seen[key]; ok {
			if existing.Version == "" {
				seen[key] = j
			} else if j.Major > existing.Major {
				seen[key] = j
			}
		} else {
			seen[key] = j
		}
	}

	result := make([]*JDKInfo, 0, len(seen))
	for _, j := range seen {
		result = append(result, j)
	}
	return result
}

func DetermineTag(major int) string {
	switch major {
	case 8:
		return "[Minecraft 1.12-]"
	case 17:
		return "[Minecraft 1.18-1.20]"
	case 21:
		return "[Minecraft 1.21+]"
	default:
		return ""
	}
}
