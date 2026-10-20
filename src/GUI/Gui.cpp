#include "Gui.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <set>
#include <sys/stat.h>

#include "../Interfaces.h"
#include "../Settings.h"
#include "../Hooks/Hooks.h"
#include "../Utils/Memory.h"
#include "../Utils/Util.h"

// The whole UI is Panorama JS, so no IUIPanel vtable is needed on the native side.
// JS -> native: the menu encodes its state in the id of a child panel ("D2H:<timestamp>:acc=1,dist=1200,ls=1,lss=10");
// PollBridge() scans the UI engine's panel table for it. Scripts run from inside Panorama never reach our
// RunScript hook, so an id is the one thing JS can write that native can read with verified offsets.
static const char* toggleMenuJS = R"JS((function(){
var ctx=$.GetContextPanel(); var root=ctx; while(root.GetParent()) root=root.GetParent();
var seq=0; function log(s){ try{ var q=$.CreatePanel('Panel',root,'D2HLOG:'+Date.now()+'_'+(seq++)+':'+String(s).replace(/\s+/g,'_')); q.DeleteAsync(4); }catch(e){} }
var p=root.FindChildTraverse('D2HMain');
if(p){ p.visible=!p.visible; log('menu toggled visible='+p.visible+' size='+p.actuallayoutwidth+'x'+p.actuallayoutheight+' at '+p.actualxoffset+','+p.actualyoffset+' root='+root.id+' '+root.actuallayoutwidth+'x'+root.actuallayoutheight); return; }
var host=root.FindChildTraverse('HUDElements')||root;
p=$.CreatePanel('Panel',host,'D2HMain');
log('menu created under '+host.id+' (root '+root.id+' '+root.actuallayoutwidth+'x'+root.actuallayoutheight+', ctx '+ctx.id+')');
var s=p.style; s.width='380px'; s.height='fit-children'; s.horizontalAlign='center'; s.verticalAlign='center';
s.backgroundColor='#1c1d20ee'; s.border='2px solid #666'; s.padding='14px'; s.flowChildren='down'; s.zIndex=1000;
p.hittest=true;
var st={acc:%s?1:0, dist:%d, ls:%s?1:0, lss:%d};
var bus=$.CreatePanel('Panel',p,'D2HBus'); bus.style.width='0px'; bus.style.height='0px';
function bridge(k,v){ st[k]=v; bus.RemoveAndDeleteChildren(); $.CreatePanel('Panel',bus,'D2H:'+Date.now()+':acc='+st.acc+',dist='+st.dist+',ls='+st.ls+',lss='+st.lss); }
function label(parent,txt){ var l=$.CreatePanel('Label',parent,''); l.text=txt; l.style.color='#eee'; l.style.fontSize='16px'; return l; }
function button(parent,txt,fn){ var b=$.CreatePanel('Button',parent,''); b.style.padding='4px 12px'; b.style.margin='0px 4px'; b.style.backgroundColor='#333'; b.style.border='1px solid #777'; label(b,txt); b.SetPanelEvent('onactivate',fn); return b; }
function toggle(txt,key){
  var row=$.CreatePanel('Panel',p,''); row.style.flowChildren='right'; row.style.marginBottom='8px';
  var on=st[key]==1;
  var b=button(row,on?'ON':'OFF',function(){ on=!on; b.GetChild(0).text=on?'ON':'OFF'; b.style.backgroundColor=on?'#2a6a2a':'#333'; bridge(key,on?1:0); });
  b.style.width='64px'; b.style.backgroundColor=on?'#2a6a2a':'#333';
  var l=label(row,txt); l.style.verticalAlign='center'; l.style.marginLeft='8px';
}
function stepper(txt,key,min,max,step){
  var row=$.CreatePanel('Panel',p,''); row.style.flowChildren='right'; row.style.marginBottom='8px';
  var lbl;
  button(row,'-',function(){ st[key]=Math.max(min,st[key]-step); lbl.text=''+st[key]; bridge(key,st[key]); });
  lbl=label(row,''+st[key]); lbl.style.width='60px'; lbl.style.textAlign='center'; lbl.style.verticalAlign='center';
  button(row,'+',function(){ st[key]=Math.min(max,st[key]+step); lbl.text=''+st[key]; bridge(key,st[key]); });
  var l=label(row,txt); l.style.verticalAlign='center'; l.style.marginLeft='8px';
}
var t=label(p,'Dota2Helper'); t.style.fontSize='22px'; t.style.marginBottom='12px'; t.style.color='#fff';
toggle('Auto-accept match','acc');
stepper('Camera distance','dist',800,3000,100);
toggle('Enemy last-seen on minimap','ls');
stepper('Last-seen marker lifetime (sec)','lss',1,60,1);
var c=button(p,'Close (END)',function(){ p.visible=false; }); c.style.horizontalAlign='right'; c.style.marginTop='6px';
})();)JS";

