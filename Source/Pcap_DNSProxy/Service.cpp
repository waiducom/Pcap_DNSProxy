﻿// This code is part of Pcap_DNSProxy
// A local DNS server based on WinPcap and LibPcap
// Copyright (C) 2012-2016 Chengr28
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


#include "Service.h"

#if defined(PLATFORM_WIN)
//Catch Control-C exception from keyboard
BOOL WINAPI CtrlHandler(
	const DWORD ControlType)
{
//Print to screen.
	if (GlobalRunningStatus.IsConsole)
	{
		switch (ControlType)
		{
		//Handle the CTRL-C signal.
			case CTRL_C_EVENT:
			{
				PrintToScreen(true, L"[Notice] Get Control-C.\n");
			}break;
		//Handle the CTRL-Break signal.
			case CTRL_BREAK_EVENT:
			{
				PrintToScreen(true, L"[Notice] Get Control-Break.\n");
			}break;
		//Handle other signals.
			default:
			{
				PrintToScreen(true, L"[Notice] Get closing signal.\n");
			}break;
		}
	}

	return FALSE;
}

//Service Main function
size_t WINAPI ServiceMain(
	DWORD argc, 
	LPTSTR *argv)
{
//Disable console mode printing.
	GlobalRunningStatus.IsConsole = false;

//Service initialization
	ServiceStatusHandle = RegisterServiceCtrlHandlerW(
		SYSTEM_SERVICE_NAME, 
		(LPHANDLER_FUNCTION)ServiceControl);
	if (ServiceStatusHandle == nullptr)
		return FALSE;

//Update service status(Part 1).
	if (UpdateServiceStatus(SERVICE_START_PENDING, NO_ERROR, 0, 1U, UPDATE_SERVICE_TIME) == FALSE)
	{
		CloseHandle(
			ServiceStatusHandle);
		return FALSE;
	}

//Create service event.
	ServiceEvent = CreateEventW(
		0, 
		TRUE, 
		FALSE, 
		0);
	if (ServiceEvent == nullptr)
	{
		CloseHandle(
			ServiceStatusHandle);
		return FALSE;
	}

//Update service status(Part 2).
	if (UpdateServiceStatus(SERVICE_START_PENDING, NO_ERROR, 0, 2U, STANDARD_TIMEOUT) == FALSE)
	{
		CloseHandle(
			ServiceStatusHandle);
		CloseHandle(
			ServiceEvent);
		return FALSE;
	}

//Create thread.
	const auto ServiceThread = ExecuteService();
	if (ServiceThread == nullptr)
	{
		CloseHandle(
			ServiceStatusHandle);
		CloseHandle(
			ServiceEvent);
		return FALSE;
	}

//Update service status.
	ServiceCurrentStatus = SERVICE_RUNNING;
	if (UpdateServiceStatus(SERVICE_RUNNING, NO_ERROR, 0, 0, 0) == FALSE)
	{
		CloseHandle(
			ServiceStatusHandle);
		CloseHandle(
			ServiceEvent);
		CloseHandle(
			ServiceThread);
		return FALSE;
	}

//Wait signal to shutdown.
	WaitForSingleObject(
		ServiceEvent, 
		INFINITE);
	CloseHandle(
		ServiceStatusHandle);
	CloseHandle(
		ServiceEvent);
	CloseHandle(
		ServiceThread);
	return EXIT_SUCCESS;
}

//Service controller
size_t WINAPI ServiceControl(
	const DWORD ControlCode)
{
	switch(ControlCode)
	{
	//Handle the shutdown signal.
		case SERVICE_CONTROL_SHUTDOWN:
		{
			TerminateService();
			return EXIT_SUCCESS;
		}
	//Handle the stop signal.
		case SERVICE_CONTROL_STOP:
		{
			ServiceCurrentStatus = SERVICE_STOP_PENDING;
			UpdateServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 0, 1U, UPDATE_SERVICE_TIME);
			TerminateService();

			return EXIT_SUCCESS;
		}
	//Handle other signals.
		default:
		{
			break;
		}
	}

	UpdateServiceStatus(ServiceCurrentStatus, NO_ERROR, 0, 0, 0);
	return EXIT_SUCCESS;
}

