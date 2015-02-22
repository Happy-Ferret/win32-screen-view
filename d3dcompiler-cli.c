#define CINTERFACE
#define COBJMACROS

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include <d3dcompiler.h>

static inline void autofree_clb(void *pMem) {
    free(*(void**)pMem);
}
#define _autofree_ __attribute__((__cleanup__(autofree_clb)))

#define SAFE_RELEASE(a) do { if (a) (a)->lpVtbl->Release(a); } while (0)

static void print_help(void)
{
    fprintf(stderr,
            "d3dcompiler-cli -tTARGET -eENTRYPOINT [-vXX] [-O?] [-hC/HEADER/FILE] HLSL/SHADER/FILE \n"
            "Command line frontend for the d3dcompiler dll\n"
            "\n"
            "OPTIONS AND ARGUMENTS\n"
            "   -tTARGET        Compile to shader target TARGET (e.g. 'ps_4.0')\n"
            "   -eENTRYPOINT    Compile entry point ENTRYPOINT and name of the C array\n"
            "   -vXX            Use d3dcompiler_XX.dll (default: latest available one)\n"
            "   -O?             Optimization level (0-3)\n"
            "   -hHEADER/FILE   C header file to create. Default: stdout\n"
            "   -nCXX_NAMESPACE C++ Namespace for the generated header file\n"
            "   -pC_PREFIX      Prefix for names in the C header file\n"
            "   SHADER/FILE     HLSL shader file (ASCII encoding)\n"
    );
}

static char *wcsdup_to_codepage(UINT codepage, const wchar_t *utf16)
{
    char *ansi = NULL;

    int needed_buffer = WideCharToMultiByte(codepage, 0, utf16, -1, NULL, 0, NULL, NULL);
    if (!needed_buffer)
        return NULL;

    ansi = malloc(needed_buffer);
    WideCharToMultiByte(codepage, 0, utf16, -1, ansi, needed_buffer, NULL, NULL);

    return ansi;
}
static char *wcsdup_to_utf8(const wchar_t *utf16)
{
    return wcsdup_to_codepage(CP_UTF8, utf16);
}
static char *wcsdup_to_ansi(const wchar_t *utf16)
{
    return wcsdup_to_codepage(CP_ACP, utf16);
}

static wchar_t *wcsdup_printf(const wchar_t *format, ...)
{
    int size;
    wchar_t* buf;
    va_list args;

    va_start(args, format);
    size = _vscwprintf(format, args);
    if (size < 0)
        return NULL; //FIXME: shouldn't happen

    buf = calloc(size + 1, sizeof(wchar_t));
    _vswprintf(buf, format, args);

    va_end(args);

    return buf;
}

static char *strdup_strerror(HRESULT hr)
{
    wchar_t *errorText = NULL;

    FormatMessage(
        FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,    // unused with FORMAT_MESSAGE_FROM_SYSTEM
        hr,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (wchar_t*)&errorText,  // output
        0, NULL);

    if (errorText) {
        char *copied = wcsdup_to_utf8(errorText);
        LocalFree(errorText);
        return copied;
    } else
        return NULL;
}

typedef HRESULT (WINAPI *D3DCompile_t)(
    void *pSrcData,
    SIZE_T SrcDataSize,
    LPCSTR pSourceName,
    const D3D_SHADER_MACRO *pDefines,
    ID3DInclude *pInclude,
    LPCSTR pEntrypoint,
    LPCSTR pTarget,
    UINT Flags1,
    UINT Flags2,
    ID3DBlob **ppCode,
    ID3DBlob **ppErrorMsgs
);