static const char* destroyMenuJS = // in game the menu sits under HUDElements and the overlay under #minimap, so search the whole tree
    "(function(){ var r=$.GetContextPanel(); while(r.GetParent()) r=r.GetParent(); ['D2HMain','D2HMinimap','D2HGround'].forEach(function(id){ var p=r.FindChildTraverse(id); if(p) p.DeleteAsync(0); }); })();";


// The whole minimap overlay, run on every push in the current HUD context: finds #minimap by id (the first DOTAMinimap
// in the tree is the collapsed hotkey map #overlaymap), finds or creates the icons by id, places them and hides the rest.
// Nothing outlives one run, so a new HUD or context needs no setup. Logs its state when it changes, and every 10 s.
// It also (re)defines D2H_ground, which places the ground icons every Panorama frame at the positions from UI::UpdateGround.
static const char* pushMarkersJS = R"JS(var D2H_sig, D2H_g, D2H_gstate, D2H_wst, D2H_wards, D2H_loop, D2H_bld, D2H_clk, D2H_mm, D2H_tl;
// "12s" under a marker icon (seconds since the enemy was last seen), or "TP 2s" left until a teleport lands, centred on cx
D2H_timer=function(p,id,m,cx,top,w,fs){
  var lb=p.FindChild(id), t=m.t?'TP '+Math.max(0,Math.ceil(m.c-m.a))+'s':Math.floor(m.a)+'s';
  if(!lb){ lb=$.CreatePanel('Label',p,id); lb.hittest=false; var s=lb.style; s.width=w+'px'; s.textAlign='center'; s.fontSize=fs+'px'; s.fontWeight='bold'; s.color='#ffffff'; s.textShadow='0px 0px 3px 4.0 #000000'; }
  if(lb.text!=t) lb.text=t;
  lb.style.position=(cx-w/2)+'px '+top+'px 0px'; lb.visible=true;
};
// ability icons with cooldown seconds under every enemy hero in sight. The game's JS entity API returns nothing in matches,
// so D2H_units (abilities, wards) comes from the native side 4 times a second and D2H_pos (where they are) every frame
D2H_cds=function(g){
  var el=Game.GetGameTime()-D2H_ut;
  D2H_units.forEach(function(u){
    var o=D2H_pos[u.i]||[u.x,u.y,u.z], x=Game.WorldToScreenX(o[0],o[1],o[2]), y=Game.WorldToScreenY(o[0],o[1],o[2]);
    if(!(x>=0&&y>=0&&x<=Game.GetScreenWidth()&&y<=Game.GetScreenHeight())) return;
    var id='cd'+u.i, row=g.FindChild(id), k=0;
    if(!row){ row=$.CreatePanel('Panel',g,id); row.hittest=false; row.hittestchildren=false; row.style.flowChildren='right'; }
    u.ab.forEach(function(a){
      var ic=row.GetChild(k);
      if(!ic){ ic=$.CreatePanel('DOTAAbilityImage',row,''); ic.hittest=false; var st=ic.style; st.width=st.height='24px'; st.marginRight='2px';
        var lb=$.CreatePanel('Label',ic,''); lb.hittest=false; var ls=lb.style; ls.width='100%'; ls.height='100%'; ls.textAlign='center'; ls.verticalAlign='center'; ls.fontSize='15px'; ls.fontWeight='bold'; ls.color='#ffffff'; ls.textShadow='0px 0px 3px 4.0 #000000';
        var pp=$.CreatePanel('Panel',ic,''); pp.hittest=false; var ps=pp.style; ps.flowChildren='right'; ps.horizontalAlign='center'; ps.verticalAlign='bottom'; ps.marginBottom='1px'; }
      if(ic.abilityname!=a[0]) ic.abilityname=a[0];
      var cd=a[3]-el, t=cd>0?''+Math.ceil(cd):'', lb=ic.GetChild(0);
      if(lb.text!=t) lb.text=t;
      var lv=a[1], mx=a[2], pp=ic.GetChild(1), key=lv+'/'+mx; // level pips like the ability bar
      if(pp.D2H_key!=key){ pp.D2H_key=key; pp.RemoveAndDeleteChildren();
        for(var i=0;i<mx;i++){ var q=$.CreatePanel('Panel',pp,''); var qs=q.style; qs.width='4px'; qs.height='3px'; qs.marginRight='1px'; qs.backgroundColor=i<lv?'#f0c030':'#303030'; } }
      ic.style.saturation=lv>0?'1':'0'; ic.style.opacity=cd>0?'0.4':'0.6'; ic.visible=true; k++;
    });
    for(var r=k;r<row.GetChildCount();r++) row.GetChild(r).visible=false;
    row.style.position=(x/g.actualuiscale_x-k*13)+'px '+(y/g.actualuiscale_y+28)+'px 0px'; row.visible=true;
  });
};
// enemy ward tracker: counts observer/sentry charges while an enemy is visible (fogged data is frozen); a drop while we keep
// him in sight means a ward was placed on the spot. A drop found when he comes out of the fog is ignored: no telling where
D2H_wst=D2H_wst||{}; D2H_wards=D2H_wards||[];
D2H_wardscan=function(){
  var now=Game.GetGameTime(), seen={};
  D2H_units.forEach(function(u){
    if(!u.w) return; // enemies in sight and alive: the native side leaves out the fog and the dead
    var e=u.i, st=D2H_wst[e]||(D2H_wst[e]={}), obs=u.w[0], sen=u.w[1], o=D2H_pos[e]||[u.x,u.y,u.z];
    seen[e]=1;
    if(st.vis) [['obs',st.obs-obs,360],['sen',st.sen-sen,420]].forEach(function(k){
      if(k[1]>0) D2H_wards.push({k:k[0],n:k[1],t:now,life:k[2],x:o[0],y:o[1],z:o[2]});
    });
    st.obs=obs; st.sen=sen; st.vis=true;
  });
  for(var k in D2H_wst) if(!seen[k]) D2H_wst[k].vis=false;
  D2H_wards=D2H_wards.filter(function(w){ return now-w.t<w.life; });
};
// yellow ring panel `id` under p, centre (cx,cy), radii rx,ry in layout px
D2H_mmss=function(s){ s=Math.max(0,Math.ceil(s)); return Math.floor(s/60)+':'+(s%60<10?'0':'')+s%60; };
D2H_circle=function(p,id,w,cx,cy,rx,ry,fs){
  var c=p.FindChild(id);
  if(!c){ c=$.CreatePanel('Panel',p,id); c.hittest=false; var s=c.style; s.borderRadius='50%'; s.border='2px solid #ffd020';
    var l=$.CreatePanel('Label',c,''); l.hittest=false; var ls=l.style; ls.horizontalAlign='center'; ls.verticalAlign='center'; ls.fontSize=fs+'px'; ls.fontWeight='bold'; ls.color='#ffe060'; ls.textShadow='0px 0px 3px 4.0 #000000'; }
  c.style.width=(2*rx)+'px'; c.style.height=(2*ry)+'px'; c.style.position=(cx-rx)+'px '+(cy-ry)+'px 0px';
  var t=w.k+(w.n>1?' x'+w.n:'')+' '+D2H_mmss(w.life-(Game.GetGameTime()-w.t)), l=c.GetChild(0); if(l.text!=t) l.text=t;
  c.visible=true;
};
D2H_ground=function(){
  var g=D2H_g, shown='';
  if(!(g&&g.IsValid()&&g.actualuiscale_x&&typeof D2H_pos=='object')) return;
  try{
    for(var i=0;i<g.GetChildCount();i++) g.GetChild(i).visible=false;
    D2H_cds(g);
    D2H_wardscan();
    D2H_wards.forEach(function(w,i){
      var x=Game.WorldToScreenX(w.x,w.y,w.z), y=Game.WorldToScreenY(w.x,w.y,w.z), r=100; // ring radius on the ground, world units
      if(x<-2000||y<-2000||x>Game.GetScreenWidth()+2000||y>Game.GetScreenHeight()+2000) return;
      var rx=Math.abs(Game.WorldToScreenX(w.x+r,w.y,w.z)-x), yf=Game.WorldToScreenY(w.x,w.y+r,w.z), yn=Game.WorldToScreenY(w.x,w.y-r,w.z); // lying flat: the far and near points squash it
      D2H_circle(g,'gw'+Math.round(w.t*1000)+w.k,w,x/g.actualuiscale_x,(yf+yn)/2/g.actualuiscale_y,rx/g.actualuiscale_x,Math.abs(yn-yf)/2/g.actualuiscale_y,16);
    });
    if(D2H_cfg.on) D2H_marks.forEach(function(m){
      var x=Game.WorldToScreenX(m.x,m.y,m.z), y=Game.WorldToScreenY(m.x,m.y,m.z);
      if(!(x>=0&&y>=0&&x<=Game.GetScreenWidth()&&y<=Game.GetScreenHeight())) return;
      var id='g'+m.n+m.i, ic=g.FindChild(id);
      if(!ic){ ic=$.CreatePanel('DOTAHeroImage',g,id); ic.hittest=false; ic.heroname=m.n; ic.heroimagestyle='icon'; var s=ic.style; s.width=s.height='40px'; s.borderRadius='50%'; s.border='2px solid '+(m.t?'#30a0ff':'#ff3030'); }
      ic.style.position=(x/g.actualuiscale_x-20)+'px '+(y/g.actualuiscale_y-20)+'px 0px';
      ic.style.opacity=''+Math.max(0.35,1-m.a/Math.max(1,D2H_cfg.secs)*0.6);
      ic.visible=true; shown+=' '+id;
      D2H_timer(g,'t'+id,m,x/g.actualuiscale_x,y/g.actualuiscale_y+22,60,16);
    });
    D2H_wavedots();
    D2H_gstate='ground'+(shown||'_none');
  }catch(e){ D2H_gstate='ground_error_'+e; }
};
// placed in Panorama's own frame, with the camera it lays out with: placed from the native render-end stage it lagged a frame
if(!D2H_loop){ D2H_loop=1; (function tick(){ D2H_ground(); $.Schedule(0,tick); })(); }
// enemy creep waves out of sight, walking their lanes: one leaves each enemy barracks every 30 s of the game clock at 325/s
// (npc_units.txt), along the lane through both sides' towers, until our first creep or standing tower on it would see it
D2H_bld=D2H_bld||{}; D2H_clk=D2H_clk||{};
D2H_waves=function(lb){ // the top bar's clock label -> [[x,y],...]
  var out=[], m=lb&&lb.IsValid()&&/^(-?)(?:(\d+):)?(\d+):(\d\d)$/.exec(lb.text), creeps=[];
  if(typeof D2H_lane!='object'||!m) return out;
  var sec=((+m[2]||0)*3600+(+m[3])*60+(+m[4]))*(m[1]?-1:1), now=Game.GetGameTime();
  if(D2H_clk.s!==sec) D2H_clk={s:sec,t:now}; // the clock shows whole seconds: the game time since it last ticked is the fraction
  var clock=sec+Math.min(1,now-D2H_clk.t), us=D2H_lane.t==2?'npc_dota_goodguys':'npc_dota_badguys', them=D2H_lane.t==2?'npc_dota_badguys':'npc_dota_goodguys';
  D2H_lane.p.forEach(function(p){ if(p[0]) D2H_bld[p[0]]=p; else if(p[3]) creeps.push(p); }); // a fallen tower keeps its spot on the lane
  ['top','mid','bot'].forEach(function(lane){
    var B=function(n){ return D2H_bld[n+'_'+lane]; }, pts=[], g1=B('npc_dota_goodguys_tower1')||B('npc_dota_goodguys_tower2'), b1=B('npc_dota_badguys_tower1')||B('npc_dota_badguys_tower2');
    [them+'_melee_rax',them+'_tower3',them+'_tower2',them+'_tower1'].forEach(function(n){ if(B(n)) pts.push(B(n)); });
    if(lane!='mid'&&g1&&b1) pts.push(['',lane=='top'?g1[1]:b1[1],lane=='top'?b1[2]:g1[2],0]); // a side lane turns in the corner: radiant's run up the left and along the bottom
    [us+'_tower1',us+'_tower2',us+'_tower3',us+'_melee_rax'].forEach(function(n){ if(B(n)) pts.push(B(n)); });
    if(pts.length<2) return;
    var len=[0], front;
    for(var i=1;i<pts.length;i++) len.push(len[i-1]+Math.hypot(pts[i][1]-pts[i-1][1],pts[i][2]-pts[i-1][2]));
    front=len[len.length-1];
    pts.forEach(function(p,i){ if(p[0].indexOf(us)==0&&p[3]) front=Math.min(front,len[i]-1800); }); // a standing tower of ours sees 1800 ahead
    creeps.forEach(function(c){ // and a creep of ours on this lane 700
      for(var i=1;i<pts.length;i++){
        var ax=pts[i-1][1], ay=pts[i-1][2], dx=pts[i][1]-ax, dy=pts[i][2]-ay, l=Math.hypot(dx,dy)||1, u=Math.max(0,Math.min(1,((c[1]-ax)*dx+(c[2]-ay)*dy)/(l*l)));
        if(Math.hypot(c[1]-ax-u*dx,c[2]-ay-u*dy)<1200) front=Math.min(front,len[i-1]+u*l-700);
      }
    });
    for(var j=0;j<3;j++){ // the waves that left at the last three :00/:30
      var t0=(Math.floor(clock/30)-j)*30, s=325*(clock-t0);
      if(t0<0||s>=front) continue;
      for(var i=1;i<pts.length;i++) if(s<=len[i]){ var u=(s-len[i-1])/((len[i]-len[i-1])||1); out.push([pts[i-1][1]+u*(pts[i][1]-pts[i-1][1]),pts[i-1][2]+u*(pts[i][2]-pts[i-1][2])]); break; }
    }
  });
  return out;
};
// the dots glide: they move every frame, the rest of the minimap overlay 4 times a second
D2H_wavedots=function(){
  var o=D2H_mm, c=D2H_cfg, k=0, d;
  if(!(o&&o.IsValid())) return;
  var w=o.actuallayoutwidth/o.actualuiscale_x, h=o.actuallayoutheight/o.actualuiscale_y;
  if(c.on&&w) D2H_waves(D2H_tl).forEach(function(p){ // an enemy wave out of sight on its lane: orange dot
    var id='wv'+k++, ic=o.FindChild(id);
    if(!ic){ ic=$.CreatePanel('Panel',o,id); ic.hittest=false; var ws=ic.style; ws.width=ws.height='10px'; ws.borderRadius='50%'; ws.backgroundColor='#e07010d0'; ws.border='1px solid #000000'; }
    ic.style.position=((p[0]-c.x0)/c.span*w-5)+'px '+((c.y0-p[1])/c.span*h-5)+'px 0px'; ic.visible=true;
  });
  for(;(d=o.FindChild('wv'+k));k++) d.visible=false;
};
(function(){
var r=$.GetContextPanel(); while(r.GetParent()) r=r.GetParent();
var c=D2H_cfg, state='no_minimap_under_'+r.id, ids='', pos='';
try{
  var tb=r.FindChildTraverse('topbar');
  if(!(D2H_g&&D2H_g.IsValid())){ // first child of HUDElements: under every HUD element, and not mirrored by .HUDFlipped
    var e=r.FindChildTraverse('HUDElements')||r; D2H_g=e.FindChild('D2HGround');
    if(!D2H_g){ D2H_g=$.CreatePanel('Panel',e,'D2HGround'); D2H_g.hittest=false; D2H_g.hittestchildren=false; D2H_g.style.width='100%'; D2H_g.style.height='100%'; e.MoveChildBefore(D2H_g,e.GetChild(0)); }
  }
  var mm=r.FindChildTraverse('minimap'), o=mm&&mm.FindChild('D2HMinimap');
  if(mm&&!o){ o=$.CreatePanel('Panel',mm,'D2HMinimap'); o.hittest=false; o.hittestchildren=false; o.style.width='100%'; o.style.height='100%'; }
  if(o){
    var w=o.actuallayoutwidth/o.actualuiscale_x, h=o.actuallayoutheight/o.actualuiscale_y;
    state='map_'+Math.round(w)+'x'+Math.round(h)+'_on='+c.on;
    D2H_mm=o; D2H_tl=tb&&tb.FindChildTraverse('GameTime');
    for(var i=0;i<o.GetChildCount();i++) if(!/^wv/.test(o.GetChild(i).id)) o.GetChild(i).visible=false; // the wave dots are D2H_wavedots'
    if(c.on&&w) D2H_marks.forEach(function(m){
      var id=m.n+m.i, ic=o.FindChild(id), sz=10;
      var px=(m.x-c.x0)/c.span*w, py=(c.y0-m.y)/c.span*h;
      if(!ic){
        ic=$.CreatePanel('DOTAHeroImage',o,id); ic.hittest=false; ic.heroname=m.n; ic.heroimagestyle='icon';
        var s=ic.style; s.width=s.height=2*sz+'px'; s.borderRadius='50%'; s.border='1px solid '+(m.t?'#30a0ff':'#ff3030');
      }
      ic.style.position=(px-sz)+'px '+(py-sz)+'px 0px';
      ic.style.opacity=''+Math.max(0.35,1-m.a/Math.max(1,c.secs)*0.6);
      ic.visible=true;
      D2H_timer(o,'t'+id,m,px,py+sz,30,11);
      ids+=' '+id; pos+=' '+id+'='+Math.round(px)+','+Math.round(py);
    });
    if(c.on&&w&&typeof D2H_wards=='object') D2H_wards.forEach(function(wd){
      D2H_circle(o,'mw'+Math.round(wd.t*1000)+wd.k,wd,(wd.x-c.x0)/c.span*w,(c.y0-wd.y)/c.span*h,4,4,9);
    });
  }
  // enemy health and mana in the top bar: the game has both bars under every portrait, but feeds and shows only allies'
  for(var pid=0;tb&&typeof D2H_bars=='object'&&pid<10;pid++){
    var s=tb.FindChildTraverse((pid<5?'RadiantPlayer':'DirePlayer')+pid), hi=s&&s.FindChildTraverse('HeroImage');
    if(!hi||!s.BHasClass('EnemyTeam')) continue;
    var b=D2H_bars['npc_dota_hero_'+hi.heroname], dead=s.BHasClass('Dead'); // [health,max,mana,max,visible]
    ['HealthBar','ManaBar'].forEach(function(id,k){
      var bar=s.FindChildTraverse(id); if(!bar) return;
      bar.style.opacity=b&&!dead?(b[4]?'1':'0.5'):'0'; // dimmed: the values from before he went into the fog
      bar.style.height='12px'; bar.style.transform='translateY('+(15+4*k)+'px)'; // under the portrait, where Alt puts the allies' 8px bars
      if(!b) return;
      var v=b[2*k], mx=b[2*k+1], t=v+'/'+mx, lb=bar.FindChild('D2HText');
      bar.value=mx>0?v/mx:0;
      if(!lb){ lb=$.CreatePanel('Label',bar,'D2HText'); lb.hittest=false; var ls=lb.style; ls.width='100%'; ls.textAlign='center'; ls.verticalAlign='center'; ls.fontSize='10px'; ls.fontWeight='bold'; ls.color='#ffffff'; ls.textShadow='0px 0px 2px 3.0 #000000'; }
      if(lb.text!=t) lb.text=t;
    });
  }
}catch(e){ state='error_'+e; }
var sig=state+ids+D2H_gstate+Math.floor(Date.now()/10000);
if(sig!=D2H_sig){ D2H_sig=sig; $.CreatePanel('Panel',r,('D2HLOG:'+Date.now()+'_0:push_ctx_'+$.GetContextPanel().id+'_'+state+pos+(D2H_gstate?' '+D2H_gstate:'')).replace(/[^\w:=,.-]+/g,'_')).DeleteAsync(4); }
})();)JS";

