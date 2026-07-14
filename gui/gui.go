package gui

import (
	"fmt"
	"image"
	"image/color"
	"os"
	"sync"
	"time"
	"unsafe"

	"gioui.org/app"
	"gioui.org/font/gofont"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/text"
	"gioui.org/unit"
	"gioui.org/widget"
	"gioui.org/widget/material"
	"golang.org/x/sys/windows"

	"jvs/core"
)

const (
	dlgNone = iota
	dlgVersion
)

type UI struct {
	Config *core.Config
	w      *app.Window
	th     *material.Theme

	mu      sync.Mutex
	jdkList []*core.JDKInfo
	status  string
	loading bool
	scanOk  bool

	scanBtn  widget.Clickable
	addBtn   widget.Clickable
	dlBtn    widget.Clickable
	swBtn    widget.Clickable
	dlgState int

	list       widget.List
	itemClicks []widget.Clickable
	selIdx     int

	dlVerSel widget.Enum
	dlVerCfm widget.Clickable
	dlVerCan widget.Clickable

	scanCh chan []*core.JDKInfo
	swCh   chan *core.SwitchResult
}

func Run(cfg *core.Config) error {
	u := &UI{
		Config: cfg,
		status: "就绪",
		selIdx: -1,
		scanCh: make(chan []*core.JDKInfo, 1),
		swCh:   make(chan *core.SwitchResult, 1),
	}
	u.list.Axis = layout.Vertical
	go u.runLoop()
	app.Main()
	return nil
}

func (u *UI) runLoop() {
	u.w = new(app.Window)
	u.w.Option(
		app.Title("Java Version Switcher"),
		app.Size(unit.Dp(720), unit.Dp(540)),
	)
	u.th = material.NewTheme()
	u.th.Shaper = text.NewShaper(text.WithCollection(gofont.Collection()))
	u.th.Palette = material.Palette{
		Fg:         color.NRGBA{R: 33, G: 33, B: 33, A: 255},
		Bg:         color.NRGBA{R: 250, G: 250, B: 250, A: 255},
		ContrastBg: color.NRGBA{R: 25, G: 118, B: 210, A: 255},
		ContrastFg: color.NRGBA{R: 255, G: 255, B: 255, A: 255},
	}

	var ops op.Ops
	go u.scan()

	for {
		switch e := u.w.Event().(type) {
		case app.FrameEvent:
			gtx := app.NewContext(&ops, e)
			u.frame(gtx)
			e.Frame(gtx.Ops)
		case app.DestroyEvent:
			return
		}
	}
}

func (u *UI) frame(gtx layout.Context) {
	u.handleInput(gtx)
	u.drainCh()

	if u.dlgState == dlgVersion {
		u.drawDlg(gtx)
	} else {
		layout.UniformInset(unit.Dp(16)).Layout(gtx, func(gtx layout.Context) layout.Dimensions {
			return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
				layout.Rigid(func(gtx layout.Context) layout.Dimensions { return u.card(gtx) }),
				layout.Rigid(layout.Spacer{Height: unit.Dp(12)}.Layout),
				layout.Flexed(1, func(gtx layout.Context) layout.Dimensions { return u.listView(gtx) }),
				layout.Rigid(layout.Spacer{Height: unit.Dp(12)}.Layout),
				layout.Rigid(func(gtx layout.Context) layout.Dimensions { return u.toolbar(gtx) }),
				layout.Rigid(layout.Spacer{Height: unit.Dp(4)}.Layout),
				layout.Rigid(func(gtx layout.Context) layout.Dimensions { return u.statusLine(gtx) }),
			)
		})
	}
}

func (u *UI) handleInput(gtx layout.Context) {
	if u.scanBtn.Clicked(gtx) {
		go u.scan()
	}
	if u.addBtn.Clicked(gtx) {
		go u.add()
	}
	if u.dlBtn.Clicked(gtx) {
		u.dlgState = dlgVersion
		u.w.Invalidate()
	}
	if u.dlVerCfm.Clicked(gtx) && u.dlgState == dlgVersion {
		ver := u.dlVerSel.Value
		u.dlgState = dlgNone
		if ver != "" {
			go u.download(ver)
		}
	}
	if u.dlVerCan.Clicked(gtx) && u.dlgState == dlgVersion {
		u.dlgState = dlgNone
		u.w.Invalidate()
	}
	if u.swBtn.Clicked(gtx) {
		go u.doSwitch()
	}
	for i := range u.itemClicks {
		if u.itemClicks[i].Clicked(gtx) {
			u.selIdx = i
		}
	}
}

