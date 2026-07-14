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
	cfg *core.Config
	w   *app.Window
	th  *material.Theme

	mu   sync.Mutex
	list []*core.JDKInfo
	stat string

	scan widget.Clickable
	add  widget.Clickable
	dl   widget.Clickable
	sw   widget.Clickable

	lst  widget.List
	item []widget.Clickable
	sel  int

	showDl bool
	dlVer  widget.Enum
	dlOk   widget.Clickable
	dlNo   widget.Clickable

	sc chan []*core.JDKInfo
	sc2 chan *core.SwitchResult
}

func Run(cfg *core.Config) error {
	u := &UI{cfg: cfg, sel: -1, stat: "Ready", sc: make(chan []*core.JDKInfo, 1), sc2: make(chan *core.SwitchResult, 1)}
	u.lst.Axis = layout.Vertical
	go u.loop()
	app.Main()
	return nil
}

func (u *UI) loop() {
	u.w = new(app.Window)
	u.w.Option(app.Title("Java Version Switcher"), app.Size(unit.Dp(720), unit.Dp(540)))
	u.th = material.NewTheme()
	u.th.Shaper = text.NewShaper(text.WithCollection(gofont.Collection()))
	u.th.Palette = material.Palette{Fg: n(33), Bg: n(245), ContrastBg: nrgba(25, 118, 210), ContrastFg: n(255)}
	u.th.TextSize = unit.Sp(14)

	var ops op.Ops
	go u.doScan()

	for {
		switch e := u.w.Event().(type) {
		case app.FrameEvent:
			gtx := app.NewContext(&ops, e)
			u.tick(gtx)
			e.Frame(gtx.Ops)
		case app.DestroyEvent:
			return
		}
	}
}

func nrgba(r, g, b byte) color.NRGBA { return color.NRGBA{r, g, b, 255} }
func n(v byte) color.NRGBA { return color.NRGBA{v, v, v, 255} }

func (u *UI) tick(gtx layout.Context) {
	u.input(gtx)
	u.recv()

	if u.showDl {
		u.drawDl(gtx)
		return
	}

	layout.UniformInset(unit.Dp(16)).Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
			layout.Rigid(func(gtx layout.Context) layout.Dimensions { return u.header(gtx) }),
			layout.Rigid(layout.Spacer{Height: unit.Dp(12)}.Layout),
			layout.Rigid(func(gtx layout.Context) layout.Dimensions { return u.infoCard(gtx) }),
			layout.Rigid(layout.Spacer{Height: unit.Dp(12)}.Layout),
			layout.Rigid(func(gtx layout.Context) layout.Dimensions { return u.items(gtx) }),
			layout.Rigid(layout.Spacer{Height: unit.Dp(12)}.Layout),
			layout.Rigid(func(gtx layout.Context) layout.Dimensions { return u.btns(gtx) }),
			layout.Rigid(layout.Spacer{Height: unit.Dp(4)}.Layout),
			layout.Rigid(func(gtx layout.Context) layout.Dimensions { return u.st(gtx) }),
		)
	})
}

func (u *UI) input(gtx layout.Context) {
	if u.scan.Clicked(gtx) { go u.doScan() }
	if u.add.Clicked(gtx) { go u.doAdd() }
	if u.dl.Clicked(gtx) { u.showDl = true; u.w.Invalidate() }
	if u.dlOk.Clicked(gtx) && u.showDl {
		u.showDl = false
		if u.dlVer.Value != "" { go u.doDl(u.dlVer.Value) }
	}
	if u.dlNo.Clicked(gtx) && u.showDl { u.showDl = false; u.w.Invalidate() }
	if u.sw.Clicked(gtx) { go u.doSw() }
	for i := range u.item {
		if u.item[i].Clicked(gtx) { u.sel = i }
	}
}

func (u *UI) recv() {
	select {
	case v := <-u.sc:
		u.mu.Lock()
		u.list = v
		u.item = make([]widget.Clickable, len(v))
		u.mu.Unlock()
		u.sel = -1
		u.stat = fmt.Sprintf("Found %d JDK(s)", len(v))
		u.w.Invalidate()
	default:
	}
	select {
	case r := <-u.sc2:
		if r.Success {
			u.stat = fmt.Sprintf("Switched! Cleaned %d path(s)", r.PathCleaned)
			go u.doScan()
		} else {
			u.stat = "Error: " + r.Error
		}
		u.w.Invalidate()
	default:
	}
}

