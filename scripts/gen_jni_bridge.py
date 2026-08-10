import re
import os

JNI_H_PATH = "../include/jni.h"
OUT_INC_PATH = "../src/kudroid_jni_impl.inc"

def generate_bridge():
    with open(JNI_H_PATH, 'r') as f:
        content = f.read()
        
    start = content.find('struct JNINativeInterface_ {')
    end = content.find('};', start)
    struct_body = content[start:end]
    
    # We want to match: return_type (JNICALL *FuncName)(args...);
    # Regex for this: 
    # ([\w\s\*]+)\s*\(\s*JNICALL\s*\*\s*(\w+)\s*\)\s*\(([^)]*)\);
    
    pattern = re.compile(r'([\w\s\*]+)\s*\(\s*JNICALL\s*\*\s*(\w+)\s*\)\s*\(([^)]*)\);')
    
    matches = pattern.findall(struct_body)
    
    out_lines = []
    out_lines.append("// AUTO-GENERATED JNI BRIDGE IMPLEMENTATION")
    out_lines.append("// DO NOT EDIT DIRECTLY")
    out_lines.append("")
    
    # Pre-defined implementations that shouldn't be overridden by dummy
    predefined = {
        "GetVersion", "FindClass", "GetMethodID", "GetStaticMethodID",
        "NewStringUTF", "GetStringUTFChars", "ReleaseStringUTFChars", "GetStringUTFLength",
        "RegisterNatives"
    }
    
    func_names = []
    
    for ret_type, func_name, args_str in matches:
        func_names.append(func_name)
        if func_name in predefined:
            continue
            
        ret_type = ret_type.strip()
        args = [arg.strip() for arg in args_str.split(',') if arg.strip()]
        
        # Build signature
        arg_decls = []
        arg_names = []
        for i, arg in enumerate(args):
            # clean up newlines and extra spaces
            arg = ' '.join(arg.split())
            if arg == 'void':
                continue
            if arg == '...':
                arg_decls.append('...')
                arg_names.append('...')
                continue
                
            arg_decls.append(arg)
            # extract name (last token or pointer)
            # e.g., "JNIEnv *env" -> "env", "const char *name" -> "name", "va_list args" -> "args"
            parts = arg.split()
            name = parts[-1].replace('*', '')
            if not name:
                name = f"arg{i}"
            arg_names.append(name)
            
        sig = ", ".join(arg_decls)
        if not sig:
            sig = "void"
            
        out_lines.append(f"static {ret_type} JNICALL jni_{func_name}({sig}) {{")
        # log_jni
        fmt_args = []
        fmt_str = []
        for name in arg_names:
            if name != '...':
                fmt_str.append(f"{name}=%p")
                fmt_args.append(f"(void*){name}")
                
        if fmt_str:
            out_lines.append(f'    log_jni("[AUTO] {func_name}(" "{ ", ".join(fmt_str)} "")", {", ".join(fmt_args)});')
        else:
            out_lines.append(f'    log_jni("[AUTO] {func_name}()");')
            
        # return dummy
        if ret_type == "void":
            out_lines.append("    return;")
        elif ret_type.endswith("*") or ret_type in ["jobject", "jclass", "jstring", "jarray", "jthrowable", "jmethodID", "jfieldID"]:
            out_lines.append("    return nullptr;")
        elif ret_type == "jboolean":
            out_lines.append("    return JNI_FALSE;")
        else:
            out_lines.append("    return 0;")
        out_lines.append("}")
        out_lines.append("")
        
    # Generate init function
    out_lines.append("static void init_generated_jni_interface(JNINativeInterface_* iface) {")
    for func_name in func_names:
        out_lines.append(f"    iface->{func_name} = jni_{func_name};")
    out_lines.append("}")
    
    with open(OUT_INC_PATH, 'w') as f:
        f.write("\n".join(out_lines))
        
    print(f"Generated {len(func_names)} JNI functions to {OUT_INC_PATH}")

if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    generate_bridge()
