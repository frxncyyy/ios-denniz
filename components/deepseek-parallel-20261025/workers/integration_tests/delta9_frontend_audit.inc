/* Integration-owned DELTA9 extracted-selector and metallib-function audit.
 * Included after delta9_selectors.inc by native_compute_frontend_test.py.
 * Test-only code; no production edits. */

static unsigned extracted_checks, extracted_missing, extracted_unreachable;
static unsigned metallib_total, metallib_lookup_nil, metallib_specialized,
                metallib_specialized_nil;

static void audit_extracted_selectors(void)
{
    for (unsigned i = 0; i < delta9_selector_count; i++) {
        const struct delta9_selector_case *c = &delta9_selectors[i];
        if (c->unreachable) {
            extracted_unreachable++;
            continue;
        }
        Class cls = c->cls ? objc_getClass(c->cls) : 0;
        SEL sel = sel_registerName(c->sel);
        int ok = cls && [(id)cls instancesRespondToSelector:sel];
        extracted_checks++;
        if (!ok) {
            fprintf(stderr, "SELECTOR_MISSING class=%s selector=%s source=extracted\n",
                    c->cls ? c->cls : "?", c->sel ? c->sel : "?");
            extracted_missing++;
        }
    }
    fprintf(stderr, "EXTRACTED_SELECTOR checks=%u missing=%u unreachable=%u\n",
            extracted_checks, extracted_missing, extracted_unreachable);
}

static void audit_all_metallib_functions(TeguComputeDevice *device, const char *path)
{
    id error = 0;
    TeguLibrary *library = [device newLibraryWithFile:
        [NSString stringWithUTF8String:path] error:&error];
    if (!library || error) {
        fprintf(stderr, "METAL_FUNCTIONS library=missing\n");
        metallib_total++;
        return;
    }
    TeguAIRLibrary *air = library->library;
    size_t total = tegu_air_function_count(air);
    metallib_total = (unsigned)total;
    for (size_t i = 0; i < total; i++) {
        char name[4096];
        uint32_t kind = 0;
        size_t required = tegu_air_function_info(air, i, name, sizeof(name), &kind);
        if (!required || required > sizeof(name) || !kind) {
            fprintf(stderr, "METAL_FUNCTION_INVALID index=%zu\n", i);
            metallib_lookup_nil++;
            continue;
        }
        TeguFunction *function = [library newFunctionWithName:
            [NSString stringWithUTF8String:name]];
        if (!function) {
            fprintf(stderr, "METAL_FUNCTION_LOOKUP_NIL name=%s\n", name);
            metallib_lookup_nil++;
            continue;
        }
        [function release];
        char message[512] = {0};
        TeguAIRLibrary *specialized = tegu_air_library_specialize(
            air, (const uint8_t *)name, strlen(name), 0, 0,
            message, sizeof(message));
        if (!specialized) {
            fprintf(stderr, "METAL_FUNCTION_NIL name=%s reason=%s\n",
                    name, message[0] ? message : "no-default-constants");
            metallib_specialized_nil++;
            continue;
        }
        tegu_air_library_close(specialized);
        metallib_specialized++;
    }
    [library release];
    fprintf(stderr,
            "METAL_FUNCTIONS total=%u lookup_nil=%u specialized=%u nil=%u\n",
            metallib_total, metallib_lookup_nil, metallib_specialized,
            metallib_specialized_nil);
}
