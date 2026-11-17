struct S { char padding[72]; unsigned value; };
unsigned acquire(struct S *p) { return __atomic_load_n(&p->value, __ATOMIC_ACQUIRE); }
void release(struct S *p, unsigned v) { __atomic_store_n(&p->value, v, __ATOMIC_RELEASE); }