//Start Main process
HANDLE WINAPI ExecuteService(
	void)
{
	DWORD ThreadID = 0;
	const HANDLE ServiceThread = CreateThread(
		0, 
		0, 
		(PTHREAD_START_ROUTINE)ServiceProc, 
		nullptr, 
		0, 
		&ThreadID);
	if (ServiceThread != nullptr)
	{
		IsServiceRunning = TRUE;
		return ServiceThread;
	}

	return nullptr;
}

//Service Main process thread
DWORD WINAPI ServiceProc(
	PVOID ProcParameter)
{
//Start main process.
	if (IsServiceRunning == FALSE || !MonitorInit())
	{
		TerminateService();
		return FALSE;
	}

	TerminateService();
	return EXIT_SUCCESS;
}

//Change status of service
BOOL WINAPI UpdateServiceStatus(
	const DWORD CurrentState, 
	const DWORD WinExitCode, 
	const DWORD ServiceSpecificExitCode, 
	const DWORD CheckPoint, 
	const DWORD WaitHint)
{
//Initialization
	SERVICE_STATUS ServiceStatus;
	memset(&ServiceStatus, 0, sizeof(ServiceStatus));
	ServiceStatus.dwServiceType = SERVICE_WIN32;
	ServiceStatus.dwCurrentState = CurrentState;
	if (CurrentState == SERVICE_START_PENDING)
		ServiceStatus.dwControlsAccepted = 0;
	else 
		ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
	if (ServiceSpecificExitCode == 0)
		ServiceStatus.dwWin32ExitCode = WinExitCode;
	else 
		ServiceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
	ServiceStatus.dwServiceSpecificExitCode = ServiceSpecificExitCode;
	ServiceStatus.dwCheckPoint = CheckPoint;
	ServiceStatus.dwWaitHint = WaitHint;

//Service status setting
	if (SetServiceStatus(
			ServiceStatusHandle, 
			&ServiceStatus) == 0)
	{
		TerminateService();
		return FALSE;
	}

	return TRUE;
}

//Terminate service
void WINAPI TerminateService(
	void)
{
	IsServiceRunning = FALSE;
	SetEvent(ServiceEvent);
	UpdateServiceStatus(SERVICE_STOPPED, NO_ERROR, 0, 0, 0);

	return;
}

