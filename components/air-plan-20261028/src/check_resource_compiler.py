#!/usr/bin/env python3
"""Adversarial, subprocess-isolated review of the bounded resource compiler."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path

p=argparse.ArgumentParser()
for n in ('library','baseline','vertex','fragment','old-vertex','old-fragment','work'):
    p.add_argument('--'+n,type=Path,required=True)
p.add_argument('--spirv-dis',default='spirv-dis');p.add_argument('--spirv-as',default='spirv-as')
p.add_argument('--spirv-val',default='spirv-val');p.add_argument('--python',default='python3')
a=p.parse_args();a.work.mkdir(parents=True,exist_ok=False)
runner=Path(__file__).with_name('resource_compiler_case.py')
vsrc=a.vertex.read_bytes();fsrc=a.fragment.read_bytes()
expected={'vs':'40e8dca1a260e5527cb8979161b6e15493d85d0a1b993da207e87c89859a7e52',
          'fs':'b9fd4e2fc71b84aa397804bb5145ec949aebbb7bfedcff3bb0d063f15ea0f4fa'}
def sha(b):return hashlib.sha256(b).hexdigest()
def execute(v,f,mode,label,library=None,expect_refusal=False):
    vp=a.work/(label+'.vert.spv');fp=a.work/(label+'.frag.spv')
    vp.write_bytes(v);fp.write_bytes(f)
    if mode!='sentinel':
        for path in (vp,fp):
            subprocess.run([a.spirv_val,'--target-env','vulkan1.2','--uniform-buffer-standard-layout',str(path)],check=True)
    outdir=a.work/(label+'-out')
    cmd=[a.python,str(runner),'--library',str((library or a.library)),'--vertex',str(vp),
       '--fragment',str(fp),'--mode',mode,'--output-dir',str(outdir)]
    if expect_refusal:cmd.append('--expect-refusal')
    proc=subprocess.run(cmd,text=True,capture_output=True,timeout=40)
    if proc.returncode:
        raise RuntimeError(f'{label}: backend process did not return normally rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}')
    result=json.loads(proc.stdout.strip().splitlines()[-1]);result['label']=label
    if mode in ('resources','texture') and result['status']==0:
        if mode=='resources':assert result['plan_version']==1 and result['pair_version']==2
        result['output_hashes']={'vs':result.pop('vs_sha256'),'fs':result.pop('fs_sha256')}
    (a.work/(label+'.json')).write_text(json.dumps(result,indent=2)+'\n')
    return result
def dis(path,name):
    out=a.work/(name+'.spvasm')
    subprocess.run([a.spirv_dis,'--raw-id',str(path),'-o',str(out)],check=True)
    return out.read_text()
def assemble(text,name):
    src=a.work/(name+'.spvasm');out=a.work/(name+'.spv');src.write_text(text)
    subprocess.run([a.spirv_as,'--preserve-numeric-ids','--target-env','vulkan1.2',str(src),'-o',str(out)],check=True)
    return out.read_bytes()

# Exact input must pass with stable stage code and all plan slots.
base=execute(vsrc,fsrc,'resources','exact-positive')
if base['status']!=0:raise SystemExit(f'exact pair refused: {base}')
f=dis(a.fragment,'fragment-base')
mutations={}
def add(label,text):
    assert text!=f,label+' substitution did not change module'
    mutations[label]=text
add('wrong-set',f.replace('OpDecorate %7 DescriptorSet 0','OpDecorate %7 DescriptorSet 1',1))
add('wrong-binding',f.replace('OpDecorate %7 Binding 2','OpDecorate %7 Binding 9',1))
add('wrong-varying-location',f.replace('OpDecorate %22 Location 0','OpDecorate %22 Location 1',1))
add('wrong-fragment-output',f.replace('OpDecorate %19 Location 0','OpDecorate %19 Location 1',1))

# Change one buffer descriptor into StorageBuffer, preserving pointer types.
ssbo=f.replace('%6 = OpTypePointer Uniform %5','%6 = OpTypePointer Uniform %5\n%600 = OpTypePointer StorageBuffer %2\n%601 = OpTypePointer StorageBuffer %5',1)
ssbo=ssbo.replace('%7 = OpVariable %6 Uniform','%7 = OpVariable %601 StorageBuffer',1)
ssbo=ssbo.replace('OpAccessChain %85 %7','OpAccessChain %600 %7')
ssbo=ssbo.replace('; Bound: 535','; Bound: 602',1)
add('ssbo',ssbo)

# Add a valid but unreferenced set0/binding5 UBO declaration. This checks whether
# the gate accepts an extra declaration after NIR dead-variable cleanup.
extra=f.replace('OpDecorate %5 Block','OpDecorate %600 ArrayStride 4\nOpDecorate %601 Block\nOpMemberDecorate %601 0 Offset 0\nOpDecorate %603 DescriptorSet 0\nOpDecorate %603 Binding 5\nOpDecorate %5 Block',1)
extra=extra.replace('%78 = OpVariable %77 UniformConstant',
 '%78 = OpVariable %77 UniformConstant',1)
extra=extra.replace('%27 = OpConstant %2 0',
 '%27 = OpConstant %2 0\n%600 = OpTypeArray %2 %26\n%601 = OpTypeStruct %600\n%602 = OpTypePointer Uniform %601\n%603 = OpVariable %602 Uniform',1)
extra=extra.replace('; Bound: 535','; Bound: 604',1)
extra=extra.replace('OpEntryPoint Fragment %120 "main" %7 %12 %15 %19 %22 %78',
 'OpEntryPoint Fragment %120 "main" %7 %12 %15 %19 %22 %78 %603',1)
add('extra-ubo',extra)
# Statically use a fifth UBO and feed it to the output so it cannot be removed
# as an unused declaration before the strict resource-count checks.
extra_used=extra.replace('%602 = OpTypePointer Uniform %601\n%603 = OpVariable %602 Uniform',
 '%602 = OpTypePointer Uniform %601\n%604 = OpTypePointer Uniform %2\n%603 = OpVariable %602 Uniform',1)
extra_used=extra_used.replace('OpStore %19 %534',
 '%605 = OpAccessChain %604 %603 %27 %27\n'
 '%606 = OpLoad %2 %605\n'
 '%607 = OpConvertUToF %16 %606\n'
 '%608 = OpCompositeConstruct %17 %607 %607 %607 %607\n'
 '%609 = OpFAdd %17 %534 %608\n'
 '               OpStore %19 %609',1).replace('; Bound: 604','; Bound: 610',1)
add('extra-ubo-used',extra_used)

# One-element combined-sampler array. Each old variable load is replaced by a
# constant-index OpAccessChain to preserve module validity.
arr=f.replace('%78 = OpVariable %77 UniformConstant',
 '%600 = OpTypeArray %76 %26\n%601 = OpTypePointer UniformConstant %600\n%78 = OpVariable %601 UniformConstant',1)
for idx,resultid in enumerate((230,319,385,451)):
    old=f'%{resultid} = OpLoad %76 %78'
    new=f'%{602+idx} = OpAccessChain %77 %78 %27\n%{resultid} = OpLoad %76 %{602+idx}'
    assert old in arr
    arr=arr.replace(old,new,1)
arr=arr.replace('; Bound: 535','; Bound: 606',1)
add('descriptor-array',arr)

# Nonzero LOD and an added fifth sample whose value contributes to the output.
add('nonzero-lod',re.sub(r'(OpImageSampleExplicitLod[^\n]* Lod )%91\b',r'\1%87',f,count=1))
sample=f.replace('OpStore %19 %534',
 ' %600 = OpFAdd %20 %273 %88\n'
 ' %601 = OpImageSampleExplicitLod %17 %230 %600 Lod %91\n'
 ' %602 = OpFAdd %17 %534 %601\n'
 '               OpStore %19 %602',1).replace('; Bound: 535','; Bound: 603',1)
add('sample-count',sample)

results={'positive':base,'mutations':{}}
for label,asm in mutations.items():
    mutated=assemble(asm,label)
    result=execute(vsrc,mutated,'resources',label,expect_refusal=True)
    results['mutations'][label]=result
    if result['status']==0:raise SystemExit(f'{label} adversarial module was accepted')
    reasons={
      'wrong-set':'Unexpected UBO descriptor set or binding',
      'wrong-binding':'Unexpected UBO descriptor set or binding',
      'wrong-varying-location':'Varying interface mismatch',
      'wrong-fragment-output':'Unsupported graphics output',
      'ssbo':'Only read-only UBO buffers are supported',
      'extra-ubo':'Unexpected UBO descriptor set or binding',
      'extra-ubo-used':'Unexpected UBO descriptor set or binding',
      'descriptor-array':'non-array non-depth sampler2D',
      'nonzero-lod':'LOD must be constant 0.0',
      'sample-count':'More than four texture samples',
    }
    if reasons[label] not in result['error']:
        raise SystemExit(f'{label}: unexpected refusal reason {result["error"]!r}')

# New resources entrypoint must reject alias; legacy pair/pair-texture APIs must
# continue refusing this exact resource-bearing pair without crashing.
for mode,label in [('alias','output-alias'),('pair2','legacy-pair2-exact'),('texture','legacy-texture-exact')]:
    results[label]=execute(vsrc,fsrc,mode,label,expect_refusal=True)
bad_vertex=bytearray(vsrc); bad_vertex[:4]=b'BAD!'
results['sentinel-refusal']=execute(bytes(bad_vertex),fsrc,'sentinel','sentinel-refusal',expect_refusal=True)
results['plan-failure-preservation']=execute(vsrc,assemble(mutations['wrong-fragment-output'],'plan-failure-input'),
 'plan-sentinel','plan-failure-preservation',expect_refusal=True)

# Old explicitly sampled fixture: code and ABI behavior must remain byte-identical
# between the pinned old library and new resource-enabled library.
old_base=execute(a.old_vertex.read_bytes(),a.old_fragment.read_bytes(),'texture','old-texture-parity',a.baseline)
old_comp=execute(a.old_vertex.read_bytes(),a.old_fragment.read_bytes(),'texture','new-texture-parity')
if old_base['status']!=0 or old_comp['status']!=0:
    raise SystemExit(f'explicit legacy texture fixture did not compile: {old_base} {old_comp}')
parity=(old_base['output_hashes']==old_comp['output_hashes'] and
        old_base['graphics']==old_comp['graphics'] and old_base['pair']==old_comp['pair'] and
        old_base.get('texture')==old_comp.get('texture') and
        (a.work/'old-texture-parity-out/vs.mali').read_bytes()==(a.work/'new-texture-parity-out/vs.mali').read_bytes() and
        (a.work/'old-texture-parity-out/fs.mali').read_bytes()==(a.work/'new-texture-parity-out/fs.mali').read_bytes())
if not parity:raise SystemExit('explicit legacy VS/FS machine-code parity changed')
results['legacy-byte-parity']={'passed':True,'old':old_base['output_hashes'],'new':old_comp['output_hashes']}

results['positive']['expected_hashes']=expected
if results['positive']['output_hashes']!=expected:
    raise SystemExit(f'exact compositor code hash changed: {results["positive"]["output_hashes"]}')
(a.work/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
(a.work/'inputs.json').write_text(json.dumps({
 'library':str(a.library),'library_sha256':sha(a.library.read_bytes()),
 'baseline':str(a.baseline),'baseline_sha256':sha(a.baseline.read_bytes()),
 'vertex_sha256':sha(vsrc),'fragment_sha256':sha(fsrc),
 'old_vertex_sha256':sha(a.old_vertex.read_bytes()),'old_fragment_sha256':sha(a.old_fragment.read_bytes()),
 'spirv_val_flags':['--target-env','vulkan1.2','--uniform-buffer-standard-layout']
},indent=2)+'\n')
print(json.dumps({'positive':results['positive'],'mutation_statuses':{k:v['status'] for k,v in results['mutations'].items()},
 'legacy_refusals':{k:results[k]['status'] for k in ('legacy-pair2-exact','legacy-texture-exact','output-alias')},
 'legacy_byte_parity':parity,'work':str(a.work)},indent=2))