static const char* devScriptPath = "/tmp/d2h_run.js";
static const char* overlayScriptPath = "/tmp/d2h_overlay.js";

static panorama::IUIPanel* HudPanel()
{
	if( Hooks::hudPanel && panoramaEngine->AccessUIEngine()->IsValidPanelPointer( Hooks::hudPanel ) )
		return Hooks::hudPanel;
	Hooks::hudPanel = nullptr;
	return nullptr;
}

// In-game the HUD is its own panel tree, so prefer a live HUD panel seen by the RunScript hook;
// otherwise the dashboard Play page, which exists for the whole session.
static panorama::IUIPanel* ContextPanel()
{
	if( panorama::IUIPanel* hud = HudPanel() )
		return hud;
	panorama::CPanel2D* play = g_ppDBPlayPanel ? *g_ppDBPlayPanel : nullptr;
	return play ? play->UIPanel() : nullptr;
}

static void RunJS( panorama::IUIPanel* context, const char* js )
{
	if( !context ){
		MC_PRINTF_ERROR( "No panel to run UI script in\n" );
		return;
	}
	// raw slot: the SDK header's RunScript index is stale. arg5 is the script origin's line offset: with 0, RunScript reuses
	// the source it cached for this path on the first call (panorama_script_cache_enabled) and never compiles `js`
	uiEngineVMT->GetOriginalMethod<RunScriptFn>( kRunScriptSlot )( panoramaEngine->AccessUIEngine(), context, js, "s2r://panorama/scripts/d2h.vjs", 1 );
}

