package core

import (
	"archive/zip"
	"compress/gzip"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

func GetDownloadURL(version string, mirror string) string {
	mirror = strings.TrimRight(mirror, "/")
	version = strings.ReplaceAll(version, "+", "%2B")
	return fmt.Sprintf("%s/%s/jdk-%s_windows-x64_bin.zip", mirror, version, version)
}

func ListAvailableVersions() []string {
	return []string{"8", "11", "17", "21", "22", "23"}
}

type DownloadProgress struct {
	Downloaded int64
	Total      int64
	Done       bool
	Error      error
}

func DownloadJDK(version, mirror string, progressCh chan<- DownloadProgress) (string, error) {
	url := GetDownloadURL(version, mirror)

	cacheDir := filepath.Join(os.Getenv("USERPROFILE"), ".jvs", "jdk")
	if err := os.MkdirAll(cacheDir, 0755); err != nil {
		return "", fmt.Errorf("创建缓存目录失败: %w", err)
	}

	zipPath := filepath.Join(cacheDir, fmt.Sprintf("jdk-%s-windows-x64.zip", version))

	client := &http.Client{Timeout: 30 * time.Minute}

	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return "", fmt.Errorf("创建请求失败: %w", err)
	}

	req.Header.Set("User-Agent", "JVS/2.0")

	if fi, err := os.Stat(zipPath); err == nil && fi.Size() > 0 {
		req.Header.Set("Range", fmt.Sprintf("bytes=%d-", fi.Size()))
	}

	resp, err := client.Do(req)
	if err != nil {
		return "", fmt.Errorf("下载失败: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK && resp.StatusCode != http.StatusPartialContent {
		return "", fmt.Errorf("服务器返回 %s", resp.Status)
	}

	total := resp.ContentLength
	if resp.StatusCode == http.StatusPartialContent {
		if fi, _ := os.Stat(zipPath); fi != nil {
			total += fi.Size()
		}
	}

	var outFile *os.File
	if resp.StatusCode == http.StatusPartialContent {
		outFile, err = os.OpenFile(zipPath, os.O_APPEND|os.O_WRONLY, 0644)
	} else {
		outFile, err = os.Create(zipPath)
	}
	if err != nil {
		return "", fmt.Errorf("创建文件失败: %w", err)
	}
	defer outFile.Close()

	var written int64
	buf := make([]byte, 32*1024)
	lastReport := time.Now()

	for {
		nr, er := resp.Body.Read(buf)
		if nr > 0 {
			nw, ew := outFile.Write(buf[:nr])
			if nw != nr {
				return "", fmt.Errorf("写入不完整")
			}
			if ew != nil {
				return "", ew
			}
			written += int64(nw)

			if time.Since(lastReport) > 200*time.Millisecond && progressCh != nil {
				progressCh <- DownloadProgress{
					Downloaded: written,
					Total:      total,
				}
				lastReport = time.Now()
			}
		}
		if er != nil {
			if er != io.EOF {
				return "", fmt.Errorf("下载中断: %w", er)
			}
			break
		}
	}

	outFile.Close()

	if progressCh != nil {
		progressCh <- DownloadProgress{Done: true, Total: total}
	}

	if strings.HasSuffix(strings.ToLower(zipPath), ".zip") {
		extractDir := filepath.Join(cacheDir, version)
		if err := ExtractZip(zipPath, extractDir); err != nil {
			return "", fmt.Errorf("解压失败: %w", err)
		}
		return extractDir, nil
	}

	return zipPath, nil
}

func ExtractZip(src, dest string) error {
	r, err := zip.OpenReader(src)
	if err != nil {
		return err
	}
	defer r.Close()

	if err := os.MkdirAll(dest, 0755); err != nil {
		return err
	}

	for _, f := range r.File {
		fpath := filepath.Join(dest, f.Name)
		if f.FileInfo().IsDir() {
			os.MkdirAll(fpath, 0755)
			continue
		}

		if err := os.MkdirAll(filepath.Dir(fpath), 0755); err != nil {
			return err
		}

		rc, err := f.Open()
		if err != nil {
			return err
		}

		outFile, err := os.OpenFile(fpath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, f.Mode())
		if err != nil {
			rc.Close()
			return err
		}

		_, err = io.Copy(outFile, rc)
		rc.Close()
		outFile.Close()
		if err != nil {
			return err
		}
	}

	entries, _ := os.ReadDir(dest)
	if len(entries) == 1 && entries[0].IsDir() {
		innerDir := filepath.Join(dest, entries[0].Name())
		tempDest := dest + "_tmp"
		os.Rename(innerDir, tempDest)
		os.RemoveAll(dest)
		os.Rename(tempDest, dest)
	}

	return nil
}

func ExtractTarGz(src, dest string) error {
	f, err := os.Open(src)
	if err != nil {
		return err
	}
	defer f.Close()

	gzr, err := gzip.NewReader(f)
	if err != nil {
		return err
	}
	defer gzr.Close()

	if _, err := io.Copy(io.Discard, gzr); err != nil {
		return err
	}

	return nil
}
