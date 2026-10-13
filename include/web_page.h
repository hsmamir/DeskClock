#pragma once
#include <pgmspace.h>

// Settings page served at http://deskclock.local/ — one self-contained file,
// no external resources except the Open-Meteo city search (called by the
// phone's browser, not by the clock).
// The TZ table below maps a city's IANA timezone (what the city search
// returns) to a POSIX rule that includes DST. Governments change DST rules;
// the "custom" field in the page covers anything not listed.
static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="fa" dir="rtl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskClock</title>
<style>
:root{--bg:#f3f5f8;--card:#fff;--fg:#1d2330;--mut:#6a7385;--acc:#0e8fb0;--on:#fff;--bd:#dde2ea;--ok:#188a4a;--err:#c93a3a}
@media(prefers-color-scheme:dark){:root{--bg:#0e1116;--card:#171b22;--fg:#e7eaf0;--mut:#98a1b3;--acc:#35c8ee;--on:#00141c;--bd:#272d38;--ok:#4cc27d;--err:#ff6b6b}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.6 system-ui,-apple-system,"Segoe UI",Tahoma,sans-serif}
main{max-width:520px;margin:0 auto;padding:16px}
h1{font-size:1.4rem;margin:8px 0 0}
h2{font-size:1rem;margin:0 0 8px;color:var(--acc)}
section{background:var(--card);border:1px solid var(--bd);border-radius:14px;padding:14px 16px;margin:12px 0}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:8px 0}
.mut{color:var(--mut);font-size:.88rem;margin:4px 0}
input,select,button{font:inherit;color:inherit}
input[type=text],input[type=time],select{background:var(--bg);border:1px solid var(--bd);border-radius:10px;padding:8px 10px;min-width:0}
input[type=text]{flex:1}
select{width:100%}
input[type=range]{flex:1;accent-color:var(--acc)}
input[type=checkbox]{width:22px;height:22px;accent-color:var(--acc)}
button{background:var(--acc);color:var(--on);border:0;border-radius:10px;padding:9px 16px;cursor:pointer}
button.ghost{background:transparent;color:var(--acc);border:1px solid var(--acc)}
.days{display:flex;gap:6px;margin:8px 0}
.days label{flex:1;position:relative}
.days input{position:absolute;opacity:0;width:0;height:0}
.days span{display:block;text-align:center;padding:8px 0;border:1px solid var(--bd);border-radius:10px;background:var(--bg);cursor:pointer;user-select:none}
.days input:checked+span{background:var(--acc);color:var(--on);border-color:var(--acc)}
#aSound{flex:1;width:auto}
button.danger{background:transparent;color:var(--err);border:1px solid var(--err)}
button:disabled{opacity:.4;cursor:default}
.btns{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0}
.btns button{flex:1;min-width:90px}
#save{width:100%;padding:12px;font-size:1.05rem}
.res button{display:block;width:100%;text-align:right;background:var(--bg);color:var(--fg);border:1px solid var(--bd);margin:6px 0}
details{margin-top:10px}
#tzp{width:100%;direction:ltr;margin-top:8px}
#msg{min-height:1.6em;text-align:center}
.rem{border:1px solid var(--bd);border-radius:10px;padding:8px 12px;margin:8px 0}
.rem.off{opacity:.5}
.rem .t{font-size:1.15rem;font-weight:600;direction:ltr;display:inline-block;color:var(--acc)}
.rem .x{margin:2px 0;word-break:break-word}
.rem .btns{margin:6px 0 0}.rem .btns button{min-width:64px;padding:6px 10px;font-size:.9rem}
#rText{width:100%;flex:none}
#bl{width:100%;border-collapse:collapse;direction:ltr;font-size:.92rem}
#bl td,#bl th{padding:4px 6px;text-align:left;border-bottom:1px solid var(--bd)}
#bl .bad{color:var(--err);font-weight:600}
</style>
</head>
<body>
<main>
<h1>⏰ DeskClock</h1>
<p class="mut" id="now">…</p>

<section>
<h2>آلارم</h2>
<label class="row"><span>فعال</span><input type="checkbox" id="aEn"></label>
<label class="row"><span>ساعت زنگ</span><input type="time" id="aTime"></label>
<div class="mut">روزهای زنگ</div>
<div class="days" id="days"></div>
<div class="row"><span>صدای زنگ</span><select id="aSound"></select><button type="button" class="ghost" id="aSoundBtn">پیش‌نمایش</button></div>
<div class="row"><span>ولوم آلارم</span><input type="range" id="avol" min="1" max="30"><output id="avOut"></output></div>
</section>

<section>
<h2>صدای نوازش</h2>
<div class="row"><span>ولوم</span><input type="range" id="vol" min="0" max="30"><output id="volOut"></output></div>
<button type="button" class="ghost" id="testBtn">پخش صدای نوازش</button>
<div class="row"><span>تست همه‌ی صداها</span><select id="tSound"></select><button type="button" class="ghost" id="tSoundBtn">پخش</button></div>
<label class="row"><span>بوق تنظیمات (روشن/خاموش/خطا)</span><input type="checkbox" id="uiOn"></label>
</section>

<section>
<h2>مناسبت‌های سالانه</h2>
<label class="row"><span>انیمیشن مناسبت‌ها</span><input type="checkbox" id="occOn"></label>
<div class="row"><span>پیش‌نمایش</span><select id="occSel"><option value="1">هالووین</option><option value="2">عید پاک</option><option value="3">کریسمس</option><option value="4">شب سال نو میلادی</option><option value="5">ولنتاین</option><option value="0">توقف پیش‌نمایش</option></select><button type="button" class="ghost" id="occBtn">نمایش</button></div>
<p class="mut">هالووین ۲۸ تا ۳۱ اکتبر، عید پاک از جمعه تا دوشنبه، ولنتاین ۱۳ و ۱۴ فوریه، کریسمس ۲۰ تا ۲۶ دسامبر، شب سال نو از ساعت ۱۸ روز ۳۱ دسامبر.</p>
</section>

<section>
<h2>واکنش‌های نوازش</h2>
<label class="row"><span>واکنش‌های متنوع</span><input type="checkbox" id="petOn"></label>
<p class="mut" id="petStats">…</p>
<p class="mut" id="bondInfo"></p>
<p class="mut">چند بار پشت‌سرهم = قلب، بعد گیجی و در آخر اخم 😄 — شب خواب‌آلود، در باران یک ابرو بالا، و نگه‌داشتن دکمه بیش از یک ثانیه = خرخر. خاموش = همیشه همان چهره‌ی خوشحال.</p>
<p class="mut">دلبستگی: هر چه منظم‌تر نوازشش کنی (یا نگه داشتن دکمه تا خرخر) بیشتر بهت وابسته می‌شه: صبورتر می‌شه، گاهی خودش لبخند می‌زنه و رنگ چشم‌هاش کمی صورتی می‌شه. اگه چند روز نبینیش دلتنگ می‌شه، گاهی آه می‌کشه و دیرتر قهرش تموم می‌شه. افزایش روزانه محدوده، پس نوازش پشت‌سرهم زیاد فایده‌ای نداره.</p>
</section>

<section>
<h2>تست آلارم</h2>
<p class="mut" id="aState">…</p>
<div class="btns">
<button type="button" id="aTest">تست آلارم</button>
<button type="button" class="ghost" id="aSnooze">اسنوز</button>
<button type="button" class="danger" id="aOff">خاموش</button>
</div>
<p class="mut">آلارم واقعی با صدا و ولوم آلارمِ <b>ذخیره‌شده</b> زنگ می‌خورد (تغییرات بالا را اول ذخیره کنید)، بدون توجه به روزهای هفته. اسنوز و خاموش روی آلارم واقعی هم کار می‌کنند.</p>
</section>

<section>
<h2>یادآورها</h2>
<div id="rList"></div>
<p class="mut" id="rEmpty">هنوز یادآوری اضافه نشده.</p>
<label class="row"><span>صدای یادآور</span><input type="checkbox" id="rSnd"></label>
<h2 id="rFormTitle" style="margin-top:16px">یادآور جدید</h2>
<input type="text" id="rText" dir="auto" placeholder="مثلاً: قرص‌هایت را بخور" autocomplete="off">
<p class="mut" id="rCount"></p>
<div class="row"><span>ساعت</span><input type="time" id="rTime" value="09:00"></div>
<div class="row"><span>تکرار</span><select id="rMode"><option value="once">فقط یک‌بار</option><option value="week">هفتگی</option></select></div>
<div class="row" id="rDateRow"><span>تاریخ</span><input type="date" id="rDate"></div>
<div id="rDaysBox" style="display:none"><div class="mut">روزها</div><div class="days" id="rDays"></div></div>
<label class="row"><span>فعال</span><input type="checkbox" id="rEn" checked></label>
<div class="btns">
<button type="button" id="rSave">افزودن</button>
<button type="button" class="ghost" id="rTest">نمایش روی ساعت</button>
<button type="button" class="ghost" id="rCancel" style="display:none">لغو ویرایش</button>
</div>
<p class="mut">سر ساعت مقرر، متن تمام‌صفحه روی ساعت نشان داده می‌شود و تا فشردن <b>هر یک از دکمه‌ها</b> می‌ماند (حداکثر چند دقیقه). متن فارسی و انگلیسی پشتیبانی می‌شود. «نمایش روی ساعت» همین الان متن را روی نمایشگر می‌آورد تا خوانایی را ببینید. یادآورِ «یک‌بار» بعد از نمایش پاک می‌شود. شبِ صدا خاموش است (مثل صدای نوازش).</p>
</section>

<section>
<h2>شهر (برای آب‌وهوا)</h2>
<div class="row"><input type="text" id="q" placeholder="نام شهر…"><button type="button" id="findBtn">جستجو</button></div>
<div class="res" id="results"></div>
<p class="mut">شهر فعلی: <b id="cityName">—</b> <span id="coords" dir="ltr"></span></p>
</section>

<section>
<h2>منطقه‌ی زمانی</h2>
<select id="tz"></select>
<p class="mut">تغییر ساعت تابستانی به‌صورت خودکار اعمال می‌شود.</p>
<details><summary class="mut">پیشرفته: قاعده‌ی POSIX</summary>
<input type="text" id="tzp" spellcheck="false"></details>
</section>

<section>
<h2>تشخیص و ریست‌ها</h2>
<p class="mut" id="blSum">…</p>
<table id="bl"><tbody></tbody></table>
<p class="mut">Brownout یعنی افت ولتاژ تغذیه (سروو و صدا روی یک تغذیه‌اند): منبع تغذیه و کابل را بررسی کنید. ثبت‌ها در حافظهٔ فلش می‌مانند و با قطع برق پاک نمی‌شوند.</p>
</section>

<button id="save">ذخیره</button>
<p id="msg"></p>
</main>
<script>
const $=id=>document.getElementById(id);
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
let S={lat:0,lon:0,city:''};
function msg(t,ok){const m=$('msg');m.textContent=t;m.style.color=ok?'var(--ok)':'var(--err)';}
const tzById=id=>TZ.find(z=>z[0]===id);
const tzByIana=n=>TZ.find(z=>z[3].split(',').includes(n));
function setLoc(lat,lon,city){S.lat=lat;S.lon=lon;S.city=city;$('cityName').textContent=city||'—';$('coords').textContent='('+lat.toFixed(3)+', '+lon.toFixed(3)+')';}
function pickTz(id,posix){$('tz').value=tzById(id)?id:'custom';$('tzp').value=posix;}
(function(){const sel=$('tz');TZ.forEach(z=>{const o=document.createElement('option');o.value=z[0];o.textContent=z[1];sel.appendChild(o);});
const c=document.createElement('option');c.value='custom';c.textContent='سفارشی (قاعده‌ی POSIX)';sel.appendChild(c);})();
$('tz').onchange=()=>{const z=tzById($('tz').value);if(z)$('tzp').value=z[2];};
$('tzp').oninput=()=>{$('tz').value='custom';};
$('vol').oninput=()=>{$('volOut').textContent=$('vol').value;};
$('avol').oninput=()=>{$('avOut').textContent=$('avol').value;};
// [tm_wday, short label, full name] in Persian week order (Saturday first)
const DAYS=[[6,'ش','شنبه'],[0,'ی','یکشنبه'],[1,'د','دوشنبه'],[2,'س','سه‌شنبه'],[3,'چ','چهارشنبه'],[4,'پ','پنجشنبه'],[5,'ج','جمعه']];
(function(){const box=$('days');DAYS.forEach(d=>{const l=document.createElement('label');l.title=d[2];
const i=document.createElement('input');i.type='checkbox';i.dataset.d=d[0];
const s=document.createElement('span');s.textContent=d[1];l.append(i,s);box.appendChild(l);});})();
function setDays(m){document.querySelectorAll('#days input').forEach(i=>{i.checked=((m>>(+i.dataset.d))&1)===1;});}
function getDays(){let m=0;document.querySelectorAll('#days input').forEach(i=>{if(i.checked)m|=1<<(+i.dataset.d);});return m;}
const SN=['آرام','اصلی','جیک نوازش','ملودیک','بلند','بویینگ','بیپ‌ها','خمیازه','زنگ ساعت','روشن‌شدن','UI روشن','UI خاموش','تیک‌تاک','UI خطا'];
function sName(k){return k+' - '+(SN[k-1]||'');}
function fillSounds(list,cur){const sel=$('aSound');sel.textContent='';
list.forEach(k=>{const o=document.createElement('option');o.value=k;o.textContent=sName(k);sel.appendChild(o);});sel.value=cur;}
(function(){const sel=$('tSound');for(let k=1;k<=SN.length;k++){const o=document.createElement('option');o.value=k;o.textContent=sName(k);sel.appendChild(o);}})();
async function load(){
 try{
  const s=await (await fetch('/api/settings')).json();
  $('aEn').checked=s.alarmEnabled;
  $('aTime').value=String(s.alarmHour).padStart(2,'0')+':'+String(s.alarmMinute).padStart(2,'0');
  setDays(s.alarmDays);fillSounds(s.alarmSounds,s.alarmSound);
  $('avol').value=s.alarmVolume;$('avOut').textContent=s.alarmVolume;
  $('vol').value=s.volume;$('volOut').textContent=s.volume;
  $('petOn').checked=s.petReactions;$('uiOn').checked=s.uiSounds;$('occOn').checked=s.occasions;
  setLoc(s.latitude,s.longitude,s.city);
  pickTz(s.tzName,s.tzPosix);
  $('now').textContent='ساعت دستگاه: '+(s.time||'هنوز همگام نشده');
 }catch(e){msg('ارتباط با ساعت برقرار نشد',false);}
}
async function save(){
 const t=$('aTime').value;
 if(!t){msg('ساعت زنگ را وارد کنید',false);return;}
 const days=getDays();
 if(!days){msg('حداقل یک روز را برای زنگ انتخاب کنید',false);return;}
 const body={alarmEnabled:$('aEn').checked,alarmHour:+t.slice(0,2),alarmMinute:+t.slice(3,5),
  alarmDays:days,alarmSound:+$('aSound').value,alarmVolume:+$('avol').value,
  volume:+$('vol').value,petReactions:$('petOn').checked,uiSounds:$('uiOn').checked,occasions:$('occOn').checked,latitude:S.lat,longitude:S.lon,city:S.city,
  tzName:$('tz').value,tzPosix:$('tzp').value.trim()};
 try{
  const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  const j=await r.json();
  if(!r.ok){msg('خطا: '+(j.error||r.status),false);return;}
  await load();msg('ذخیره شد ✓',true);
 }catch(e){msg('ذخیره نشد؛ ارتباط با ساعت قطع است',false);}
}
async function find(){
 const q=$('q').value.trim();if(!q)return;
 const box=$('results');box.textContent='…';
 try{
  const r=await fetch('https://geocoding-api.open-meteo.com/v1/search?count=6&language=fa&format=json&name='+encodeURIComponent(q));
  const list=(await r.json()).results||[];
  box.textContent='';
  if(!list.length){box.textContent='چیزی پیدا نشد';return;}
  list.forEach(c=>{
   const b=document.createElement('button');b.type='button';
   b.textContent=[c.name,c.admin1,c.country].filter(Boolean).join('، ');
   b.onclick=()=>{
    setLoc(c.latitude,c.longitude,c.name);box.textContent='';
    const z=tzByIana(c.timezone);
    if(z){$('tz').value=z[0];$('tzp').value=z[2];msg('شهر و منطقه‌ی زمانی انتخاب شد؛ ذخیره را بزنید',true);}
    else msg('منطقه‌ی زمانی این شهر ('+c.timezone+') در فهرست نیست؛ خودتان انتخاب کنید',false);
   };
   box.appendChild(b);
  });
 }catch(e){box.textContent='';msg('جستجو ممکن نشد (اینترنت گوشی؟)',false);}
}
$('findBtn').onclick=find;
$('q').onkeydown=e=>{if(e.key==='Enter')find();};
$('save').onclick=save;
async function preview(q){
 try{const r=await fetch('/api/test-sound?'+q,{method:'POST'});
  if(!r.ok){const j=await r.json();msg('خطا: '+(j.error||r.status),false);}else msg('',true);
 }catch(e){msg('ارتباط با ساعت برقرار نشد',false);}
}
$('occBtn').onclick=async()=>{try{const r=await fetch('/api/test-occasion?id='+$('occSel').value,{method:'POST'});if(!r.ok){const j=await r.json();msg('خطا: '+(j.error||r.status),false);}else msg('',true);}catch(e){msg('ارتباط با ساعت برقرار نشد',false);}};
$('testBtn').onclick=()=>preview('volume='+$('vol').value);
$('tSoundBtn').onclick=()=>preview('sound='+$('tSound').value+'&volume='+$('vol').value);
$('aSoundBtn').onclick=()=>preview('sound='+$('aSound').value+'&volume='+$('avol').value);
const AST={idle:'آماده (آلارم زنگ نمی‌زند)',ringing:'در حال زنگ‌زدن 🔔',snoozed:'اسنوز شده'};
async function alarmState(){
 try{
  const j=await (await fetch('/api/alarm')).json();
  let t=AST[j.state]||'—';
  if(j.state==='snoozed'&&j.snoozeLeftS>=0){const m=Math.floor(j.snoozeLeftS/60),s=j.snoozeLeftS%60;t+=' — زنگ بعدی تا '+m+':'+String(s).padStart(2,'0');}
  $('aState').textContent=t;
  $('aTest').disabled=j.state!=='idle';
  $('aSnooze').disabled=j.state!=='ringing';
  $('aOff').disabled=j.state==='idle';
 }catch(e){$('aState').textContent='ارتباط با ساعت برقرار نشد';}
}
async function alarmAct(a){
 try{
  const r=await fetch('/api/alarm/'+a,{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});
  if(!r.ok){const j=await r.json();msg('خطا: '+(j.error||r.status),false);}else msg('',true);
 }catch(e){msg('ارتباط با ساعت برقرار نشد',false);}
 setTimeout(alarmState,600);
}
$('aTest').onclick=()=>alarmAct('test');
$('aSnooze').onclick=()=>alarmAct('snooze');
$('aOff').onclick=()=>alarmAct('dismiss');
async function bootLog(){
 try{
  const j=await (await fetch('/api/bootlog')).json();
  $('blSum').textContent='تعداد بوت: '+j.boots+' — brownout: '+j.brownouts;
  const tb=document.querySelector('#bl tbody');tb.textContent='';
  (j.history||[]).forEach(h=>{const tr=document.createElement('tr');
   const a=document.createElement('td');a.textContent='#'+h.boot;
   const b=document.createElement('td');b.textContent=h.reason;if(h.bad)b.className='bad';
   tr.append(a,b);tb.append(tr);});
 }catch(e){$('blSum').textContent='ارتباط با ساعت برقرار نشد';}
}
async function petStats(){
 try{
  const j=await (await fetch('/api/pet')).json();
  $('petStats').textContent='امروز: '+j.today+' — بیشترین پشت‌سرهم: '+j.bestStreak+' — کل: '+j.total;
  const BL=['دلتنگ','آشنا','صمیمی','دلبسته'];
  $('bondInfo').textContent=j.bondOn?('دلبستگی: '+j.bond+'٪ — '+(BL[j.bondLevel]||'')):'';
 }catch(e){$('petStats').textContent='ارتباط با ساعت برقرار نشد';}
}
setInterval(()=>{if(!document.hidden)alarmState();},2000);
setInterval(()=>{if(!document.hidden)petStats();},10000);
load();
alarmState();
bootLog();
petStats();

// ---- reminders ----
let RS={items:[],max:12,textMax:79,sound:true},rEdit=0;
const byteLen=t=>new TextEncoder().encode(t).length;
function todayStr(){const d=new Date();return d.getFullYear()+'-'+String(d.getMonth()+1).padStart(2,'0')+'-'+String(d.getDate()).padStart(2,'0');}
(function(){const box=$('rDays');DAYS.forEach(d=>{const l=document.createElement('label');l.title=d[2];
const i=document.createElement('input');i.type='checkbox';i.dataset.d=d[0];i.checked=true;
const s=document.createElement('span');s.textContent=d[1];l.append(i,s);box.appendChild(l);});})();
function rGetDays(){let m=0;document.querySelectorAll('#rDays input').forEach(i=>{if(i.checked)m|=1<<(+i.dataset.d);});return m;}
function rSetDays(m){document.querySelectorAll('#rDays input').forEach(i=>{i.checked=((m>>(+i.dataset.d))&1)===1;});}
function rMode(){const once=$('rMode').value==='once';$('rDateRow').style.display=once?'':'none';$('rDaysBox').style.display=once?'none':'';}
function rCount(){const n=byteLen($('rText').value.trim());const c=$('rCount');c.textContent=n+' / '+RS.textMax+' بایت';c.style.color=n>RS.textMax?'var(--err)':'';}
function dayText(m){if(m===127)return 'هر روز';return DAYS.filter(d=>(m>>d[0])&1).map(d=>d[2]).join('، ');}
function two(n){return String(n).padStart(2,'0');}
function rRender(){
 const box=$('rList');box.textContent='';
 const items=RS.items.slice().sort((a,b)=>(a.hour*60+a.minute)-(b.hour*60+b.minute));
 $('rEmpty').style.display=items.length?'none':'';
 $('rSnd').checked=RS.sound;
 items.forEach(r=>{
  const d=document.createElement('div');d.className='rem'+(r.enabled?'':' off');
  const t=document.createElement('span');t.className='t';t.textContent=two(r.hour)+':'+two(r.minute);
  const sch=document.createElement('span');sch.className='mut';sch.textContent=' — '+(r.days?dayText(r.days):('یک‌بار، '+r.date));
  const x=document.createElement('div');x.className='x';x.dir='auto';x.textContent=r.text;
  const b=document.createElement('div');b.className='btns';
  const bt=document.createElement('button');bt.className='ghost';bt.textContent=r.enabled?'غیرفعال':'فعال';bt.onclick=()=>rPost('/api/reminders',Object.assign({},r,{enabled:!r.enabled}));
  const be=document.createElement('button');be.className='ghost';be.textContent='ویرایش';be.onclick=()=>rStartEdit(r);
  const bd=document.createElement('button');bd.className='danger';bd.textContent='حذف';bd.onclick=()=>{if(confirm('حذف شود؟'))rPost('/api/reminders/delete',{id:r.id});};
  b.append(bt,be,bd);d.append(t,sch,x,b);box.appendChild(d);
 });
}
async function rPost(url,body,okMsg){
 try{
  const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  const j=await r.json();
  if(!r.ok){msg('خطا: '+(j.error||r.status),false);return false;}
  if(j.items){RS=j;rRender();rCount();}
  if(okMsg)msg(okMsg,true);
  return true;
 }catch(e){msg('ارتباط با ساعت برقرار نشد',false);return false;}
}
async function rLoad(){
 try{RS=await (await fetch('/api/reminders')).json();rRender();rCount();}catch(e){}
}
function rReset(){
 rEdit=0;$('rText').value='';$('rTime').value='09:00';$('rMode').value='once';$('rDate').value=todayStr();$('rEn').checked=true;rSetDays(127);
 $('rFormTitle').textContent='یادآور جدید';$('rSave').textContent='افزودن';$('rCancel').style.display='none';rMode();rCount();
}
function rStartEdit(r){
 rEdit=r.id;$('rText').value=r.text;$('rTime').value=two(r.hour)+':'+two(r.minute);$('rEn').checked=r.enabled;
 $('rMode').value=r.days?'week':'once';$('rDate').value=r.date||todayStr();rSetDays(r.days||127);
 $('rFormTitle').textContent='ویرایش یادآور';$('rSave').textContent='ذخیرهٔ تغییرات';$('rCancel').style.display='';rMode();rCount();
 $('rText').scrollIntoView({behavior:'smooth',block:'center'});
}
$('rMode').onchange=rMode;
$('rText').oninput=rCount;
$('rCancel').onclick=rReset;
$('rSnd').onchange=()=>rPost('/api/reminders/sound',{on:$('rSnd').checked});
$('rTest').onclick=()=>{const t=$('rText').value.trim();if(!t){msg('متن را وارد کنید',false);return;}rPost('/api/reminders/test',{text:t},'روی ساعت نمایش داده شد؛ برای بستن یک دکمه را بزنید');};
$('rSave').onclick=async()=>{
 const text=$('rText').value.trim();
 if(!text){msg('متن را وارد کنید',false);return;}
 if(byteLen(text)>RS.textMax){msg('متن خیلی بلند است',false);return;}
 const tm=$('rTime').value;if(!tm){msg('ساعت را وارد کنید',false);return;}
 const once=$('rMode').value==='once';
 const days=once?0:rGetDays();
 if(!once&&!days){msg('حداقل یک روز را انتخاب کنید',false);return;}
 if(once&&!$('rDate').value){msg('تاریخ را انتخاب کنید',false);return;}
 const ok=await rPost('/api/reminders',{id:rEdit,enabled:$('rEn').checked,hour:+tm.slice(0,2),minute:+tm.slice(3,5),days:days,date:once?$('rDate').value:'',text:text},rEdit?'ذخیره شد ✓':'اضافه شد ✓');
 if(ok)rReset();
};
setInterval(()=>{if(!document.hidden&&!rEdit&&!$('rText').value)rLoad();},15000);
rReset();rLoad();
</script>
</body>
</html>)HTML";