static bool ReadFile( const char* path, std::string& out )
{
	FILE* f = fopen( path, "rb" );
	if( !f )
		return false;
	char buf[4096];
	size_t n;
	out.clear();
	while( ( n = fread( buf, 1, sizeof( buf ), f ) ) > 0 )
		out.append( buf, n );
	fclose( f );
	return true;
}

// World square the minimap shows: x from mapX, y down from mapY, mapSpan units each way. The 'dota' map's until ReadOverview runs.
static float mapX = -9472.0f, mapY = 9472.0f, mapSpan = 18944.0f;

// #minimap's CDOTA_Hud_Minimap (CUIPanel::ClientPtr, +0x8) owns a CDOTA_PanoramaMinimapRenderer (+0x30) that keeps the current map's
// resource/overviews/<map>.txt at +0x10: pos_x, pos_y, ?, scale, zoom (libclient's loader). The overview image is 1024 px of `scale` units.
static void ReadOverview( panorama::IUIPanel* minimap )
{
	void* client = nullptr;
	void* renderer = nullptr;
	float ov[5];
	if( !Memory::SafeRead( reinterpret_cast<char*>( minimap ) + 0x8, &client, sizeof( client ) ) || !client ||
	    !Memory::SafeRead( reinterpret_cast<char*>( client ) + 0x30, &renderer, sizeof( renderer ) ) || !renderer ||
	    !Memory::SafeRead( reinterpret_cast<char*>( renderer ) + 0x10, ov, sizeof( ov ) ) )
		return;
	if( !( ov[3] > 0.5f && ov[3] < 100.0f ) || fabsf( ov[0] ) > 65536.0f || fabsf( ov[1] ) > 65536.0f )
		return; // not loaded yet (zeroed), or the layout moved
	if( ov[0] != mapX || ov[1] != mapY || ov[3] * 1024.0f != mapSpan ) // shortcut: zoom is only logged (1 on every shipped map), apply it if a map sets it
		Logger::Log( "minimap overview: pos (%g, %g), scale %g, zoom %g", ov[0], ov[1], ov[3], ov[4] );
	mapX = ov[0];
	mapY = ov[1];
	mapSpan = ov[3] * 1024.0f;
}

