#pragma once
#include <pgmspace.h>

// Settings page served at http://deskclock.local/ — one self-contained file,
// no external resources except the Open-Meteo city search (called by the
// phone's browser, not by the clock).
//
// Layout: five tabs (Home / Alarm / Reminders / Settings / System) switched with
// the URL hash. Everything under "Alarm" and "Settings" is saved together with one
// button that appears only while something has changed; reminders, the alarm test
// and the update buttons act immediately. The browser talks to the clock one
// request at a time (the ESP32 web server is single-threaded) and only polls what
// the visible tab needs.
//
// The TZ table below maps a city's IANA timezone (what the city search
// returns) to a POSIX rule that includes DST. Governments change DST rules;
// the "custom" field in the page covers anything not listed.
static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="fa" dir="rtl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="light dark">
<meta name="theme-color" content="#0b8aab">
<title>DeskClock</title>
<style>
:root{--bg:#f2f5f9;--card:#fff;--fg:#17202e;--mut:#667085;--acc:#0b8aab;--acc2:#096781;--on:#fff;--bd:#e1e6ee;--soft:#eef3f8;--ok:#14803f;--err:#c62f2f;--r:16px;--nav:62px}
@media(prefers-color-scheme:dark){:root{--bg:#0d1117;--card:#161b22;--fg:#e8ecf2;--mut:#93a0b4;--acc:#38c6ec;--acc2:#1a9bc0;--on:#00141c;--bd:#262d38;--soft:#1e2531;--ok:#4cc27d;--err:#ff7b7b}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
[hidden]{display:none!important}
html{scroll-padding-top:72px}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.7 Vazirmatn,IRANSans,system-ui,-apple-system,"Segoe UI",Tahoma,sans-serif;-webkit-text-size-adjust:100%}
:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
.in{max-width:560px;margin:0 auto;padding-inline:16px}
.ic{width:22px;height:22px;flex:none;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
.mut{color:var(--mut)}.sm{font-size:.85rem}.err{color:var(--err)}

/* header + status */
#top{position:sticky;top:0;z-index:20;background:var(--bg);border-bottom:1px solid var(--bd);padding-top:env(safe-area-inset-top)}
#top .in{display:flex;align-items:center;justify-content:space-between;height:56px}
h1{display:flex;align-items:center;gap:8px;margin:0;font-size:1.1rem}
h1 .ic{color:var(--acc)}
.pill{display:inline-flex;align-items:center;gap:6px;font-size:.8rem;padding:3px 12px;border-radius:99px;background:var(--soft);color:var(--mut)}
.pill i{width:8px;height:8px;border-radius:50%;background:var(--mut)}
.pill.on i{background:var(--ok)}.pill.off i{background:var(--err)}
#ring{position:sticky;top:calc(57px + env(safe-area-inset-top));z-index:19;background:var(--err);color:#fff}
#ring .in{display:flex;align-items:center;justify-content:space-between;gap:10px;padding-block:8px}
#ring .btn{min-height:36px;padding:0 14px;background:#fff3;color:#fff}
#ring .btns2{display:flex;gap:6px}
main{max-width:560px;margin:0 auto;padding:14px 16px calc(var(--nav) + 110px + env(safe-area-inset-bottom))}

/* cards + rows */
.card{background:var(--card);border:1px solid var(--bd);border-radius:var(--r);padding:4px 16px 8px;margin:0 0 14px}
.card>h2{display:flex;align-items:center;gap:8px;margin:0;padding:12px 0 6px;font-size:.95rem;color:var(--acc)}
.card>h2 .mut{margin-inline-start:auto;font-weight:400}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;min-height:52px}
.row+.row,.pad+.row,.row+.pad,.slider+.row,.row+.slider,.row+details{border-top:1px solid var(--bd)}
.lb small{display:block;color:var(--mut);font-size:.8rem;line-height:1.4}
.pad{padding:8px 0}
.hint{color:var(--mut);font-size:.85rem;margin:0;padding:4px 0 8px}
.grp{display:flex;align-items:center;gap:8px;min-width:0;flex:1;justify-content:flex-end}
.grp select{flex:1}
details{padding:2px 0}
summary{cursor:pointer;color:var(--mut);font-size:.88rem;padding:8px 0}
.empty{text-align:center;padding:20px 0 8px;color:var(--mut)}.empty div{font-size:2rem}.empty p{margin:0}

/* controls */
.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;min-height:44px;padding:0 18px;border:0;border-radius:12px;background:var(--acc);color:var(--on);font:inherit;font-weight:600;cursor:pointer;transition:filter .15s,transform .06s}
.btn:hover{filter:brightness(1.07)}.btn:active{transform:scale(.97)}
.btn.soft{background:var(--soft);color:var(--acc)}
.btn.danger{background:var(--soft);color:var(--err)}
.btn.icon{padding:0;width:44px;flex:none}
.btn:disabled{opacity:.4;cursor:default;filter:none;transform:none}
.btns{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;padding:8px 0}
input[type=text],input[type=search],input[type=time],input[type=date],select{font:inherit;color:inherit;background:var(--soft);border:1px solid transparent;border-radius:12px;min-height:44px;padding:0 12px;min-width:0;max-width:100%}
input[type=text],input[type=search]{width:100%}
input:focus,select:focus{border-color:var(--acc);outline:0}
input[type=time],input[type=date]{direction:ltr;text-align:center}
input.big{font-size:1.8rem;font-weight:700;min-height:62px;padding:0 18px}
.sr{display:flex;gap:8px;padding:6px 0}
.sw{position:relative;width:48px;height:28px;flex:none}
.sw input{position:absolute;inset:0;width:100%;height:100%;margin:0;opacity:0;cursor:pointer;z-index:1}
.sw i{position:absolute;inset:0;border-radius:99px;background:var(--bd);transition:background .2s}
.sw i::after{content:"";position:absolute;top:3px;inset-inline-start:3px;width:22px;height:22px;border-radius:50%;background:#fff;box-shadow:0 1px 3px #0005;transition:inset-inline-start .2s}
.sw input:checked+i{background:var(--acc)}
.sw input:checked+i::after{inset-inline-start:23px}
.sw input:focus-visible+i{outline:2px solid var(--acc);outline-offset:2px}
.slider{padding:10px 0 6px}
.sh{display:flex;align-items:center;justify-content:space-between;gap:8px}
.sh output{min-width:34px;text-align:center;font-weight:700;color:var(--acc);font-variant-numeric:tabular-nums}
.sb{display:flex;align-items:center;gap:10px}
input[type=range]{-webkit-appearance:none;appearance:none;direction:ltr;flex:1;min-width:0;height:34px;margin:0;background:transparent;--p:50%}
input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:3px;background:linear-gradient(to right,var(--acc) var(--p),var(--bd) var(--p))}
input[type=range]::-moz-range-track{height:6px;border-radius:3px;background:linear-gradient(to right,var(--acc) var(--p),var(--bd) var(--p))}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;box-sizing:border-box;width:26px;height:26px;margin-top:-10px;border-radius:50%;background:#fff;border:2px solid var(--acc);box-shadow:0 1px 4px #0004}
input[type=range]::-moz-range-thumb{box-sizing:border-box;width:26px;height:26px;border-radius:50%;background:#fff;border:2px solid var(--acc)}
.days{display:grid;grid-template-columns:repeat(7,1fr);gap:6px;padding:6px 0}
.days label{position:relative}
.days input{position:absolute;inset:0;width:100%;height:100%;margin:0;opacity:0;cursor:pointer}
.days span{display:flex;align-items:center;justify-content:center;height:44px;border-radius:12px;background:var(--soft);color:var(--mut);font-weight:600;transition:background .15s;user-select:none}
.days input:checked+span{background:var(--acc);color:var(--on)}
.days input:focus-visible+span{outline:2px solid var(--acc);outline-offset:2px}
.chips{display:flex;flex-wrap:wrap;gap:8px;padding:6px 0 8px}
.chip{min-height:38px;padding:0 14px;border:0;border-radius:99px;background:var(--soft);color:var(--fg);font:inherit;cursor:pointer}
.chip:active{background:var(--acc);color:var(--on)}
.seg{display:flex;gap:3px;padding:3px;background:var(--soft);border-radius:12px;margin:8px 0}
.seg label{flex:1;position:relative}
.seg input{position:absolute;inset:0;width:100%;height:100%;margin:0;opacity:0;cursor:pointer}
.seg span{display:block;text-align:center;line-height:38px;border-radius:10px;color:var(--mut);font-weight:600}
.seg input:checked+span{background:var(--card);color:var(--acc);box-shadow:0 1px 3px #0003}

/* home */
.hero{background:linear-gradient(135deg,var(--acc),var(--acc2));color:var(--on);border-radius:20px;padding:22px 16px;margin-bottom:14px;text-align:center}
.ht{font-size:3.6rem;font-weight:700;line-height:1.1;font-variant-numeric:tabular-nums;letter-spacing:.02em}
.hd{opacity:.92;margin-top:2px}
.hn{display:inline-flex;align-items:center;gap:8px;margin-top:14px;padding:5px 14px;border-radius:99px;background:#0003;color:#fff;font-size:.9rem}
.hn .ic{width:18px;height:18px}
.tiles{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:14px}
.tile{display:flex;align-items:center;gap:10px;padding:14px;background:var(--card);border:1px solid var(--bd);border-radius:var(--r);color:var(--fg);font:inherit;font-weight:600;cursor:pointer;text-align:start}
.tile .ic{color:var(--acc);width:24px;height:24px}
.tile:active{background:var(--soft)}.tile:disabled{opacity:.45;cursor:default}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;padding:6px 0 10px}
.stat{background:var(--soft);border-radius:12px;padding:10px 4px;text-align:center}
.stat b{display:block;font-size:1.4rem;line-height:1.3;font-variant-numeric:tabular-nums}
.stat small{display:block;color:var(--mut);font-size:.75rem;line-height:1.4}
.stat.bad b{color:var(--err)}
.bar{height:8px;border-radius:4px;background:var(--bd);overflow:hidden;margin:6px 0 4px}
.bar i{display:block;height:100%;width:0;border-radius:4px;background:var(--acc);transition:width .4s}

/* lists */
.rem{display:grid;grid-template-columns:1fr auto;align-items:center;gap:0 12px;padding:12px 0}
.rem+.rem{border-top:1px solid var(--bd)}
.rem.off .rt,.rem.off .rx{opacity:.45}
.rt{display:flex;align-items:baseline;flex-wrap:wrap;gap:4px 10px}
.rt b{font-size:1.5rem;direction:ltr;color:var(--acc);font-variant-numeric:tabular-nums}
.rx{word-break:break-word}
.ra{grid-row:1/3;grid-column:2;display:flex;align-items:center;gap:6px}
.res{display:flex;flex-direction:column;align-items:flex-start;width:100%;padding:10px 12px;margin:6px 0;border:0;border-radius:12px;background:var(--soft);color:var(--fg);font:inherit;text-align:start;cursor:pointer}
.res span{font-size:.85rem}
.loc{display:flex;align-items:center;gap:8px;padding:8px 0;color:var(--mut)}
.loc .ic{color:var(--acc)}.loc b{color:var(--fg)}
.bl{display:flex;justify-content:space-between;gap:10px;padding:8px 0;border-top:1px solid var(--bd);direction:ltr;font-size:.9rem}
.bl.bad span:last-child{color:var(--err);font-weight:700}

/* floating bits */
nav{position:fixed;inset-inline:0;bottom:0;z-index:40;background:var(--card);border-top:1px solid var(--bd);padding-bottom:env(safe-area-inset-bottom)}
nav .in{display:flex;padding-inline:6px}
nav a{position:relative;flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1px;height:var(--nav);color:var(--mut);text-decoration:none;font-size:.72rem;font-weight:600}
nav a[aria-current=page]{color:var(--acc)}
nav a[aria-current=page]::before{content:"";position:absolute;top:0;width:30px;height:3px;border-radius:0 0 3px 3px;background:var(--acc)}
#dirty{position:fixed;inset-inline:0;bottom:calc(var(--nav) + env(safe-area-inset-bottom) + 10px);z-index:30;pointer-events:none;opacity:0;visibility:hidden;transform:translateY(30px);transition:transform .25s,opacity .25s,visibility .25s}
#dirty.show{opacity:1;visibility:visible;transform:none}
#dirty .box{pointer-events:auto;display:flex;align-items:center;gap:8px;padding:8px 8px 8px 14px;background:var(--card);border:1px solid var(--acc);border-radius:16px;box-shadow:0 8px 28px #0004}
#dirty .box span{flex:1;font-weight:600}
#toast{position:fixed;inset-inline:16px;top:calc(env(safe-area-inset-top) + 66px);z-index:60;max-width:440px;margin:0 auto;padding:10px 16px;border-radius:12px;background:var(--fg);color:var(--bg);text-align:center;font-weight:600;box-shadow:0 6px 24px #0005;opacity:0;transform:translateY(-8px);pointer-events:none;transition:opacity .2s,transform .2s}
#toast.show{opacity:1;transform:none}
#toast.err{background:var(--err);color:#fff}
dialog{width:100%;max-width:560px;margin:auto auto 0;padding:0;border:0;border-radius:22px 22px 0 0;background:var(--card);color:var(--fg);max-height:92vh;overflow:auto}
dialog::backdrop{background:#0009}
dialog[open]{animation:up .22s ease-out}
@keyframes up{from{transform:translateY(40px);opacity:0}}
dialog .in2{padding:16px 16px calc(16px + env(safe-area-inset-bottom))}
dialog header{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px}
dialog h3{margin:0;font-size:1.05rem}
dialog .foot{display:grid;grid-template-columns:1fr 1fr;gap:8px;padding-top:12px}
@media(prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}
</style>
</head>
<body>
<svg width="0" height="0" style="position:absolute" aria-hidden="true"><defs>
<symbol id="i-home" viewBox="0 0 24 24"><path d="M3 10.5 12 3l9 7.5V20a1 1 0 0 1-1 1h-5v-6H9v6H4a1 1 0 0 1-1-1z"/></symbol>
<symbol id="i-bell" viewBox="0 0 24 24"><path d="M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9M10.3 21a1.94 1.94 0 0 0 3.4 0"/></symbol>
<symbol id="i-clock" viewBox="0 0 24 24"><circle cx="12" cy="13" r="8"/><path d="M12 9v4l2 2M5 3 2 6M22 6l-3-3"/></symbol>
<symbol id="i-list" viewBox="0 0 24 24"><path d="m9 11 3 3L22 4M21 12v7a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11"/></symbol>
<symbol id="i-sliders" viewBox="0 0 24 24"><path d="M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6"/></symbol>
<symbol id="i-cpu" viewBox="0 0 24 24"><rect x="4" y="4" width="16" height="16" rx="2"/><rect x="9" y="9" width="6" height="6"/><path d="M9 1v3M15 1v3M9 20v3M15 20v3M20 9h3M20 14h3M1 9h3M1 14h3"/></symbol>
<symbol id="i-vol" viewBox="0 0 24 24"><path d="M11 5 6 9H2v6h4l5 4zM15.5 8.5a5 5 0 0 1 0 7M19 5a9 9 0 0 1 0 14"/></symbol>
<symbol id="i-play" viewBox="0 0 24 24"><path d="M7 4.5v15l12-7.5z"/></symbol>
<symbol id="i-trash" viewBox="0 0 24 24"><path d="M3 6h18M8 6V4h8v2M19 6l-1 14H6L5 6M10 11v6M14 11v6"/></symbol>
<symbol id="i-edit" viewBox="0 0 24 24"><path d="M12 20h9M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4z"/></symbol>
<symbol id="i-plus" viewBox="0 0 24 24"><path d="M12 5v14M5 12h14"/></symbol>
<symbol id="i-pin" viewBox="0 0 24 24"><path d="M20 10c0 6-8 12-8 12S4 16 4 10a8 8 0 0 1 16 0"/><circle cx="12" cy="10" r="3"/></symbol>
<symbol id="i-x" viewBox="0 0 24 24"><path d="M18 6 6 18M6 6l12 12"/></symbol>
<symbol id="i-power" viewBox="0 0 24 24"><path d="M12 2v10M18.4 6.6a9 9 0 1 1-12.8 0"/></symbol>
<symbol id="i-heart" viewBox="0 0 24 24"><path d="M20.8 4.6a5.5 5.5 0 0 0-7.8 0L12 5.7l-1.1-1.1a5.5 5.5 0 0 0-7.8 7.8l1.1 1.1L12 21l7.8-7.5 1.1-1.1a5.5 5.5 0 0 0-.1-7.8z"/></symbol>
<symbol id="i-star" viewBox="0 0 24 24"><path d="m12 2 3.1 6.3 6.9 1-5 4.9 1.2 6.8L12 17.8 5.8 21l1.2-6.8-5-4.9 6.9-1z"/></symbol>
<symbol id="i-globe" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M2 12h20M12 2a15 15 0 0 1 0 20M12 2a15 15 0 0 0 0 20"/></symbol>
<symbol id="i-down" viewBox="0 0 24 24"><path d="M12 3v12M7 10l5 5 5-5M5 21h14"/></symbol>
</defs></svg>

<header id="top"><div class="in">
<h1><svg class="ic"><use href="#i-clock"/></svg>DeskClock</h1>
<span id="conn" class="pill"><i></i><span>در حال اتصال…</span></span>
</div></header>

<div id="ring" hidden><div class="in">
<span><b id="ringTxt"></b></span>
<span class="btns2"><button type="button" class="btn" data-act="snooze">اسنوز</button><button type="button" class="btn" data-act="dismiss">خاموش</button></span>
</div></div>

<main>

<!-- ============ خانه ============ -->
<section class="page" id="p-home">
<div class="hero">
<div class="ht" id="hTime" dir="ltr">--:--</div>
<div class="hd" id="hDate">…</div>
<div class="hn"><svg class="ic"><use href="#i-bell"/></svg><span>آلارم بعدی:</span><b id="hNext">—</b></div>
</div>
<div class="tiles">
<button type="button" class="tile" data-act="test"><svg class="ic"><use href="#i-bell"/></svg>تست آلارم</button>
<button type="button" class="tile" id="qPet"><svg class="ic"><use href="#i-vol"/></svg>صدای نوازش</button>
<button type="button" class="tile" id="qRem"><svg class="ic"><use href="#i-plus"/></svg>یادآور جدید</button>
<button type="button" class="tile" id="qOcc"><svg class="ic"><use href="#i-star"/></svg>مناسبت‌ها</button>
</div>
<div class="card">
<h2><svg class="ic"><use href="#i-heart"/></svg>نوازش</h2>
<div class="stats">
<div class="stat"><b id="pToday">–</b><small>امروز</small></div>
<div class="stat"><b id="pBest">–</b><small>بیشترین پشت‌سرهم</small></div>
<div class="stat"><b id="pTotal">–</b><small>کل</small></div>
</div>
<div id="bondBox" hidden>
<div class="sh"><span>دلبستگی</span><span class="mut" id="bondTxt"></span></div>
<div class="bar"><i id="bondBar"></i></div>
</div>
</div>
</section>

<!-- ============ آلارم ============ -->
<section class="page" id="p-alarm" hidden>
<div class="card">
<h2><svg class="ic"><use href="#i-clock"/></svg>زمان و روزها</h2>
<label class="row"><span>آلارم فعال</span><span class="sw"><input type="checkbox" id="aEn" data-s><i></i></span></label>
<div class="row"><span>ساعت زنگ</span><input type="time" class="big" id="aTime" data-s></div>
<div class="pad"><div class="mut sm">روزهای زنگ</div><div class="days" id="days"></div>
<div class="chips"><button type="button" class="chip" data-m="127">هر روز</button><button type="button" class="chip" data-m="95">به‌جز جمعه</button></div></div>
</div>
<div class="card">
<h2><svg class="ic"><use href="#i-vol"/></svg>صدای زنگ</h2>
<div class="row"><span>صدا</span><span class="grp"><select id="aSound" data-s></select><button type="button" class="btn soft icon" id="aSoundBtn" aria-label="پیش‌نمایش صدا"><svg class="ic"><use href="#i-play"/></svg></button></span></div>
<div class="slider"><div class="sh"><span>ولوم آلارم</span><output id="avOut"></output></div><div class="sb"><input type="range" id="avol" min="1" max="30" data-s></div></div>
</div>
<div class="card">
<h2><svg class="ic"><use href="#i-power"/></svg>تست و کنترل</h2>
<p class="pad" id="aState" style="margin:0">…</p>
<div class="btns">
<button type="button" class="btn" data-act="test">تست آلارم</button>
<button type="button" class="btn soft" data-act="snooze">اسنوز</button>
<button type="button" class="btn danger" data-act="dismiss">خاموش</button>
</div>
<details><summary>راهنما</summary>
<p class="hint">آلارم واقعی با صدا و ولوم آلارمِ <b>ذخیره‌شده</b> زنگ می‌خورد (اگر تغییری داده‌اید اول ذخیره کنید)، بدون توجه به روزهای هفته. اسنوز و خاموش روی آلارم واقعی هم کار می‌کنند.</p></details>
</div>
</section>

<!-- ============ یادآورها ============ -->
<section class="page" id="p-rem" hidden>
<div class="card">
<h2><svg class="ic"><use href="#i-list"/></svg>یادآورها<span class="mut sm" id="rCnt"></span></h2>
<div id="rList"></div>
<div class="empty" id="rEmpty"><div>📝</div><p>هنوز یادآوری اضافه نشده</p></div>
<div class="pad"><button type="button" class="btn" id="rAdd" style="width:100%"><svg class="ic"><use href="#i-plus"/></svg>یادآور جدید</button></div>
<label class="row"><span>صدای یادآور</span><span class="sw"><input type="checkbox" id="rSnd"><i></i></span></label>
<details><summary>راهنما</summary>
<p class="hint">سر ساعت مقرر، متن تمام‌صفحه روی ساعت نشان داده می‌شود و تا فشردن <b>هر یک از دکمه‌ها</b> می‌ماند (حداکثر چند دقیقه). متن فارسی و انگلیسی پشتیبانی می‌شود. «نمایش روی ساعت» همین الان متن را روی نمایشگر می‌آورد تا خوانایی را ببینید. یادآورِ «یک‌بار» بعد از نمایش پاک می‌شود. شبِ صدا خاموش است (مثل صدای نوازش).</p></details>
</div>
</section>

<!-- ============ تنظیمات ============ -->
<section class="page" id="p-set" hidden>
<div class="card">
<h2><svg class="ic"><use href="#i-vol"/></svg>صدا</h2>
<div class="slider"><div class="sh"><span>ولوم صدای نوازش</span><output id="volOut"></output></div>
<div class="sb"><input type="range" id="vol" min="0" max="30" data-s><button type="button" class="btn soft icon" id="testBtn" aria-label="پخش صدای نوازش"><svg class="ic"><use href="#i-play"/></svg></button></div></div>
<label class="row"><span class="lb">بوق تنظیمات<small>روشن / خاموش / خطا</small></span><span class="sw"><input type="checkbox" id="uiOn" data-s><i></i></span></label>
<details><summary>تست همه‌ی صداها</summary>
<div class="row"><select id="tSound" style="flex:1"></select><button type="button" class="btn soft icon" id="tSoundBtn" aria-label="پخش"><svg class="ic"><use href="#i-play"/></svg></button></div></details>
</div>

<div class="card">
<h2><svg class="ic"><use href="#i-heart"/></svg>واکنش‌های نوازش</h2>
<label class="row"><span class="lb">واکنش‌های متنوع<small>خاموش = همیشه همان چهره‌ی خوشحال</small></span><span class="sw"><input type="checkbox" id="petOn" data-s><i></i></span></label>
<details><summary>چطور کار می‌کند؟</summary>
<p class="hint">چند بار پشت‌سرهم = قلب، بعد گیجی و در آخر اخم 😄 — شب خواب‌آلود، در باران یک ابرو بالا، و نگه‌داشتن دکمه بیش از یک ثانیه = خرخر.</p>
<p class="hint">دلبستگی: هر چه منظم‌تر نوازشش کنی (یا نگه داشتن دکمه تا خرخر) بیشتر بهت وابسته می‌شه: صبورتر می‌شه، گاهی خودش لبخند می‌زنه و رنگ چشم‌هاش کمی صورتی می‌شه. اگه چند روز نبینیش دلتنگ می‌شه، گاهی آه می‌کشه و دیرتر قهرش تموم می‌شه. افزایش روزانه محدوده، پس نوازش پشت‌سرهم زیاد فایده‌ای نداره.</p></details>
</div>

<div class="card" id="cOcc">
<h2><svg class="ic"><use href="#i-star"/></svg>مناسبت‌های سالانه</h2>
<label class="row"><span>انیمیشن مناسبت‌ها</span><span class="sw"><input type="checkbox" id="occOn" data-s><i></i></span></label>
<div class="pad"><div class="mut sm">پیش‌نمایش روی ساعت (۲۵ ثانیه)</div>
<div class="chips" id="occ">
<button type="button" class="chip" data-o="1">🎃 هالووین</button>
<button type="button" class="chip" data-o="2">🐣 عید پاک</button>
<button type="button" class="chip" data-o="3">🎄 کریسمس</button>
<button type="button" class="chip" data-o="4">🎆 شب سال نو</button>
<button type="button" class="chip" data-o="5">💝 ولنتاین</button>
<button type="button" class="chip" data-o="6">🔥 چهارشنبه‌سوری</button>
<button type="button" class="chip" data-o="7">🍉 شب یلدا</button>
<button type="button" class="chip" data-o="8">🌱 نوروز</button>
<button type="button" class="chip" data-o="0">⏹ توقف</button>
</div></div>
<details><summary>زمان نمایش هر مناسبت</summary>
<p class="hint">هالووین ۲۸ تا ۳۱ اکتبر، عید پاک از جمعه تا دوشنبه، ولنتاین ۱۳ و ۱۴ فوریه، کریسمس ۲۰ تا ۲۶ دسامبر، شب سال نو از ساعت ۱۸ روز ۳۱ دسامبر.</p></details>
</div>

<div class="card">
<h2><svg class="ic"><use href="#i-pin"/></svg>شهر (برای آب‌وهوا)</h2>
<div class="loc"><svg class="ic"><use href="#i-pin"/></svg><b id="cityName">—</b><span id="coords" dir="ltr"></span></div>
<div class="sr"><input type="search" id="q" placeholder="نام شهر را جستجو کنید…" enterkeyhint="search" autocomplete="off"><button type="button" class="btn" id="findBtn">جستجو</button></div>
<div id="results"></div>
</div>

<div class="card">
<h2><svg class="ic"><use href="#i-globe"/></svg>منطقه‌ی زمانی</h2>
<div class="pad"><select id="tz" data-s style="width:100%"></select>
<p class="hint">تغییر ساعت تابستانی به‌صورت خودکار اعمال می‌شود.</p></div>
<details><summary>پیشرفته: قاعده‌ی POSIX</summary>
<div class="pad"><input type="text" id="tzp" data-s dir="ltr" spellcheck="false" autocomplete="off"></div></details>
</div>
</section>

<!-- ============ سیستم ============ -->
<section class="page" id="p-sys" hidden>
<div class="card" id="updSec">
<h2><svg class="ic"><use href="#i-down"/></svg>به‌روزرسانی نرم‌افزار</h2>
<div class="row"><span class="mut">نسخه‌ی فعلی</span><span><b id="uVer" dir="ltr">—</b> <span class="mut sm" id="uVar" dir="ltr"></span></span></div>
<p class="pad" id="uMsg" style="margin:0">…</p>
<div class="bar" id="uBar" hidden><i id="uProg"></i></div>
<div class="btns" style="grid-template-columns:1fr 1fr">
<button type="button" class="btn soft" id="uCheck">بررسی به‌روزرسانی</button>
<button type="button" class="btn" id="uInstall" hidden>نصب نسخه‌ی جدید</button>
</div>
<p class="hint"><a id="uNotes" href="#" target="_blank" rel="noopener" hidden style="color:var(--acc)">تغییرات این نسخه در GitHub</a></p>
<details><summary>درباره‌ی به‌روزرسانی</summary>
<p class="hint">نسخه‌ی جدید فقط از Releaseهای رسمی GitHub گرفته می‌شود و تا تأیید شما نصب نمی‌شود. فایل با checksum منتشرشده کنترل می‌شود و در صورت مغایرت نصب نمی‌شود. تنظیمات شما حفظ می‌ماند.</p></details>
</div>

<div class="card">
<h2><svg class="ic"><use href="#i-cpu"/></svg>تشخیص و ریست‌ها</h2>
<div class="stats">
<div class="stat"><b id="bBoots">–</b><small>تعداد بوت</small></div>
<div class="stat" id="bBrownBox"><b id="bBrown">–</b><small>Brownout</small></div>
<div class="stat"><b id="bUp" style="font-size:1rem;padding:4px 0">–</b><small>مدت روشن‌بودن</small></div>
</div>
<div id="bList"></div>
<details><summary>Brownout یعنی چه؟</summary>
<p class="hint">Brownout یعنی افت ولتاژ تغذیه (سروو و صدا روی یک تغذیه‌اند): منبع تغذیه و کابل را بررسی کنید. ثبت‌ها در حافظهٔ فلش می‌مانند و با قطع برق پاک نمی‌شوند.</p></details>
</div>
</section>

</main>

<div id="dirty" role="region" aria-label="تغییرات ذخیره‌نشده"><div class="in"><div class="box">
<span>تغییرات ذخیره‌نشده</span>
<button type="button" class="btn soft" id="revert">لغو</button>
<button type="button" class="btn" id="save">ذخیره</button>
</div></div></div>

<nav aria-label="بخش‌ها"><div class="in">
<a href="#home" data-t="home"><svg class="ic"><use href="#i-home"/></svg><span>خانه</span></a>
<a href="#alarm" data-t="alarm"><svg class="ic"><use href="#i-bell"/></svg><span>آلارم</span></a>
<a href="#rem" data-t="rem"><svg class="ic"><use href="#i-list"/></svg><span>یادآورها</span></a>
<a href="#set" data-t="set"><svg class="ic"><use href="#i-sliders"/></svg><span>تنظیمات</span></a>
<a href="#sys" data-t="sys"><svg class="ic"><use href="#i-cpu"/></svg><span>سیستم</span></a>
</div></nav>

<div id="toast" role="status" aria-live="polite"></div>

<dialog id="sheet"><div class="in2">
<header><h3 id="rFormTitle">یادآور جدید</h3><button type="button" class="btn soft icon" id="rClose" aria-label="بستن"><svg class="ic"><use href="#i-x"/></svg></button></header>
<form id="rForm" novalidate>
<input type="text" id="rText" dir="auto" placeholder="مثلاً: قرص‌هایت را بخور" autocomplete="off" aria-label="متن یادآور">
<p class="hint" id="rCount"></p>
<div class="row"><span>ساعت</span><input type="time" id="rTime" value="09:00"></div>
<div class="seg" role="radiogroup" aria-label="تکرار">
<label><input type="radio" name="rm" value="once" checked><span>فقط یک‌بار</span></label>
<label><input type="radio" name="rm" value="week"><span>هفتگی</span></label>
</div>
<div class="row" id="rDateRow"><span>تاریخ</span><input type="date" id="rDate"></div>
<div id="rDaysBox" hidden><div class="days" id="rDays"></div></div>
<label class="row"><span>فعال</span><span class="sw"><input type="checkbox" id="rEn" checked><i></i></span></label>
<div class="foot">
<button type="submit" class="btn" id="rSave">افزودن</button>
<button type="button" class="btn soft" id="rTest">نمایش روی ساعت</button>
</div>
</form>
</div></dialog>

<script>
'use strict';
const $=id=>document.getElementById(id),qa=(s,r)=>[...(r||document).querySelectorAll(s)];
const two=n=>String(n).padStart(2,'0');
function h(tag,cls,txt){const e=document.createElement(tag);if(cls)e.className=cls;if(txt!=null)e.textContent=txt;return e;}
function ic(n){const N='http://www.w3.org/2000/svg',s=document.createElementNS(N,'svg'),u=document.createElementNS(N,'use');s.setAttribute('class','ic');u.setAttribute('href','#i-'+n);s.appendChild(u);return s;}

// [id, label, POSIX rule, IANA names]
const TZ=[
["Asia-Tehran","ایران (تهران)","<+0330>-3:30","Asia/Tehran"],
["UTC","UTC","UTC0","UTC,Etc/UTC"],
["Europe-London","بریتانیا و ایرلند (لندن)","GMT0BST,M3.5.0/1,M10.5.0","Europe/London,Europe/Dublin"],
["Europe-Lisbon","پرتغال (لیسبون)","WET0WEST,M3.5.0/1,M10.5.0","Europe/Lisbon,Atlantic/Canary"],
["Europe-Central","اروپای مرکزی (برلین، پاریس، رم…)","CET-1CEST,M3.5.0,M10.5.0/3","Europe/Berlin,Europe/Paris,Europe/Rome,Europe/Madrid,Europe/Amsterdam,Europe/Vienna,Europe/Zurich,Europe/Brussels,Europe/Stockholm,Europe/Oslo,Europe/Copenhagen,Europe/Prague,Europe/Warsaw,Europe/Budapest"],
["Europe-Eastern","اروپای شرقی (آتن، هلسینکی، کیف…)","EET-2EEST,M3.5.0/3,M10.5.0/4","Europe/Athens,Europe/Helsinki,Europe/Kyiv,Europe/Kiev,Europe/Bucharest,Europe/Sofia,Europe/Riga,Europe/Tallinn,Europe/Vilnius"],
["UTC+3","UTC+3 (استانبول، مسکو، ریاض، بغداد…)","<+03>-3","Europe/Istanbul,Europe/Moscow,Europe/Minsk,Asia/Riyadh,Asia/Baghdad,Asia/Kuwait,Asia/Qatar,Asia/Bahrain,Asia/Amman,Asia/Damascus,Africa/Nairobi,Africa/Addis_Ababa"],
["UTC+4","UTC+4 (دبی، باکو، ایروان، تفلیس)","<+04>-4","Asia/Dubai,Asia/Muscat,Asia/Baku,Asia/Yerevan,Asia/Tbilisi,Europe/Samara"],
["Asia-Kabul","افغانستان (کابل)","<+0430>-4:30","Asia/Kabul"],
["UTC+5","UTC+5 (کراچی، تاشکند، اکاترینبورگ)","<+05>-5","Asia/Karachi,Asia/Tashkent,Asia/Samarkand,Asia/Ashgabat,Asia/Dushanbe,Asia/Yekaterinburg,Asia/Almaty"],
["Asia-Kolkata","هند و سریلانکا","IST-5:30","Asia/Kolkata,Asia/Calcutta,Asia/Colombo"],
["Asia-Kathmandu","نپال (کاتماندو)","<+0545>-5:45","Asia/Kathmandu"],
["UTC+6","UTC+6 (داکا، بیشکک)","<+06>-6","Asia/Dhaka,Asia/Bishkek,Asia/Omsk"],
["UTC+7","UTC+7 (بانکوک، جاکارتا، هوشی‌مین)","<+07>-7","Asia/Bangkok,Asia/Jakarta,Asia/Ho_Chi_Minh,Asia/Novosibirsk,Asia/Krasnoyarsk"],
["UTC+8","UTC+8 (شانگهای، سنگاپور، هنگ‌کنگ، پرث)","<+08>-8","Asia/Shanghai,Asia/Singapore,Asia/Hong_Kong,Asia/Taipei,Asia/Kuala_Lumpur,Asia/Manila,Australia/Perth,Asia/Irkutsk"],
["UTC+9","UTC+9 (توکیو، سئول)","<+09>-9","Asia/Tokyo,Asia/Seoul"],
["Australia-East","استرالیای شرقی (سیدنی، ملبورن)","AEST-10AEDT,M10.1.0,M4.1.0/3","Australia/Sydney,Australia/Melbourne,Australia/Canberra,Australia/Hobart"],
["Australia-Brisbane","بریزبن","AEST-10","Australia/Brisbane"],
["Pacific-Auckland","نیوزیلند (اوکلند)","NZST-12NZDT,M9.5.0,M4.1.0/3","Pacific/Auckland"],
["US-Eastern","شرق آمریکا و کانادا (نیویورک، تورنتو)","EST5EDT,M3.2.0,M11.1.0","America/New_York,America/Toronto,America/Detroit"],
["US-Central","مرکز آمریکا (شیکاگو)","CST6CDT,M3.2.0,M11.1.0","America/Chicago,America/Winnipeg"],
["US-Mountain","کوهستان آمریکا (دنور)","MST7MDT,M3.2.0,M11.1.0","America/Denver,America/Edmonton"],
["US-Arizona","آریزونا (فینیکس)","MST7","America/Phoenix"],
["US-Pacific","غرب آمریکا و کانادا (لس‌آنجلس، ونکوور)","PST8PDT,M3.2.0,M11.1.0","America/Los_Angeles,America/Vancouver"],
["US-Alaska","آلاسکا","AKST9AKDT,M3.2.0,M11.1.0","America/Anchorage"],
["Pacific-Honolulu","هاوایی","HST10","Pacific/Honolulu"],
["America-Mexico","مکزیک (مکزیکوسیتی)","CST6","America/Mexico_City"],
["UTC-3","UTC-3 (سائوپائولو، بوئنوس‌آیرس)","<-03>3","America/Sao_Paulo,America/Argentina/Buenos_Aires,America/Montevideo"],
["UTC-5","UTC-5 (بوگوتا، لیما)","<-05>5","America/Bogota,America/Lima,America/Panama"],
["Africa-Cairo","مصر (قاهره)","EET-2EEST,M4.5.5/0,M10.5.4/24","Africa/Cairo"],
["Africa-Johannesburg","آفریقای جنوبی","SAST-2","Africa/Johannesburg,Africa/Harare"],
["Africa-Lagos","نیجریه (لاگوس)","WAT-1","Africa/Lagos"],
["Asia-Jerusalem","اسرائیل","IST-2IDT,M3.4.4/26,M10.5.0","Asia/Jerusalem,Asia/Tel_Aviv"],
["Asia-Beirut","لبنان (بیروت)","EET-2EEST,M3.5.0/0,M10.5.0/0","Asia/Beirut"],
["Atlantic-Reykjavik","ایسلند / GMT بدون DST","GMT0","Atlantic/Reykjavik,Africa/Accra,Africa/Abidjan"]
];
const SN=['آرام','اصلی','جیک نوازش','ملودیک','بلند','بویینگ','بیپ‌ها','خمیازه','زنگ ساعت','روشن‌شدن','UI روشن','UI خاموش','تیک‌تاک','UI خطا'];
const UERR={wifi:'ساعت به اینترنت وصل نیست',time:'ساعت هنوز زمان دقیق (NTP) را نگرفته؛ چند دقیقه بعد دوباره تلاش کنید',heap:'حافظه‌ی آزاد کافی نیست؛ کمی بعد دوباره تلاش کنید',tls:'اتصال امن برقرار نشد (گواهی GitHub تأیید نشد)',connect:'اتصال به GitHub برقرار نشد',rate_limit:'GitHub موقتاً درخواست‌ها را محدود کرده؛ بعداً دوباره تلاش کنید',no_release:'هنوز هیچ Releaseای منتشر نشده',no_asset:'این Release فایل مخصوص نمایشگر این ساعت را ندارد',too_big:'فایل از فضای به‌روزرسانی بزرگ‌تر است',bad_tag:'نسخه‌ی Release قابل خواندن نیست',json:'پاسخ GitHub قابل خواندن نیست',redirect:'آدرس دانلود نامعتبر است',size_mismatch:'اندازه‌ی فایل با Release نمی‌خواند',bad_checksum_file:'فایل checksum نامعتبر است',flash_begin:'شروع نوشتن در فلش ممکن نشد',download:'دانلود ناقص ماند؛ دوباره تلاش کنید',checksum:'checksum فایل درست نبود؛ چیزی نصب نشد',flash_end:'تأیید فایل نصب‌شده ناموفق بود؛ چیزی تغییر نکرد',busy:'عملیات دیگری در حال اجراست',alarm_busy:'آلارم در حال زنگ‌زدن یا اسنوز است',not_available:'ابتدا دوباره بررسی کنید',disabled:'این قابلیت در این نسخه غیرفعال است'};
// [tm_wday, short label, full name] in Persian week order (Saturday first)
const DAYS=[[6,'ش','شنبه'],[0,'ی','یکشنبه'],[1,'د','دوشنبه'],[2,'س','سه‌شنبه'],[3,'چ','چهارشنبه'],[4,'پ','پنجشنبه'],[5,'ج','جمعه']];
const AST={idle:'آماده (آلارم زنگ نمی‌زند)',ringing:'در حال زنگ‌زدن 🔔',snoozed:'اسنوز شده'};
const BL=['دلتنگ','آشنا','صمیمی','دلبسته'];
const ERR={'alarm is active':'آلارم فعال است','alarm is already active':'آلارم از قبل فعال است','alarm is not ringing':'آلارم در حال زنگ‌زدن نیست','alarm is not active':'آلارم فعال نیست','alarm is ringing':'آلارم در حال زنگ‌زدن است','sound out of range':'شماره‌ی صدا نامعتبر است','text is empty':'متن را وارد کنید','text too long':'متن خیلی بلند است','text is not valid UTF-8':'متن نامعتبر است','text missing':'متن را وارد کنید','no such reminder':'یادآور پیدا نشد','bad JSON':'درخواست نامعتبر است','bad date':'تاریخ نامعتبر است','id out of range':'مناسبت نامعتبر است'};
const tr=c=>{c=String(c||'');return ERR[c]||UERR[c]||(c.startsWith('http_')?'خطای HTTP '+c.slice(5):c);};
const eMsg=e=>e.http?'خطا: '+tr(e.message):'ارتباط با ساعت برقرار نشد';

const S={lat:0,lon:0,city:''};
const TABS=['home','alarm','rem','set','sys'];
let tab='home',lastS=null,lastKey='',baseline='',dirty=false;

/* ---------- network: one request at a time (the ESP32 web server is single-threaded) ---------- */
let chain=Promise.resolve();
function api(url,body,ms){
 const run=async()=>{
  const c=new AbortController(),tm=setTimeout(()=>c.abort(),ms||8000);
  try{
   const o={signal:c.signal,cache:'no-store'};
   if(body!==undefined){o.method='POST';o.headers={'Content-Type':'application/json'};o.body=JSON.stringify(body);}
   const r=await fetch(url,o);
   let j={};try{j=await r.json();}catch(e){}
   online(true);
   if(!r.ok){const er=new Error(j.error||('http_'+r.status));er.http=true;throw er;}
   return j;
  }catch(e){if(!e.http)online(false);throw e;}
  finally{clearTimeout(tm);}
 };
 const p=chain.then(run,run);chain=p.catch(()=>{});return p;
}
function online(ok){const c=$('conn');c.className='pill '+(ok?'on':'off');c.lastChild.textContent=ok?'متصل':'قطع';}
let tt=0;
function toast(t,k){const e=$('toast');if(!t){e.className='';return;}e.textContent=t;e.className='show '+(k||'');clearTimeout(tt);tt=setTimeout(()=>{e.className='';},k==='err'?5000:2800);}

/* ---------- polling: only while the tab is visible and the page needs it ---------- */
const runners=[];
function poll(fn,ms,when){let busy=false;const run=async()=>{if(document.hidden||busy||(when&&!when()))return;busy=true;try{await fn();}catch(e){}busy=false;};setInterval(run,ms);runners.push(run);return run;}
document.addEventListener('visibilitychange',()=>{if(!document.hidden)runners.forEach(r=>r());});

/* ---------- navigation ---------- */
function go(t){
 if(!TABS.includes(t))t='home';tab=t;
 TABS.forEach(x=>{$('p-'+x).hidden=x!==t;});
 qa('nav a').forEach(a=>a.setAttribute('aria-current',a.dataset.t===t?'page':'false'));
 if(location.hash!=='#'+t)history.replaceState(null,'','#'+t);
 scrollTo(0,0);
 if(t==='home')petStats().catch(()=>{});
 else if(t==='rem')rLoad().catch(()=>{});
 else if(t==='sys'){updateState();bootLog();}
}
addEventListener('hashchange',()=>go(location.hash.slice(1)));

/* ---------- settings form ---------- */
const tzById=id=>TZ.find(z=>z[0]===id);
const tzByIana=n=>TZ.find(z=>z[3].split(',').includes(n));
function setLoc(lat,lon,city){S.lat=lat;S.lon=lon;S.city=city;$('cityName').textContent=city||'—';$('coords').textContent='('+lat.toFixed(3)+', '+lon.toFixed(3)+')';}
function pickTz(id,posix){$('tz').value=tzById(id)?id:'custom';$('tzp').value=posix;}
function rng(el){el.style.setProperty('--p',((el.value-el.min)/(el.max-el.min)*100)+'%');$(el.id==='vol'?'volOut':'avOut').textContent=el.value;}
function setRange(id,v){const e=$(id);e.value=v;rng(e);}
function mkDays(id,isSetting){DAYS.forEach(d=>{const l=h('label');l.title=d[2];const i=h('input');i.type='checkbox';i.dataset.d=d[0];if(isSetting)i.setAttribute('data-s','');l.append(i,h('span',null,d[1]));$(id).appendChild(l);});}
const getMask=id=>qa('#'+id+' input').reduce((m,i)=>i.checked?m|(1<<+i.dataset.d):m,0);
const setMask=(id,m)=>qa('#'+id+' input').forEach(i=>{i.checked=((m>>+i.dataset.d)&1)===1;});
const sName=k=>k+' - '+(SN[k-1]||'');
function fillSounds(list,cur){const sel=$('aSound');sel.textContent='';list.forEach(k=>{const o=h('option',null,sName(k));o.value=k;sel.appendChild(o);});sel.value=cur;}
function collect(){const t=$('aTime').value||'00:00';return{alarmEnabled:$('aEn').checked,alarmHour:+t.slice(0,2),alarmMinute:+t.slice(3,5),alarmDays:getMask('days'),alarmSound:+$('aSound').value,alarmVolume:+$('avol').value,volume:+$('vol').value,petReactions:$('petOn').checked,uiSounds:$('uiOn').checked,occasions:$('occOn').checked,latitude:S.lat,longitude:S.lon,city:S.city,tzName:$('tz').value,tzPosix:$('tzp').value.trim()};}
function checkDirty(){dirty=baseline!==''&&JSON.stringify(collect())!==baseline;$('dirty').classList.toggle('show',dirty);}
function applySettings(s){
 lastS=s;{const{time,...rest}=s;lastKey=JSON.stringify(rest);}
 $('aEn').checked=s.alarmEnabled;
 $('aTime').value=two(s.alarmHour)+':'+two(s.alarmMinute);
 setMask('days',s.alarmDays);fillSounds(s.alarmSounds,s.alarmSound);
 setRange('avol',s.alarmVolume);setRange('vol',s.volume);
 $('petOn').checked=s.petReactions;$('uiOn').checked=s.uiSounds;$('occOn').checked=s.occasions;
 setLoc(s.latitude,s.longitude,s.city);pickTz(s.tzName,s.tzPosix);
 baseline=JSON.stringify(collect());checkDirty();renderStatus(s);
}
async function refreshStatus(){
 const s=await api('/api/settings');
 const{time,...rest}=s;
 if(!dirty&&JSON.stringify(rest)!==lastKey)applySettings(s);
 else{if(lastS)lastS.time=time;renderStatus(lastS||s);}
}
async function save(){
 if(!$('aTime').value){toast('ساعت زنگ را وارد کنید','err');go('alarm');return;}
 if(!getMask('days')){toast('حداقل یک روز را برای زنگ انتخاب کنید','err');go('alarm');return;}
 const b=$('save');b.disabled=true;
 try{applySettings(await api('/api/settings',collect()));toast('ذخیره شد ✓');}
 catch(e){toast(e.http?eMsg(e):'ذخیره نشد؛ ارتباط با ساعت قطع است','err');}
 b.disabled=false;
}
function revert(){if(lastS){applySettings(lastS);toast('تغییرات لغو شد','');}}

/* ---------- home: clock + next alarm ---------- */
function nextAlarm(s){
 if(!s.alarmEnabled||!s.alarmDays)return null;
 const t=two(s.alarmHour)+':'+two(s.alarmMinute),m=/^(\d+)-(\d+)-(\d+) (\d+):(\d+)/.exec(s.time||'');
 if(!m)return{t,d:''};
 const now=new Date(+m[1],+m[2]-1,+m[3],+m[4],+m[5]);
 for(let i=0;i<8;i++){
  const d=new Date(now.getFullYear(),now.getMonth(),now.getDate()+i,s.alarmHour,s.alarmMinute);
  if(d<=now||!((s.alarmDays>>d.getDay())&1))continue;
  return{t,d:i===0?'امروز':i===1?'فردا':DAYS.find(x=>x[0]===d.getDay())[2]};
 }
 return null;
}
function renderStatus(s){
 const m=/^(\d+)-(\d+)-(\d+) (\d+):(\d+)/.exec(s.time||'');
 $('hTime').textContent=m?m[4]+':'+m[5]:'--:--';
 let d='ساعت هنوز همگام نشده';
 if(m){try{const q={};new Intl.DateTimeFormat('fa-IR-u-ca-persian',{weekday:'long',day:'numeric',month:'long',year:'numeric'}).formatToParts(new Date(+m[1],+m[2]-1,+m[3])).forEach(x=>{q[x.type]=x.value;});d=q.weekday+' '+q.day+' '+q.month+' '+q.year;}catch(e){d=m[1]+'-'+m[2]+'-'+m[3];}}
 $('hDate').textContent=d;
 const n=nextAlarm(s);$('hNext').textContent=n?(n.d?n.d+' ':'')+n.t:(s.alarmEnabled?'—':'خاموش');
}
async function petStats(){
 const j=await api('/api/pet');
 $('pToday').textContent=j.today;$('pBest').textContent=j.bestStreak;$('pTotal').textContent=j.total;
 $('bondBox').hidden=!j.bondOn;$('bondBar').style.width=j.bond+'%';$('bondTxt').textContent=j.bond+'٪ — '+(BL[j.bondLevel]||'');
}

/* ---------- alarm control ---------- */
async function alarmState(){
 try{
  const j=await api('/api/alarm');
  let t=AST[j.state]||'—';
  if(j.state==='snoozed'&&j.snoozeLeftS>=0)t+=' — زنگ بعدی تا '+Math.floor(j.snoozeLeftS/60)+':'+two(j.snoozeLeftS%60);
  $('aState').textContent=t;
  qa('[data-act=test]').forEach(b=>{b.disabled=j.state!=='idle';});
  qa('[data-act=snooze]').forEach(b=>{b.disabled=j.state!=='ringing';});
  qa('[data-act=dismiss]').forEach(b=>{b.disabled=j.state==='idle';});
  $('ring').hidden=j.state==='idle';
  $('ringTxt').textContent=j.state==='ringing'?'🔔 آلارم در حال زنگ‌زدن است':t;
 }catch(e){$('aState').textContent='ارتباط با ساعت برقرار نشد';}
}
const pAlarm=poll(alarmState,3000);
async function alarmAct(a){
 if(a==='test'&&dirty)toast('تست با تنظیمات ذخیره‌شده انجام می‌شود','');
 try{await api('/api/alarm/'+a,{});}catch(e){toast(eMsg(e),'err');}
 setTimeout(pAlarm,600);
}
async function preview(q){try{await api('/api/test-sound?'+q,{});}catch(e){toast(eMsg(e),'err');}}
async function occPreview(id){try{await api('/api/test-occasion?id='+id,{});}catch(e){toast(eMsg(e),'err');}}

/* ---------- city search (called from the phone, not the clock) ---------- */
let sAbort=null;
async function find(){
 const q=$('q').value.trim();if(!q)return;
 const box=$('results');box.textContent='';box.appendChild(h('p','hint','در حال جستجو…'));
 if(sAbort)sAbort.abort();sAbort=new AbortController();
 try{
  const r=await fetch('https://geocoding-api.open-meteo.com/v1/search?count=6&language=fa&format=json&name='+encodeURIComponent(q),{signal:sAbort.signal});
  const list=(await r.json()).results||[];
  box.textContent='';
  if(!list.length){box.appendChild(h('p','hint','چیزی پیدا نشد'));return;}
  list.forEach(c=>{
   const b=h('button','res');b.type='button';
   b.append(h('b',null,c.name),h('span','mut',[c.admin1,c.country].filter(Boolean).join('، ')));
   b.onclick=()=>{
    setLoc(c.latitude,c.longitude,c.name);box.textContent='';$('q').value='';
    const z=tzByIana(c.timezone);
    if(z){$('tz').value=z[0];$('tzp').value=z[2];toast('شهر و منطقه‌ی زمانی انتخاب شد؛ ذخیره را بزنید','');}
    else toast('منطقه‌ی زمانی این شهر ('+c.timezone+') در فهرست نیست؛ خودتان انتخاب کنید','err');
    checkDirty();
   };
   box.appendChild(b);
  });
 }catch(e){if(e.name==='AbortError')return;box.textContent='';box.appendChild(h('p','hint err','جستجو ممکن نشد (اینترنت گوشی؟)'));}
}

/* ---------- reminders ---------- */
let RS={items:[],max:12,textMax:79,sound:true},rEdit=0,rKey='';
const byteLen=t=>new TextEncoder().encode(t).length;
function todayStr(){const d=new Date();return d.getFullYear()+'-'+two(d.getMonth()+1)+'-'+two(d.getDate());}
const rGetDays=()=>getMask('rDays');
const rMode=()=>document.querySelector('input[name=rm]:checked').value;
function rShowMode(){const once=rMode()==='once';$('rDateRow').hidden=!once;$('rDaysBox').hidden=once;}
function rCount(){const n=byteLen($('rText').value.trim()),c=$('rCount');c.textContent=n+' / '+RS.textMax+' بایت';c.className='hint'+(n>RS.textMax?' err':'');}
function dayText(m){return m===127?'هر روز':DAYS.filter(d=>(m>>d[0])&1).map(d=>d[2]).join('، ');}
function rRender(){
 const box=$('rList');box.textContent='';
 const items=RS.items.slice().sort((a,b)=>(a.hour*60+a.minute)-(b.hour*60+b.minute));
 $('rEmpty').hidden=items.length>0;$('rSnd').checked=RS.sound;
 $('rCnt').textContent=items.length+' از '+RS.max;$('rAdd').disabled=items.length>=RS.max;
 items.forEach(r=>{
  const d=h('div','rem'+(r.enabled?'':' off')),top=h('div','rt');
  top.append(h('b',null,two(r.hour)+':'+two(r.minute)),h('span','mut sm',r.days?dayText(r.days):('یک‌بار، '+r.date)));
  const x=h('div','rx',r.text);x.dir='auto';
  const a=h('div','ra');
  const sw=h('span','sw'),cb=h('input');cb.type='checkbox';cb.checked=r.enabled;cb.setAttribute('aria-label','فعال');
  cb.onchange=()=>rPost('/api/reminders',Object.assign({},r,{enabled:cb.checked}));sw.append(cb,h('i'));
  const be=h('button','btn soft icon');be.type='button';be.setAttribute('aria-label','ویرایش');be.append(ic('edit'));be.onclick=()=>rOpen(r);
  const bd=h('button','btn danger icon');bd.type='button';bd.setAttribute('aria-label','حذف');bd.append(ic('trash'));
  bd.onclick=()=>{if(confirm('این یادآور حذف شود؟'))rPost('/api/reminders/delete',{id:r.id});};
  a.append(sw,be,bd);d.append(top,x,a);box.appendChild(d);
 });
}
function rApply(j){RS=j;rKey=JSON.stringify(j);rRender();rCount();}
async function rPost(url,body,ok){
 try{const j=await api(url,body);if(j.items)rApply(j);if(ok)toast(ok);return true;}
 catch(e){toast(eMsg(e),'err');if(url==='/api/reminders'||url==='/api/reminders/sound')rLoad().catch(()=>{});return false;}
}
async function rLoad(){const j=await api('/api/reminders');if(JSON.stringify(j)!==rKey)rApply(j);}
function rOpen(r){
 rEdit=r?r.id:0;
 $('rText').value=r?r.text:'';$('rTime').value=r?two(r.hour)+':'+two(r.minute):'09:00';$('rEn').checked=r?r.enabled:true;
 document.querySelector('input[name=rm][value='+(r&&r.days?'week':'once')+']').checked=true;
 $('rDate').value=(r&&r.date)||todayStr();setMask('rDays',r&&r.days?r.days:127);
 $('rFormTitle').textContent=r?'ویرایش یادآور':'یادآور جدید';$('rSave').textContent=r?'ذخیره‌ی تغییرات':'افزودن';
 rShowMode();rCount();
 if(!$('sheet').open)$('sheet').showModal();
 if(!r)$('rText').focus();
}
async function rSave(){
 const text=$('rText').value.trim();
 if(!text){toast('متن را وارد کنید','err');return;}
 if(byteLen(text)>RS.textMax){toast('متن خیلی بلند است','err');return;}
 const tm=$('rTime').value;if(!tm){toast('ساعت را وارد کنید','err');return;}
 const once=rMode()==='once',days=once?0:rGetDays();
 if(!once&&!days){toast('حداقل یک روز را انتخاب کنید','err');return;}
 if(once&&!$('rDate').value){toast('تاریخ را انتخاب کنید','err');return;}
 const ok=await rPost('/api/reminders',{id:rEdit,enabled:$('rEn').checked,hour:+tm.slice(0,2),minute:+tm.slice(3,5),days:days,date:once?$('rDate').value:'',text:text},rEdit?'ذخیره شد ✓':'اضافه شد ✓');
 if(ok)$('sheet').close();
}

/* ---------- system: update + boot log ---------- */
let uTimer=null,uLast='';
async function updateState(){
 try{
  const j=await api('/api/update');
  if(!j.enabled){$('updSec').hidden=true;return;}
  $('updSec').hidden=false;
  $('uVer').textContent=j.current;$('uVar').textContent='('+j.variant+')';
  const busy=j.state==='checking'||j.state==='downloading'||j.state==='done';
  let t='';
  switch(j.state){
   case 'idle':t=(uLast==='downloading'||uLast==='done')?'به‌روزرسانی انجام شد. نسخه‌ی فعلی: '+j.current:(j.autoCheck?'هنوز بررسی نشده؛ ساعت خودش چند دقیقه بعد از روشن‌شدن بررسی می‌کند.':'هنوز بررسی نشده.');break;
   case 'checking':t='در حال بررسی…';break;
   case 'uptodate':t='آخرین نسخه ('+j.latest+') نصب است.';break;
   case 'available':t='نسخه‌ی جدید '+j.latest+' ('+j.sizeKb+' کیلوبایت) آماده‌ی نصب است.';break;
   case 'downloading':t='در حال دانلود و نصب… '+j.percent+'٪ — ساعت را از برق نکشید.';break;
   case 'done':t='نصب شد؛ ساعت در حال ریستارت است…';break;
   case 'failed':t='ناموفق: '+tr(j.error);break;
  }
  if(j.checkedAgoS>=0&&(j.state==='uptodate'||j.state==='available')){const m=Math.floor(j.checkedAgoS/60);t+=' (آخرین بررسی: '+(m<1?'همین الان':m<60?m+' دقیقه پیش':Math.floor(m/60)+' ساعت پیش')+(j.autoCheck?'؛ ساعت خودش هم روزی دو بار بررسی می‌کند':'')+')';}
  const mm=$('uMsg');mm.textContent=t;mm.className='pad'+(j.state==='failed'?' err':'');
  $('uBar').hidden=j.state!=='downloading';$('uProg').style.width=(j.percent||0)+'%';
  $('uCheck').disabled=busy;$('uInstall').hidden=j.state!=='available';$('uInstall').dataset.v=j.latest;
  const n=$('uNotes');n.hidden=!(j.notes&&(j.state==='available'||j.state==='uptodate'));n.href=j.notes||'#';
  uLast=j.state;
  if(busy&&!uTimer)uTimer=setInterval(updateState,1000);
  if(!busy&&uTimer){clearInterval(uTimer);uTimer=null;}
 }catch(e){
  if(uLast==='downloading'||uLast==='done'){$('uMsg').textContent='ساعت در حال ریستارت است؛ چند ثانیه صبر کنید…';if(!uTimer)uTimer=setInterval(updateState,2000);}
  else if(e.http&&e.message==='http_404')$('updSec').hidden=true;
  else $('uMsg').textContent='ارتباط با ساعت برقرار نشد';
 }
}
async function uPost(path,body){
 try{await api(path,body||{});}catch(e){toast(eMsg(e),'err');}
 updateState();
}
function fmtUp(s){const d=Math.floor(s/86400),hr=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return d?d+' روز '+hr+' ساعت':hr?hr+' ساعت '+m+' دقیقه':m+' دقیقه';}
async function bootLog(){
 try{
  const j=await api('/api/bootlog');
  $('bBoots').textContent=j.boots;$('bBrown').textContent=j.brownouts;$('bUp').textContent=fmtUp(j.uptimeS||0);
  $('bBrownBox').classList.toggle('bad',j.brownouts>0);
  const box=$('bList');box.textContent='';
  (j.history||[]).forEach(x=>{const r=h('div','bl'+(x.bad?' bad':''));r.append(h('span',null,'#'+x.boot),h('span',null,x.reason));box.appendChild(r);});
 }catch(e){$('bList').textContent='ارتباط با ساعت برقرار نشد';}
}

/* ---------- wiring ---------- */
(function init(){
 const sel=$('tz');TZ.forEach(z=>{const o=h('option',null,z[1]);o.value=z[0];sel.appendChild(o);});
 {const c=h('option',null,'سفارشی (قاعده‌ی POSIX)');c.value='custom';sel.appendChild(c);}
 const ts=$('tSound');for(let k=1;k<=SN.length;k++){const o=h('option',null,sName(k));o.value=k;ts.appendChild(o);}
 mkDays('days',true);mkDays('rDays',false);setMask('rDays',127);
 $('rDate').value=todayStr();

 sel.onchange=()=>{const z=tzById(sel.value);if(z)$('tzp').value=z[2];};
 $('tzp').oninput=()=>{sel.value='custom';};
 addEventListener('input',e=>{if(e.target.type==='range')rng(e.target);if(e.target.hasAttribute&&e.target.hasAttribute('data-s'))checkDirty();});
 addEventListener('change',e=>{if(e.target.hasAttribute&&e.target.hasAttribute('data-s'))checkDirty();});
 addEventListener('beforeunload',e=>{if(dirty){e.preventDefault();e.returnValue='';}});

 $('save').onclick=save;$('revert').onclick=revert;
 qa('.chip[data-m]').forEach(b=>{b.onclick=()=>{setMask('days',+b.dataset.m);checkDirty();};});
 qa('[data-act]').forEach(b=>{b.onclick=()=>alarmAct(b.dataset.act);});
 qa('#occ .chip').forEach(b=>{b.onclick=()=>occPreview(b.dataset.o);});
 $('testBtn').onclick=()=>preview('volume='+$('vol').value);
 $('tSoundBtn').onclick=()=>preview('sound='+$('tSound').value+'&volume='+$('vol').value);
 $('aSoundBtn').onclick=()=>preview('sound='+$('aSound').value+'&volume='+$('avol').value);
 $('qPet').onclick=()=>preview('volume='+$('vol').value);
 $('qRem').onclick=()=>{go('rem');rOpen(null);};
 $('qOcc').onclick=()=>{go('set');$('cOcc').scrollIntoView({block:'start'});};
 $('findBtn').onclick=find;$('q').onkeydown=e=>{if(e.key==='Enter')find();};

 $('rAdd').onclick=()=>rOpen(null);
 $('rClose').onclick=()=>$('sheet').close();
 $('sheet').addEventListener('click',e=>{if(e.target===$('sheet'))$('sheet').close();});
 $('rForm').onsubmit=e=>{e.preventDefault();rSave();};
 $('rText').oninput=rCount;
 qa('input[name=rm]').forEach(r=>{r.onchange=rShowMode;});
 $('rSnd').onchange=()=>rPost('/api/reminders/sound',{on:$('rSnd').checked});
 $('rTest').onclick=()=>{const t=$('rText').value.trim();if(!t){toast('متن را وارد کنید','err');return;}rPost('/api/reminders/test',{text:t},'روی ساعت نمایش داده شد؛ برای بستن یک دکمه را بزنید');};

 $('uCheck').onclick=()=>uPost('/api/update/check');
 $('uInstall').onclick=()=>{const v=$('uInstall').dataset.v;if(confirm('نسخه‌ی '+v+' نصب شود؟\n\nدانلود و نصب حدود یک دقیقه طول می‌کشد. در این مدت آلارم زنگ نمی‌زند و ساعت دوباره راه‌اندازی می‌شود. ساعت را از برق نکشید.'))uPost('/api/update/install',{version:v});};

 poll(refreshStatus,20000);
 poll(petStats,10000,()=>tab==='home');
 poll(rLoad,15000,()=>tab==='rem'&&!$('sheet').open);
 poll(updateState,30000,()=>tab==='sys'&&!uTimer);

 go(location.hash.slice(1));
 refreshStatus().catch(()=>toast('ارتباط با ساعت برقرار نشد','err'));
 alarmState();
})();
</script>
</body>
</html>)HTML";