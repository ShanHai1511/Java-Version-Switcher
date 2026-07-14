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

type UI struct {
	Config *core.Config
	w      *app.Window
	th     *material.Theme

	mu      sync.Mutex
	jdkList []*core.JDKInfo
	status  string
	loading bool

	scanBtn  widget.Clickable
	addBtn   widget.Clickable
	dlBtn    widget.Clickable
	swBtn    widget.Clickable
	list     widget.List
	selIdx   int

	dlOpen   bool
	dlSel    widget.Enum
	dlVers   []string
	dlCfm    widget.Clickable
	dlCancel widget.Clickable

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
	go u.loop()
	app.Main()
	return nil
}

func (u *UI) loop() {
	u.w = new(app.Window)
	u.w.Option(
		app.Title("Java Version Switcher"),
		app.Size(unit.Dp(720), unit.Dp(540)),
	)
	u.th = material.NewTheme()
	u.th.Shaper = text.NewShaper(text.WithCollection(gofont.Collection()))
	u.th.Palette = material.Palette{
		Fg: color.NRGBA{R: 33, G: 33, B: 33, A: 255},
		Bg: color.NRGBA{R: 250, G: 250, B: 250, A: 255},
		ContrastBg: color.NRGBA{R: 25, G: 118, B: 210, A: 255},
		ContrastFg: color.NRGBA{R: 255, G: 255, B: 255, A: 255},
	}

	var ops op.Ops
	go u.startScan()

	for {
		switch e := u.w.Event().(type) {
		case app.FrameEvent:
			gtx := app.NewContext(&ops, e)
			u.layout(gtx)
			e.Frame(gtx.Ops)
		case app.DestroyEvent:
			return
		}
	}
}

func (u *UI) layout(gtx layout.Context) {
	u.handleClicks(gtx)
	u.checkCh()

	layout.UniformInset(unit.Dp(16)).Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
			layout.Rigid(func(gtx layout.Context) layout.Dimensions { return u.card(gtx) }),
			layout.Rigid(layout.Spacer{Height: unit.Dp(12)}.Layout),
			layout.Flexed(1, func(gtx layout.Context) layout.Dimensions { return u.listLayout(gtx) }),
			layout.Rigid(layout.Spacer{Height: unit.Dp(12)}.Layout),
			layout.Rigid(func(gtx layout.Context) layout.Dimensions { return u.buttons(gtx) }),
			layout.Rigid(layout.Spacer{Height: unit.Dp(4)}.Layout),
			layout.Rigid(func(gtx layout.Context) layout.Dimensions { return u.statusLine(gtx) }),
		)
	})
	if u.dlOpen {
		u.dialog(gtx)
	}
}

func (u *UI) handleClicks(gtx layout.Context) {
	if u.scanBtn.Clicked(gtx) {
		go u.startScan()
	}
	if u.addBtn.Clicked(gtx) {
		go u.addJDK()
	}
	if u.dlBtn.Clicked(gtx) {
		u.dlOpen = true
		u.dlVers = core.ListAvailableVersions()
	}
	if u.swBtn.Clicked(gtx) {
		go u.doSwitch()
	}
	if u.dlCfm.Clicked(gtx) && u.dlOpen {
		u.dlOpen = false
		go u.download(u.dlSel.Value)
	}
	if u.dlCancel.Clicked(gtx) && u.dlOpen {
		u.dlOpen = false
	}
}

func (u *UI) checkCh() {
	select {
	case jdks := <-u.scanCh:
		u.mu.Lock()
		u.jdkList = jdks
		u.mu.Unlock()
		u.loading = false
		u.status = fmt.Sprintf("就绪 — 共找到 %d 个 JDK", len(jdks))
		u.w.Invalidate()
	case r := <-u.swCh:
		u.loading = false
		if r.Success {
			u.status = fmt.Sprintf("切换成功 — 已清理 %d 条旧路径", r.PathCleaned)
			go u.startScan()
		} else {
			u.status = "切换失败: " + r.Error
		}
		u.w.Invalidate()
	default:
	}
}

func (u *UI) current() *core.JDKInfo {
	for _, j := range u.jdkList {
		if j.IsCurrent {
			return j
		}
	}
	return nil
}