//Mailslot of flush DNS cache Monitor
bool Flush_DNS_MailSlotMonitor(
	void)
{
//System security initialization
	std::shared_ptr<uint8_t> ACL_Buffer(new uint8_t[FILE_BUFFER_SIZE]());
	memset(ACL_Buffer.get(), 0, FILE_BUFFER_SIZE);
	SECURITY_ATTRIBUTES SecurityAttributes;
	SECURITY_DESCRIPTOR SecurityDescriptor;
	memset(&SecurityAttributes, 0, sizeof(SecurityAttributes));
	memset(&SecurityDescriptor, 0, sizeof(SecurityDescriptor));
	PSID SID_Value = nullptr;

//System security setting
	if (InitializeSecurityDescriptor(
			&SecurityDescriptor, 
			SECURITY_DESCRIPTOR_REVISION) == 0 || 
		InitializeAcl(
			(PACL)ACL_Buffer.get(), 
			FILE_BUFFER_SIZE, 
			ACL_REVISION) == 0 || 
		ConvertStringSidToSidW(
			SID_ADMINISTRATORS_GROUP, 
			&SID_Value) == 0 || 
		AddAccessAllowedAce(
			(PACL)ACL_Buffer.get(), 
			ACL_REVISION, 
			GENERIC_ALL, 
			SID_Value) == 0 || 
		SetSecurityDescriptorDacl(
			&SecurityDescriptor, 
			true, 
			(PACL)ACL_Buffer.get(), 
			false) == 0)
	{
		PrintError(LOG_LEVEL_TYPE::LEVEL_2, LOG_ERROR_TYPE::SYSTEM, L"Create mailslot error", GetLastError(), nullptr, 0);
		if (SID_Value != nullptr)
			LocalFree(SID_Value);

		return false;
	}
	else {
		SecurityAttributes.lpSecurityDescriptor = &SecurityDescriptor;
		SecurityAttributes.bInheritHandle = true;
	}

//Create mailslot.
	const HANDLE MailslotHandle = CreateMailslotW(
		MAILSLOT_NAME, 
		FILE_BUFFER_SIZE - 1U, 
		MAILSLOT_WAIT_FOREVER, 
		&SecurityAttributes);
	if (MailslotHandle == INVALID_HANDLE_VALUE)
	{
		PrintError(LOG_LEVEL_TYPE::LEVEL_2, LOG_ERROR_TYPE::SYSTEM, L"Create mailslot error", GetLastError(), nullptr, 0);
		if (SID_Value != nullptr)
			LocalFree(SID_Value);

		return false;
	}

//Free pointer.
	ACL_Buffer.reset();
	if (SID_Value != nullptr)
		LocalFree(SID_Value);

//Initialization
	std::shared_ptr<wchar_t> Buffer(new wchar_t[FILE_BUFFER_SIZE]());
	wmemset(Buffer.get(), 0, FILE_BUFFER_SIZE);
	std::wstring Message;
	std::string Domain;
	DWORD MessageLength = 0;

//Mailslot monitor
	for (;;)
	{
	//Reset parameters.
		wmemset(Buffer.get(), 0, FILE_BUFFER_SIZE);
		MessageLength = 0;

	//Read message from mailslot.
		const auto Result = ReadFile(
			MailslotHandle, 
			Buffer.get(), 
			FILE_BUFFER_SIZE, 
			&MessageLength, 
			nullptr);
		if (Result == FALSE)
		{
			PrintError(LOG_LEVEL_TYPE::LEVEL_3, LOG_ERROR_TYPE::SYSTEM, L"Mailslot read messages error", GetLastError(), nullptr, 0);

			CloseHandle(MailslotHandle);
			return false;
		}
		else {
			Message = Buffer.get();
			Domain.clear();

		//Read message.
			if (Message == MAILSLOT_MESSAGE_FLUSH_DNS) //Flush all DNS cache.
			{
				Flush_DNS_Cache(nullptr);
			}
			else if (Message.compare(0, wcslen(MAILSLOT_MESSAGE_FLUSH_DNS_DOMAIN), MAILSLOT_MESSAGE_FLUSH_DNS_DOMAIN) == 0 && //Flush single domain cache.
				Message.length() > wcslen(MAILSLOT_MESSAGE_FLUSH_DNS_DOMAIN) + DOMAIN_MINSIZE && //Domain length check
				Message.length() < wcslen(MAILSLOT_MESSAGE_FLUSH_DNS_DOMAIN) + DOMAIN_MAXSIZE)
			{
				if (WCS_To_MBS_String(Message.c_str() + wcslen(MAILSLOT_MESSAGE_FLUSH_DNS_DOMAIN), DOMAIN_MAXSIZE, Domain) && 
					Domain.length() > DOMAIN_MINSIZE && Domain.length() < DOMAIN_MAXSIZE)
						Flush_DNS_Cache((const uint8_t *)Domain.c_str());
				else 
					PrintError(LOG_LEVEL_TYPE::LEVEL_2, LOG_ERROR_TYPE::SYSTEM, L"Convert multiple byte or wide char string error", 0, nullptr, 0);
			}
			else {
				Sleep(Parameter.FileRefreshTime);
			}
		}
	}

//Monitor terminated
	CloseHandle(MailslotHandle);
	PrintError(LOG_LEVEL_TYPE::LEVEL_2, LOG_ERROR_TYPE::SYSTEM, L"Mailslot module Monitor terminated", 0, nullptr, 0);
	return false;
}

//Mailslot of flush DNS cache sender
bool WINAPI Flush_DNS_MailSlotSender(
	const wchar_t * const Domain)
{
//Mailslot initialization
	const HANDLE FileHandle = CreateFileW(
		MAILSLOT_NAME, 
		GENERIC_WRITE, 
		FILE_SHARE_READ, 
		nullptr, 
		OPEN_EXISTING, 
		FILE_ATTRIBUTE_NORMAL, 
		nullptr);
	if (FileHandle == INVALID_HANDLE_VALUE)
	{
		std::wstring InnerMessage(L"[System Error] Create mailslot error");
		if (GetLastError() == 0)
		{
			InnerMessage.append(L".\n");
			PrintToScreen(true, InnerMessage.c_str());
		}
		else {
			ErrorCodeToMessage(LOG_ERROR_TYPE::SYSTEM, GetLastError(), InnerMessage);
			InnerMessage.append(L".\n");
			PrintToScreen(true, InnerMessage.c_str(), GetLastError());
		}

		return false;
	}

//Message initialization
	std::wstring Message(MAILSLOT_MESSAGE_FLUSH_DNS);
	if (Domain != nullptr && wcsnlen_s(Domain, DOMAIN_MAXSIZE) > DOMAIN_MINSIZE)
	{
		Message.append(L": ");
		Message.append(Domain);
	}

//Write into mailslot.
	DWORD WrittenBytes = 0;
	if (WriteFile(
			FileHandle, 
			Message.c_str(), 
			(DWORD)(sizeof(wchar_t) * Message.length() + 1U), 
			&WrittenBytes, 
			nullptr) == 0)
	{
		CloseHandle(FileHandle);
		std::wstring InnerMessage(L"[System Error] Mailslot write messages error");
		if (GetLastError() == 0)
		{
			InnerMessage.append(L".\n");
			PrintToScreen(true, InnerMessage.c_str());
		}
		else {
			ErrorCodeToMessage(LOG_ERROR_TYPE::SYSTEM, GetLastError(), InnerMessage);
			InnerMessage.append(L".\n");
			PrintToScreen(true, InnerMessage.c_str(), GetLastError());
		}

		return false;
	}
	else {
		CloseHandle(FileHandle);
		PrintToScreen(true, L"[Notice] Flush DNS cache message was sent successfully.\n");
	}

	return true;
}

