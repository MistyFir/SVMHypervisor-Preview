#include <iostream>
#include <Windows.h>
#include <winternl.h>
typedef struct _MY_CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
}MY_CLIENT_ID,*PMY_CLIENT_ID;
typedef NTSTATUS(__stdcall* _NtOpenProcess)(PHANDLE ProcessHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PMY_CLIENT_ID ClientId);
typedef NTSTATUS(__stdcall* _NtClose)(HANDLE Handle);
typedef NTSTATUS(__stdcall* _NtTerminateProcess)(HANDLE ProcessHandle, NTSTATUS ExitStatus);
using namespace std;
_NtOpenProcess g_NtOpenProcess = NULL;
_NtClose g_NtClose = NULL;
_NtTerminateProcess g_NtTerminateProcess = NULL;
VOID MyThread(PVOID lpParam)
{
	for (size_t i = 0; i < 500; i++)
	{
		HANDLE hProcess = NULL;
		OBJECT_ATTRIBUTES objAttr = { 0 };
		InitializeObjectAttributes(&objAttr, NULL, NULL, NULL, NULL);
		MY_CLIENT_ID clientId = { 0 };
		clientId.UniqueProcess = (HANDLE)0x1438;
		clientId.UniqueThread = NULL;
		NTSTATUS status = g_NtOpenProcess(&hProcess, PROCESS_TERMINATE, &objAttr, &clientId);
		if (status == 0)
		{
			g_NtTerminateProcess(hProcess, 0);
			g_NtClose(hProcess);
		}
	}
}
int main()
{
	_NtOpenProcess NtOpenProcess = (_NtOpenProcess)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtOpenProcess");
	_NtTerminateProcess NtTerminateProcess = (_NtTerminateProcess)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtTerminateProcess");
	_NtClose NtClose = (_NtClose)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtClose");
	g_NtOpenProcess = NtOpenProcess;
	g_NtClose = NtClose;
	g_NtTerminateProcess = NtTerminateProcess;
	for (int i = 0; i < 30; i++)
	{
		HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MyThread, NULL, 0, NULL);
		CloseHandle(hThread);
	}
	Sleep(20000);
}
