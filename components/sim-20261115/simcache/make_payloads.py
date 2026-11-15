#!/usr/bin/env python3
# builds payload-simcache1* dirs + owned-simcache1.json from build/n2simcache (run build_ios.sh first)
import hashlib, json, os, shutil
H=os.path.dirname(os.path.abspath(__file__)); B=open(H+'/build/n2simcache','rb').read(); S=hashlib.sha256(B).hexdigest()
JOB={'Label':'com.nord2.simcache','UserName':'root','RunAtLoad':True,'LaunchOnlyOnce':True,'KeepAlive':False,
     'ProgramArguments':['/usr/libexec/n2simcache'],'POSIXSpawnType':'Adaptive','EnablePressuredExit':False,
     'StandardErrorPath':'/private/var/db/nord2-simcache/stderr.log'}
V={'simcache1':('install',[],[]), 'simcache1-norestart':('install',['norestart'],[]), 'simcache1-dryrun':('install',['dryrun'],[]),
   'simcache1-off':('switches',['disabled'],[]), 'simcache1-on':('switches',[],['norestart-clear']),
   'simcache1-sw-norestart':('switches',['norestart'],[]), 'simcache1-sw-keepkey':('switches',['keepkey'],[])}
for n,(mode,sw,fl) in V.items():
    d=H+'/payload-'+n; shutil.rmtree(d,ignore_errors=True); os.makedirs(d)
    plan={'payload':n,'mode':mode,'switches':sw,'flags':fl,'job':JOB,'n2simcache_sha256':S}
    if mode=='install': shutil.copy2(H+'/build/n2simcache',d+'/n2simcache')
    json.dump(plan,open(d+'/plan.json','w'),indent=1)
d=H+'/payload-simcache1-undo'; shutil.rmtree(d,ignore_errors=True); os.makedirs(d); json.dump({'payload':'simcache1-undo','mode':'undo'},open(d+'/plan.json','w'))
D='/private/var/db/nord2-simcache'
own=['/System/Library/xpc/launchd.plist','/private/var/db/nord2-metal/receipt.json','/usr/libexec/n2simcache',D]+[D+'/'+x for x in
     ['receipt.json','disabled','norestart','keepkey','dryrun','log','stderr.log']]
json.dump(own,open(H+'/owned-simcache1.json','w'),indent=0)
print('payloads built, n2simcache', S)