#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
//Flush DNS cache FIFO Monitor
bool Flush_DNS_FIFO_Monitor(
	void)
{
//Initialization
	std::shared_ptr<uint8_t> Buffer(new uint8_t[FILE_BUFFER_SIZE]());
	memset(Buffer.get(), 0, FILE_BUFFER_SIZE);
	std::string Message;
	int FIFO_Handle = 0;
	ssize_t Length = 0;

//FIFO Monitor
	for (;;)
	{
	//Create FIFO and create its notify monitor.
		unlink(FIFO_PATH_NAME);
		errno = 0;
		if (mkfifo(FIFO_PATH_NAME, O_CREAT) == RETURN_ERROR || 
			chmod(FIFO_PATH_NAME, S_IRUSR | S_IWUSR | S_IWGRP | S_IWOTH) == RETURN_ERROR)
		{
			PrintError(LOG_LEVEL_TYPE::LEVEL_2, LOG_ERROR_TYPE::SYSTEM, L"Create FIFO error", errno, nullptr, 0);

			unlink(FIFO_PATH_NAME);
			return false;
		}

	//Open FIFO.
		errno = 0;
		FIFO_Handle = open(FIFO_PATH_NAME, O_RDONLY, 0);
		if (FIFO_Handle == RETURN_ERROR)
		{
			PrintError(LOG_LEVEL_TYPE::LEVEL_2, LOG_ERROR_TYPE::SYSTEM, L"Create FIFO error", errno, nullptr, 0);

			unlink(FIFO_PATH_NAME);
			return false;
		}

	//Read file data.
		memset(Buffer.get(), 0, FILE_BUFFER_SIZE);
		errno = 0;
		Length = read(FIFO_Handle, Buffer.get(), FILE_BUFFER_SIZE);
		if (Length == RETURN_ERROR || Length < (ssize_t)DOMAIN_MINSIZE || Length > (ssize_t)DOMAIN_MAXSIZE)
		{
			PrintError(LOG_LEVEL_TYPE::LEVEL_3, LOG_ERROR_TYPE::SYSTEM, L"FIFO read messages error", errno, nullptr, 0);
		}
		else {
			Message = (const char *)Buffer.get();

		//Read message.
			if (Message == FIFO_MESSAGE_FLUSH_DNS) //Flush all DNS cache.
				Flush_DNS_Cache(nullptr);
			else if (Message.compare(0, strlen(FIFO_MESSAGE_FLUSH_DNS_DOMAIN), FIFO_MESSAGE_FLUSH_DNS_DOMAIN) == 0 && //Flush single domain cache.
				Message.length() > strlen(FIFO_MESSAGE_FLUSH_DNS_DOMAIN) + DOMAIN_MINSIZE && //Domain length check
				Message.length() < strlen(FIFO_MESSAGE_FLUSH_DNS_DOMAIN) + DOMAIN_MAXSIZE)
					Flush_DNS_Cache((const uint8_t *)Message.c_str() + strlen(FIFO_MESSAGE_FLUSH_DNS_DOMAIN));
			else 
				Sleep(Parameter.FileRefreshTime);
		}

	//Close FIFO.
		close(FIFO_Handle);
		FIFO_Handle = 0;
	}

//Monitor terminated
	close(FIFO_Handle);
	unlink(FIFO_PATH_NAME);
	PrintError(LOG_LEVEL_TYPE::LEVEL_2, LOG_ERROR_TYPE::SYSTEM, L"FIFO module Monitor terminated", 0, nullptr, 0);
	return true;
}

