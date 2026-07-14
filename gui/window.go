package gui

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"sync"
	"syscall"
	"time"
	"unsafe"

	"jvs/core"
)

var (
	user32                    = syscall.NewLazyDLL("user32.dll")
	kernel32                  = syscall.NewLazyDLL("kernel32.dll")
	comctl32                  = syscall.NewLazyDLL("comctl32.dll")
	shell32                   = syscall.NewLazyDLL("shell32.dll")
	procCreateWindowExW       = user32.NewProc("CreateWindowExW")
	procDefWindowProcW        = user32.NewProc("DefWindowProcW")
	procRegisterClassExW      = user32.NewProc("RegisterClassExW")
	procGetMessageW           = user32.NewProc("GetMessageW")
	procDispatchMessageW      = user32.NewProc("DispatchMessageW")
	procTranslateMessage      = user32.NewProc("TranslateMessage")
	procPostQuitMessage       = user32.NewProc("PostQuitMessage")
	procShowWindow            = user32.NewProc("ShowWindow")
	procUpdateWindow          = user32.NewProc("UpdateWindow")
	procLoadCursorW           = user32.NewProc("LoadCursorW")
	procLoadIconW             = user32.NewProc("LoadIconW")
	procMessageBoxW           = user32.NewProc("MessageBoxW")
	procSendMessageW          = user32.NewProc("SendMessageW")
	procSetWindowTextW        = user32.NewProc("SetWindowTextW")
	procDestroyWindow         = user32.NewProc("DestroyWindow")
	procEnableWindow          = user32.NewProc("EnableWindow")
	procSetForegroundWindow   = user32.NewProc("SetForegroundWindow")
	procInitCommonControlsEx  = comctl32.NewProc("InitCommonControlsEx")
	procSHBrowseForFolderW    = shell32.NewProc("SHBrowseForFolderW")
	procSHGetPathFromIDListW  = shell32.NewProc("SHGetPathFromIDListW")
	procGetModuleHandleW      = kernel32.NewProc("GetModuleHandleW")
	procSetWindowPos          = user32.NewProc("SetWindowPos")
)

type (
	HWND      uintptr
	HINSTANCE uintptr
	HICON     uintptr
	HCURSOR   uintptr
	HBRUSH    uintptr
)

type WNDCLASSEXW struct {
	Size       uint32
	Style      uint32
	WndProc    uintptr
	ClsExtra   int32
	WndExtra   int32
	Instance   HINSTANCE
	Icon       HICON
	Cursor     HCURSOR
	Background HBRUSH
	MenuName   *uint16
	ClassName  *uint16
	IconSm     HICON
}

type MSG struct {
	Hwnd    HWND
	Message uint32
	WParam  uintptr
	LParam  uintptr
	Time    uint32
	Pt      struct{ X, Y int32 }
}

type BROWSEINFOW struct {
	HwndOwner      HWND
	PidlRoot       uintptr
	PszDisplayName *uint16
	LpszTitle      *uint16
	UlFlags        uint32
	Lpfn           uintptr
	LParam         uintptr
	IImage         int32
}

