"""T5/T6 source-level checks; never reports source steps as Ripes --iret.
Example: python tests/check_stage4.py solver_assembly.s --oracle bfs.exe
"""
import argparse,json,subprocess,tempfile,time
from pathlib import Path
from check_embedded_asm import expand,load,run
CASES=[(0,'21345671111111',11),(1,'12345671111111',0),(2,'25314672313211',1),(3,'12345672121232',11)]
NAMES=['R','R2',"R'",'B','B2',"B'",'D','D2',"D'"]
SOURCE=[[1,4,2,0,3,5,6],[0,1,2,4,5,6,3],[0,2,5,3,1,4,6]]
TWIST=[[1,2,0,2,1,0,0],[0,0,0,1,2,1,2],[0]*7]
def replay(text,path):
    p=[int(x)-1 for x in text[:7]];o=[int(x)-1 for x in text[7:]]
    for move in path:
        assert 0<=move<9
        for _ in range(move%3+1):
            f=move//3
            p,o=[p[SOURCE[f][i]] for i in range(7)],[(o[SOURCE[f][i]]+TWIST[f][i])%3 for i in range(7)]
    return ''.join(str(x+1) for x in p+o)
def main():
    ap=argparse.ArgumentParser();ap.add_argument('assembly');ap.add_argument('--oracle',required=True);ap.add_argument('--output');a=ap.parse_args()
    source=Path(a.assembly).read_text(encoding='utf-8');rows=[]
    with tempfile.TemporaryDirectory() as tmp:
        for case,text,distance in CASES:
            target=Path(tmp)/'selected.s';target.write_text(expand(source,0,case));parsed=load(target)
            at=parsed[2]['target_input'];assert bytes(parsed[3][at:at+15])==text.encode()+b'\0'
            oracle=subprocess.run([a.oracle,text],capture_output=True,text=True,check=True).stdout.split()
            assert len(oracle)==distance and replay(text,[NAMES.index(x) for x in oracle])=='12345671111111'
            start=time.perf_counter();status,length,path,steps=run(parsed,text)
            assert status==0 and length==distance and len(path)==length
            assert replay(text,path)=='12345671111111'
            row=dict(test_case=case,input=text,length=length,moves=[NAMES[x] for x in path],replay='12345671111111',exact_bfs_distance=len(oracle),source_steps=steps,source_test_wall_seconds=round(time.perf_counter()-start,3))
            rows.append(row);print(json.dumps(row),flush=True)
    if a.output:Path(a.output).write_text(json.dumps({'validation':'source interpreter + native C BFS; NOT final ELF or Ripes measurement','cases':rows},indent=2)+'\n')
if __name__=='__main__':main()