func (u *UI) header(gtx layout.Context) layout.Dimensions {
	h := gtx.Dp(48)
	paint.FillShape(gtx.Ops, nrgba(25, 118, 210), clip.Rect{Max: image.Pt(gtx.Constraints.Max.X, h)}.Op())
	return layout.Inset{Left: unit.Dp(16), Top: unit.Dp(12)}.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		l := material.Label(u.th, unit.Sp(18), "Java Version Switcher")
		l.Color = n(255); l.Font.Weight = 700
		return l.Layout(gtx)
	})
}

func (u *UI) info() *core.JDKInfo {
	for _, v := range u.list { if v.IsCurrent { return v } }; return nil
}

func (u *UI) infoCard(gtx layout.Context) layout.Dimensions {
	c := u.info()
	t, s, c2 := "No JDK selected", "Pick one and click Switch", n(158)
	if c != nil {
		t = fmt.Sprintf("✓ JDK %s · %s", c.Version, c.Vendor)
		s = c.Path; c2 = nrgba(76, 175, 80)
	}
	return card(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.UniformInset(unit.Dp(14)).Layout(gtx, func(gtx layout.Context) layout.Dimensions {
			return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					l := material.Label(u.th, unit.Sp(15), t)
					l.Color = c2; l.Font.Weight = 700
					return l.Layout(gtx)
				}),
				layout.Rigid(layout.Spacer{Height: unit.Dp(4)}.Layout),
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					l := material.Label(u.th, unit.Sp(11), s)
					l.Color = n(117)
					return l.Layout(gtx)
				}),
			)
		})
	})
}

func (u *UI) items(gtx layout.Context) layout.Dimensions {
	u.mu.Lock()
	v := u.list
	cl := u.item
	u.mu.Unlock()

	if len(v) == 0 {
		return card(gtx, func(gtx layout.Context) layout.Dimensions {
			return layout.UniformInset(unit.Dp(32)).Layout(gtx, func(gtx layout.Context) layout.Dimensions {
				l := material.Label(u.th, unit.Sp(14), "No JDK found. Click Scan or Add.")
				l.Color = n(158)
				return l.Layout(gtx)
			})
		})
	}

	return layout.Stack{Alignment: layout.W}.Layout(gtx,
		layout.Expanded(func(gtx layout.Context) layout.Dimensions {
			paint.FillShape(gtx.Ops, n(255), clip.Rect{Max: gtx.Constraints.Max}.Op())
			sz := gtx.Constraints.Max
			paint.FillShape(gtx.Ops, n(224), clip.Rect{Max: image.Pt(sz.X, 1)}.Op())
			paint.FillShape(gtx.Ops, n(224), clip.Rect{Min: image.Pt(0, sz.Y-1), Max: sz}.Op())
			return layout.Dimensions{Size: sz}
		}),
		layout.Stacked(func(gtx layout.Context) layout.Dimensions {
			return u.lst.Layout(gtx, len(v), func(gtx layout.Context, i int) layout.Dimensions {
				j := v[i]
				bg := n(255)
				if j.IsCurrent { bg = nrgba(232, 245, 233) }
				if u.sel == i { bg = nrgba(227, 242, 253) }

				return layout.Stack{Alignment: layout.W}.Layout(gtx,
					layout.Expanded(func(gtx layout.Context) layout.Dimensions {
						paint.FillShape(gtx.Ops, bg, clip.Rect{Max: gtx.Constraints.Max}.Op())
						if u.sel == i {
							paint.FillShape(gtx.Ops, nrgba(25, 118, 210), clip.Rect{Max: image.Pt(3, gtx.Constraints.Max.Y)}.Op())
						}
						paint.FillShape(gtx.Ops, n(240), clip.Rect{Min: image.Pt(0, gtx.Constraints.Max.Y-1), Max: gtx.Constraints.Max}.Op())
						return layout.Dimensions{Size: gtx.Constraints.Max}
					}),
					layout.Stacked(func(gtx layout.Context) layout.Dimensions {
						l := unit.Dp(16)
						if u.sel == i { l = unit.Dp(13) }
						return layout.Inset{Left: l}.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
							return u.row(gtx, j, &cl[i])
						})
					}),
				)
			})
		}),
	)
}

