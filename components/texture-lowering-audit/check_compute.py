import ctypes as C, sys
class Blob(C.Structure): _fields_=[('data',C.c_void_p),('size',C.c_size_t)]
lib=C.CDLL(sys.argv[1]);lib.tegu_mali_compile.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Blob),C.c_void_p,C.c_size_t]
s=open(sys.argv[2],'rb').read();b=Blob();e=C.create_string_buffer(512)
r=lib.tegu_mali_compile(s,len(s),C.byref(b),e,len(e));print(r,e.value,b.size)