func (u *UI) drainCh() {
	select {
	case jdks := <-u.scanCh:
		u.mu.Lock()
		u.jdkList = jdks
		u.itemClicks = make([]widget.Clickable, len(jdks))
		u.mu.Unlock()
		u.selIdx = -1
		u.loading = false
		u.scanOk = true
		u.setStatusF("就绪 — 共找到 %d 个 JDK", len(jdks))
	default:
	}
	select {
	case r := <-u.swCh:
		u.loading = false
		if r.Success {
			u.setStatusF("切换成功 — 已清理 %d 条旧路径", r.PathCleaned)
			go u.scan()
		} else {
			u.setStatus("切换失败: " + r.Error)
		}
	default:
	}
}

func (u *UI) setStatus(s string) {
	u.status = s
	u.w.Invalidate()
}

func (u *UI) setStatusF(f string, a ...interface{}) {
	u.setStatus(fmt.Sprintf(f, a...))
}

func (u *UI) currentJDK() *core.JDKInfo {
	for _, j := range u.jdkList {
		if j.IsCurrent {
			return j
		}
	}
	return nil
}

// ── Card ──────────────────────────────────────────────

func (u *UI) card(gtx layout.Context) layout.Dimensions {
	cur := u.currentJDK()

	title := "当前未选择"
	sub := "点击下方列表中的 JDK 后点击 [切换]"
	col := color.NRGBA{R: 150, G: 150, B: 150, A: 255}
	if cur != nil {
		title = fmt.Sprintf("✓  JDK %s  %s", cur.Version, cur.Vendor)
		sub = cur.Path
		col = color.NRGBA{R: 56, G: 142, B: 60, A: 255}
	}

	return card(gtx, u.th, func(gtx layout.Context) layout.Dimensions {
		return layout.UniformInset(unit.Dp(14)).Layout(gtx, func(gtx layout.Context) layout.Dimensions {
			return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					l := material.Label(u.th, unit.Sp(15), title)
					l.Color = col
					l.Font.Weight = 700
					return l.Layout(gtx)
				}),
				layout.Rigid(layout.Spacer{Height: unit.Dp(4)}.Layout),
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					l := material.Label(u.th, unit.Sp(12), sub)
					l.Color = color.NRGBA{R: 130, G: 130, B: 130, A: 255}
					return l.Layout(gtx)
				}),
			)
		})
	})
}

// ── List ──────────────────────────────────────────────

func (u *UI) listView(gtx layout.Context) layout.Dimensions {
	u.mu.Lock()
	list := u.jdkList
	clicks := u.itemClicks
	u.mu.Unlock()

	if len(list) == 0 {
		return material.Label(u.th, unit.Sp(14), "未检测到 JDK\n点击「扫描」重新搜索，或「添加」手动指定").Layout(gtx)
	}

	return u.list.Layout(gtx, len(list), func(gtx layout.Context, i int) layout.Dimensions {
		j := list[i]
		bg := color.NRGBA{R: 255, G: 255, B: 255, A: 255}
		if j.IsCurrent {
			bg = color.NRGBA{R: 237, G: 247, B: 237, A: 255}
		}

		return layout.Stack{Alignment: layout.W}.Layout(gtx,
			layout.Expanded(func(gtx layout.Context) layout.Dimensions {
				paint.FillShape(gtx.Ops, bg, clip.Rect(image.Rectangle{Max: gtx.Constraints.Max}).Op())
				return layout.Dimensions{Size: gtx.Constraints.Max}
			}),
			layout.Stacked(func(gtx layout.Context) layout.Dimensions {
				br := widget.Border{
					Color:        color.NRGBA{R: 224, G: 224, B: 224, A: 255},
					CornerRadius: unit.Dp(6),
					Width:        unit.Dp(1),
				}
				return br.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
					return layout.UniformInset(unit.Dp(10)).Layout(gtx, func(gtx layout.Context) layout.Dimensions {
						return u.itemBody(gtx, j, &clicks[i])
					})
				})
			}),
		)
	})
}