func (u *UI) row(gtx layout.Context, j *core.JDKInfo, c *widget.Clickable) layout.Dimensions {
	dc := color.NRGBA{}
	tg := j.Tag
	if j.IsCurrent { dc = nrgba(76, 175, 80); tg = "✓ Active" }

	return c.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.Inset{Top: unit.Dp(10), Bottom: unit.Dp(10)}.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
			return layout.Flex{Axis: layout.Horizontal, Alignment: layout.Middle}.Layout(gtx,
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					gtx.Constraints.Min.X = gtx.Dp(20)
					l := material.Label(u.th, unit.Sp(16), "●")
					l.Color = dc
					return l.Layout(gtx)
				}),
				layout.Flexed(1, func(gtx layout.Context) layout.Dimensions {
					return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
						layout.Rigid(func(gtx layout.Context) layout.Dimensions {
							l := material.Label(u.th, unit.Sp(14), fmt.Sprintf("JDK %s  %s", j.Version, j.Vendor))
							l.Font.Weight = 600
							if j.IsCurrent { l.Color = nrgba(46, 125, 50) }
							return l.Layout(gtx)
						}),
						layout.Rigid(func(gtx layout.Context) layout.Dimensions {
							l := material.Label(u.th, unit.Sp(11), j.Path)
							l.Color = n(130)
							return l.Layout(gtx)
						}),
					)
				}),
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					if tg == "" { return layout.Dimensions{} }
					return material.Label(u.th, unit.Sp(11), tg).Layout(gtx)
				}),
			)
		})
	})
}

func (u *UI) btns(gtx layout.Context) layout.Dimensions {
	b := func(c *widget.Clickable, t string, p bool) material.ButtonStyle {
		mb := material.Button(u.th, c, t)
		mb.Inset = layout.Inset{Top: unit.Dp(8), Bottom: unit.Dp(8), Left: unit.Dp(16), Right: unit.Dp(16)}
		mb.CornerRadius = unit.Dp(6)
		if p { mb.Background = nrgba(25, 118, 210) }
		return mb
	}
	return layout.Flex{Axis: layout.Horizontal, Alignment: layout.Middle}.Layout(gtx,
		layout.Rigid(b(&u.scan, "Scan", false).Layout),
		layout.Rigid(layout.Spacer{Width: unit.Dp(6)}.Layout),
		layout.Rigid(b(&u.add, "Add", false).Layout),
		layout.Rigid(layout.Spacer{Width: unit.Dp(6)}.Layout),
		layout.Rigid(b(&u.dl, "Download", false).Layout),
		layout.Flexed(1, layout.Spacer{Width: unit.Dp(1)}.Layout),
		layout.Rigid(b(&u.sw, "Switch", true).Layout),
	)
}

func (u *UI) st(gtx layout.Context) layout.Dimensions {
	l := material.Label(u.th, unit.Sp(12), u.stat)
	l.Color = n(158)
	return l.Layout(gtx)
}

func (u *UI) drawDl(gtx layout.Context) {
	paint.FillShape(gtx.Ops, color.NRGBA{0, 0, 0, 120}, clip.Rect{Max: gtx.Constraints.Max}.Op())
	layout.Center.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return card(gtx, func(gtx layout.Context) layout.Dimensions {
			return layout.UniformInset(unit.Dp(24)).Layout(gtx, func(gtx layout.Context) layout.Dimensions {
				return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
					layout.Rigid(material.H6(u.th, "Select JDK Version").Layout),
					layout.Rigid(layout.Spacer{Height: unit.Dp(16)}.Layout),
					layout.Rigid(func(gtx layout.Context) layout.Dimensions {
						var ch []layout.FlexChild
						for _, v := range core.ListAvailableVersions() {
							v := v
							ch = append(ch, layout.Rigid(material.RadioButton(u.th, &u.dlVer, v, "JDK "+v).Layout))
						}
						return layout.Flex{Axis: layout.Vertical}.Layout(gtx, ch...)
					}),
					layout.Rigid(layout.Spacer{Height: unit.Dp(16)}.Layout),
					layout.Rigid(func(gtx layout.Context) layout.Dimensions {
						return layout.Flex{Axis: layout.Horizontal, Spacing: layout.SpaceEnd}.Layout(gtx,
							layout.Rigid(material.Button(u.th, &u.dlNo, "Cancel").Layout),
							layout.Rigid(layout.Spacer{Width: unit.Dp(8)}.Layout),
							layout.Rigid(func(gtx layout.Context) layout.Dimensions {
								mb := material.Button(u.th, &u.dlOk, "Download")
								mb.Background = nrgba(25, 118, 210)
								return mb.Layout(gtx)
							}),
						)
					}),
				)
			})
		})
	})
}

