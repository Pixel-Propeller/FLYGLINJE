# Bettet webui/index.html als WebUI.h in die Firmware ein und erzeugt die Logo-Schrift aus Font.h-Daten.
import re,sys,os,importlib.util
HERE=os.path.dirname(os.path.abspath(__file__))
base=os.path.join(HERE,'..','FLYGLINJE','')
spec=importlib.util.spec_from_file_location('gf',os.path.join(HERE,'gen_font.py'));gf=importlib.util.module_from_spec(spec);spec.loader.exec_module(gf)
glyph={ch:gf.cols(ch) for ch in '\x06 HEJ!'}
js="const GLYPH={"+",".join("'%s':[%s]"%(('\\u%04x'%ord(k)) if ord(k)<32 else k,",".join("0x%02X"%v for v in c)) for k,c in glyph.items())+"};"
html=open(base+'webui/index.html',encoding='utf-8').read()
html=re.sub(r"const GLYPH=\{.*?\};",lambda m:js,html,count=1,flags=re.S)
open(base+'webui/index.html','w',encoding='utf-8').write(html)
assert ')FLYG"' not in html
import gzip,hashlib
raw=html.encode('utf-8')
gz=gzip.compress(raw,compresslevel=9,mtime=0)
etag=hashlib.sha256(raw).hexdigest()[:12]
lines=[]
for i in range(0,len(gz),24): lines.append('  '+','.join('0x%02x'%b for b in gz[i:i+24])+',')
out=('#pragma once\n#include <Arduino.h>\n// Automatisch erzeugt aus webui/index.html (firmware/tools/build_webui.py). Nicht von Hand bearbeiten.\n'
     '// Gzip-komprimiert: %d statt %d Bytes – spart Flash und WLAN-Zeit beim Öffnen.\n'
     'namespace WebUI {\nconst char ETAG[] = "\\"%s\\"";\nconst size_t PAGE_GZ_LEN = %d;\nconst uint8_t PAGE_GZ[] PROGMEM = {\n%s\n};\n}\n') % (len(gz),len(raw),etag,len(gz),'\n'.join(lines))
# Flughafen-Liste als eigene Datei (wird erst im Album geladen)
ap=open(base+'webui/airports.js','rb').read();apgz=gzip.compress(ap,compresslevel=9,mtime=0)
apl=[]
for i in range(0,len(apgz),24): apl.append('  '+','.join('0x%02x'%b for b in apgz[i:i+24])+',')
out=out.rstrip()[:-1]+('const char AP_ETAG[] = "\\"%s\\"";\nconst size_t AP_GZ_LEN = %d;\nconst uint8_t AP_GZ[] PROGMEM = {\n%s\n};\n}\n')%(hashlib.sha256(ap).hexdigest()[:12],len(apgz),'\n'.join(apl))
open(base+'WebUI.h','w',encoding='utf-8').write(out)
print('airports.js',len(ap),'->',len(apgz))
print(len(raw),'bytes ->',len(gz),'gzip')