func (u *UI) card(gtx layout.Context) layout.Dimensions {
	u.mu.Lock()
	cur := u.current()
	u.mu.Unlock()

	title := "当前未选择"
	sub := "点击下方列表中的 JDK 后点击 [切换]"
	col := color.NRGBA{R: 150, G: 150, B: 150, A: 255}
	if cur != nil {
		title = fmt.Sprintf("✓  JDK %s  %s", cur.Version, cur.Vendor)
		sub = cur.Path
		col = color.NRGBA{R: 56, G: 142, B: 60, A: 255}
	}

	border := widget.Border{
		Color: color.NRGBA{R: 224, G: 224, B: 224, A: 255},
		CornerRadius: unit.Dp(8),
		Width: unit.Dp(1),
	}
	return border.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.Background{}.Layout(gtx,
			func(gtx layout.Context) layout.Dimensions {
				paint.FillShape(gtx.Ops, color.NRGBA{R: 255, G: 255, B: 255, A: 255}, clip.Rect(image.Rectangle{Max: gtx.Constraints.Max}).Op())
				return layout.Dimensions{Size: gtx.Constraints.Max}
			},
			func(gtx layout.Context) layout.Dimensions {
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
	})
}

func (u *UI) listLayout(gtx layout.Context) layout.Dimensions {
	u.mu.Lock()
	list := u.jdkList
	u.mu.Unlock()

	if len(list) == 0 {
		return material.Label(u.th, unit.Sp(14), "未检测到 JDK\n点击「扫描」重新搜索，或「添加」手动指定").Layout(gtx)
	}

	return material.List(u.th, &u.list).Layout(gtx, len(list), func(gtx layout.Context, i int) layout.Dimensions {
		u.mu.Lock()
		j := list[i]
		u.mu.Unlock()

		bg := color.NRGBA{R: 255, G: 255, B: 255, A: 255}
		if j.IsCurrent {
			bg = color.NRGBA{R: 237, G: 247, B: 237, A: 255}
		}

		paint.FillShape(gtx.Ops, bg, clip.Rect(image.Rectangle{Max: gtx.Constraints.Max}).Op())

		br := widget.Border{
			Color:        color.NRGBA{R: 224, G: 224, B: 224, A: 255},
			CornerRadius: unit.Dp(6),
			Width:        unit.Dp(1),
		}
		return br.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
			return layout.UniformInset(unit.Dp(10)).Layout(gtx, func(gtx layout.Context) layout.Dimensions {
				return u.itemLayout(gtx, j)
			})
		})
	})
}

func (u *UI) itemLayout(gtx layout.Context, j *core.JDKInfo) layout.Dimensions {
	dotCol := color.NRGBA{}
	tagText := j.Tag
	tagCol := color.NRGBA{R: 200, G: 100, B: 50, A: 255}
	if j.IsCurrent {
		dotCol = color.NRGBA{R: 76, G: 175, B: 80, A: 255}
		tagText = "✓ 当前使用"
		tagCol = color.NRGBA{R: 56, G: 142, B: 60, A: 255}
	}

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
}

func (u *UI) buttons(gtx layout.Context) layout.Dimensions {
	return layout.Flex{Axis: layout.Horizontal, Alignment: layout.Middle}.Layout(gtx,
		layout.Rigid(material.Button(u.th, &u.scanBtn, "扫描").Layout),
		layout.Rigid(layout.Spacer{Width: unit.Dp(8)}.Layout),
		layout.Rigid(material.Button(u.th, &u.addBtn, "添加").Layout),
		layout.Rigid(layout.Spacer{Width: unit.Dp(8)}.Layout),
		layout.Rigid(material.Button(u.th, &u.dlBtn, "下载 JDK").Layout),
		layout.Flexed(1, func(gtx layout.Context) layout.Dimensions {
			return layout.Spacer{Width: unit.Dp(1)}.Layout(gtx)
		}),
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			btn := material.Button(u.th, &u.swBtn, "切换")
			btn.Background = color.NRGBA{R: 25, G: 118, B: 210, A: 255}
			return btn.Layout(gtx)
		}),
	)
}

func (u *UI) statusLine(gtx layout.Context) layout.Dimensions {
	l := material.Label(u.th, unit.Sp(12), u.status)
	l.Color = color.NRGBA{R: 130, G: 130, B: 130, A: 255}
	return l.Layout(gtx)
}