static std::string WithConfig( const std::string& js )
{
	char head[128];
	snprintf( head, sizeof( head ), "var D2H_cfg={on:%d,secs:%d,x0:%g,y0:%g,span:%g};\n", Settings::lastSeen ? 1 : 0, Settings::lastSeenSecs, mapX, mapY, mapSpan );
	return head + js;
}

void UI::ToggleUI()
{
	static char buffer[8192];
	int distance = Settings::camDistance > 0.0f ? (int)Settings::camDistance : 1200;
	snprintf( buffer, sizeof( buffer ), toggleMenuJS,
	          Settings::autoAccept ? "true" : "false", distance,
	          Settings::lastSeen ? "true" : "false", Settings::lastSeenSecs );

	Logger::Log( "ToggleUI (hud panel %p)", (void*)Hooks::hudPanel );
	RunJS( ContextPanel(), buffer );
}

void UI::Destroy()
{
	RunJS( ContextPanel(), destroyMenuJS );
}

void UI::RunDevScript()
{
	std::string js;
	if( !ReadFile( devScriptPath, js ) ){
		Logger::Log( "dev script %s not found", devScriptPath );
		return;
	}
	Logger::Log( "running dev script (%zu bytes) in %s context", js.size(), HudPanel() ? "HUD" : "dashboard" );
	RunJS( ContextPanel(), WithConfig( js ).c_str() );
}

