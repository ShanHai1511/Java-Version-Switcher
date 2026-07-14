package gui

import (
	"fmt"
	"os"
	"sync"
	"syscall"
	"time"
	"unsafe"

	"golang.org/x/sys/windows"

	"jvs/core"
)

var (
	user32                  = syscall.NewLazyDLL("user32.dll")
	comctl32                = syscall.NewLazyDLL("comctl32.dll")
	shell32                 = syscall.NewLazyDLL("shell32.dll")
	kernel32                = syscall.NewLazyDLL("kernel32.dll")
	procCreateWindowExW     = user32.NewProc("CreateWindowExW")
	procDefWindowProcW      = user32.NewProc("DefWindowProcW")
	procRegisterClassExW    = user32.NewProc("RegisterClassExW")
	procGetMessageW         = user32.NewProc("GetMessageW")
	procDispatchMessageW    = user32.NewProc("DispatchMessageW")
	procTranslateMessage    = user32.NewProc("TranslateMessage")
	procPostQuitMessage     = user32.NewProc("PostQuitMessage")
	procShowWindow          = user32.NewProc("ShowWindow")
	procUpdateWindow        = user32.NewProc("UpdateWindow")
	procLoadCursorW         = user32.NewProc("LoadCursorW")
	procMessageBoxW         = user32.NewProc("MessageBoxW")
	procSendMessageW        = user32.NewProc("SendMessageW")
	procSetWindowTextW      = user32.NewProc("SetWindowTextW")
	procDestroyWindow       = user32.NewProc("DestroyWindow")
	procEnableWindow        = user32.NewProc("EnableWindow")
	procSetWindowPos        = user32.NewProc("SetWindowPos")
	procGetModuleHandleW    = kernel32.NewProc("GetModuleHandleW")
	procInitCommonControlsEx = comctl32.NewProc("InitCommonControlsEx")
	procSHBrowseForFolderW  = shell32.NewProc("SHBrowseForFolderW")
	procSHGetPathFromIDListW = shell32.NewProc("SHGetPathFromIDListW")
)

type App struct {
	Instance uintptr
	Hwnd     uintptr
	ListView uintptr
	Status   uintptr
	Btns     [4]uintptr

	Config  *core.Config
	JDKList []*core.JDKInfo
	mu      sync.Mutex
}

var theApp *App

func Run(config *core.Config) error {
	theApp = &App{Config: config}
	initCC()

	hinst, _, _ := procGetModuleHandleW.Call(0)
	theApp.Instance = hinst

	cn := syscall.StringToUTF16Ptr("JVS_Win")
	wc := struct {
		Size       uint32
		Style      uint32
		WndProc    uintptr
		ClsExtra   int32
		WndExtra   int32
		Instance   uintptr
		Icon       uintptr
		Cursor     uintptr
		Background uintptr
		MenuName   *uint16
		ClassName  *uint16
		IconSm     uintptr
	}{
		Size:      uint32(unsafe.Sizeof(struct {
			Size, Style uint32
			WndProc     uintptr
			ClsExtra, WndExtra int32
			Instance, Icon, Cursor, Background uintptr
			MenuName, ClassName *uint16
			IconSm uintptr
		}{})),
		WndProc:    syscall.NewCallback(wndProc),
		Instance:   theApp.Instance,
		Cursor:     loadCur(32512),
		Background: 6,
		ClassName:  cn,
	}
	procRegisterClassExW.Call(uintptr(unsafe.Pointer(&wc)))

	t := syscall.StringToUTF16Ptr("Java Version Switcher")
	h, _, _ := procCreateWindowExW.Call(
		0, uintptr(unsafe.Pointer(cn)), uintptr(unsafe.Pointer(t)),
		0x00CF0000|0x02000000,
		200, 100, 680, 520, 0, 0, theApp.Instance, 0)
	if h == 0 {
		return fmt.Errorf("create window fail")
	}
	theApp.Hwnd = h
	procShowWindow.Call(h, 1)
	procUpdateWindow.Call(h)
	return loop()
}

