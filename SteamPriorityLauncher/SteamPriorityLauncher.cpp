// SteamPriorityLauncher.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <cstdio>

#define _WIN32_WINNT 0x502
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#include "atlstr.h"
#include "shellapi.h"
#include "tlhelp32.h"
#include "psapi.h"

constexpr auto VERSION = "1.1";

constexpr auto PRI_COUNT = 6;
constexpr auto PRI_DEFAULT = 3; // A (Above Normal)
const char* PRI_NAMES[] = { "L", "B", "N", "A", "H", "R" };
const char* PRI_DETAILS[] = { "Low/Idle*", "Below Normal*", "Normal", "Above Normal", "High*", "Realtime* (requires admin privileges, may cause system instability!)" };
const DWORD PRI_VALUES[] = { IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, NORMAL_PRIORITY_CLASS, ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS, REALTIME_PRIORITY_CLASS };

enum ParseState {
	Default, NextIsPriValue, NextIsGameID, NextIsGameExe, NextIsAffinity
};

void printError(const char* errSrc, DWORD err) {
	MessageBeep(MB_ICONERROR);
	char* errMsg = NULL;
	DWORD fmtMsgErr = ERROR_SUCCESS;
	if (!FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		err,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)& errMsg,
		0,
		NULL))
		fmtMsgErr = GetLastError();
	if (fmtMsgErr == ERROR_SUCCESS)
		printf("ERROR: %s, error code 0x%X: %s", errSrc, err, errMsg);
	else {
		printf("ERROR: %s, error code 0x%X\n", errSrc, err);
		printf("additionally, FormatMessageA failed, error code 0x%X\n", fmtMsgErr);
	}
	printf("press any key to exit...\n");
	system("pause>nul");
	LocalFree(errMsg);
}