//Flush DNS cache FIFO sender
bool Flush_DNS_FIFO_Sender(
	const uint8_t * const Domain)
{
//Message initialization
	std::string Message(FIFO_MESSAGE_FLUSH_DNS);
	if (Domain != nullptr && strnlen((const char *)Domain, DOMAIN_MAXSIZE) > DOMAIN_MINSIZE)
	{
		Message.append(": ");
		Message.append((const char *)Domain);
	}

//Write into FIFO file.
	errno = 0;
	const int FIFO_Handle = open(FIFO_PATH_NAME, O_WRONLY | O_TRUNC | O_NONBLOCK, 0);
	if (FIFO_Handle > 0)
	{
		if (write(FIFO_Handle, Message.c_str(), Message.length() + 1U) > 0)
		{
			close(FIFO_Handle);
			PrintToScreen(true, L"[Notice] Flush DNS cache message was sent successfully.\n");

			return true;
		}
		else {
			close(FIFO_Handle);
		}
	}

//Print error log.
	std::wstring InnerMessage(L"[System Error] FIFO write messages error");
	if (errno == 0)
	{
		InnerMessage.append(L".\n");
		PrintToScreen(true, InnerMessage.c_str());
	}
	else {
		ErrorCodeToMessage(LOG_ERROR_TYPE::SYSTEM, errno, InnerMessage);
		InnerMessage.append(L".\n");
		PrintToScreen(true, InnerMessage.c_str(), errno);
	}

	return false;
}
#endif

//Flush DNS cache
void Flush_DNS_Cache(
	const uint8_t * const Domain)
{
//Flush DNS cache in process.
	std::unique_lock<std::mutex> DNSCacheListMutex(DNSCacheListLock);
	if (Domain == nullptr || //Flush all DNS cache.
		strnlen_s((const char *)Domain, DOMAIN_MAXSIZE) > DOMAIN_MINSIZE)
	{
		DNSCacheList.clear();
	}
	else { //Flush single domain cache.
		for (auto DNSCacheDataIter = DNSCacheList.begin();DNSCacheDataIter != DNSCacheList.end();)
		{
			if (DNSCacheDataIter->Domain == (const char *)Domain)
				DNSCacheDataIter = DNSCacheList.erase(DNSCacheDataIter);
			else 
				++DNSCacheDataIter;
		}
	}

	DNSCacheListMutex.unlock();

#if (defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
//Flush system DNS interval time check
	if (LastFlushDNSTime > 0 && LastFlushDNSTime < GetCurrentSystemTime() + FLUSH_DNS_CACHE_INTERVAL_TIME * SECOND_TO_MILLISECOND)
		return;
	else 
		LastFlushDNSTime = GetCurrentSystemTime();
#endif

//Flush DNS cache in system.
	std::lock_guard<std::mutex> ScreenMutex(ScreenLock);
#if defined(PLATFORM_WIN)
	system("ipconfig /flushdns 2>nul"); //All Windows version
	fwprintf_s(stderr, L"\n");
#elif defined(PLATFORM_LINUX)
	#if defined(PLATFORM_OPENWRT)
		system("/etc/init.d/dnsmasq restart 2>/dev/null"); //Dnsmasq manage DNS cache on OpenWrt
	#else
		auto Result = system("service nscd restart 2>/dev/null"); //Name Service Cache Daemon service
		Result = system("service dnsmasq restart 2>/dev/null"); //Dnsmasq service
		Result = system("rndc restart 2>/dev/null"); //Name server control utility of BIND(9.1.3 and older version)
		Result = system("rndc flush 2>/dev/null"); //Name server control utility of BIND(9.2.0 and newer version)
	#endif
#elif defined(PLATFORM_MACOS)
//	system("lookupd -flushcache 2>/dev/null"); //Less than Mac OS X Tiger(10.4)
//	system("dscacheutil -flushcache 2>/dev/null"); //Mac OS X Leopard(10.5) and Snow Leopard(10.6)
	system("killall -HUP mDNSResponder 2>/dev/null"); //Mac OS X Lion(10.7), Mountain Lion(10.8) and Mavericks(10.9)
	system("discoveryutil mdnsflushcache 2>/dev/null"); //Mac OS X Yosemite(10.10) and newer Mac OS X/macOS version
#endif

	return;
}
