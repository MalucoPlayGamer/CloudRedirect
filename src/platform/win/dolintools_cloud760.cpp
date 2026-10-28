// Keep Steam Cloud management inside the DolinTools worker process.
#define main Cloud760_Main
#include "cloud760_tool.cpp"
#undef main
extern "C" __declspec(dllexport) int CloudRedirect_Cloud760Main(int argc, char** argv) {
    int code = Cloud760_Main(argc, argv);
    fflush(nullptr);
    return code;
}