func (u *UI) dialog(gtx layout.Context) {
	layout.Center.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		br := widget.Border{
			Color: color.NRGBA{R: 200, G: 200, B: 200, A: 255},
			CornerRadius: unit.Dp(8),
			Width: unit.Dp(1),
		}
		return br.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
			return layout.Background{}.Layout(gtx,
				func(gtx layout.Context) layout.Dimensions {
					paint.FillShape(gtx.Ops, color.NRGBA{R: 255, G: 255, B: 255, A: 255}, clip.Rect(image.Rectangle{Max: gtx.Constraints.Max}).Op())
					return layout.Dimensions{Size: gtx.Constraints.Max}
				},
				func(gtx layout.Context) layout.Dimensions {
					return layout.UniformInset(unit.Dp(20)).Layout(gtx, func(gtx layout.Context) layout.Dimensions {
						return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
							layout.Rigid(material.H6(u.th, "选择 JDK 版本").Layout),
							layout.Rigid(layout.Spacer{Height: unit.Dp(12)}.Layout),
							layout.Rigid(func(gtx layout.Context) layout.Dimensions {
								var items []layout.FlexChild
								for _, v := range u.dlVers {
									v := v
									items = append(items, layout.Rigid(
										material.RadioButton(u.th, &u.dlSel, v, "JDK "+v).Layout,
									))
								}
								return layout.Flex{Axis: layout.Vertical}.Layout(gtx, items...)
							}),
							layout.Rigid(layout.Spacer{Height: unit.Dp(16)}.Layout),
							layout.Rigid(func(gtx layout.Context) layout.Dimensions {
								return layout.Flex{Axis: layout.Horizontal, Spacing: layout.SpaceEnd}.Layout(gtx,
									layout.Rigid(material.Button(u.th, &u.dlCancel, "取消").Layout),
									layout.Rigid(layout.Spacer{Width: unit.Dp(8)}.Layout),
									layout.Rigid(material.Button(u.th, &u.dlCfm, "下载").Layout),
								)
							}),
						)
					})
				})
		})
	})
}

func (u *UI) startScan() {
	u.loading = true
	u.status = "正在扫描 JDK..."
	u.w.Invalidate()
	jdks := core.ScanAll(u.Config)
	u.scanCh <- jdks
}

func (u *UI) addJDK() {
	p := pickFolder()
	if p == "" {
		return
	}
	jdks := core.ScanDirectory(p)
	if len(jdks) == 0 {
		u.status = "添加失败：路径下未找到 JDK（需包含 bin/javac.exe）"
		u.w.Invalidate()
		return
	}
	u.Config.AddScanPath(p)
	for _, j := range jdks {
		j.IsPortable = true
	}
	u.mu.Lock()
	u.jdkList = append(u.jdkList, jdks...)
	u.mu.Unlock()
	u.status = fmt.Sprintf("已添加: %s", jdks[0].Version)
	u.w.Invalidate()
}

func (u *UI) download(ver string) {
	if ver == "" {
		return
	}
	u.loading = true
	u.status = fmt.Sprintf("正在下载 JDK %s ...", ver)
	u.w.Invalidate()
	p, err := core.DownloadJDK(ver, u.Config.Mirror, nil)
	if err != nil {
		u.status = "下载失败: " + err.Error()
		u.loading = false
		u.w.Invalidate()
		return
	}
	u.status = fmt.Sprintf("JDK %s 已下载至 %s", ver, p)
	u.loading = false
	go u.startScan()
	u.w.Invalidate()
}

func (u *UI) doSwitch() {
	u.mu.Lock()
	idx := u.selIdx
	u.mu.Unlock()

	if idx < 0 || idx >= len(u.jdkList) {
		u.status = "请先在列表中点击选择一个 JDK"
		u.w.Invalidate()
		return
	}
	j := u.jdkList[idx]
	if j.IsCurrent {
		u.status = "该版本已是当前使用的版本"
		u.w.Invalidate()
		return
	}

	u.loading = true
	u.status = "正在请求管理员权限..."
	u.w.Invalidate()

	e, _ := os.Executable()
	a := fmt.Sprintf(`--switch "%s"`, j.Path)
	sw := windows.NewLazySystemDLL("shell32.dll").NewProc("ShellExecuteW")
	op := windows.StringToUTF16Ptr("runas")
	fp := windows.StringToUTF16Ptr(e)
	pr := windows.StringToUTF16Ptr(a)
	sw.Call(0, uintptr(unsafe.Pointer(op)), uintptr(unsafe.Pointer(fp)), uintptr(unsafe.Pointer(pr)), 0, 0)

	rp := core.ResultFilePath()
	dl := time.Now().Add(60 * time.Second)
	var rs *core.SwitchResult
	for time.Now().Before(dl) {
		if _, e2 := os.Stat(rp); e2 == nil {
			r, e2 := core.ReadSwitchResult()
			if e2 == nil {
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