func card(gtx layout.Context, w layout.Widget) layout.Dimensions {
	return widget.Border{Color: n(224), CornerRadius: unit.Dp(8), Width: unit.Dp(1)}.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.Background{}.Layout(gtx,
			func(gtx layout.Context) layout.Dimensions {
				paint.FillShape(gtx.Ops, n(255), clip.Rect{Max: gtx.Constraints.Max}.Op())
				return layout.Dimensions{Size: gtx.Constraints.Max}
			}, w)
	})
}

// ── actions ───────────────────────────────────────────

func (u *UI) doScan() {
	u.stat = "Scanning..."; u.w.Invalidate()
	u.sc <- core.ScanAll(u.cfg)
}

func (u *UI) doAdd() {
	p := folder()
	if p == "" { return }
	v := core.ScanDirectory(p)
	if len(v) == 0 { u.stat = "No JDK in that path"; u.w.Invalidate(); return }
	u.cfg.AddScanPath(p)
	u.mu.Lock()
	u.list = append(u.list, v...)
	u.item = make([]widget.Clickable, len(u.list))
	u.mu.Unlock()
	u.stat = fmt.Sprintf("Added %s", v[0].Version)
	u.w.Invalidate()
}

func (u *UI) doDl(ver string) {
	u.stat = fmt.Sprintf("Downloading JDK %s ...", ver); u.w.Invalidate()
	_, err := core.DownloadJDK(ver, u.cfg.Mirror, nil)
	if err != nil { u.stat = "Failed: " + err.Error(); u.w.Invalidate(); return }
	u.stat = fmt.Sprintf("Downloaded JDK %s", ver)
	go u.doScan()
}

func (u *UI) doSw() {
	if u.sel < 0 || u.sel >= len(u.list) { u.stat = "Select a JDK first"; u.w.Invalidate(); return }
	j := u.list[u.sel]
	if j.IsCurrent { u.stat = "Already active"; u.w.Invalidate(); return }
	u.stat = "Requesting admin..."; u.w.Invalidate()

	e, _ := os.Executable()
	a := fmt.Sprintf(`--switch "%s"`, j.Path)
	sw := windows.NewLazySystemDLL("shell32.dll").NewProc("ShellExecuteW")
	sw.Call(0, uintptr(unsafe.Pointer(windows.StringToUTF16Ptr("runas"))), uintptr(unsafe.Pointer(windows.StringToUTF16Ptr(e))), uintptr(unsafe.Pointer(windows.StringToUTF16Ptr(a))), 0, 0)

	rp := core.ResultFilePath()
	dl := time.Now().Add(60 * time.Second)
	var rs *core.SwitchResult
	for time.Now().Before(dl) {
		if _, e2 := os.Stat(rp); e2 == nil {
			if r, e2 := core.ReadSwitchResult(); e2 == nil { rs = r; core.CleanResultFile(); break }
		}
		time.Sleep(200e6)
	}
	if rs == nil { rs = &core.SwitchResult{Success: false, Error: "Timeout"} }
	u.sc2 <- rs
}

func folder() string {
	shl := windows.NewLazySystemDLL("shell32.dll")
	bw := shl.NewProc("SHBrowseForFolderW")
	gp := shl.NewProc("SHGetPathFromIDListW")
	buf := make([]uint16, 260)
	bi := struct {
		Owner, Root uintptr
		Display     *uint16
		Title       *uint16
		Flags       uint32
		Callback    uintptr
		Param       uintptr
		Image       int32
	}{Display: &buf[0], Title: windows.StringToUTF16Ptr("Select JDK folder"), Flags: 0x0041}
	pidl, _, _ := bw.Call(uintptr(unsafe.Pointer(&bi)))
	if pidl == 0 { return "" }
	pb := make([]uint16, 260)
	gp.Call(pidl, uintptr(unsafe.Pointer(&pb[0])))
	return windows.UTF16ToString(pb)
}
