package gui

import (
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/exec"
	"runtime"
	"sync"
	"time"
	"unsafe"

	"golang.org/x/sys/windows"

	"jvs/core"
)

type server struct {
	cfg   *core.Config
	mu    sync.Mutex
	jdks  []*core.JDKInfo
}

func Run(cfg *core.Config) error {
	s := &server{cfg: cfg}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return fmt.Errorf("listen: %w", err)
	}
	port := ln.Addr().(*net.TCPAddr).Port

	mux := http.NewServeMux()
	mux.HandleFunc("/", s.serveIndex)
	mux.HandleFunc("/api/jdks", s.serveJDKs)
	mux.HandleFunc("/api/scan", s.serveScan)
	mux.HandleFunc("/api/switch", s.serveSwitch)
	mux.HandleFunc("/api/add", s.serveAdd)
	mux.HandleFunc("/api/download", s.serveDownload)
	mux.HandleFunc("/api/versions", s.serveVersions)

	go http.Serve(ln, mux)
	openURL(fmt.Sprintf("http://127.0.0.1:%d", port))
	select {}
}

func openURL(url string) {
	runtime.GOMAXPROCS(1)
	switch runtime.GOOS {
	case "windows":
		exec.Command("rundll32", "url.dll,FileProtocolHandler", url).Start()
	default:
		exec.Command("xdg-open", url).Start()
	}
}

func (s *server) serveIndex(w http.ResponseWriter, r *http.Request) {
	w.Write(indexHTML)
}

func (s *server) serveScan(w http.ResponseWriter, r *http.Request) {
	go func() {
		jdks := core.ScanAll(s.cfg)
		s.mu.Lock()
		s.jdks = jdks
		s.mu.Unlock()
	}()
	json.NewEncoder(w).Encode(map[string]string{"status": "scanning"})
}

