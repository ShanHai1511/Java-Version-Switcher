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
	{regexp.MustCompile(`(?i)Temurin|Eclipse Foundation|AdoptOpenJDK`), "Adoptium"},
	{regexp.MustCompile(`(?i)GraalVM`), "GraalVM"},
	{regexp.MustCompile(`(?i)OpenJDK`), "OpenJDK"},
	{regexp.MustCompile(`(?i)Java\s*\(?\s*TM`), "Oracle"},
	{regexp.MustCompile(`(?i)Microsoft`), "Microsoft"},
	{regexp.MustCompile(`(?i)SAP`), "SAP"},
	{regexp.MustCompile(`(?i)Liberica|BellSoft`), "Liberica"},
	{regexp.MustCompile(`(?i)Corretto`), "Amazon Corretto"},
	{regexp.MustCompile(`(?i)Dragonwell`), "Alibaba Dragonwell"},
	{regexp.MustCompile(`HotSpot`), "Oracle HotSpot"},
}

func ScanAll(cfg *Config) []*JDKInfo {
	var (
		mu   sync.Mutex
		jdks []*JDKInfo
		wg   sync.WaitGroup
		dirs = scanDirs()
	)
	// 对 cfg.ScanPaths 做快照，避免 GUI 线程 AddScanPath 修改时引发 Data Race
	scanPaths := make([]string, len(cfg.ScanPaths))
	copy(scanPaths, cfg.ScanPaths)

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

	for _, customPath := range scanPaths {
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

	jdks = filterNestedJRE(jdks)
	jdks = Deduplicate(jdks)
	sort.Slice(jdks, func(i, j int) bool {
		return jdks[i].Major > jdks[j].Major
	})

	return jdks
}

func scanDirs() []string {
	dirSet := make(map[string]bool)

	addIfExists := func(path string) {
		if info, err := os.Stat(path); err == nil && info.IsDir() {
			dirSet[path] = true
		}
	}

	// Standard Java installation paths
	programFiles := os.Getenv("ProgramFiles")
	programFilesX86 := os.Getenv("ProgramFiles(x86)")
	userProfile := os.Getenv("USERPROFILE")

	addIfExists(filepath.Join(programFiles, "Java"))
	if programFilesX86 != "" {
		addIfExists(filepath.Join(programFilesX86, "Java"))
	}

	// Common JDK vendors
	addIfExists(filepath.Join(programFiles, "Eclipse Adoptium"))
	addIfExists(filepath.Join(programFiles, "Eclipse Foundation"))
	addIfExists(filepath.Join(programFiles, "Amazon Corretto"))
	addIfExists(filepath.Join(programFiles, "Microsoft"))
	addIfExists(filepath.Join(programFiles, "BellSoft"))
	addIfExists(filepath.Join(programFiles, "Liberica JDK"))
	addIfExists(filepath.Join(programFiles, "GraalVM"))

	addIfExists(filepath.Join(programFilesX86, "Eclipse Adoptium"))
	addIfExists(filepath.Join(programFilesX86, "Amazon Corretto"))
	addIfExists(filepath.Join(programFilesX86, "Microsoft"))

	// JVS download cache
	addIfExists(filepath.Join(userProfile, ".jvs", "jdk"))

	// Current JAVA_HOME (if set and points to a different location)
	javaHome := GetCurrentJAVA_HOME()
	if javaHome != "" {
		parent := filepath.Dir(javaHome)
		if parent != "." && parent != string(filepath.Separator) {
			addIfExists(parent)
		}
		addIfExists(javaHome)
	}

	// Scoop-managed JDK
	scoopDir := filepath.Join(userProfile, "scoop", "apps")
	if scoopApps, err := os.ReadDir(scoopDir); err == nil {
		for _, app := range scoopApps {
			if app.IsDir() && strings.Contains(strings.ToLower(app.Name()), "jdk") {
				addIfExists(filepath.Join(scoopDir, app.Name(), "current"))
			}
		}
	}

	result := make([]string, 0, len(dirSet))
	for d := range dirSet {
		result = append(result, d)
	}
	return result
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
	javacExe := filepath.Join(jdkPath, "bin", "javac.exe")
	if _, err := os.Stat(javaExe); os.IsNotExist(err) {
		return nil
	}
	// Skip JRE-only installations (no javac = not a JDK)
	if _, err := os.Stat(javacExe); os.IsNotExist(err) {
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

// filterNestedJRE 过滤嵌套在 JDK 内部的 JRE 目录，只保留完整 JDK。
// 逻辑：若某条目的路径以 \jre 或 \jre\ 结尾/包含，且存在一个同级的 JDK 条目为其父目录，则跳过该 JRE。
// 注意：此函数仅匹配 \jre 命名，Android NDK 等非标准 JRE 布局不在本函数覆盖范围内。
func filterNestedJRE(list []*JDKInfo) []*JDKInfo {
	var result []*JDKInfo
outer:
	for _, j := range list {
		lower := strings.ToLower(j.Path)
		// Skip if this is a JRE directory
		if strings.HasSuffix(lower, `\jre`) || strings.Contains(lower, `\jre\`) {
			// Check if any ancestor is also in the list as a JDK
			parent := filepath.Dir(j.Path)
			for _, other := range list {
				if other == j {
					continue
				}
				if strings.EqualFold(other.Path, parent) {
					continue outer // Skip this nested JRE
				}
			}
		}
		result = append(result, j)
	}
	return result
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