func initCC() {
	s := struct {
		Size uint32
		ICC  uint32
	}{Size: 8, ICC: 1}
	procInitCommonControlsEx.Call(uintptr(unsafe.Pointer(&s)))
}

func loadCur(id uintptr) uintptr {
	r, _, _ := procLoadCursorW.Call(0, id)
	return r
}

func loop() error {
	var m struct {
		Hwnd    uintptr
		Msg     uint32
		WParam  uintptr
		LParam  uintptr
		Time    uint32
		PtX, PtY int32
	}
	for {
		r, _, _ := procGetMessageW.Call(uintptr(unsafe.Pointer(&m)), 0, 0, 0)
		if r == 0 {
			break
		}
		procTranslateMessage.Call(uintptr(unsafe.Pointer(&m)))
		procDispatchMessageW.Call(uintptr(unsafe.Pointer(&m)))
	}
	return nil
}

func wndProc(h uintptr, m uint32, w, l uintptr) uintptr {
	switch m {
	case 0x0001: // WM_CREATE
		theApp.Hwnd = h
		createUI(h)
		go scan()
		return 0
	case 0x0005: // WM_SIZE
		onSize(l)
		return 0
	case 0x0111: // WM_COMMAND
		onCmd(w)
		return 0
	case 0x004E: // WM_NOTIFY
		onNotify(l)
		return 0
	case 0x0010: // WM_CLOSE
		procDestroyWindow.Call(h)
		return 0
	case 0x0002: // WM_DESTROY
		procPostQuitMessage.Call(0)
		return 0
	}
	r, _, _ := procDefWindowProcW.Call(h, uintptr(m), w, l)
	return r
}

func createUI(h uintptr) {
	gap := uintptr(12)
	bw := uintptr(110)
	bh := uintptr(30)

	lh := uintptr(350)
	ly := gap + 40
	by := ly + lh + gap

	// listview
	theApp.ListView = createCtrl(h, "SysListView32", "",
		0x40000000|0x10000000|0x00010000|0x0001|0x0004|0x0008,
		gap, ly, 640, lh, 201)

	addCol(theApp.ListView, 0, "版本 · 厂商", 300)
	addCol(theApp.ListView, 1, "安装路径", 330)

	// buttons
	theApp.Btns[0] = createCtrl(h, "Button", "扫描", 0x40000000|0x10000000|0x00010000, gap, by, bw, bh, 102)
	theApp.Btns[1] = createCtrl(h, "Button", "添加", 0x40000000|0x10000000|0x00010000, gap*2+bw, by, bw, bh, 103)
	theApp.Btns[2] = createCtrl(h, "Button", "下载", 0x40000000|0x10000000|0x00010000, gap*3+bw*2, by, bw, bh, 104)
	theApp.Btns[3] = createCtrl(h, "Button", "切换", 0x40000000|0x10000000|0x00010000, gap*4+bw*3, by, 100, bh, 101)

	// status
	theApp.Status = createCtrl(h, "Static", "就绪", 0x40000000|0x10000000, gap, by+bh+gap, 640, 22, 202)
}

func createCtrl(parent uintptr, cls, text string, style uintptr, x, y, w, h uintptr, id int) uintptr {
	var tp *uint16
	if text != "" {
		tp = syscall.StringToUTF16Ptr(text)
	}
	r, _, _ := procCreateWindowExW.Call(
		0,
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(cls))),
		uintptr(unsafe.Pointer(tp)),
		style|0x00020000, // WS_EX_STATICEDGE for list
		x, y, w, h,
		parent, uintptr(id), theApp.Instance, 0)
	return r
}

func addCol(lv uintptr, idx int, text string, w int32) {
	c := struct {
		Mask       uint32
		Fmt        int32
		Cx         int32
		PszText    *uint16
		CchTextMax int32
	}{Mask: 7, Fmt: 0, Cx: w, PszText: syscall.StringToUTF16Ptr(text)}
	procSendMessageW.Call(lv, 0x1061, uintptr(idx), uintptr(unsafe.Pointer(&c)))
}

