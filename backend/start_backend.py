"""One-click hidden backend startup, with local model identity and voice preflight."""
from pathlib import Path
import argparse,csv,hashlib,json,os,socket,subprocess,sys,time,urllib.error,urllib.request
HERE=Path(__file__).resolve().parent
PYTHON=Path(os.environ.get('VOXCPM_PYTHON', sys.executable))  # Python z zainstalowanym VoxCPM2
GAME=Path(os.environ.get('XARDAS_GAME_DIR', r'C:\Games\Gothic II'))  # folder Gothica II z Union
ROOT=GAME/'System/NBXardasAI'
MODEL=HERE/'models/Nemotron-3.5-Bolt-E4B-Q4_K_M.gguf'
MODEL_SHA256='c8af2b8aea10a690a1d9491db744a9446660173cb38fc489612b556615a69612'
RUNNER=HERE/'runner/llama-server.exe'

def verify_model():
    digest=hashlib.sha256()
    with MODEL.open('rb') as source:
        for block in iter(lambda:source.read(4*1024*1024),b''):digest.update(block)
    if digest.hexdigest()!=MODEL_SHA256:raise RuntimeError('Model file hash mismatch')
    if not RUNNER.is_file():raise RuntimeError('Local model runner is missing')

def port_available():
    """A successful exclusive bind proves no conflicting IPv4 listener now."""
    with socket.socket(socket.AF_INET,socket.SOCK_STREAM) as probe:
        if hasattr(socket,'SO_EXCLUSIVEADDRUSE'):
            probe.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
        try:probe.bind(('127.0.0.1',18081))
        except OSError:return False
        return True

def model_ready(*,starting=False):
    try:
        with urllib.request.urlopen('http://127.0.0.1:18081/props',timeout=2) as r:p=json.load(r)
        if Path(p.get('model_path','')).resolve()!=MODEL.resolve() or p.get('model_alias')!='nemotron-bolt':
            raise RuntimeError('Port 18081 belongs to another model; it will not be stopped')
        return True
    except urllib.error.HTTPError as error:
        if starting and error.code==503:return False
        raise RuntimeError(f'Model endpoint returned HTTP {error.code}; no process was stopped') from error
    except urllib.error.URLError as error:
        if isinstance(error.reason,(ConnectionRefusedError,TimeoutError)) and port_available():return False
        if starting and isinstance(error.reason,(ConnectionRefusedError,TimeoutError)):return False
        raise RuntimeError('Model endpoint could not be verified; no process was stopped') from error

def start_model():
    if not model_ready():
        logs=HERE/'logs';logs.mkdir(exist_ok=True)
        args=[str(RUNNER),'-m',str(MODEL),'--alias','nemotron-bolt','--host','127.0.0.1',
              '--port','18081','-c','4096','-ngl','999','--jinja','--skip-chat-parsing',
              '--reasoning','off','--reasoning-budget','0','--parallel','1',
              '--cache-type-k','q8_0','--cache-type-v','q8_0','--no-webui','--verbose']
        with (logs/'llama-stdout.log').open('ab') as stdout,(logs/'llama-stderr.log').open('ab') as stderr:
            child=subprocess.Popen(args,cwd=RUNNER.parent,stdout=stdout,stderr=stderr,creationflags=subprocess.CREATE_NO_WINDOW)
        (HERE/'runner.pid').write_text(str(child.pid),encoding='ascii')
        for _ in range(60):
            if child.poll() is not None:raise RuntimeError('Local model exited; see backend/logs/llama-stderr.log')
            if model_ready(starting=True):return
            time.sleep(1)
        raise RuntimeError('Local model did not become ready; see backend/logs/llama-stderr.log')

def worker_alive(pid):
    if type(pid) is not int or pid <= 0:return False
    listing=subprocess.run(['tasklist','/FI',f'PID eq {pid}','/FO','CSV','/NH'],
        capture_output=True,text=True,encoding='ascii',errors='replace',creationflags=subprocess.CREATE_NO_WINDOW,check=True)
    return any(len(row)>1 and row[0].lower()=='python.exe' and row[1]==str(pid)
               for row in csv.reader(listing.stdout.splitlines()))

def current_worker():
    try:
        row=json.loads((ROOT/'worker_ready.json').read_text(encoding='utf8'))
        return row if row.get('ready') and 0<=time.time()-row['heartbeat_utc']<8 and worker_alive(row.get('pid')) else None
    except (OSError,ValueError,KeyError):return None