int main(int argc, char* argv[])
{
	printf("Steam Priority Launcher v%s\nbuilt on %s at %s\n\n", VERSION, __DATE__, __TIME__);

	DWORD priValue = PRI_VALUES[PRI_DEFAULT];
	DWORD affMask = 0, affMaskSystem = 0;
	char gameID[0x101] = "";
	char gameExe[MAX_PATH] = "";
	char affMaskBuf[0x101] = "";
	bool gameIDSet = false, gameExeSet = false, affMaskSet = false;

	// get system affinity mask
	{
		DWORD useless = 0; // we don't need our affinity mask
		GetProcessAffinityMask(GetCurrentProcess(), &useless, &affMaskSystem);
	}

	if (argc == 1) {
		printf("usage: %s -priority <priority> -gameID <steam ID of game to launch> -gameExe <name of game EXE> -affinity <list of cores>\n", argv[0]);
		printf("Only the -gameID and -gameExe options are required.\n");
		printf("Valid values for <priority> are:\n");
		for (int i = 0; i < PRI_COUNT; i++)
			printf("  %s - %s\n", PRI_NAMES[i], PRI_DETAILS[i]);
		printf("  (* - not recommended)\n");
		printf("If the priority parameter is not specified, it'll default to %s.\n", PRI_NAMES[PRI_DEFAULT]);
		printf("<list of cores> - First core is 0, not 1. Separated by semicolons. Only recognizes decimal format.\n");
		printf("If you have more than 64 cores, each entry defines a group of cores, rather than one.\n");
		printf("(but let's be real, if you have a machine with >64 cores, it's probably not for gaming)\n");
		return 0;
	}

	ParseState ps = Default;
	char *ptr = NULL, *ptr_next = NULL;
	for (int i = 1; i < argc; i++) {
		switch (ps) {
		case NextIsPriValue:
			for (int j = 0; j < PRI_COUNT; j++) {
				if (strcmp(argv[i], PRI_NAMES[j]) == 0) {
					priValue = PRI_VALUES[j];
					ps = Default;
					break;
				}
			}
			if (ps == Default)
				break;
			printf("ERROR: unknown priority \"%s\"\n", argv[i]);
			printf("press any key to exit...\n");
			system("pause>nul");
			ExitProcess(-1);
		case NextIsGameID:
			gameIDSet = true;
			ZeroMemory(gameID, sizeof(gameID));
			strcpy_s(gameID, argv[i]);
			ps = Default;
			break;
		case NextIsGameExe:
			gameExeSet = true;
			ZeroMemory(gameExe, sizeof(gameExe));
			strcpy_s(gameExe, argv[i]);
			ps = Default;
			break;
		case NextIsAffinity:
			affMaskSet = true;
			ZeroMemory(affMaskBuf, sizeof(affMaskBuf));
			strcpy_s(affMaskBuf, argv[i]);
			ptr = strtok_s(affMaskBuf, ";", &ptr_next);
			while (ptr) {
				DWORD affBit = strtoul(ptr, NULL, 10);
				affMask |= 1 << affBit;
				ptr = strtok_s(NULL, ";", &ptr_next);
			}
			// AND affinity mask and system affinity mask, since the former has to be a subset of the latter
			affMask &= affMaskSystem;
			ps = Default;
			break;
		case Default:
		default:
			if (strcmp(argv[i], "-priority") == 0) {
				// next "parameter" is actually our priority value
				ps = NextIsPriValue;
				continue;
			}
			if (strcmp(argv[i], "-gameID") == 0) {
				// next "parameter" is actually our game ID
				ps = NextIsGameID;
				continue;
			}
			if (strcmp(argv[i], "-gameExe") == 0) {
				// next "parameter" is actually our game EXE name
				ps = NextIsGameExe;
				continue;
			}
			if (strcmp(argv[i], "-affinity") == 0) {
				// next "parameter" is actually our core affinity list
				ps = NextIsAffinity;
				continue;
			}
			printf("WARNING: unknown option \"%s\"\n", argv[i]);
			break;
		}
	}

	// quit if we don't have a required parameter
	if (!gameIDSet) {
		MessageBeep(MB_ICONERROR);
		printf("ERROR: game ID not set!\n");
		printf("please specify the -gameID option.\n");
		printf("press any key to exit...\n");
		system("pause>nul");
		return -1;
	}
	if (!gameExeSet) {
		MessageBeep(MB_ICONERROR);
		printf("ERROR: game EXE not set!\n");
		printf("please specify the -gameExe option.\n");
		printf("press any key to exit...\n");
		system("pause>nul");
		return -1;
	}

	// create steam browser protocl command
	char steamCmd[0x100 + 13] = "steam://run/";
	strcat_s(steamCmd, gameID);

	// run the command
	int errcode = 0;
	if ((errcode = (int)ShellExecuteA(NULL, "open", steamCmd, NULL, NULL, SW_SHOW)) <= 32) {
		printError("ShellExecuteA failed", errcode);
		return -1;
	}

	// wait for a bit
	Sleep(2000);

	// TIME TO SEARCH FOR THE GAME EXE, YAY

	// convert EXE name to TCHAR*
	USES_CONVERSION;
	TCHAR* gameExeT = A2W(gameExe);

	// setup to loop through process snapshot
	HINSTANCE hInst = GetModuleHandle(NULL);
	HANDLE hSnap = INVALID_HANDLE_VALUE;
	PROCESSENTRY32 pe32;
	ZeroMemory(&pe32, sizeof(pe32));
	pe32.dwSize = sizeof(pe32);

	// take that snapshot
	hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (!hSnap) {
		DWORD err = GetLastError();
		printError("CreateToolhelp32Snapshot failed; could not get a snapshot of all processes", err);
		return -1;
	}

	// loop through and grab the matching EXE
	HANDLE hProc = INVALID_HANDLE_VALUE;
	bool match = false;
	if (Process32First(hSnap, &pe32)) {
		do {
			hProc = OpenProcess(PROCESS_QUERY_INFORMATION, true, pe32.th32ProcessID);
			// if we can't open the process, continue
			if (!hProc) {
				CloseHandle(hProc);
				hProc = INVALID_HANDLE_VALUE;
				continue;
			}
#ifdef DEBUG
			_tprintf(_T("checking \"%s\""), pe32.szExeFile);
#endif
			// if the EXE names don't match, continue
			if (StrCmp(pe32.szExeFile, gameExeT) != 0) {
#ifdef DEBUG
				printf(": no match\n");
#endif
				CloseHandle(hProc);
				hProc = INVALID_HANDLE_VALUE;
				continue;
			}
			// we got a match! open the process and break this loop
#ifdef DEBUG
			printf(": it's a match!\n");
#endif
			hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION, true, pe32.th32ProcessID);
			match = true;
			break;
		} while (Process32Next(hSnap, &pe32));
	}
	CloseHandle(hSnap);

	// if we didn't find the EXE, error out
	if (!match) {
		if (hProc) CloseHandle(hProc);
		printf("ERROR: could not find game EXE \"%s\"\n", gameExe);
		printf("press any key to exit...\n");
		system("pause>nul");
		return -1;
	}

	// and finally, set the new priority!
	if (!SetPriorityClass(hProc, priValue)) {
		DWORD err = GetLastError();
		printError("SetPriorityClass failed", err);
		CloseHandle(hProc);
		return -1;
	}

	// oh, and set affinity too I guess
	if (affMaskSet) {
		if (!SetProcessAffinityMask(hProc, affMask)) {
			DWORD err = GetLastError();
			printError("SetProcessAffinityMask failed", err);
			CloseHandle(hProc);
			return -1;
		}
	}

	CloseHandle(hProc);

#ifdef DEBUG
	printf("done.\n");
	printf("press any key to exit...\n");
	system("pause>nul");
#endif

	return 0;
}