func onSize(l uintptr) {
	w := int32(l & 0xFFFF)
	h := int32((l >> 16) & 0xFFFF)
	if w < 100 {
		return
	}
	cw := uintptr(w) - 24
	gap := uintptr(12)
	bw := uintptr(110)
	bh := uintptr(30)
	ly := gap + 40
	lh := uintptr(h) - 162
	by := ly + lh + gap

	procSetWindowPos.Call(theApp.ListView, 0, gap, ly, cw, lh, 4)
	btns := []uintptr{gap, gap*2 + bw, gap*3 + bw*2, gap*4 + bw*3}
	for i := range btns {
		procSetWindowPos.Call(theApp.Btns[i], 0, btns[i], by, bw, bh, 4)
	}
	procSetWindowPos.Call(theApp.Status, 0, gap, by+bh+gap, cw, 22, 4)
}

func onCmd(w uintptr) {
	switch w & 0xFFFF {
	case 102:
		go scan()
	case 103:
		onAdd()
	case 104:
		onDl()
	case 101:
		onSw()
	}
}

func onNotify(l uintptr) {
	code := *(*uint32)(unsafe.Pointer(l + 2*unsafe.Sizeof(uintptr(0))))
	if code == 0xFFFFFFFD {
		onSw()
	}
}

func setSt(s string) {
	procSetWindowTextW.Call(theApp.Status, uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(s))))
}

func msg(t, s string) {
	procMessageBoxW.Call(theApp.Hwnd,
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(s))),
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(t))), 0)
}

func setLd(v bool) {
	n := uintptr(1)
	if v {
		n = 0
	}
	for _, b := range theApp.Btns {
		procEnableWindow.Call(b, n)
	}
}

func scan() {
	setLd(true)
	setSt("正在扫描 JDK ...")
	j := core.ScanAll(theApp.Config)
	theApp.mu.Lock()
	theApp.JDKList = j
	theApp.mu.Unlock()
	fill(j)
	setSt(fmt.Sprintf("就绪 — 共找到 %d 个 JDK", len(j)))
	setLd(false)
}

func fill(j []*core.JDKInfo) {
	procSendMessageW.Call(theApp.ListView, 0x1009, 0, 0)
	for i, v := range j {
		m := " "
		if v.IsCurrent {
			m = "●"
		}
		d := fmt.Sprintf("%s JDK %s  %s", m, v.Version, v.Vendor)
		if v.Tag != "" {
			d += "  " + v.Tag
		}
		it := struct {
			Mask       uint32
			IItem      int32
			ISubItem   int32
			State      uint32
			StateMask  uint32
			PszText    *uint16
			CchTextMax int32
			IImage     int32
			LParam     uintptr
		}{Mask: 1, IItem: int32(i), PszText: syscall.StringToUTF16Ptr(d)}
		procSendMessageW.Call(theApp.ListView, 0x104D, 0, uintptr(unsafe.Pointer(&it)))
		s := struct {
			Mask       uint32
			IItem      int32
			ISubItem   int32
			State      uint32
			StateMask  uint32
			PszText    *uint16
			CchTextMax int32
			IImage     int32
			LParam     uintptr
		}{Mask: 1, IItem: int32(i), ISubItem: 1, PszText: syscall.StringToUTF16Ptr(v.Path)}
		procSendMessageW.Call(theApp.ListView, 0x104E, uintptr(i), uintptr(unsafe.Pointer(&s)))
	}
}

func sel() *core.JDKInfo {
	theApp.mu.Lock()
	defer theApp.mu.Unlock()
	r, _, _ := procSendMessageW.Call(theApp.ListView, 0x100C, ^uintptr(0), 2)
	i := int(r)
	if i < 0 || i >= len(theApp.JDKList) {
		return nil
	}
	return theApp.JDKList[i]
}

