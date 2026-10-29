"""Private Xardas dataset from the user's installed original Polish speech.

No game recordings are included in the redistributable bridge source package.
Speaker 14 is verified by the original Xardas response IDs (hero is 15).
"""
from pathlib import Path
import json, os, struct, subprocess, wave, hashlib, random

HERE = Path(__file__).resolve().parent
WORK = HERE.parents[1]
GAME = Path(os.environ.get('XARDAS_GAME_DIR', r'C:\Games\Gothic II'))  # folder Gothica II z Union
OUT = HERE / 'voice-data'

def catalog(path):
    with path.open('rb') as f:
        f.seek(272)
        count, _, _, _, offset, _ = struct.unpack('<6I', f.read(24))
        f.seek(offset or 296)
        entries = [struct.unpack('<64s4I', f.read(80)) for _ in range(count)]
    return {name.split(b'\0')[0].rstrip().decode('ascii',errors='replace').upper(): (addr, size)
            for name, addr, size, flags, _ in entries if not flags & 0x80000000}

def main():
    OUT.mkdir(exist_ok=True)
    rows = json.loads((WORK/'dialogue-audit/dialogues.json').read_text(encoding='utf-8'))
    rows = {r['file']:r['text'] for r in rows if r['file'].startswith('DIA_XARDAS_')
            and '_14_' in r['file'] and len(r['text'].strip()) > 18}
    records = []
    seen = set()
    for archive in ('Speech1.vdf', 'Speech2.vdf', 'Speech_Addon.vdf'):
        source = GAME/'Data'/archive
        for filename, (offset, size) in catalog(source).items():
            if filename not in rows or filename in seen:
                continue
            seen.add(filename)
            text = rows[filename]
            if len(text) > 230:
                continue
            raw = OUT/(filename+'.source')
            with source.open('rb') as f:
                f.seek(offset)
                raw.write_bytes(f.read(size))
            audio = OUT/(Path(filename).stem+'.wav')
            subprocess.run(['ffmpeg','-v','error','-y','-i',str(raw),
                            '-af','silenceremove=start_periods=1:start_threshold=-46dB:stop_periods=-1:stop_threshold=-46dB:stop_duration=0.35',
                            '-ar','16000','-ac','1','-c:a','pcm_s16le',str(audio)],check=True)
            raw.unlink()
            with wave.open(str(audio),'rb') as wav:
                duration=wav.getnframes()/wav.getframerate()
            if 3 <= duration <= 14:
                records.append({'audio':str(audio),'text':text,'duration':duration,
                                'source_archive':archive,'original_name':filename,
                                'sha256':hashlib.sha256(audio.read_bytes()).hexdigest()})
    random.Random(7914).shuffle(records)
    heldout = records[:max(6,len(records)//10)]
    train = records[len(heldout):]
    for i,r in enumerate(train):
        if i%2==0:
            ref=train[(i+7)%len(train)]
            r['ref_audio']=ref['audio'];r['ref_duration']=ref['duration']
    for name,values in [('train',train),('validation',heldout)]:
        (OUT/(name+'.jsonl')).write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in values),encoding='utf-8')
    summary={'speaker':'Xardas Polish original voice 14','train_clips':len(train),'heldout_clips':len(heldout),
             'train_seconds':sum(r['duration'] for r in train),'heldout_seconds':sum(r['duration'] for r in heldout),
             'provenance':'Only original Polish Speech1/2/Addon archives in user installation; private adaptation, no redistribution.'}
    (OUT/'summary.json').write_text(json.dumps(summary,indent=2,ensure_ascii=False),encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=False),flush=True)

if __name__=='__main__':main()
