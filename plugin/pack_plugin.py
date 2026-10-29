from pathlib import Path
import struct,hashlib,json,datetime

root=Path(__file__).resolve().parent
files={'SYSTEM/AUTORUN/NBXARDASAI.DLL':(root/'build/Release/NBXardasAI.dll').read_bytes(),
       'SYSTEM/AUTORUN/NBXARDASAI.PATCH':(root/'NBXardasAI.patch').read_bytes()}
tree={}
for path,data in files.items():
    node=tree
    parts=path.split('/')
    for part in parts[:-1]:node=node.setdefault(part,{})
    node[parts[-1]]=data
entries=[]
def emit(node):
    start=len(entries);pairs=sorted(node.items())
    for i,(name,val) in enumerate(pairs):entries.append([name,0,0,(0x80000000 if isinstance(val,dict) else 0)|(0x40000000 if i==len(pairs)-1 else 0),0 if isinstance(val,dict) else 32,val])
    for i,(_,val) in enumerate(pairs):
        if isinstance(val,dict):entries[start+i][1]=emit(val)
    return start
emit(tree);offset=296+80*len(entries);payload=[]
for e in entries:
    if not e[3]&0x80000000:e[1]=offset;e[2]=len(e[5]);payload.append(e[5]);offset+=e[2]
header=b'NB Xardas AI: native dialogue and restricted local bridge'.ljust(256,b'\x1a')+b'PSVDSC_V2.00\n\r\n\r'+struct.pack('<6I',len(entries),len(files),2116091908,sum(map(len,payload)),296,80)
output=root/'NBXardasAI.vdf'
with output.open('wb') as f:
    f.write(header)
    for name,address,size,flags,attr,_ in entries:f.write(struct.pack('<64s4I',name.encode().ljust(64,b' '),address,size,flags,attr))
    for data in payload:f.write(data)
assert output.stat().st_size==offset
manifest={'vdf':str(output),'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'files':{p:hashlib.sha256(b).hexdigest() for p,b in files.items()},'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
(root/'build-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
print(json.dumps(manifest,indent=2))