const (
	WS_OVERLAPPEDWINDOW = 0x00CF0000
	WS_CHILD            = 0x40000000
	WS_VISIBLE          = 0x10000000
	WS_BORDER           = 0x00800000
	WS_TABSTOP          = 0x00010000
	WS_VSCROLL          = 0x00200000
	WS_CLIPSIBLINGS     = 0x04000000
	WS_EX_CLIENTEDGE    = 0x00000200

	WM_CREATE  = 0x0001
	WM_DESTROY = 0x0002
	WM_SIZE    = 0x0005
	WM_COMMAND = 0x0111
	WM_NOTIFY  = 0x004E
	WM_CLOSE   = 0x0010

	BN_CLICKED = 0

	LVM_INSERTITEMW  = 0x104D
	LVM_DELETEALLITEMS = 0x1009
	LVM_GETNEXTITEM  = 0x100C
	LVM_GETITEMTEXTW = 0x102D

	LVIF_TEXT = 0x0001
	LVNI_SELECTED = 0x0002

	LVS_REPORT    = 0x0001
	LVS_SINGLESEL = 0x0004
	LVS_SHOWSELALWAYS = 0x0008

	LVM_INSERTCOLUMNW = 0x1061

	LVCFMT_LEFT = 0x0000

	NM_DBLCLK = 0xFFFFFFFD

	SW_SHOWNORMAL = 1

	MB_OK             = 0x00000000
	MB_ICONINFORMATION = 0x00000040
	MB_ICONERROR      = 0x00000010
	MB_ICONQUESTION   = 0x00000020
	MB_OKCANCEL       = 0x00000001
	IDOK              = 1
	IDCANCEL          = 2

	BIF_RETURNONLYFSDIRS = 0x0001
	BIF_NEWDIALOGSTYLE   = 0x0040

	ICC_LISTVIEW_CLASSES = 0x00000001

	ID_BTN_SWITCH   = 101
	ID_BTN_SCAN     = 102
	ID_BTN_ADD      = 103
	ID_BTN_DOWNLOAD = 104
	ID_LISTVIEW     = 201
	ID_STATUSBAR    = 202
)

var (
	listViewClass = syscall.StringToUTF16Ptr("SysListView32")
	buttonClass   = syscall.StringToUTF16Ptr("Button")
	staticClass   = syscall.StringToUTF16Ptr("Static")
)

type App struct {
	Instance    HINSTANCE
	MainWindow  HWND
	ListView    HWND
	StatusBar   HWND
	BtnSwitch   HWND
	BtnScan     HWND
	BtnAdd      HWND
	BtnDownload HWND
	Config       *core.Config
	JDKList      []*core.JDKInfo
	mu           sync.Mutex
}

var theApp *App

func Run(config *core.Config) error {
	runtime.LockOSThread()
	theApp = &App{Config: config}

	initCommonControls()

	hinst, _, _ := procGetModuleHandleW.Call(0)
	theApp.Instance = HINSTANCE(hinst)

	className := syscall.StringToUTF16Ptr("JVS_MainWindow")
	cursorID := uintptr(32512)

	wc := WNDCLASSEXW{
		Size:       uint32(unsafe.Sizeof(WNDCLASSEXW{})),
		WndProc:    syscall.NewCallback(wndProc),
		Instance:   theApp.Instance,
		Cursor:     loadCursor(cursorID),
		Background: HBRUSH(colorWindow + 1),
		ClassName:  className,
	}

	atom, _, _ := procRegisterClassExW.Call(uintptr(unsafe.Pointer(&wc)))
	if atom == 0 {
		return fmt.Errorf("注册窗口类失败")
	}

	title := syscall.StringToUTF16Ptr("Java Version Switcher")
	hwnd, _, _ := procCreateWindowExW.Call(
		0,
		uintptr(unsafe.Pointer(className)),
		uintptr(unsafe.Pointer(title)),
		WS_OVERLAPPEDWINDOW,
		200, 100, 650, 520,
		0, 0, uintptr(theApp.Instance), 0,
	)
	if hwnd == 0 {
		return fmt.Errorf("创建窗口失败")
	}

	theApp.MainWindow = HWND(hwnd)
	procShowWindow.Call(hwnd, SW_SHOWNORMAL)
	procUpdateWindow.Call(hwnd)

	return messageLoop()
}

func initCommonControls() {
	icc := struct {
		Size uint32
		ICC  uint32
	}{
		Size: 8,
		ICC:  ICC_LISTVIEW_CLASSES,
	}
	procInitCommonControlsEx.Call(uintptr(unsafe.Pointer(&icc)))
}

func loadCursor(id uintptr) HCURSOR {
	ret, _, _ := procLoadCursorW.Call(0, id)
	return HCURSOR(ret)
}

var colorWindow uintptr = 5

type NMHDR struct {
	HwndFrom HWND
	IdFrom   uintptr
	Code     uint32
}

func messageLoop() error {
	var msg MSG
	for {
		ret, _, _ := procGetMessageW.Call(uintptr(unsafe.Pointer(&msg)), 0, 0, 0)
		if ret == 0 {
			break
		}
		procTranslateMessage.Call(uintptr(unsafe.Pointer(&msg)))
		procDispatchMessageW.Call(uintptr(unsafe.Pointer(&msg)))
	}
	return nil
}