func onSw() {
	j := sel()
	if j == nil {
		msg("提示", "请先在列表中点击选择一个 JDK 版本")
		return
	}
	if j.IsCurrent {
		msg("提示", "该版本已是当前使用的版本")
		return
	}
	setLd(true)
	setSt("正在请求管理员权限 ...")

	e, _ := os.Executable()
	a := fmt.Sprintf(`--switch "%s"`, j.Path)
	op := windows.StringToUTF16Ptr("runas")
	fp := windows.StringToUTF16Ptr(e)
	pr := windows.StringToUTF16Ptr(a)
	sh := shell32.NewProc("ShellExecuteW")
	sh.Call(0, uintptr(unsafe.Pointer(op)), uintptr(unsafe.Pointer(fp)), uintptr(unsafe.Pointer(pr)), 0, 0)

	rp := core.ResultFilePath()
	dl := time.Now().Add(60 * time.Second)
	var rs *core.SwitchResult
	for time.Now().Before(dl) {
		if _, e := os.Stat(rp); e == nil {
			r, e := core.ReadSwitchResult()
			if e == nil {
				rs = r
				core.CleanResultFile()
				break
			}
		}
		time.Sleep(200e6)
	}
	setLd(false)
	if rs == nil {
		msg("超时", "提权超时（60秒），请检查 UAC")
		setSt("切换超时")
		return
	}
	if rs.Success {
		j2 := core.ScanAll(theApp.Config)
		theApp.mu.Lock()
		theApp.JDKList = j2
		theApp.mu.Unlock()
		fill(j2)
		setSt(fmt.Sprintf("切换成功 — JDK %s（清理 %d 条）", j.Version, rs.PathCleaned))
	} else {
		msg("切换失败", rs.Error)
		setSt("切换失败")
	}
}

func onAdd() {
	b := make([]uint16, 260)
	t := syscall.StringToUTF16Ptr("选择 JDK 安装目录")
	bi := struct {
		Owner    uintptr
		Root     uintptr
		Display  *uint16
		Title    *uint16
		Flags    uint32
		Callback uintptr
		Param    uintptr
		Image    int32
	}{Owner: theApp.Hwnd, Display: &b[0], Title: t, Flags: 0x0041}
	pidl, _, _ := procSHBrowseForFolderW.Call(uintptr(unsafe.Pointer(&bi)))
	if pidl == 0 {
		return
	}
	pb := make([]uint16, 260)
	procSHGetPathFromIDListW.Call(pidl, uintptr(unsafe.Pointer(&pb[0])))
	p := syscall.UTF16ToString(pb)
	if p == "" {
		return
	}
	j := core.ScanDirectory(p)
	if len(j) == 0 {
		msg("添加失败", "路径下未找到 JDK（需要 bin/javac.exe）")
		return
	}
	theApp.Config.AddScanPath(p)
	for _, v := range j {
		v.IsPortable = true
		theApp.mu.Lock()
		theApp.JDKList = append(theApp.JDKList, v)
		theApp.mu.Unlock()
	}
	fill(theApp.JDKList)
	setSt(fmt.Sprintf("已添加: %s", j[0].Version))
}

func onDl() {
	v := "21"
	setLd(true)
	setSt(fmt.Sprintf("正在下载 JDK %s ...", v))
	go func() {
		p, e := core.DownloadJDK(v, theApp.Config.Mirror, nil)
		if e != nil {
			msg("下载失败", e.Error())
			setSt("下载失败")
			setLd(false)
			return
		}
		j2 := core.ScanAll(theApp.Config)
		theApp.mu.Lock()
		theApp.JDKList = j2
		theApp.mu.Unlock()
		fill(j2)
		setLd(false)
		setSt(fmt.Sprintf("JDK %s 已下载至 %s", v, p))
		msg("下载完成", fmt.Sprintf("JDK %s 已下载到:\n%s", v, p))
	}()
}
