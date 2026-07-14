package gui

import (
	"syscall"
	"unsafe"
)

type LVCOLUMNW struct {
	Mask       uint32
	Fmt        int32
	Cx         int32
	PszText    *uint16
	CchTextMax int32
	ISubItem   int32
	IImage     int32
	IOrder     int32
}

const (
	LVCF_FMT   = 0x0001
	LVCF_WIDTH = 0x0002
	LVCF_TEXT  = 0x0004

	LVM_SETITEMTEXTW = 0x104E
)

func createControls(hwnd HWND) {
	createListView(hwnd)
	createButtons(hwnd)
	createStatusBar(hwnd)
}

func createListView(parent HWND) {
	hwndLV, _, _ := procCreateWindowExW.Call(
		WS_EX_CLIENTEDGE,
		uintptr(unsafe.Pointer(listViewClass)),
		0,
		WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_VSCROLL,
		10, 40, 610, 360,
		uintptr(parent),
		ID_LISTVIEW,
		uintptr(theApp.Instance),
		0,
	)
	theApp.ListView = HWND(hwndLV)

	addColumn(HWND(hwndLV), 0, "版本与厂商", 350)
	addColumn(HWND(hwndLV), 1, "安装路径", 230)
}

func addColumn(hwnd HWND, index int, text string, width int32) {
	col := LVCOLUMNW{
		Mask:  LVCF_FMT | LVCF_WIDTH | LVCF_TEXT,
		Fmt:   LVCFMT_LEFT,
		Cx:    width,
		PszText: syscall.StringToUTF16Ptr(text),
	}
	procSendMessageW.Call(uintptr(hwnd), LVM_INSERTCOLUMNW, uintptr(index), uintptr(unsafe.Pointer(&col)))
}

func createButtons(parent HWND) {
	theApp.BtnSwitch = createButton(parent, "切换", 10, 410, 130, 32, ID_BTN_SWITCH)
	theApp.BtnScan = createButton(parent, "扫描", 150, 410, 130, 32, ID_BTN_SCAN)
	theApp.BtnAdd = createButton(parent, "添加", 290, 410, 130, 32, ID_BTN_ADD)
	theApp.BtnDownload = createButton(parent, "下载 JDK", 430, 410, 130, 32, ID_BTN_DOWNLOAD)
}

func createButton(parent HWND, text string, x, y, w, h, id int) HWND {
	hwnd, _, _ := procCreateWindowExW.Call(
		0,
		uintptr(unsafe.Pointer(buttonClass)),
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(text))),
		WS_CHILD|WS_VISIBLE|WS_TABSTOP,
		uintptr(x), uintptr(y), uintptr(w), uintptr(h),
		uintptr(parent),
		uintptr(id),
		uintptr(theApp.Instance),
		0,
	)
	return HWND(hwnd)
}

func createStatusBar(parent HWND) {
	hwnd, _, _ := procCreateWindowExW.Call(
		0,
		uintptr(unsafe.Pointer(staticClass)),
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr("就绪"))),
		WS_CHILD|WS_VISIBLE|WS_BORDER,
		10, 450, 610, 25,
		uintptr(parent),
		ID_STATUSBAR,
		uintptr(theApp.Instance),
		0,
	)
	theApp.StatusBar = HWND(hwnd)
}