void UI::PushMarkers( const char* marksJsArray, const char* unitsJsArray, const char* barsJsObject, const char* lanesJsObject )
{
	static panorama::IUIPanel* last = nullptr;
	panorama::IUIPanel* hud = HudPanel();
	if( hud != last ){
		last = hud;
		Logger::Log( "markers: HUD context %p", (void*)hud );
	}
	if( !hud )
		return;
	static std::string overlay; // /tmp/d2h_overlay.js overrides the built-in script while it exists, re-read on every change
	static long overlayMtime = -1;
	struct stat st;
	long mtime = stat( overlayScriptPath, &st ) == 0 ? (long)st.st_mtime : 0;
	if( mtime != overlayMtime ){
		overlayMtime = mtime;
		if( !mtime || !ReadFile( overlayScriptPath, overlay ) )
			overlay.clear();
		Logger::Log( "overlay script: %s", overlay.empty() ? "built-in" : overlayScriptPath );
	}
	RunJS( hud, WithConfig( std::string( "var D2H_marks=" ) + marksJsArray + ";\nvar D2H_units=" + unitsJsArray + ";\nvar D2H_bars=" + barsJsObject + ";\nvar D2H_lane=" + lanesJsObject + ";\nvar D2H_ut=Game.GetGameTime();\n" + ( overlay.empty() ? pushMarkersJS : overlay.c_str() ) ).c_str() );
}

