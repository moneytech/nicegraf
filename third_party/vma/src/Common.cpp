#include "Common.h"

#ifdef _WIN32

void ReadFile(std::vector<char>& out, const char* fileName)
{
    std::ifstream file(fileName, std::ios::ate | std::ios::binary);
    assert(file.is_open());
    size_t fileSize = (size_t)file.tellg();
    if(fileSize > 0)
    {
        out.resize(fileSize);
        file.seekg(0);
        file.read(out.data(), fileSize);
    }
    else
        out.clear();
}

void SetConsoleColor(CONSOLE_COLOR color)
{
    WORD attr = 0;
    switch(color)
    {
    case CONSOLE_COLOR::INFO:
        attr = FOREGROUND_INTENSITY;;
        break;
    case CONSOLE_COLOR::NORMAL:
        attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        break;
    case CONSOLE_COLOR::WARNING:
        attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        break;
    case CONSOLE_COLOR::ERROR_:
        attr = FOREGROUND_RED | FOREGROUND_INTENSITY;
        break;
    default:
        assert(0);
    }

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(out, attr);
}

void PrintMessage(CONSOLE_COLOR color, const char* msg)
{
    if(color != CONSOLE_COLOR::NORMAL)
        SetConsoleColor(color);
    
    printf("%s\n", msg);
    
    if (color != CONSOLE_COLOR::NORMAL)
        SetConsoleColor(CONSOLE_COLOR::NORMAL);
}

void PrintMessage(CONSOLE_COLOR color, const wchar_t* msg)
{
    if(color != CONSOLE_COLOR::NORMAL)
        SetConsoleColor(color);
    
    wprintf(L"%s\n", msg);
    
    if (color != CONSOLE_COLOR::NORMAL)
        SetConsoleColor(CONSOLE_COLOR::NORMAL);
}

static const size_t CONSOLE_SMALL_BUF_SIZE = 256;

void PrintMessageV(CONSOLE_COLOR color, const char* format, va_list argList)
{
	size_t dstLen = (size_t)::_vscprintf(format, argList);
	if(dstLen)
	{
		bool useSmallBuf = dstLen < CONSOLE_SMALL_BUF_SIZE;
		char smallBuf[CONSOLE_SMALL_BUF_SIZE];
		std::vector<char> bigBuf(useSmallBuf ? 0 : dstLen + 1);
		char* bufPtr = useSmallBuf ? smallBuf : bigBuf.data();
		::vsprintf_s(bufPtr, dstLen + 1, format, argList);
		PrintMessage(color, bufPtr);
	}
}

void PrintMessageV(CONSOLE_COLOR color, const wchar_t* format, va_list argList)
{
	size_t dstLen = (size_t)::_vcwprintf(format, argList);
	if(dstLen)
	{
		bool useSmallBuf = dstLen < CONSOLE_SMALL_BUF_SIZE;
		wchar_t smallBuf[CONSOLE_SMALL_BUF_SIZE];
		std::vector<wchar_t> bigBuf(useSmallBuf ? 0 : dstLen + 1);
		wchar_t* bufPtr = useSmallBuf ? smallBuf : bigBuf.data();
		::vswprintf_s(bufPtr, dstLen + 1, format, argList);
		PrintMessage(color, bufPtr);
	}
}

void PrintMessageF(CONSOLE_COLOR color, const char* format, ...)
{
	va_list argList;
	va_start(argList, format);
	PrintMessageV(color, format, argList);
	va_end(argList);
}

void PrintMessageF(CONSOLE_COLOR color, const wchar_t* format, ...)
{
	va_list argList;
	va_start(argList, format);
	PrintMessageV(color, format, argList);
	va_end(argList);
}

void PrintWarningF(const char* format, ...)
{
	va_list argList;
	va_start(argList, format);
	PrintMessageV(CONSOLE_COLOR::WARNING, format, argList);
	va_end(argList);
}

void PrintWarningF(const wchar_t* format, ...)
{
	va_list argList;
	va_start(argList, format);
	PrintMessageV(CONSOLE_COLOR::WARNING, format, argList);
	va_end(argList);
}

void PrintErrorF(const char* format, ...)
{
	va_list argList;
	va_start(argList, format);
	PrintMessageV(CONSOLE_COLOR::WARNING, format, argList);
	va_end(argList);
}

void PrintErrorF(const wchar_t* format, ...)
{
	va_list argList;
	va_start(argList, format);
	PrintMessageV(CONSOLE_COLOR::WARNING, format, argList);
	va_end(argList);
}

void SaveFile(const wchar_t* filePath, const void* data, size_t dataSize)
{
    FILE* f = nullptr;
    _wfopen_s(&f, filePath, L"wb");
    if(f)
    {
        fwrite(data, 1, dataSize, f);
        fclose(f);
    }
    else
        assert(0);
}

#endif // #ifdef _WIN32