func (s *server) serveJDKs(w http.ResponseWriter, r *http.Request) {
	s.mu.Lock()
	jdks := s.jdks
	s.mu.Unlock()
	if jdks == nil {
		jdks = core.ScanAll(s.cfg)
		s.mu.Lock()
		s.jdks = jdks
		s.mu.Unlock()
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(jdks)
}

func (s *server) serveSwitch(w http.ResponseWriter, r *http.Request) {
	path := r.URL.Query().Get("path")
	if path == "" {
		http.Error(w, "missing path", 400)
		return
	}

	e, _ := os.Executable()
	args := fmt.Sprintf(`--switch "%s"`, path)
	sw := windows.NewLazySystemDLL("shell32.dll").NewProc("ShellExecuteW")
	sw.Call(0,
		uintptr(unsafe.Pointer(windows.StringToUTF16Ptr("runas"))),
		uintptr(unsafe.Pointer(windows.StringToUTF16Ptr(e))),
		uintptr(unsafe.Pointer(windows.StringToUTF16Ptr(args))),
		0, 0)

	dl := time.Now().Add(60 * time.Second)
	var rs *core.SwitchResult
	for time.Now().Before(dl) {
		if _, e2 := os.Stat(core.ResultFilePath()); e2 == nil {
			if r, e2 := core.ReadSwitchResult(); e2 == nil {
				rs = r
				core.CleanResultFile()
				break
			}
		}
		time.Sleep(200e6)
	}
	if rs == nil {
		rs = &core.SwitchResult{Success: false, Error: "UAC timeout (60s)"}
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(rs)
}

func (s *server) serveAdd(w http.ResponseWriter, r *http.Request) {
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
	if pidl == 0 {
		json.NewEncoder(w).Encode(map[string]string{"status": "cancelled"})
		return
	}
	pb := make([]uint16, 260)
	gp.Call(pidl, uintptr(unsafe.Pointer(&pb[0])))
	p := windows.UTF16ToString(pb)
	if p == "" {
		json.NewEncoder(w).Encode(map[string]string{"status": "cancelled"})
		return
	}

	jdks := core.ScanDirectory(p)
	if len(jdks) == 0 {
		json.NewEncoder(w).Encode(map[string]string{"status": "notfound"})
		return
	}
	s.cfg.AddScanPath(p)
	s.mu.Lock()
	s.jdks = append(s.jdks, jdks...)
	s.mu.Unlock()
	json.NewEncoder(w).Encode(map[string]string{"status": "ok", "version": jdks[0].Version})
}

func (s *server) serveDownload(w http.ResponseWriter, r *http.Request) {
	ver := r.URL.Query().Get("version")
	if ver == "" {
		http.Error(w, "missing version", 400)
		return
	}
	go func() {
		core.DownloadJDK(ver, s.cfg.Mirror, nil)
		jdks := core.ScanAll(s.cfg)
		s.mu.Lock()
		s.jdks = jdks
		s.mu.Unlock()
	}()
	json.NewEncoder(w).Encode(map[string]string{"status": "downloading"})
}

func (s *server) serveVersions(w http.ResponseWriter, r *http.Request) {
	json.NewEncoder(w).Encode(core.ListAvailableVersions())
}

var indexHTML = []byte(`<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Java Version Switcher</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#f5f5f5;color:#212121;min-height:100vh}
.app-bar{background:#1565C0;color:#fff;padding:16px 24px;font-size:20px;font-weight:600;letter-spacing:.3px}
.container{max-width:800px;margin:0 auto;padding:20px 16px}
.card{background:#fff;border-radius:12px;box-shadow:0 1px 3px rgba(0,0,0,.12);padding:16px 20px;margin-bottom:12px}
.card-title{font-size:15px;font-weight:700;margin-bottom:4px}
.card-sub{font-size:12px;color:#757575}
.green{color:#43A047}
.gray{color:#9E9E9E}
.badge{display:inline-block;font-size:11px;padding:2px 8px;border-radius:4px;background:#E8F5E9;color:#43A047;margin-left:8px}
.badge-mc{background:#FFF3E0;color:#E65100}
.list{background:#fff;border-radius:12px;box-shadow:0 1px 3px rgba(0,0,0,.12);overflow:hidden}
.item{display:flex;align-items:center;padding:12px 16px;cursor:pointer;border-left:3px solid transparent;transition:.15s}
.item:hover{background:#F5F5F5}
.item.sel{border-left-color:#1565C0;background:#E3F2FD}
.item.cur{background:#E8F5E9}
.dot{width:16px;height:16px;border-radius:50%;margin-right:12px;flex-shrink:0;border:2px solid transparent}
.dot.active{background:#43A047;border-color:#43A047}
.dot.inactive{border-color:#BDBDBD}
.item-body{flex:1;min-width:0}
.item-version{font-size:14px;font-weight:600}
.item-path{font-size:11px;color:#757575;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.item-tag{font-size:11px;flex-shrink:0}
.toolbar{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.toolbar .spacer{flex:1}
.btn{display:inline-flex;align-items:center;gap:6px;padding:8px 16px;border-radius:8px;border:1px solid #E0E0E0;background:#fff;font-size:13px;font-weight:500;cursor:pointer;transition:.15s;color:#424242}
.btn:hover{background:#F5F5F5;border-color:#BDBDBD}
.btn-primary{background:#1565C0;color:#fff;border-color:#1565C0}
.btn-primary:hover{background:#1976D2}
.btn:disabled{opacity:.5;cursor:default}
.status{margin-top:8px;font-size:12px;color:#9E9E9E}
.empty{text-align:center;padding:48px 16px;color:#9E9E9E;font-size:14px;line-height:1.6}
.modal-overlay{position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,.5);display:flex;align-items:center;justify-content:center;z-index:100}
.modal{background:#fff;border-radius:16px;padding:24px;min-width:320px;box-shadow:0 8px 32px rgba(0,0,0,.2)}
.modal h3{margin-bottom:16px;font-size:18px;font-weight:600}
.modal-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:20px}
input[type=radio]{margin-right:8px}
.modal label{display:block;padding:6px 0;font-size:14px;cursor:pointer}
@keyframes spin{to{transform:rotate(360deg)}}
.spinner{display:inline-block;width:14px;height:14px;border:2px solid #E0E0E0;border-top-color:#1565C0;border-radius:50%;animation:spin .6s linear infinite;vertical-align:middle;margin-right:6px}
</style>
</head>
<body>
<div class="app-bar">Java Version Switcher</div>
<div class="container" id="app">
  <div class="card" id="infoCard">
    <div class="card-title gray" id="infoTitle">No JDK selected</div>
    <div class="card-sub" id="infoSub">Pick one from the list and click Switch</div>
  </div>
  <div id="listArea"></div>
  <div class="toolbar" style="margin-top:12px">
    <button class="btn" onclick="scan()">&#x21bb; Scan</button>
    <button class="btn" onclick="addJDK()">&#x2b; Add</button>
    <button class="btn" onclick="showDownload()">&#x2193; Download JDK</button>
    <span class="spacer"></span>
    <button class="btn btn-primary" id="switchBtn" onclick="doSwitch()">&#x25b6; Switch</button>
  </div>
  <div class="status" id="status">Ready</div>
</div>
<div id="modal"></div>
<script>
let jdks=[],selected=-1;
function api(p){return fetch(p).then(r=>r.json())}
function load(){api('/api/jdks').then(d=>{jdks=d;render();status('Found '+d.length+' JDK(s)')})}
function render(){
  const el=document.getElementById('listArea');
  if(jdks.length===0){el.innerHTML='<div class="empty">No JDK found.<br>Click Scan or Add to begin.</div>';return}
  el.innerHTML='<div class="list">'+jdks.map((j,i)=>{
    const sel=selected===i?' sel':'',cur=j.IsCurrent?' cur':'',dot=j.IsCurrent?'dot active':'dot inactive';
    const tag=j.IsCurrent?'<span class="badge">Active</span>':j.Tag?'<span class="badge badge-mc">'+j.Tag+'</span>':'';
    return '<div class="item'+sel+cur+'" onclick="select('+i+')">'+
      '<div class="'+dot+'"></div>'+
      '<div class="item-body"><div class="item-version'+(j.IsCurrent?' green':'')+'">JDK '+j.Version+' &middot; '+j.Vendor+'</div>'+
      '<div class="item-path">'+j.Path+'</div></div>'+
      '<div class="item-tag">'+tag+'</div>'+
      '</div>';
  }).join('')+'</div>';
  const cur=jdks.find(j=>j.IsCurrent);
  document.getElementById('infoTitle').textContent=cur?'✓ JDK '+cur.Version+' · '+cur.Vendor:'No JDK selected';
  document.getElementById('infoTitle').className='card-title'+(cur?' green':' gray');
  document.getElementById('infoSub').textContent=cur?cur.Path:'Pick one from the list and click Switch';
}
function select(i){selected=i;render()}
function status(s){document.getElementById('status').textContent=s}
function scan(){status('Scanning...');api('/api/scan').then(()=>setTimeout(load,500))}
function addJDK(){api('/api/add').then(r=>{if(r.status==='ok'){status('Added '+r.version);load()}else if(r.status==='notfound'){status('No JDK in that path')}else{status('Cancelled')}})}
function doSwitch(){
  if(selected<0||selected>=jdks.length){status('Select a JDK first');return}
  if(jdks[selected].IsCurrent){status('Already using this version');return}
  status('Requesting admin...');
  fetch('/api/switch?path='+encodeURIComponent(jdks[selected].Path)).then(r=>r.json()).then(d=>{
    if(d.success){status('Switched!');load()}else{status('Failed: '+d.error)}
  })
}
function showDownload(){
  api('/api/versions').then(vs=>{
    const m=document.getElementById('modal');
    m.innerHTML='<div class="modal-overlay" onclick="closeModal(e)"><div class="modal" onclick="event.stopPropagation()"><h3>Select JDK Version</h3>'+
      vs.map(v=>'<label><input type="radio" name="ver" value="'+v+'"> JDK '+v+'</label>').join('')+
      '<div class="modal-actions"><button class="btn" onclick="closeModal()">Cancel</button><button class="btn btn-primary" onclick="doDownload()">Download</button></div></div></div>';
  })
}
function doDownload(){
  const v=document.querySelector('input[name=ver]:checked');
  if(!v)return;
  closeModal();status('Downloading JDK '+v.value+'...');
  api('/api/download?version='+v.value).then(()=>setTimeout(load,2000));
}
function closeModal(){document.getElementById('modal').innerHTML=''}
load()
setInterval(load,5000)
</script>
</body>
</html>`)