def worker(mode):
    import msvcrt
    ROOT.mkdir(parents=True,exist_ok=True)
    lock=(ROOT/'worker.lock').open('a+b');lock.seek(0)
    if lock.read(1)==b'':lock.write(b'0');lock.flush()
    lock.seek(0)
    try:msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
    except OSError:raise SystemExit('Another Xardas worker already holds the lock')
    from bridge import Bridge,LocalVoice,atomic_json,CAPABILITIES
    voice=LocalVoice(ROOT/'audio',HERE/'voice-checkpoints/latest')
    player_voice=LocalVoice(ROOT/'audio',HERE/'voice-hero-checkpoints/latest',manifest=HERE/'voice-hero-data/train.jsonl',shared=voice,speaker='hero')
    bridge=Bridge(ROOT,voice=voice,player_voice=player_voice,mode=mode)
    voice.load()
    player_voice.load()
    report=[]
    for scene in ['chat']:
        row=bridge.answer({'protocol_version':1,'id':'preflight_'+scene,'session_id':'offline_preflight',
                 'scene':scene,'player_level':1,'npc_instance':'NONE_100_XARDAS',
                 **({'player_text':'Witaj. Czy mogę z tobą porozmawiać o czymkolwiek?'} if scene=='chat' else {})})
        # These actions are only inspected here; no responses are put in the
        # game's queue, so preflight cannot transfer an item or kill the hero.
        report.append(row)
        print('PREFLIGHT '+scene+': '+row['text'],flush=True)
    import scroll_policy
    document=ROOT/'scroll-demo.txt'
    if document.is_file() and document.stat().st_size<=2048:
        payload=document.read_bytes()
        if hashlib.sha256(payload).hexdigest()==scroll_policy.SHA256:
            # Warm the exact two voices and the stream cache offline. No
            # response files/actions are ever delivered to the game here.
            bridge.npc_ready=lambda row:None
            try:
                row=bridge.answer(dict(protocol_version=1,id='preflight_scroll',session_id='offline_scroll',
                    scene='chat',player_level=1,npc_instance='NONE_100_XARDAS',player_text=scroll_policy.PROMPT,
                    scroll_document=dict(source='prepared_demo_scroll',text=payload.decode('utf8'),sha256=scroll_policy.SHA256)))
                report.append(row)
                row=bridge.answer(dict(protocol_version=1,id='preflight_scroll_alt',session_id='offline_scroll_alt',
                    scene='chat',player_level=1,npc_instance='NONE_100_XARDAS',player_text=scroll_policy.ALT_PROMPT,
                    scroll_document=dict(source='prepared_demo_scroll',text=payload.decode('utf8'),sha256=scroll_policy.SHA256)))
                report.append(row)
            finally:bridge.npc_ready=None
    atomic_json(HERE/'logs/preflight.json',{'ready':True,'pid':os.getpid(),'mode':mode,'results':report,
        'voice_adapter_sha256':voice.voice_hash,'player_voice_adapter_sha256':player_voice.voice_hash,'note':'Offline validation, action envelopes were not delivered to the game.'})
    atomic_json(ROOT/'worker_ready.json',{'protocol_version':1,'pid':os.getpid(),'ready':True,'mode':mode,'capabilities':CAPABILITIES,'heartbeat_utc':time.time()})
    (HERE/'backend.pid').write_text(str(os.getpid()),encoding='ascii')
    bridge.run()

def main():
    p=argparse.ArgumentParser();p.add_argument('--worker',action='store_true');p.add_argument('--mode',choices=['staged_vulnerable','live_model','secure'],default='live_model');a=p.parse_args()
    if a.worker:return worker(a.mode)
    verify_model()
    start_model()
    ready=current_worker()
    if ready:
        if ready['mode']!=a.mode:raise RuntimeError('Existing worker uses another mode; stop that worker explicitly first')
        if not {'free_text_chat','session_chat_history','native_json_plans','dual_trained_voices','directed_tower_emergency','directed_admin_punishment','directed_scroll_injection','scroll_tool_lab_v2','early_player_audio','npc_stream_audio'}<=set(ready.get('capabilities',[])):
            from worker_update import restart_outdated_worker
            restart_outdated_worker(ready,ROOT,PYTHON,Path(__file__).resolve())
        else:
            print(json.dumps(ready));return
    if not PYTHON.is_file():raise RuntimeError('Existing local voice Python is missing')
    (HERE/'logs').mkdir(exist_ok=True)
    with (HERE/'logs/backend-worker.log').open('a',encoding='utf8') as log:
        child=subprocess.Popen([str(PYTHON),'-X','utf8','-u',str(Path(__file__).resolve()),'--worker','--mode',a.mode],
                cwd=HERE,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    for _ in range(240):
        ready=current_worker()
        if ready:print(json.dumps(ready));return
        if child.poll() is not None:raise RuntimeError('Backend preflight failed; see logs/backend-worker.log')
        time.sleep(1)
    raise RuntimeError('Voice preflight is still running; see logs/backend-worker.log')

if __name__=='__main__':main()