void UI::UpdateGround( const char* positionsJsObject )
{
	if( panorama::IUIPanel* hud = HudPanel() )
		RunJS( hud, ( std::string( "var D2H_pos=" ) + positionsJsObject + ";" ).c_str() );
}


// CUIEngine's panel table (layout from GetPanelPtr / IsValidPanelPointer): 32-byte slots at +0x218,
// allocation count at +0x210; slot +0 is a tree link that equals the slot's own index when the slot is free.
struct PanelSlot
{
	int32_t link;
	int32_t pad0;
	void* unk;
	panorama::IUIPanel* panel;
	int32_t serial;
	int32_t pad1;
};
static_assert( sizeof( PanelSlot ) == 32, "panel slot stride" );

void UI::PollBridge()
{
	static long lastMs = 0;
	long now = Util::GetEpochMs();
	if( now - lastMs < 250 )
		return;
	lastMs = now;

	panorama::UIEngine* ui = panoramaEngine->AccessUIEngine();
	int count = *reinterpret_cast<int32_t*>( reinterpret_cast<char*>( ui ) + 0x210 );
	PanelSlot* slots = *reinterpret_cast<PanelSlot**>( reinterpret_cast<char*>( ui ) + 0x218 );
	if( !slots || count <= 0 || count > 1000000 )
		return;

	static double lastStamp = 0.0;
	static std::set<std::string> seenLogs;
	for( int i = 0; i < count; i++ ){
		panorama::IUIPanel* panel = slots[i].panel;
		if( !panel || slots[i].link == i || !ui->IsValidPanelPointer( panel ) )
			continue;

		const char* id = *reinterpret_cast<const char**>( reinterpret_cast<char*>( panel ) + 0x10 ); // CUIPanel::m_pszID
		if( id && !strcmp( id, "minimap" ) )
			ReadOverview( panel );
		// The in-game HUD root (hud_reborn.xml). RunScript can't be trusted for this: scripts compile on child panels and the
		// versus screen's scene panels (DireScene) come later with a JS context that has no entity API
		if( id && !strcmp( id, "Hud" ) && panel != Hooks::hudPanel ){
			Hooks::hudPanel = panel;
			Logger::Log( "HUD context %p from the panel table", (void*)panel );
		}
		if( !id || strncmp( id, "D2H", 3 ) != 0 )
			continue;

		if( !strncmp( id, "D2HLOG:", 7 ) ){
			if( seenLogs.size() > 2000 )
				seenLogs.clear();
			if( seenLogs.insert( id ).second ){
				const char* text = strchr( id + 7, ':' );
				Logger::Log( "JS: %s", text ? text + 1 : id );
			}
			continue;
		}

		double stamp;
		int acc, dist, ls, lss;
		if( !strncmp( id, "D2H:", 4 ) && sscanf( id + 4, "%lf:acc=%d,dist=%d,ls=%d,lss=%d", &stamp, &acc, &dist, &ls, &lss ) == 5 && stamp > lastStamp ){
			lastStamp = stamp;
			Settings::autoAccept = acc != 0;
			Settings::camDistance = (float)dist;
			Settings::lastSeen = ls != 0;
			Settings::lastSeenSecs = lss;
			Logger::Log( "settings from menu: accept=%d dist=%d lastseen=%d secs=%d", acc, dist, ls, lss );
			Settings::Save();
		}
	}
}
