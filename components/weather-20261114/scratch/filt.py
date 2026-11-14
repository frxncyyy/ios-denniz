import sys, json, os, collections
keep = set("weatherd Weather WeatherWidget WeatherPoster WeatherIntents trustd timed nsurlsessiond apsd akd cloudd mDNSResponder symptomsd configd mobileactivationd identityservicesd geod appstored itunesstored securityd keybagd networkserviceproxy tccd captiveagent ntpd appleaccountd com.apple.weatherkit.authservice absd adid fairplayd.H2 fairplayd lockdownd nehelper nesessionmanager amsaccountsd amsengagementd countryd".split())
chatty = set("locationd mobileassetd routined Preferences SpringBoard CommCenter dasd wifid WiFiAgent newsd NewsToday2 Maps".split())
kw = (b'eather', b'SecTrust', b'ertificate', b'-9813', b'-9807', b'-9814', b'-9802', b'-1202', b'-1200', b'boringssl', b'TLS', b'clock', b'settimeofday', b'NTP', b'ntp', b'bsinthe', b'BAA', b'ttest', b'aptive', b'escal', b'ime changed', b'ime set', b'TMSet', b'SetSystemTime')
outs = {}
stats = collections.Counter()
boots = {}
os.makedirs('ul', exist_ok=True)
def out(name):
    if name not in outs:
        outs[name] = open('ul/'+name+'.tsv','w')
    return outs[name]
for raw in open(sys.argv[1],'rb'):
    try:
        i = raw.index(b'"process":"')+11
        j = raw.index(b'"', i)
    except ValueError:
        continue
    proc = raw[i:j].rsplit(b'/',1)[-1].decode('utf8','replace')
    stats[proc]+=1
    e = raw.find(b'"evidence":"')
    body = raw[:e] if e>0 else raw
    k = proc in keep
    hit = any(w in body for w in kw)
    if not k:
        if proc in chatty:
            if not hit and b'"log_type":"Error"' not in body and b'"log_type":"Fault"' not in body: continue
        elif not hit: continue
    try: d = json.loads(raw)
    except Exception: continue
    msg = (d.get('message') or '')
    if not k and proc not in chatty:
        mb = msg.encode('utf8','replace')
        if not any(w in mb for w in kw): continue
    b = d.get('boot_uuid','')[:8]
    t = d.get('time',0)/1e9
    bs = boots.setdefault(b,[t,t,0]); bs[0]=min(bs[0],t); bs[1]=max(bs[1],t); bs[2]+=1
    msg = msg.replace('\n','\\n').replace('\t',' ')
    line = f"{b}\t{t:.3f}\t{d.get('pid')}\t{proc}\t{d.get('subsystem') or ''}|{d.get('category') or ''}\t{d.get('log_type')}\t{os.path.basename(d.get('evidence',''))}\t{msg}\n"
    out(proc if (k or proc in chatty) else '_kw').write(line)
for f in outs.values(): f.close()
with open('ul/_stats.txt','w') as f:
    for p,n in stats.most_common(): f.write(f"{n}\t{p}\n")
    f.write('\nBOOTS (kept lines only)\n')
    for b,v in sorted(boots.items(), key=lambda x:x[1][0]): f.write(f"{b}\t{v}\n")