func (u *UI) itemBody(gtx layout.Context, j *core.JDKInfo, c *widget.Clickable) layout.Dimensions {
	dotCol := color.NRGBA{}
	tagText := j.Tag
	tagCol := color.NRGBA{R: 200, G: 100, B: 50, A: 255}
	if j.IsCurrent {
		dotCol = color.NRGBA{R: 76, G: 175, B: 80, A: 255}
		tagText = "✓ 当前使用"
		tagCol = color.NRGBA{R: 56, G: 142, B: 60, A: 255}
	}

	return c.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.Flex{Axis: layout.Horizontal, Alignment: layout.Middle}.Layout(gtx,
			layout.Rigid(func(gtx layout.Context) layout.Dimensions {
				gtx.Constraints.Min.X = gtx.Dp(24)
				l := material.Label(u.th, unit.Sp(16), "●")
				l.Color = dotCol
				return l.Layout(gtx)
			}),
			layout.Flexed(1, func(gtx layout.Context) layout.Dimensions {
				return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
					layout.Rigid(func(gtx layout.Context) layout.Dimensions {
						l := material.Label(u.th, unit.Sp(14), fmt.Sprintf("JDK %s  %s", j.Version, j.Vendor))
						l.Font.Weight = 600
						if j.IsCurrent {
							l.Color = color.NRGBA{R: 46, G: 125, B: 50, A: 255}
						}
						return l.Layout(gtx)
					}),
					layout.Rigid(func(gtx layout.Context) layout.Dimensions {
						l := material.Label(u.th, unit.Sp(11), j.Path)
						l.Color = color.NRGBA{R: 130, G: 130, B: 130, A: 255}
						return l.Layout(gtx)
					}),
				)
			}),
			layout.Rigid(func(gtx layout.Context) layout.Dimensions {
				if tagText == "" {
					return layout.Dimensions{}
				}
				l := material.Label(u.th, unit.Sp(11), tagText)
				l.Color = tagCol
				return l.Layout(gtx)
			}),
		)
	})
}

// ── Toolbar ───────────────────────────────────────────

func (u *UI) toolbar(gtx layout.Context) layout.Dimensions {
	return layout.Flex{Axis: layout.Horizontal, Alignment: layout.Middle}.Layout(gtx,
		layout.Rigid(material.Button(u.th, &u.scanBtn, "扫描").Layout),
		layout.Rigid(layout.Spacer{Width: unit.Dp(8)}.Layout),
		layout.Rigid(material.Button(u.th, &u.addBtn, "添加").Layout),
		layout.Rigid(layout.Spacer{Width: unit.Dp(8)}.Layout),
		layout.Rigid(material.Button(u.th, &u.dlBtn, "下载 JDK").Layout),
		layout.Flexed(1, layout.Spacer{Width: unit.Dp(1)}.Layout),
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			btn := material.Button(u.th, &u.swBtn, "切换")
			btn.Background = color.NRGBA{R: 25, G: 118, B: 210, A: 255}
			return btn.Layout(gtx)
		}),
	)
}

// ── Status ────────────────────────────────────────────

func (u *UI) statusLine(gtx layout.Context) layout.Dimensions {
	l := material.Label(u.th, unit.Sp(12), u.status)
	l.Color = color.NRGBA{R: 130, G: 130, B: 130, A: 255}
	return l.Layout(gtx)
}

// ── Dialog ────────────────────────────────────────────

func (u *UI) drawDlg(gtx layout.Context) {
	paint.FillShape(gtx.Ops, color.NRGBA{R: 0, G: 0, B: 0, A: 100}, clip.Rect(image.Rectangle{Max: gtx.Constraints.Max}).Op())

	layout.Center.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return card(gtx, u.th, func(gtx layout.Context) layout.Dimensions {
			return layout.UniformInset(unit.Dp(20)).Layout(gtx, func(gtx layout.Context) layout.Dimensions {
				return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
					layout.Rigid(material.H6(u.th, "选择 JDK 版本").Layout),
					layout.Rigid(layout.Spacer{Height: unit.Dp(12)}.Layout),
					layout.Rigid(func(gtx layout.Context) layout.Dimensions {
						var items []layout.FlexChild
						for _, v := range core.ListAvailableVersions() {
							v := v
							items = append(items, layout.Rigid(material.RadioButton(u.th, &u.dlVerSel, v, "JDK "+v).Layout))
						}
						return layout.Flex{Axis: layout.Vertical}.Layout(gtx, items...)
					}),
					layout.Rigid(layout.Spacer{Height: unit.Dp(16)}.Layout),
					layout.Rigid(func(gtx layout.Context) layout.Dimensions {
						return layout.Flex{Axis: layout.Horizontal, Spacing: layout.SpaceEnd}.Layout(gtx,
							layout.Rigid(material.Button(u.th, &u.dlVerCan, "取消").Layout),
							layout.Rigid(layout.Spacer{Width: unit.Dp(8)}.Layout),
							layout.Rigid(material.Button(u.th, &u.dlVerCfm, "下载").Layout),
						)
					}),
				)
			})
		})
	})
}

