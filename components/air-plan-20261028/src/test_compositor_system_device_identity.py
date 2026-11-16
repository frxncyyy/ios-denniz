#!/usr/bin/env python3
"""ASan/UBSan tests for the public probe's actual system-device identity helper."""
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
HEADER = HERE / 'compositor_system_device_identity.h'

HARNESS = r'''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "compositor_system_device_identity.h"
static int mode, releases, calls;
static int device, second;
static int expected_class, other_class, metal_class, bad_super;
static void *object_class(void *o) { (void)o; return mode==1 ? &other_class : &expected_class; }
static void *superclass(void *c) { (void)c; return mode==2 ? &bad_super : &metal_class; }
static uint64_t registry(void *o) { (void)o; return mode==3 ? 0 : 0x1234; }
static void *create(void) { calls++; return mode==4 ? &second : &device; }
static void release(void *o) { assert(o==&device || o==&second); releases++; }
static void run(int m, int expected) {
    mode=m; releases=0; calls=0;
    struct tegu_system_device_identity result;
    struct tegu_system_device_identity_ops ops={&expected_class,&metal_class,
        object_class,superclass,registry,create,release};
    int rc=tegu_verify_system_device_identity(&device,&ops,&result);
    assert((rc==0)==expected);
    assert(calls==1 && releases==1);
    assert(result.class_match==(m!=1));
    assert(result.superclass_match==(m!=1&&m!=2));
    assert(result.registry_id_nonzero==(m!=3));
    assert(result.repeated_discovery_same_object==(m!=4));
}
int main(void) {
    run(0,1); run(1,0); run(2,0); run(3,0); run(4,0);
    struct tegu_system_device_identity result;
    struct tegu_system_device_identity_ops ops={0};
    assert(tegu_verify_system_device_identity(NULL,&ops,&result)!=0);
    puts("PASS identity helper: class, superclass, registry id, repeated discovery, release");
    return 0;
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='tegu-system-identity-') as temp:
        temp = Path(temp)
        source = temp / 'test.c'
        binary = temp / 'test'
        source.write_text(HARNESS)
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I', str(HERE), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

if __name__ == '__main__':
    main()