func wndProc(hwnd HWND, msg uint32, wParam, lParam uintptr) uintptr {
	switch msg {
	case WM_CREATE:
		theApp.MainWindow = hwnd
		createControls(hwnd)
		startScan()
		return 0
	case WM_COMMAND:
		return handleCommand(wParam)
	case WM_NOTIFY:
		return handleNotify(lParam)
	case WM_SIZE:
		return handleSize(hwnd, lParam)
	case WM_CLOSE:
		procDestroyWindow.Call(uintptr(hwnd))
		return 0
	case WM_DESTROY:
		procPostQuitMessage.Call(0)
		return 0
	}
	ret, _, _ := procDefWindowProcW.Call(uintptr(hwnd), uintptr(msg), wParam, lParam)
	return ret
}

func startScan() {
	go func() {
		time.Sleep(100 * time.Millisecond)
		jdkList := core.ScanAll(theApp.Config)
		theApp.mu.Lock()
		theApp.JDKList = jdkList
		theApp.mu.Unlock()

		populateListView(jdkList)
		setStatusText(fmt.Sprintf("就绪 — 共找到 %d 个 JDK", len(jdkList)))
	}()
}

func handleCommand(wParam uintptr) uintptr {
	cmd := wParam & 0xFFFF
	switch cmd {
	case ID_BTN_SWITCH:
		go handleSwitch()
	case ID_BTN_SCAN:
		go handleScan()
	case ID_BTN_ADD:
		handleAdd()
	case ID_BTN_DOWNLOAD:
		go handleDownload()
	}
	return 0
}

func handleNotify(lParam uintptr) uintptr {
	nmhdr := (*NMHDR)(unsafe.Pointer(lParam))
	if nmhdr.IdFrom == ID_LISTVIEW && nmhdr.Code == NM_DBLCLK {
		go handleSwitch()
	}
	return 0
}

func handleSize(hwnd HWND, lParam uintptr) uintptr {
	width := int32(lParam & 0xFFFF)
	height := int32((lParam >> 16) & 0xFFFF)
	if width < 100 || height < 100 {
		return 0
	}

	gap := uintptr(10)
	btnW := uintptr(130)
	btnH := uintptr(32)
	listY := uintptr(40)
	btnY := uintptr(height - 32 - 10 - 5)
	listH := btnY - listY - gap

	SWP_NOZORDER := uintptr(0x0004)

	procSetWindowPos.Call(uintptr(theApp.ListView), 0, gap, listY, uintptr(width)-2*gap, listH, SWP_NOZORDER)
	procSetWindowPos.Call(uintptr(theApp.BtnSwitch), 0, gap, btnY, btnW, btnH, SWP_NOZORDER)
	procSetWindowPos.Call(uintptr(theApp.BtnScan), 0, gap*2+btnW, btnY, btnW, btnH, SWP_NOZORDER)
	procSetWindowPos.Call(uintptr(theApp.BtnAdd), 0, gap*3+btnW*2, btnY, btnW, btnH, SWP_NOZORDER)
	procSetWindowPos.Call(uintptr(theApp.BtnDownload), 0, gap*4+btnW*3, btnY, btnW, btnH, SWP_NOZORDER)
	procSetWindowPos.Call(uintptr(theApp.StatusBar), 0, gap, uintptr(height)-30, uintptr(width)-2*gap, 25, SWP_NOZORDER)

	return 0
}

func setStatusText(text string) {
	procSetWindowTextW.Call(uintptr(theApp.StatusBar), uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(text))))
}

func showMessageBox(title, message string, flags uintptr) int {
	ret, _, _ := procMessageBoxW.Call(
		uintptr(theApp.MainWindow),
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(message))),
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(title))),
		flags,
	)
	return int(ret)
}

