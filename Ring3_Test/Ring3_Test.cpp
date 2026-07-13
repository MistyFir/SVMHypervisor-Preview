#include <iostream>
#include <ostream>
#include <limits> 
#include <Windows.h>
#include <winternl.h>
#include "Ring3_Test.h"
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
		clientId.UniqueProcess = (HANDLE)lpParam;
		clientId.UniqueThread = NULL;
		NTSTATUS status = g_NtOpenProcess(&hProcess, PROCESS_TERMINATE, &objAttr, &clientId);
		if (status == 0)
		{
			g_NtTerminateProcess(hProcess, 0);
			g_NtClose(hProcess);
		}
	}
}
VOID KillProcess(DWORD64 pid)
{
	_NtOpenProcess NtOpenProcess = (_NtOpenProcess)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtOpenProcess");
	_NtTerminateProcess NtTerminateProcess = (_NtTerminateProcess)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtTerminateProcess");
	_NtClose NtClose = (_NtClose)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtClose");
	g_NtOpenProcess = NtOpenProcess;
	g_NtClose = NtClose;
	g_NtTerminateProcess = NtTerminateProcess;
	for (int i = 0; i < 30; i++)
	{
		HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MyThread, (PVOID)pid, 0, NULL);
		CloseHandle(hThread);
	}
	Sleep(20000);
}
BOOL KernelReadMemory(HANDLE hDevice, PVOID address, PVOID outbuffer, DWORD size)
{
	DWORD bytesReturned = 0;
	READ_MEMORY_INFO readInfo = { 0 };
	readInfo.Address = address;
	readInfo.OutputBufferSize = size;
	readInfo.OutputBuffer = outbuffer;
	return DeviceIoControl(hDevice, IOCTL_READ_MEMORY, &readInfo, sizeof(readInfo), &readInfo, sizeof(readInfo), &bytesReturned, NULL);
}
BOOL KernelWriteMemory(HANDLE hDevice, PVOID address, PVOID inbuffer, DWORD size)
{
	DWORD bytesReturned = 0;
	WRITE_MEMORY_INFO writeInfo = { 0 };
	writeInfo.Address = address;
	writeInfo.InputBufferSize = size;
	writeInfo.InputBuffer = inbuffer;
	return DeviceIoControl(hDevice, IOCTL_WRITE_MEMORY, &writeInfo, sizeof(writeInfo), &writeInfo, sizeof(writeInfo), &bytesReturned, NULL);
}
BOOL KernelReadMSR(HANDLE hDevice, UINT32 msr, UINT64* outvalue)
{
	DWORD bytesReturned = 0;
	READ_MSR_INFO readMsrInfo = { 0 };
	readMsrInfo.Msr = msr;
	BOOL result = DeviceIoControl(hDevice, IOCTL_READ_MSR, &readMsrInfo, sizeof(readMsrInfo), &readMsrInfo, sizeof(readMsrInfo), &bytesReturned, NULL);
	if (outvalue) *outvalue = readMsrInfo.OutputValue;
	return result;
}
BOOL KernelWriteMSR(HANDLE hDevice, UINT32 msr, UINT64 invalue)
{
	DWORD bytesReturned = 0;
	WRITE_MSR_INFO writeMsrInfo = { 0 };
	writeMsrInfo.Msr = msr;
	writeMsrInfo.InputValue = invalue;
	return DeviceIoControl(hDevice, IOCTL_WRITE_MSR, &writeMsrInfo, sizeof(writeMsrInfo), NULL,0, &bytesReturned, NULL);
}
BOOL KernelTestThread(HANDLE hDevice)
{
	DWORD bytesReturned = 0;
	return DeviceIoControl(hDevice, IOCTL_TEST_THREAD, NULL, 0, NULL, 0, &bytesReturned, NULL);
}
BOOL KernelSetProtectedProcess(HANDLE hDevice, DWORD64 pid)
{
	DWORD bytesReturned = 0;
	return DeviceIoControl(hDevice, IOCTL_SET_PROTECTED_PROCESS, &pid, sizeof(pid), NULL, 0, &bytesReturned, NULL);
}
BOOL SvmControl(HANDLE hDevice, BOOLEAN debug, BOOLEAN unload)
{
	DWORD bytesReturned = 0;
	SVM_RUN_CONTROL control = { 0 };
	control.Debug = debug;
	control.Unload = unload;
	return DeviceIoControl(hDevice, IOCTL_SVM_CONTROL, &control, sizeof(control), NULL, 0, &bytesReturned, NULL);
}
BOOL SvmStart(HANDLE hDevice)
{
	DWORD bytesReturned = 0;
	return DeviceIoControl(hDevice, IOCTL_VM_START, NULL, 0, NULL, 0, &bytesReturned, NULL);
}
BOOL KernelForceTerminateProcess(HANDLE hDevice, UINT64 pid)
{
	DWORD bytesReturned = 0;
	return DeviceIoControl(hDevice, IOCTL_FORCE_TERMINATE, &pid, sizeof(pid), NULL, 0, &bytesReturned, NULL);
}
void CinClear()
{
	if (std::cin.fail())
	{
		std::cin.clear();
	}
	std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
}
int main()
{
	system("color a");
	HANDLE hDevice = INVALID_HANDLE_VALUE;
	do
	{
		hDevice = CreateFileA("\\\\.\\SVMTest", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hDevice == INVALID_HANDLE_VALUE) break;
		while (true)
		{
			BOOL result = FALSE;
			cout << "1. Start SVM" << endl;
			cout << "2. Control SVM" << endl;
			cout << "3. Read Memory" << endl;
			cout << "4. Write Memory" << endl;
			cout << "5. Read MSR" << endl;
			cout << "6. Write MSR" << endl;
			cout << "7. Test Thread" << endl;
			cout << "8. Set Protected Process" << endl;
			cout << "9. Kill Process" << endl;
			cout << "10. Force Terminate Process" << endl;
			cout << "0. Exit" << endl;
			cout << endl;
			cout << "Please select an option: " << endl;;
			UINT64 choice = 0;
			if (!(cin >> std::dec >> choice))
			{
				CinClear();
				cout << "Invalid input!" << endl;
				continue;
			}
			CinClear();
			choice &= 0xFFFF;
			switch (choice)
			{
			case 1:
			{
				SvmStart(hDevice);
				cout << endl;
				break;
			}
			case 2:
			{
				UINT64 debug = FALSE;
				UINT64 unload = FALSE;
				cout << "input debug(0/1): "<<endl;
				cin >> std::dec >> debug;
				CinClear();
				cout << "input unload(0/1): " << endl;
				cin >> std::dec >> unload;
				CinClear();
				debug &= 0xFF;
				unload &= 0xFF;
				SvmControl(hDevice, debug, unload);
				cout << endl;
				break;
			}
			case 3:
			{
				UINT64 address = 0;
				cout << "Enter address to read: " << endl;
				cin >> std::hex >> address;
				CinClear();
				UINT64 buffer = 0;
				KernelReadMemory(hDevice, (PVOID)address, &buffer, sizeof(buffer));
				cout << "Read Value: " << std::hex << buffer << endl;
				cout << endl;
				break;
			}
			case 4:
			{
				UINT64 address = 0;
				cout << "Enter address to write: " << endl;
				cin >> std::hex >> address;
				CinClear();
				UINT64 length = 0;
				cout << "Enter length to write(1/2/4/8): " << endl;
				cin >> std::dec >> length;
				length &= 0xFF;
				if (length != 1 && length != 2 && length != 4 && length != 8)
				{
					cout << "Invalid length! " << endl;
					break;
				}
				UINT64 value = 0;
				cout << "Enter value to write: " << endl;
				cin >> std::hex >> value;
				CinClear();
				if (!KernelWriteMemory(hDevice, (PVOID)address, &value, length))
				{
					cout << "Write Memory Failed! errcode: "<< GetLastError() << endl;
				}
				else
				{
					cout << "Write Memory Success!" << endl;
				}
				cout << endl;
				break;
			}
			case 5:
			{
				UINT64 msr = 0;
				cout << "Input MSR: " << endl;
				cin >> std::hex >> msr;
				CinClear();
				UINT64 value = 0;
				if (!KernelReadMSR(hDevice, (UINT32)msr, &value))
				{
					cout << "Read MSR Failed! errcode: " << GetLastError() << endl;
				}
				else
				{
					cout << "Read MSR Success! Value: " << std::hex << value << endl;
				}
				cout << endl;
				break;
			}
			case 6:
			{
				UINT64 msr = 0;
				cout << "input msr: " << endl;
				cin >> std::hex >> msr;
				CinClear();
				UINT64 value = 0;
				cout << "input value: " << endl;
				cin >> std::hex >> value;
				CinClear();
				msr &= 0xFFFFFFFF;
				if (KernelWriteMSR(hDevice, (UINT32)msr, value))
				{
					cout << "Write MSR Success!" << endl;
				}
				else
				{
					cout << "Write MSR Failed! errcode: " << GetLastError() << endl;
				}
				break;
			}
			case 7:
			{
				KernelTestThread(hDevice);
				break;
			}
			case 8:
			{
				UINT64 pid = 0;
				cout << "Enter PID to set protected: " << endl;
				cin >> std::dec >> pid;
				CinClear();
				pid &= 0xFFFFFFFF;
				KernelSetProtectedProcess(hDevice, pid);
				cout << endl;
				break;
			}
			case 9:
			{
				DWORD64 pid = 0;
				cout << "Enter PID to kill: " << endl;
				cin >> std::dec >> pid;
				CinClear();
				pid &= 0xFFFFFFFF;
				KillProcess(pid);
				cout << endl;
				break;
			}
			case 10:
			{
				DWORD64 pid = 0;
				cout << "Enter PID to kill: " << endl;
				cin >> std::dec >> pid;
				CinClear();
				pid &= 0xFFFFFFFF;
				KernelForceTerminateProcess(hDevice, pid);
				cout << endl;
				break;
			}
			case 0:
			{
				result = TRUE;
				cout << endl;
				break;
			}
			default:
			{
				cout << "Invalid choice!" << endl;
				cout << endl;
				break;
			}
			}
			if (result) break;
		}
	} while (FALSE);
	if (hDevice != INVALID_HANDLE_VALUE) CloseHandle(hDevice);
	system("pause");
}
