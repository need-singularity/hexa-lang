#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define HEXA_ARENA_SIZE (16*1024*1024)
static char* hexa_arena = NULL;
static size_t hexa_arena_p = 0;
static void hexa_arena_init(void) {
    if (!hexa_arena) hexa_arena = (char*)malloc(HEXA_ARENA_SIZE);
}
static char* hexa_alloc(size_t n) {
    hexa_arena_init();
    if (hexa_arena_p + n >= HEXA_ARENA_SIZE) { fprintf(stderr, "hexa_arena overflow\n"); exit(1); }
    char* p = &hexa_arena[hexa_arena_p];
    hexa_arena_p += n;
    return p;
}
static const char* hexa_concat(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    char* p = hexa_alloc(la + lb + 1);
    memcpy(p, a, la); memcpy(p + la, b, lb); p[la + lb] = 0;
    return p;
}
static const char* hexa_int_to_str(long v) {
    char* p = hexa_alloc(24);
    snprintf(p, 24, "%ld", v);
    return p;
}
static const char* hexa_substr(const char* s, long a, long b) {
    long sl = (long)strlen(s);
    if (a < 0) a = 0; if (b > sl) b = sl; if (a > b) a = b;
    long n = b - a;
    char* p = hexa_alloc(n + 1);
    memcpy(p, s + a, n); p[n] = 0;
    return p;
}
static long hexa_str_len(const char* s) { return (long)strlen(s); }
static long hexa_contains(const char* h, const char* n) { return strstr(h, n) ? 1 : 0; }
static long hexa_index_of(const char* h, const char* n) {
    const char* p = strstr(h, n); if (!p) return -1;
    return (long)(p - h);
}
static long hexa_starts_with(const char* h, const char* n) {
    size_t ln = strlen(n); return strncmp(h, n, ln) == 0 ? 1 : 0;
}
static long hexa_ends_with(const char* h, const char* n) {
    size_t lh = strlen(h), ln = strlen(n);
    if (ln > lh) return 0; return strcmp(h + lh - ln, n) == 0 ? 1 : 0;
}
static const char* hexa_trim(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    long n = (long)strlen(s);
    while (n > 0 && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\n'||s[n-1]=='\r')) n--;
    char* p = hexa_alloc(n + 1);
    memcpy(p, s, n); p[n] = 0;
    return p;
}
static const char* hexa_to_upper(const char* s) {
    long n = (long)strlen(s); char* p = hexa_alloc(n + 1);
    for (long i = 0; i < n; i++) { char c = s[i]; p[i] = (c>='a'&&c<='z') ? c - 32 : c; }
    p[n] = 0; return p;
}
static const char* hexa_to_lower(const char* s) {
    long n = (long)strlen(s); char* p = hexa_alloc(n + 1);
    for (long i = 0; i < n; i++) { char c = s[i]; p[i] = (c>='A'&&c<='Z') ? c + 32 : c; }
    p[n] = 0; return p;
}
static long hexa_parse_int(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    long sign = 1; if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    long v = 0; while (*s >= '0' && *s <= '9') { v = v*10 + (*s - '0'); s++; }
    return sign * v;
}
static const char* hexa_read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "hexa_read_file: %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char* p = hexa_alloc(sz + 1);
    fread(p, 1, sz, f); p[sz] = 0; fclose(f);
    return p;
}
static long hexa_write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = strlen(content);
    fwrite(content, 1, n, f); fclose(f);
    return (long)n;
}
static long hexa_append_file(const char* path, const char* content) {
    FILE* f = fopen(path, "ab");
    if (!f) return 0;
    size_t n = strlen(content);
    fwrite(content, 1, n, f); fclose(f);
    return (long)n;
}
static const char* hexa_read_stdin(void) {
    char buf[8192]; size_t total = 0;
    char* out = hexa_alloc(65536);
    size_t r; while ((r = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        if (total + r >= 65535) break;
        memcpy(out + total, buf, r); total += r;
    }
    out[total] = 0; return out;
}
static const char* hexa_exec(const char* cmd) {
    FILE* f = popen(cmd, "r");
    if (!f) return "";
    char buf[8192]; size_t total = 0;
    char* out = hexa_alloc(8192);
    size_t r; while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (total + r >= 8191) break;
        memcpy(out + total, buf, r); total += r;
    }
    out[total] = 0; pclose(f);
    while (total > 0 && (out[total-1] == '\n' || out[total-1] == '\r')) { out[--total] = 0; }
    return out;
}
typedef struct { long* d; long n; long cap; } hexa_arr;
static hexa_arr hexa_arr_new(void) { hexa_arr a = {NULL, 0, 0}; return a; }
static hexa_arr hexa_arr_lit(const long* items, long n) {
    hexa_arr a; a.d = (long*)malloc((n>0?n:1)*sizeof(long)); a.n = n; a.cap = (n>0?n:1);
    if (n > 0) memcpy(a.d, items, n*sizeof(long));
    return a;
}
static hexa_arr hexa_arr_push(hexa_arr a, long x) {
    if (a.n >= a.cap) {
        a.cap = a.cap ? a.cap * 2 : 4;
        a.d = (long*)realloc(a.d, a.cap * sizeof(long));
    }
    a.d[a.n++] = x;
    return a;
}
static long hexa_arr_len(hexa_arr a) { return a.n; }
static long hexa_arr_get(hexa_arr a, long i) { return a.d[i]; }
static hexa_arr hexa_chars(const char* s) {
    long n = (long)strlen(s);
    hexa_arr a; a.d = (long*)malloc((n>0?n:1)*sizeof(long)); a.n = n; a.cap = (n>0?n:1);
    for (long i = 0; i < n; i++) a.d[i] = (long)(unsigned char)s[i];
    return a;
}
#include <ctype.h>
static long hexa_is_alpha(long c) { return (long)(isalpha((int)c) ? 1 : 0); }
static long hexa_is_alnum(long c) { return (long)(isalnum((int)c) ? 1 : 0); }
static int hexa_main_argc = 0;
static char** hexa_main_argv = NULL;
static hexa_arr hexa_args(void) {
    hexa_arr a; a.n = hexa_main_argc; a.cap = hexa_main_argc;
    a.d = (long*)malloc((hexa_main_argc>0?hexa_main_argc:1)*sizeof(long));
    for (int i = 0; i < hexa_main_argc; i++) a.d[i] = (long)hexa_main_argv[i];
    return a;
}
static const char* hexa_arg(long i) {
    if (i < 0 || i >= hexa_main_argc) return "";
    return hexa_main_argv[i];
}
static long hexa_argc(void) { return (long)hexa_main_argc; }
static hexa_arr hexa_split(const char* s, const char* sep) {
    hexa_arr a = hexa_arr_new();
    long sl = (long)strlen(sep); if (sl == 0) { return hexa_arr_push(a, (long)s); }
    const char* cur = s;
    while (1) {
        const char* hit = strstr(cur, sep);
        if (!hit) {
            long ln = (long)strlen(cur);
            char* p = hexa_alloc(ln + 1);
            memcpy(p, cur, ln); p[ln] = 0;
            a = hexa_arr_push(a, (long)p);
            break;
        }
        long ln = hit - cur;
        char* p = hexa_alloc(ln + 1);
        memcpy(p, cur, ln); p[ln] = 0;
        a = hexa_arr_push(a, (long)p);
        cur = hit + sl;
    }
    return a;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    const char* home = hexa_trim(hexa_exec("printenv HOME"));
    const char* root = hexa_concat(home, "/Dev/nexus");
    const char* out = hexa_concat(root, "/shared/reality_map_3d.html");
    const char* data = hexa_concat(root, "/shared/reality_map.json");
    const char* meta = hexa_trim(hexa_exec(hexa_concat(hexa_concat("python3 -c \"import json; d=json.load(open('", data), "')); n=d.get('nodes',[]); print(d.get('version','?'), len(n))\"")));
    printf("%s\n", hexa_concat("source: shared/reality_map.json -> ", meta));
    const char* html = "<!DOCTYPE html>\n<html lang=\"ko\"><head><meta charset=\"UTF-8\">\n<title>n=6 Reality Map 3D (fetch · live)</title>\n<style>\n*{margin:0;padding:0;box-sizing:border-box}\nbody{background:#07071a;color:#eee;font-family:'Segoe UI',sans-serif;overflow:hidden}\n#stats{position:fixed;top:12px;left:50%;transform:translateX(-50%);background:rgba(10,10,30,.92);border:1px solid #334;border-radius:8px;padding:10px 24px;z-index:100;font-size:13px;display:flex;gap:18px;backdrop-filter:blur(6px)}\n#stats b{color:#fff}\n.exact{color:#4fc3f7}.close{color:#ffd54f}.miss{color:#ef5350}.total{color:#b0bec5}\n#tooltip{position:fixed;display:none;background:rgba(10,10,30,.95);border:1px solid #556;border-radius:6px;padding:10px 14px;font-size:12px;max-width:380px;z-index:200;pointer-events:none;backdrop-filter:blur(8px);line-height:1.5}\n.grade-tag{display:inline-block;padding:1px 8px;border-radius:3px;font-weight:bold;font-size:11px;margin-left:6px}\n.grade-EXACT{background:#1565c0;color:#fff}.grade-CLOSE{background:#f9a825;color:#000}.grade-MISS{background:#c62828;color:#fff}\n#legend{position:fixed;bottom:12px;left:12px;background:rgba(10,10,30,.9);border:1px solid #334;border-radius:8px;padding:10px 14px;font-size:12px;z-index:100;line-height:1.7;max-width:260px}\n#legend .dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:6px;vertical-align:middle}\n#controls{position:fixed;top:12px;right:12px;background:rgba(10,10,30,.9);border:1px solid #334;border-radius:8px;padding:10px 14px;font-size:12px;z-index:100;line-height:1.9;min-width:200px}\n#controls label{display:block;cursor:pointer;user-select:none}\n#controls input[type=checkbox]{margin-right:6px;vertical-align:middle}\n#controls .group{margin-top:6px;border-top:1px solid #223;padding-top:6px}\n#status{position:fixed;left:12px;top:12px;background:rgba(10,10,30,.9);border:1px solid #334;border-radius:8px;padding:8px 12px;font-size:12px;z-index:100}\ncanvas{display:block}\n</style></head><body>\n<div id=\"status\">loading…</div>\n<div id=\"stats\" style=\"display:none\">\n  <span class=\"total\">Total <b id=\"s-total\">0</b></span>\n  <span class=\"exact\">EXACT <b id=\"s-exact\">0</b></span>\n  <span class=\"close\">CLOSE <b id=\"s-close\">0</b></span>\n  <span class=\"miss\">MISS <b id=\"s-miss\">0</b></span>\n  <span class=\"total\">|</span>\n  <span class=\"exact\">EXACT <b id=\"s-pct\">0</b>%</span>\n  <span class=\"total\">v<b id=\"s-ver\">?</b></span>\n</div>\n<div id=\"tooltip\"></div>\n<div id=\"controls\">\n  <b>Origin 필터</b>\n  <label><input type=\"checkbox\" id=\"f-natural\" checked> <span style=\"color:#66ff99\">natural</span></label>\n  <label><input type=\"checkbox\" id=\"f-engineering\" checked> <span style=\"color:#ffaa55\">engineering</span></label>\n  <label><input type=\"checkbox\" id=\"f-convention\" checked> <span style=\"color:#aaaaaa\">convention</span></label>\n  <div class=\"group\"><b>Grade 필터</b>\n  <label><input type=\"checkbox\" id=\"g-EXACT\" checked> <span class=\"exact\">EXACT</span></label>\n  <label><input type=\"checkbox\" id=\"g-CLOSE\" checked> <span class=\"close\">CLOSE</span></label>\n  <label><input type=\"checkbox\" id=\"g-MISS\" checked> <span class=\"miss\">MISS</span></label>\n  </div>\n  <div class=\"group\"><b>Edges</b>\n  <label><input type=\"checkbox\" id=\"e-parent\" checked> parent</label>\n  <label><input type=\"checkbox\" id=\"e-sibling\" checked> sibling</label>\n  <label><input type=\"checkbox\" id=\"e-thread\" checked> thread</label>\n  </div>\n  <div class=\"group\"><b>색상 모드</b>\n  <label><input type=\"radio\" name=\"cmode\" value=\"origin\" checked> origin</label>\n  <label><input type=\"radio\" name=\"cmode\" value=\"level\"> level</label>\n  <label><input type=\"radio\" name=\"cmode\" value=\"grade\"> grade</label>\n  </div>\n</div>\n<div id=\"legend\">\n  <b>축</b><br>X = level (L0→L6) · Y = grade(EXACT↑) · Z = origin<br>\n  <b>Origin 색상</b><br>\n  <span class=\"dot\" style=\"background:#66ff99\"></span>natural\n  <span class=\"dot\" style=\"background:#ffaa55;margin-left:6px\"></span>engineering\n  <span class=\"dot\" style=\"background:#aaaaaa;margin-left:6px\"></span>convention<br>\n  <b>조작</b>: 좌클릭 회전 · 우클릭 이동 · 휠 줌\n</div>\n<script type=\"importmap\">\n{\"imports\":{\"three\":\"https://unpkg.com/three@0.160.0/build/three.module.js\",\"three/addons/\":\"https://unpkg.com/three@0.160.0/examples/jsm/\"}}\n</script>\n<script type=\"module\">\nimport * as THREE from 'three';\nimport { OrbitControls } from 'three/addons/controls/OrbitControls.js';\n\nconst LEVEL_ORDER=['L0_particle','L1_atom','L2_bond','L3_molecule','L4_genetic','L5_material','L5_bio','L6_geology','L6_meteorology','L6_economics','L6_linguistics','L6_music'];\nconst LEVEL_COLOR={L0_particle:0xef5350,L1_atom:0xff9800,L2_bond:0xffee58,L3_molecule:0x66bb6a,L4_genetic:0x42a5f5,L5_material:0xab47bc,L5_bio:0xec407a,L6_geology:0x8d6e63,L6_meteorology:0x29b6f6,L6_economics:0x9ccc65,L6_linguistics:0xba68c8,L6_music:0xffd54f};\nconst ORIGIN_COLOR={natural:0x66ff99,engineering:0xffaa55,convention:0xaaaaaa};\nconst GRADE_COLOR={EXACT:0x4fc3f7,CLOSE:0xffd54f,MISS:0xef5350};\n\nconst statusEl=document.getElementById('status');\nconst tooltipEl=document.getElementById('tooltip');\n\nlet scene,camera,renderer,controls,raycaster,mouse;\nlet nodeMeshes=[]; let edgeLines={parent:null,sibling:null,thread:null};\nlet DATA=null;\n\ninit();\nloadData();\n\nfunction init(){\n  scene=new THREE.Scene();\n  scene.background=new THREE.Color(0x07071a);\n  scene.fog=new THREE.Fog(0x07071a,200,900);\n  camera=new THREE.PerspectiveCamera(55,innerWidth/innerHeight,0.1,3000);\n  camera.position.set(180,140,260);\n  renderer=new THREE.WebGLRenderer({antialias:true});\n  renderer.setSize(innerWidth,innerHeight);\n  renderer.setPixelRatio(devicePixelRatio);\n  document.body.appendChild(renderer.domElement);\n  controls=new OrbitControls(camera,renderer.domElement);\n  controls.enableDamping=true;\n  scene.add(new THREE.AmbientLight(0xffffff,0.7));\n  const dl=new THREE.DirectionalLight(0xffffff,0.6); dl.position.set(1,2,1); scene.add(dl);\n  // axes\n  scene.add(new THREE.AxesHelper(80));\n  raycaster=new THREE.Raycaster(); mouse=new THREE.Vector2();\n  addEventListener('resize',()=>{camera.aspect=innerWidth/innerHeight;camera.updateProjectionMatrix();renderer.setSize(innerWidth,innerHeight);});\n  addEventListener('mousemove',onMove);\n  // filter listeners\n  document.querySelectorAll('#controls input').forEach(el=>el.addEventListener('change',rebuild));\n  animate();\n}\n\nasync function loadData(){\n  try{\n    const r=await fetch('reality_map.json',{cache:'no-store'});\n    if(!r.ok) throw new Error('HTTP '+r.status);\n    DATA=await r.json();\n    statusEl.style.display='none';\n    document.getElementById('stats').style.display='flex';\n    rebuild();\n  }catch(e){\n    statusEl.innerHTML='<b style=\"color:#ef5350\">load failed</b>: '+e.message+'<br><span style=\"color:#888\">place reality_map.json next to this html, or serve via http</span>';\n  }\n}\n\nfunction nodePos(n,idx,total){\n  const li=Math.max(0,LEVEL_ORDER.indexOf(n.level));\n  const x=(li-5.5)*28;\n  const gradeY={EXACT:60,CLOSE:20,MISS:-20}[n.grade]||0;\n  const originZ={natural:-60,engineering:60,convention:0}[n.origin]||0;\n  // jitter via id hash to spread\n  let h=0; const s=n.id||('n'+idx); for(let i=0;i<s.length;i++){h=(h*31+s.charCodeAt(i))|0;}\n  const jx=((h&255)/255-0.5)*22;\n  const jy=(((h>>8)&255)/255-0.5)*22;\n  const jz=(((h>>16)&255)/255-0.5)*22;\n  return [x+jx,gradeY+jy,originZ+jz];\n}\n\nfunction colorFor(n,mode){\n  if(mode==='origin') return ORIGIN_COLOR[n.origin]||0xffffff;\n  if(mode==='level')  return LEVEL_COLOR[n.level]||0xffffff;\n  return GRADE_COLOR[n.grade]||0xffffff;\n}\n\nfunction rebuild(){\n  if(!DATA) return;\n  // wipe\n  nodeMeshes.forEach(m=>{scene.remove(m);m.geometry.dispose();m.material.dispose();});\n  nodeMeshes=[];\n  for(const k of Object.keys(edgeLines)){ if(edgeLines[k]){scene.remove(edgeLines[k]);edgeLines[k].geometry.dispose();edgeLines[k].material.dispose();edgeLines[k]=null;} }\n\n  const filt={\n    natural:document.getElementById('f-natural').checked,\n    engineering:document.getElementById('f-engineering').checked,\n    convention:document.getElementById('f-convention').checked,\n    EXACT:document.getElementById('g-EXACT').checked,\n    CLOSE:document.getElementById('g-CLOSE').checked,\n    MISS:document.getElementById('g-MISS').checked,\n  };\n  const cmode=document.querySelector('input[name=cmode]:checked').value;\n\n  const nodes=DATA.nodes||[];\n  const posMap={};\n  const visible={};\n  let cE=0,cC=0,cM=0;\n  nodes.forEach((n,i)=>{\n    const p=nodePos(n,i,nodes.length);\n    posMap[n.id]=p;\n    if(!filt[n.origin]) return;\n    if(!filt[n.grade]) return;\n    visible[n.id]=true;\n    if(n.grade==='EXACT')cE++; else if(n.grade==='CLOSE')cC++; else if(n.grade==='MISS')cM++;\n    const geom=new THREE.SphereGeometry(1.6,12,10);\n    const mat=new THREE.MeshStandardMaterial({color:colorFor(n,cmode),emissive:colorFor(n,cmode),emissiveIntensity:0.35,roughness:0.5});\n    const mesh=new THREE.Mesh(geom,mat);\n    mesh.position.set(p[0],p[1],p[2]);\n    mesh.userData=n;\n    scene.add(mesh); nodeMeshes.push(mesh);\n  });\n\n  // edges\n  function buildEdges(list,color,key,enabled){\n    if(!enabled||!list) return;\n    const pts=[];\n    list.forEach(e=>{\n      const a=e.from||e[0]||e.parent||e.src||e.source;\n      const b=e.to  ||e[1]||e.child ||e.dst||e.target;\n      if(!a||!b) return;\n      if(!visible[a]||!visible[b]) return;\n      const pa=posMap[a],pb=posMap[b]; if(!pa||!pb) return;\n      pts.push(pa[0],pa[1],pa[2],pb[0],pb[1],pb[2]);\n    });\n    if(!pts.length) return;\n    const g=new THREE.BufferGeometry();\n    g.setAttribute('position',new THREE.Float32BufferAttribute(pts,3));\n    const m=new THREE.LineBasicMaterial({color,transparent:true,opacity:0.45});\n    const l=new THREE.LineSegments(g,m);\n    scene.add(l); edgeLines[key]=l;\n  }\n  buildEdges(DATA.parent_edges,0xffffff,'parent',document.getElementById('e-parent').checked);\n  buildEdges(DATA.sibling_edges,0x888888,'sibling',document.getElementById('e-sibling').checked);\n  buildEdges(DATA.thread_edges,0x4fc3f7,'thread',document.getElementById('e-thread').checked);\n\n  const total=cE+cC+cM;\n  document.getElementById('s-total').textContent=total;\n  document.getElementById('s-exact').textContent=cE;\n  document.getElementById('s-close').textContent=cC;\n  document.getElementById('s-miss').textContent=cM;\n  document.getElementById('s-pct').textContent=total?((cE/total)*100).toFixed(1):'0';\n  document.getElementById('s-ver').textContent=(DATA._meta&&DATA._meta.version)||DATA.version||'?';\n}\n\nfunction onMove(ev){\n  mouse.x=(ev.clientX/innerWidth)*2-1;\n  mouse.y=-(ev.clientY/innerHeight)*2+1;\n  raycaster.setFromCamera(mouse,camera);\n  const hits=raycaster.intersectObjects(nodeMeshes);\n  if(hits.length){\n    const n=hits[0].object.userData;\n    tooltipEl.style.display='block';\n    tooltipEl.style.left=(ev.clientX+14)+'px';\n    tooltipEl.style.top=(ev.clientY+14)+'px';\n    tooltipEl.innerHTML='<b>'+(n.id||'')+'</b><span class=\"grade-tag grade-'+n.grade+'\">'+n.grade+'</span><br>'+\n      '<span style=\"color:#aaa\">'+(n.level||'')+' · '+(n.origin||'')+'</span><br>'+\n      (n.claim||'')+'<br>'+\n      '<span style=\"color:#888\">measured:</span> '+(n.measured!==undefined?n.measured:'?')+' '+(n.unit||'')+'<br>'+\n      '<span style=\"color:#888\">n6:</span> '+(n.n6_expr||'?')+' = '+(n.n6_value!==undefined?n.n6_value:'?')+'<br>'+\n      (n.cause?('<span style=\"color:#888\">cause:</span> '+n.cause):'');\n  } else {\n    tooltipEl.style.display='none';\n  }\n}\n\nfunction animate(){ requestAnimationFrame(animate); controls.update(); renderer.render(scene,camera); }\n</script></body></html>\n";
    hexa_write_file(out, html);
    printf("%s\n", hexa_concat("wrote: ", out));
    const char* bytes = hexa_trim(hexa_exec(hexa_concat("wc -c < ", out)));
    printf("%s\n", hexa_concat("bytes: ", bytes));
    const char* same = hexa_trim(hexa_exec(hexa_concat(hexa_concat("test -f ", root), "/shared/reality_map.json && echo OK || echo MISS")));
    printf("%s\n", hexa_concat("reality_map.json sibling: ", same));
    printf("%s\n", hexa_concat("source meta: ", meta));
    printf("%s\n", "done.");
    return 0;
}