func getSelectedJDK() *core.JDKInfo {
	theApp.mu.Lock()
	defer theApp.mu.Unlock()

	ret, _, _ := procSendMessageW.Call(
		uintptr(theApp.ListView),
		LVM_GETNEXTITEM,
		0xFFFFFFFF,
		LVNI_SELECTED,
	)
	index := int(ret)
	if index < 0 || index >= len(theApp.JDKList) {
		return nil
	}
	return theApp.JDKList[index]
}

func handleSwitch() {
	jdk := getSelectedJDK()
	if jdk == nil {
		showMessageBox("提示", "请先选择一个 JDK 版本", MB_OK|MB_ICONINFORMATION)
		return
	}
	if jdk.IsCurrent {
		showMessageBox("提示", "该版本已是当前使用的版本", MB_OK|MB_ICONINFORMATION)
		return
	}

	setStatusText("正在请求管理员权限...")
	procEnableWindow.Call(uintptr(theApp.BtnSwitch), 0)

	result := runElevatedSwitch(jdk.Path)

	if result.Success {
		showMessageBox("切换成功", fmt.Sprintf("已切换至 %s\n清理了 %d 条旧 JDK 路径", jdk.Version, result.PathCleaned), MB_OK|MB_ICONINFORMATION)
	} else {
		showMessageBox("切换失败", result.Error, MB_OK|MB_ICONERROR)
	}

	procEnableWindow.Call(uintptr(theApp.BtnSwitch), 1)
	jdkList := core.ScanAll(theApp.Config)
	theApp.mu.Lock()
	theApp.JDKList = jdkList
	theApp.mu.Unlock()
	populateListView(jdkList)
}

func runElevatedSwitch(jdkPath string) *core.SwitchResult {
	exePath, _ := os.Executable()
	backupFile := core.BackupFilePath()
	args := fmt.Sprintf(`--switch "%s" --backup "%s"`, jdkPath, backupFile)

	shell32 := syscall.NewLazyDLL("shell32.dll")
	procShellExecute := shell32.NewProc("ShellExecuteW")

	procShellExecute.Call(
		uintptr(theApp.MainWindow),
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr("runas"))),
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(exePath))),
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(args))),
		0,
		0,
	)

	resultFile := core.ResultFilePath()
	deadline := time.Now().Add(60 * time.Second)

	for time.Now().Before(deadline) {
		if _, err := os.Stat(resultFile); err == nil {
			result, err := core.ReadSwitchResult()
			if err == nil {
				core.CleanResultFile()
				return result
			}
		}
		time.Sleep(200 * time.Millisecond)
	}

	return &core.SwitchResult{
		Success: false,
		Error:   "提权操作超时（60秒），请检查 UAC 弹窗",
	}
}

func handleScan() {
	setStatusText("正在扫描 JDK...")
	jdkList := core.ScanAll(theApp.Config)
	theApp.mu.Lock()
	theApp.JDKList = jdkList
	theApp.mu.Unlock()
	populateListView(jdkList)
	setStatusText(fmt.Sprintf("就绪 — 共找到 %d 个 JDK", len(jdkList)))
}

func handleAdd() {
	path := browseForFolder()
	if path == "" {
		return
	}

	jdks := core.ScanDirectory(path)
	if len(jdks) == 0 {
		showMessageBox("添加失败", "该路径下未找到有效的 JDK", MB_OK|MB_ICONERROR)
		return
	}

	theApp.Config.AddScanPath(path)

	theApp.mu.Lock()
	for _, j := range jdks {
		j.IsPortable = true
		theApp.JDKList = append(theApp.JDKList, j)
	}
	theApp.mu.Unlock()

	populateListView(theApp.JDKList)
	setStatusText(fmt.Sprintf("已添加: %s", jdks[0].Version))
}

func handleDownload() {
	version := showDownloadDialog()
	if version == "" {
		return
	}

	setStatusText(fmt.Sprintf("正在下载 JDK %s ...", version))
	procEnableWindow.Call(uintptr(theApp.BtnDownload), 0)

	path, err := core.DownloadJDK(version, theApp.Config.Mirror, nil)
	if err != nil {
		showMessageBox("下载失败", err.Error(), MB_OK|MB_ICONERROR)
		procEnableWindow.Call(uintptr(theApp.BtnDownload), 1)
		return
	}

	procEnableWindow.Call(uintptr(theApp.BtnDownload), 1)
	showMessageBox("下载完成", fmt.Sprintf("JDK %s 已下载并解压到:\n%s", version, path), MB_OK|MB_ICONINFORMATION)

	jdkList := core.ScanAll(theApp.Config)
	theApp.mu.Lock()
	theApp.JDKList = jdkList
	theApp.mu.Unlock()
	populateListView(jdkList)
	setStatusText(fmt.Sprintf("就绪 — 共找到 %d 个 JDK", len(jdkList)))
}