// ── Actions ───────────────────────────────────────────

func (u *UI) scan() {
	u.setStatus("正在扫描 JDK...")
	jdks := core.ScanAll(u.Config)
	u.scanCh <- jdks
}

func (u *UI) add() {
	p := pickFolder()
	if p == "" {
		return
	}
	jdks := core.ScanDirectory(p)
	if len(jdks) == 0 {
		u.setStatus("添加失败：路径下未找到 JDK（需包含 bin/javac.exe）")
		return
	}
	u.Config.AddScanPath(p)
	for _, j := range jdks {
		j.IsPortable = true
	}
	u.mu.Lock()
	u.jdkList = append(u.jdkList, jdks...)
	u.itemClicks = make([]widget.Clickable, len(u.jdkList))
	u.mu.Unlock()
	u.setStatusF("已添加: %s", jdks[0].Version)
}

func (u *UI) download(ver string) {
	u.loading = true
	u.setStatusF("正在下载 JDK %s ...", ver)
	p, err := core.DownloadJDK(ver, u.Config.Mirror, nil)
	if err != nil {
		u.setStatus("下载失败: " + err.Error())
		u.loading = false
		return
	}
	u.setStatusF("JDK %s 已下载至 %s", ver, p)
	u.loading = false
	go u.scan()
}

func (u *UI) doSwitch() {
	u.mu.Lock()
	idx := u.selIdx
	u.mu.Unlock()

	if idx < 0 || idx >= len(u.jdkList) {
		u.setStatus("请先在列表中点击选择一个 JDK")
		return
	}
	j := u.jdkList[idx]
	if j.IsCurrent {
		u.setStatus("该版本已是当前使用的版本")
		return
	}

	u.setStatus("正在请求管理员权限...")
	e, _ := os.Executable()
	a := fmt.Sprintf(`--switch "%s"`, j.Path)
	sw := windows.NewLazySystemDLL("shell32.dll").NewProc("ShellExecuteW")
	sw.Call(0,
		uintptr(unsafe.Pointer(windows.StringToUTF16Ptr("runas"))),
		uintptr(unsafe.Pointer(windows.StringToUTF16Ptr(e))),
		uintptr(unsafe.Pointer(windows.StringToUTF16Ptr(a))),
		0, 0)

	rp := core.ResultFilePath()
	dl := time.Now().Add(60 * time.Second)
	var rs *core.SwitchResult
	for time.Now().Before(dl) {
		if _, e2 := os.Stat(rp); e2 == nil {
			if r, e2 := core.ReadSwitchResult(); e2 == nil {
				rs = r
				core.CleanResultFile()
				break
			}
		}
		time.Sleep(200e6)
	}
	if rs == nil {
		rs = &core.SwitchResult{Success: false, Error: "超时（60秒），请检查 UAC"}
	}
	u.swCh <- rs
}

// ── Helpers ───────────────────────────────────────────

func card(gtx layout.Context, th *material.Theme, body layout.Widget) layout.Dimensions {
	br := widget.Border{
		Color:        color.NRGBA{R: 224, G: 224, B: 224, A: 255},
		CornerRadius: unit.Dp(8),
		Width:        unit.Dp(1),
	}
	return br.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.Background{}.Layout(gtx,
			func(gtx layout.Context) layout.Dimensions {
				paint.FillShape(gtx.Ops, color.NRGBA{R: 255, G: 255, B: 255, A: 255}, clip.Rect(image.Rectangle{Max: gtx.Constraints.Max}).Op())
				return layout.Dimensions{Size: gtx.Constraints.Max}
			},
			body,
		)
	})
}

func pickFolder() string {
	shl := windows.NewLazySystemDLL("shell32.dll")
	bw := shl.NewProc("SHBrowseForFolderW")
	gp := shl.NewProc("SHGetPathFromIDListW")

	buf := make([]uint16, 260)
	title := windows.StringToUTF16Ptr("选择 JDK 安装目录")
	bi := struct {
		Owner    uintptr
		Root     uintptr
		Display  *uint16
		Title    *uint16
		Flags    uint32
		Callback uintptr
		Param    uintptr
		Image    int32
	}{Display: &buf[0], Title: title, Flags: 0x0041}

	pidl, _, _ := bw.Call(uintptr(unsafe.Pointer(&bi)))
	if pidl == 0 {
		return ""
	}
	pb := make([]uint16, 260)
	gp.Call(pidl, uintptr(unsafe.Pointer(&pb[0])))
	return windows.UTF16ToString(pb)
}