int wmain(int argc, wchar_t **argv)
{
    wchar_t *target = NULL;
    wchar_t *entrypoint = NULL;
    wchar_t *optimize = L"1";
    wchar_t *shader_file = NULL;
    wchar_t *header_file = NULL;
    wchar_t *version = NULL;
    wchar_t *cxx_ns = NULL;
    wchar_t *c_prefix = NULL;

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2) {
        print_help();
        return -1;
    }

    // TODO: how to deal with specifying options/arguments twice?
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == L'-') {
            switch (argv[i][1]) {
                case L't':
                    target = &argv[i][2];

                    break;
                case L'e':
                    entrypoint = &argv[i][2];

                    break;
                case L'v':
                    version = &argv[i][2];

                    break;
                case L'O':
                case L'o':
                    optimize = &argv[i][2];

                    break;
                case L'h':
                    header_file = &argv[i][2];

                    break;
                case L'n':
                    cxx_ns = &argv[i][2];

                    break;
                case L'p':
                    c_prefix = &argv[i][2];

                    break;
                default:
                {
                    _autofree_ char *option_u8 = wcsdup_to_utf8(argv[i]);

                    fprintf(stderr, "ERROR: Invalid option: %s\n", option_u8);

                    print_help();
                    return -1;
                }
            }
        } else {
            shader_file = argv[i];
        }
    }

    // check parsed arguments
    UINT optimizationFlags = 0;
    if (wcslen(optimize) != 1 || optimize[0] > L'3' || optimize[0] < L'0') {
        fprintf(stderr, "ERROR: Invalid optimization level (0-3 are valid)\n");

        print_help();
        return -1;
    }

    if (optimize[0] == '0')
        optimizationFlags = D3DCOMPILE_OPTIMIZATION_LEVEL0;
    if (optimize[0] == '1')
        optimizationFlags = D3DCOMPILE_OPTIMIZATION_LEVEL1;
    if (optimize[0] == '2')
        optimizationFlags = D3DCOMPILE_OPTIMIZATION_LEVEL2;
    if (optimize[0] == '3')
        optimizationFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;

    if (!shader_file || !wcslen(shader_file)) {
        fprintf(stderr, "ERROR: HLSL shader file not given\n");

        print_help();
        return -1;
    }
    if (!entrypoint || !wcslen(entrypoint)) {
        fprintf(stderr, "ERROR: Entry point not given\n");

        print_help();
        return -1;
    }
    if (!target || !wcslen(target)) {
        fprintf(stderr, "ERROR: Target not given\n");

        print_help();
        return -1;
    }

    // load the d3dcompiler dll
    HMODULE d3dcompiler = NULL;
    _autofree_ wchar_t *d3dcompiler_dll = NULL;
    D3DCompile_t d3dcompile_func = NULL;
    if (version) {
        d3dcompiler_dll = wcsdup_printf(L"d3dcompiler_%s.dll", version);
        _autofree_ char *compiler_u8 = wcsdup_to_utf8(d3dcompiler_dll);

        d3dcompiler = LoadLibrary(d3dcompiler_dll);
        if (!d3dcompiler) {
            _autofree_ char *errorst = strdup_strerror(GetLastError());
            fprintf(stderr, "ERROR: Couldn't load `%s' : %s\n", compiler_u8, errorst);
        }
    } else {
        for (int i = 99; i > 42; --i) {
            free(d3dcompiler_dll);
            d3dcompiler_dll = wcsdup_printf(L"d3dcompiler_%d.dll", i);

            if ((d3dcompiler = LoadLibrary(d3dcompiler_dll)))
                break;
        }
    }

    if (!d3dcompiler || !(d3dcompile_func = (D3DCompile_t)GetProcAddress(d3dcompiler, "D3DCompile"))) {
        fprintf(stderr, "ERROR: No suitable d3dompiler DLL has been found :(\n");
        return -1;
    }

    // load the shader into memory
               long  shader_data_size = 0;
    _autofree_ void *shader_data      = NULL;

    FILE *shader = _wfopen(shader_file, L"rb");
    if (!shader) {
        const char *msg = strerror(errno);
        _autofree_ char *shader_file_u8 = wcsdup_to_utf8(shader_file);

        fprintf(stderr, "ERROR: Failed to open file `%s': %s\n", shader_file_u8, msg);
        return -1;
    }

    fseek(shader, 0, SEEK_END);

    shader_data_size = ftell(shader);

    fseek(shader, 0, SEEK_SET);

    shader_data = malloc(shader_data_size);
    if (!shader_data) {
        fprintf(stderr, "ERROR: Out of memory\n");
        return -1;
    }

    size_t read = fread(shader_data, 1, shader_data_size, shader);
    if (read != (size_t)shader_data_size) {
        fprintf(stderr, "ERROR: Could not read shader data completely (WTF!?) read=%llu\n", (long long unsigned)read);

        return -1;
    }

    fclose(shader);

    // Try to compile shader
    ID3D10Blob *compiled_shader = NULL;
    ID3D10Blob *error_messages  = NULL;

    {
        _autofree_ char *entrypoint_a = wcsdup_to_ansi(entrypoint);
        _autofree_ char *target_a = wcsdup_to_ansi(target);
        _autofree_ char *shader_file_a = wcsdup_to_ansi(shader_file);

        HRESULT hr = d3dcompile_func(shader_data,
                                     shader_data_size,
                                     shader_file_a,
                                     NULL,
                                     (ID3DInclude*)1,
                                     entrypoint_a,
                                     target_a,
                                     optimizationFlags,
                                     0,
                                     &compiled_shader,
                                     &error_messages);
        if (FAILED(hr)) {
            _autofree_ char *error = strdup_strerror(hr);

            fprintf(stderr, "ERROR: Compilation failed with error message: %s\n", error);
        } else {
            fprintf(stderr, "INFO: Compilation succeeded\n");
        }
    }

    if (compiled_shader) {
        size_t         compiled_size = ID3D10Blob_GetBufferSize(compiled_shader);
        unsigned char *compiled_data = ID3D10Blob_GetBufferPointer(compiled_shader);

        // open header file
        FILE *header = NULL;
        if (header_file) {
            header = _wfopen(header_file, L"wb");
            if (!header) {
                _autofree_ char *error = strerror(errno);
                _autofree_ char *header_file_u8 = wcsdup_to_utf8(header_file);

                fprintf(stderr, "ERROR: Could not open file `%s': %s\n", header_file_u8, error);
                return -1;
            }
        } else {
            header = stdout;
        }

        // write it
        _autofree_ char *commandline_u8 = wcsdup_to_utf8(GetCommandLine());
        _autofree_ char *entrypoint_u8 = wcsdup_to_utf8(entrypoint);
        _autofree_ char *d3dcompiler_dll_u8 = wcsdup_to_utf8(d3dcompiler_dll);
        _autofree_ char *c_prefix_u8 = wcsdup_to_utf8(c_prefix ? c_prefix : L"");
        _autofree_ char *cxx_ns_u8 = wcsdup_to_utf8(cxx_ns ? cxx_ns : L"");
        int cxx_nested_ns = 0;

        fprintf(header, "/* Created automatically by d3dcompiler-cli using %s */\n", d3dcompiler_dll_u8);
        fprintf(header, "/* %s */\n\n", commandline_u8);

        for (char *r = NULL, *ns = strtok_r(cxx_ns_u8, ":", &r); ns; ns = strtok_r(NULL, ":", &r)) {
            fprintf(header, "namespace %s {\n", ns);
            cxx_nested_ns += 1;
        }

        fprintf(header, "unsigned char %s%s[%llu] = {\n", c_prefix_u8, entrypoint_u8, (long long unsigned)compiled_size);

        for (size_t chunk = 0; chunk < compiled_size/8 + (compiled_size%8 ? 1 : 0); ++chunk) {
            if (chunk == 0)
                fprintf(header, "   ");
            else
                fprintf(header, ",\n   ");

            for (size_t byte = 0; byte < 8 && (chunk*8 + byte) < compiled_size; ++byte) {
                if (byte == 0)
                    fprintf(header, " 0x%02hhX", compiled_data[chunk * 8 + byte]);
                else
                    fprintf(header, ", 0x%02hhX", compiled_data[chunk * 8 + byte]);
            }
        }
        fprintf(header, "\n};\n");

        for (int i = 0; i < cxx_nested_ns; ++i) {
            fprintf(header, "}\n");
        }

        if (header != stdout)
            fclose(header);
    }

    if (error_messages) {
        // temporarily turn console to ansi
        SetConsoleOutputCP(CP_ACP);
        SetConsoleCP(CP_ACP);

        fwrite(ID3D10Blob_GetBufferPointer(error_messages), ID3D10Blob_GetBufferSize(error_messages), 1, stderr);
    }

    SAFE_RELEASE(compiled_shader);
    SAFE_RELEASE(error_messages);

    return 0;
}