func browseForFolder() string {
	displayBuf := make([]uint16, 260)
	title := syscall.StringToUTF16Ptr("请选择 JDK 所在文件夹")

	bi := BROWSEINFOW{
		HwndOwner:      theApp.MainWindow,
		PszDisplayName: &displayBuf[0],
		LpszTitle:      title,
		UlFlags:        BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE,
	}

	pidl, _, _ := procSHBrowseForFolderW.Call(uintptr(unsafe.Pointer(&bi)))
	if pidl == 0 {
		return ""
	}

	pathBuf := make([]uint16, 260)
	ret, _, _ := procSHGetPathFromIDListW.Call(pidl, uintptr(unsafe.Pointer(&pathBuf[0])))
	if ret == 0 {
		return ""
	}

	return syscall.UTF16ToString(pathBuf)
}

func showDownloadDialog() string {
	versions := core.ListAvailableVersions()
	msg := "请选择要下载的 JDK 主版本（从华为云镜像）："
	for _, v := range versions {
		msg += "\n" + v
	}
	msg += "\n\n输入版本号（留空取消）:"

	return showInputDialog("下载 JDK", msg)
}

func showInputDialog(title, message string) string {
	exePath, _ := os.Executable()
	helperPath := filepath.Join(filepath.Dir(exePath), "jvs-input-helper.exe")
	if _, err := os.Stat(helperPath); err == nil {
		shell32 := syscall.NewLazyDLL("shell32.dll")
		procShellExecute := shell32.NewProc("ShellExecuteW")
		procShellExecute.Call(
			uintptr(theApp.MainWindow),
			0,
			uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(helperPath))),
			0, 0, 0,
		)
	}

	showMessageBox(title, message, MB_OK|MB_ICONINFORMATION)
	return ""
}

func populateListView(jdks []*core.JDKInfo) {
	procSendMessageW.Call(uintptr(theApp.ListView), LVM_DELETEALLITEMS, 0, 0)

	for i, j := range jdks {
		marker := " "
		if j.IsCurrent {
			marker = "●"
		}

		display := fmt.Sprintf("%s JDK %s  %s", marker, j.Version, j.Vendor)
		if j.Tag != "" {
			display = fmt.Sprintf("%s JDK %s  %s  %s", marker, j.Version, j.Vendor, j.Tag)
		}

		textPtr := syscall.StringToUTF16Ptr(display)
		lvi := struct {
			Mask       uint32
			IItem      int32
			ISubItem   int32
			State      uint32
			StateMask  uint32
			PszText    *uint16
			CchTextMax int32
			IImage     int32
			LParam     uintptr
			IParam     int32
		}{
			Mask:    LVIF_TEXT,
			IItem:   int32(i),
			PszText: textPtr,
		}

		procSendMessageW.Call(
			uintptr(theApp.ListView),
			LVM_INSERTITEMW,
			0,
			uintptr(unsafe.Pointer(&lvi)),
		)

		subText := syscall.StringToUTF16Ptr(j.Path)
		subLvi := struct {
			Mask       uint32
			IItem      int32
			ISubItem   int32
			State      uint32
			StateMask  uint32
			PszText    *uint16
			CchTextMax int32
			IImage     int32
			LParam     uintptr
			IParam     int32
		}{
			Mask:     LVIF_TEXT,
			IItem:    int32(i),
			ISubItem: 1,
			PszText:  subText,
		}
		procSendMessageW.Call(
			uintptr(theApp.ListView),
			uintptr(0x104E),
			0,
			uintptr(unsafe.Pointer(&subLvi)),
		)
	}
}
