"""Run the upstream VoxCPM LoRA trainer without changing the audiobook install."""
from pathlib import Path
import argparse,os,sys,runpy,json
HERE=Path(__file__).resolve().parent
args=argparse.ArgumentParser();args.add_argument('--speaker',choices=['xardas','hero'],default='xardas')
options=args.parse_args()
prefix='voice-hero' if options.speaker=='hero' else 'voice'
manifest=HERE/(prefix+'-data')
checkpoints=HERE/(prefix+'-checkpoints')
if options.speaker=='hero' and (checkpoints/'latest/lora_weights.safetensors').exists():
    raise RuntimeError('Existing trained hero adapter is preserved')
sys.path.insert(0,str(HERE/'training-deps'))
sys.path.insert(0,str(HERE/'VoxCPM/src'))
W=Path(os.environ.get('VOXCPM_HOME', str(HERE / 'voxcpm')))  # lokalny VoxCPM2: models/, hf-cache/, venv/
os.environ['HF_HUB_OFFLINE']='1'
os.environ['HF_HOME']=str(W/'hf-cache')
os.environ['TOKENIZERS_PARALLELISM']='false'
import torch
torch.set_num_threads(4)
torch.set_float32_matmul_precision('high')
lib=Path(torch.__file__).parent/'lib'
os.add_dll_directory(str(lib));os.environ['PATH']=str(lib)+os.pathsep+os.environ['PATH']
config={
 'pretrained_path':str(W/'models/voxcpm2'),
 'train_manifest':str(manifest/'train.jsonl'),
 'val_manifest':str(manifest/'validation.jsonl'),
 'sample_rate':16000,'out_sample_rate':48000,'batch_size':1,'grad_accum_steps':2,
 'num_workers':0,'preprocessing_num_workers':1,'num_iters':120,'max_steps':120,
 'log_interval':5,'valid_interval':40,'save_interval':40,'learning_rate':0.00008,
 'weight_decay':0.01,'warmup_steps':12,'max_batch_tokens':1800,'max_grad_norm':1.0,
 'save_path':str(checkpoints),'tensorboard':str(HERE/(prefix+'-tensorboard')),
 'lambdas':{'loss/diff':1.0,'loss/stop':1.0},
 'lora':{'enable_lm':True,'enable_dit':True,'enable_proj':False,'r':32,'alpha':32,'dropout':0.0}}
import yaml
(HERE/(prefix+'-train.yaml')).write_text(yaml.safe_dump(config,sort_keys=False),encoding='utf8')
sys.argv=['train_voxcpm_finetune.py','--config_path',str(HERE/(prefix+'-train.yaml'))]
if __name__=='__main__':
    print(json.dumps({'torch':torch.__version__,'cuda':torch.version.cuda,'device':torch.cuda.get_device_name(0),'config':config}),flush=True)
    runpy.run_path(str(HERE/'VoxCPM/scripts/train_voxcpm_finetune.py'),run_name='__main__')
