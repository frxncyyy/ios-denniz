#!/usr/bin/env python3
"""Check catalog reproducibility, identity refusal, keys, and C consumption."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

import generate_quartzcore_catalog as catalog_api


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode()


def sha(value):
    return hashlib.sha256(value).hexdigest()


def run_generator(args, *extra, capture=False):
    command = [
        'python3', str(args.generator), '--bridge', str(args.bridge),
        '--translator', str(args.translator), '--donor', str(args.donor),
        '--profile', str(args.profile), *map(str, extra)]
    return subprocess.run(command, check=not capture, capture_output=capture, text=True)


def check_metadata_gate(args, catalog):
    lib = C.CDLL(str(args.bridge.resolve()))
    catalog_api.configure(lib)
    lib.tegu_air_render_pipeline_catalog_metadata_check.argtypes = [
        C.c_void_p, C.c_char_p, C.c_size_t, C.c_void_p, C.c_size_t]
    lib.tegu_air_render_pipeline_catalog_metadata_check.restype = C.c_int32
    donor, profile = args.donor.read_bytes(), args.profile.read_bytes()
    error = C.create_string_buffer(2048)
    library = lib.tegu_air_library_open(donor, len(donor), error, len(error))
    assert library, error.value
    try:
        selected = [entry for entry in catalog['entries']
                    if entry['fragment'] == 'narrow_blur_23_frag_lph']
        assert len(selected) == 2
        for entry in selected:
            descriptor = catalog['descriptor_classes'][entry['descriptor_class']]
            active = [a for a in descriptor['attachments'] if a['format']]
            attachments = (catalog_api.Attachment * len(active))(
                *[catalog_api.Attachment(a['format'], a['write_mask']) for a in active])
            attributes = (catalog_api.Attribute * len(descriptor['attributes']))(
                *[catalog_api.Attribute(a['index'], a['format'], a['offset'], a['buffer_index'])
                  for a in descriptor['attributes']])
            layouts = (catalog_api.Layout * len(descriptor['layouts']))(
                *[catalog_api.Layout(x['index'], x['stride'], x['step_function'], x['step_rate'])
                  for x in descriptor['layouts']])
            outputs = [catalog_api.Blob(), catalog_api.Blob(), catalog_api.Blob()]
            pipeline = lib.tegu_air_render_pipeline_create_with_attachments(
                library, entry['vertex'].encode(), len(entry['vertex']),
                library, entry['fragment'].encode(), len(entry['fragment']),
                attachments, len(active), attributes, len(attributes), layouts, len(layouts),
                profile, len(profile), C.byref(outputs[0]), C.byref(outputs[1]), C.byref(outputs[2]),
                error, len(error))
            assert pipeline, error.value
            try:
                identity = entry['metadata_sha256'].encode()
                error.value = b''
                assert lib.tegu_air_render_pipeline_catalog_metadata_check(
                    pipeline, identity, len(identity), error, len(error)) == 0, error.value
                cross = next(row['metadata_sha256'].encode() for row in selected
                             if row['descriptor_class'] != entry['descriptor_class'])
                assert lib.tegu_air_render_pipeline_catalog_metadata_check(
                    pipeline, cross, len(cross), error, len(error)) == -1
                wrong = ('0' * 64).encode()
                assert lib.tegu_air_render_pipeline_catalog_metadata_check(
                    pipeline, wrong, len(wrong), error, len(error)) == -1
            finally:
                lib.tegu_air_render_pipeline_close(pipeline)
                for output in outputs:
                    lib.tegu_air_blob_free(C.byref(output))
    finally:
        lib.tegu_air_library_close(library)


def main():
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge', type=Path, required=True)
    parser.add_argument('--translator', type=Path, required=True)
    parser.add_argument('--donor', type=Path, required=True)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--generator', type=Path, default=here / 'generate_quartzcore_catalog.py')
    parser.add_argument('--recipes', type=Path, default=here / 'quartzcore_catalog_recipes.json')
    parser.add_argument('--catalog', type=Path, default=here / 'quartzcore_pipeline_catalog.json')
    parser.add_argument('--header', type=Path, default=here / 'quartzcore_pipeline_catalog.h')
    args = parser.parse_args()

    run_generator(args, '--recipes', args.recipes, '--out-json', args.catalog,
                  '--out-header', args.header, '--check')
    catalog = json.loads(args.catalog.read_text())
    assert catalog['schema'] == 1 and len(catalog['entries']) == 13
    assert len(catalog['contracts']) == 5
    assert catalog['function_inventory']['counts'] == {'vertex': 39, 'fragment': 95, 'kernel': 7}
    catalog_without_hash = dict(catalog)
    claimed = catalog_without_hash.pop('catalog_sha256')
    assert sha(canonical(catalog_without_hash)) == claimed
    assert len({entry['key_sha256'] for entry in catalog['entries']}) == len(catalog['entries'])
    identities = {key: catalog['inputs'][key] for key in ('donor_sha256', 'profile_sha256', 'translator_commit')}
    for contract_sha, contract in catalog['contracts'].items():
        assert sha(canonical(contract)) == contract_sha
    for entry in catalog['entries']:
        assert entry['metadata_sha256'] in catalog['contracts']
        key = {
            'schema': 1,
            'inputs': identities,
            'vertex': entry['vertex'],
            'fragment': entry['fragment'],
            'descriptor': catalog['descriptor_classes'][entry['descriptor_class']],
            'metadata_sha256': entry['metadata_sha256'],
        }
        assert sha(canonical(key)) == entry['key_sha256']
    check_metadata_gate(args, catalog)

    with tempfile.TemporaryDirectory(prefix='tegu-qc-catalog-') as directory:
        directory = Path(directory)
        smoke = directory / 'smoke.c'
        smoke.write_text('''
#include <assert.h>
#include <string.h>
#include "quartzcore_pipeline_catalog.h"
int main(void) {
  assert(TEGU_QC_CATALOG_DESCRIPTOR_COUNT==2 && TEGU_QC_CATALOG_ENTRY_COUNT==13);
  assert(!strcmp(tegu_qc_catalog_donor_sha256,"11c74cc42ce5137b73d44d427f75764f886656d4ddb374422d56b609ed2762d9"));
  assert(tegu_qc_catalog_descriptors[0].attachments[0].format==80);
  assert(tegu_qc_catalog_descriptors[0].attachments[1].write_mask==0);
  assert(tegu_qc_catalog_descriptors[0].attribute_count==4);
  assert(tegu_qc_catalog_descriptors[1].attachments[0].format==115);
  struct tegu_qc_catalog_descriptor exact=tegu_qc_catalog_descriptors[1];
  const struct tegu_qc_catalog_entry *entry=tegu_qc_catalog_find("downsample_blur_vert_lph","narrow_blur_23_frag_lph",&exact);
  assert(entry && !strcmp(entry->descriptor_class,"blur_mrt_rgba16float_lph_v1") && entry->metadata_bytes>0);
  exact.attachments[1].write_mask=15;
  assert(!tegu_qc_catalog_find("downsample_blur_vert_lph","narrow_blur_23_frag_lph",&exact));
  assert(!tegu_qc_catalog_find("wrong_vert","narrow_blur_23_frag_lph",&tegu_qc_catalog_descriptors[1]));
  return 0;
}
''')
        executable = directory / 'smoke'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(args.header.parent), str(smoke), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

        recipes = json.loads(args.recipes.read_text())
        recipes['inputs']['donor_sha256'] = '0' * 64
        bad_identity = directory / 'bad-identity.json'
        bad_identity.write_text(json.dumps(recipes))
        result = run_generator(args, '--recipes', bad_identity, '--out-json', directory / 'bad.json',
                               '--out-header', directory / 'bad.h', capture=True)
        assert result.returncode == 2 and 'donor_sha256 differs from recipe' in result.stderr

        recipes = json.loads(args.recipes.read_text())
        recipes['descriptor_classes']['blur_mrt_bgra_lph_v1']['attachments'][1]['write_mask'] = 15
        bad_descriptor = directory / 'bad-descriptor.json'
        bad_descriptor.write_text(json.dumps(recipes))
        for name in {entry['contract'] for entry in recipes['entries']}:
            (directory / name).write_bytes(args.recipes.with_name(name).read_bytes())
        result = run_generator(args, '--recipes', bad_descriptor, '--out-json', directory / 'bad2.json',
                               '--out-header', directory / 'bad2.h', capture=True)
        assert result.returncode == 2 and 'metadata differs from exact contract' in result.stderr

    print(f'PASS exact QuartzCore catalog: entries=13 contracts=5 catalog={claimed}; identity, name and descriptor mutations refused; C header consumed')


if __name__ == '__main__':
    main()
