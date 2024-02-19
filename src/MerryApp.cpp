#include "MerryApp.h"
#include <wx/stdpaths.h>
#include <chrono>
#include "MerryListBoxPanel.h"

IMPLEMENT_APP(MerryApp)

HHOOK hHook = NULL;
std::chrono::system_clock::time_point tmLastPressCtrl;
const double MIN_INTERVAL = 120;
const double MAX_INTERVAL = 350;

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKeyBoard = (KBDLLHOOKSTRUCT*)lParam;
        if (wParam == WM_KEYDOWN) {
			if (pKeyBoard->vkCode != VK_LCONTROL && pKeyBoard->vkCode != VK_RCONTROL && (GetKeyState(VK_CONTROL) & 0x8000)) {
				tmLastPressCtrl = std::chrono::system_clock::time_point();
			}
        }else if (wParam == WM_KEYUP) {
            if (pKeyBoard->vkCode == VK_LCONTROL || pKeyBoard->vkCode == VK_RCONTROL) {
				auto tmCur = std::chrono::high_resolution_clock::now();
				auto tmdiff = std::chrono::duration_cast<std::chrono::milliseconds>(tmCur - tmLastPressCtrl);
				auto diffVal = tmdiff.count();
				if(diffVal > MIN_INTERVAL && diffVal < MAX_INTERVAL){
					const MerryCommand* command = g_commands->GetCommand(0);
					assert(command);
					command->Execute(wxEmptyString);
					tmLastPressCtrl = std::chrono::system_clock::time_point();
				}else{
					tmLastPressCtrl = tmCur;
				}
            }
        }
    }
    return CallNextHookEx(hHook, nCode, wParam, lParam);
}


bool MerryApp::OnInit()
{
	const wxString name = wxString::Format("ALMRun-%s", wxGetUserId().c_str());
    m_checker = new wxSingleInstanceChecker(name);
    if (m_checker->IsAnotherRunning())//程序已经在运行中..
    {
        stClient* client = new stClient;

        // Create the connection
        wxConnectionBase* connection =
					//	client->MakeConnection( IPC_HOST,"4242","IPC TEST");
                     client->MakeConnection( IPC_HOST,IPC_SERVICE,IPC_TOPIC);

        if (connection)
        {
            // Ask the other instance to open a file or raise itself
			Execute_IPC_CMD(connection);
        }
        else
        {
            wxMessageBox(wxT("程序已经运行,但是进程通信失败!"),
                wxT("ALMRun"), wxICON_INFORMATION|wxOK);
        }
		wxDELETE(client);
		wxDELETE(m_checker);
        return false;
    }
	
	// Create a new server
    m_server = new stServer;
    if (!m_server->Create(IPC_SERVICE))
    {
        wxMessageBox("创建高级进程通信失败,无法实现单一实例进程和右键发送到等功能.");
		return false;
    }
	if (!wxApp::OnInit())
		return false;
	#if _DEBUG_LOG
        m_pLogFile = fopen( "log.txt", "w+" );
		wxLogStderr *log = new wxLogStderr(m_pLogFile);
        delete  wxLog::SetActiveTarget(log);

        wxLog::SetTimestamp(wxT("%Y-%m-%d %H:%M:%S"));
		
		wxLog::SetLogLevel(::wxLOG_Max);
		wxLogMessage("ALMRun_INIT");
	#endif
	#ifdef __WXMSW__
	wxStandardPaths std = wxStandardPaths::Get(); //<wx/stdpaths.h>
	wxFileName fname = wxFileName(std.GetExecutablePath());
	wxString volume;
	wxString pathTmp = fname.GetPathWithSep(); //<wx/filename.h>
	::wxSetEnv(wxT("ALMRUN_HOME"),pathTmp.c_str());
	::wxSetEnv(wxT("ALMRUN_ROOT"),pathTmp.c_str());
	::wxSetEnv(wxT("Desktop"),std.MSWGetShellDir(0x10));//CSIDL_DESKTOP 
	::wxSetEnv(wxT("Programs"),std.MSWGetShellDir(2));//CSIDL_PROGRAMS 
	::wxSetEnv(wxT("CommonDesktop"),std.MSWGetShellDir(0x19));//CSIDL_COMMON_DESKTOPDIRECTORY 
	::wxSetEnv(wxT("CommonPrograms"),std.MSWGetShellDir(0x17));//COMMON_PROGRAMS
	wxFileName::SplitVolume(pathTmp,&volume,NULL);
	if (!volume.empty())
	{
		volume.Append(':');
		::wxSetEnv(wxT("ALMRUN_DRIVE"),volume.c_str());
	}
	::wxSetWorkingDirectory(pathTmp);
	::wxSetEnv(wxT("ALMRUN_SYS"),IsX64()?"x64":"x86");
	// 用于防止UI自动放大
	SetProcessDPIAware();
	//pathTmp.Clear();
	//volume.Clear();
	#endif
	m_frame = NULL;
	this->NewFrame();
	assert(m_frame);
	this->Connect(wxEVT_ACTIVATE_APP,wxObjectEventFunction(&MerryApp::EvtActive));
	// 注册键盘钩子，截获双击ctrl键的事件
	#ifdef __WXMSW__
	if(g_config && g_config->get(DoubleHitCtrl)){
		hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
	}
	#endif
	return true;
}

void MerryApp::Execute_IPC_CMD(wxConnectionBase* conn)
{
	if (this->argc == 1)
		conn->Execute("SHOW");
	else
	{
		for(int i = 1;i < this->argc; ++i)
			conn->Execute(this->argv[i]);
	}
}

int MerryApp::OnExit()
{
	__DEBUG_BEGIN("")
	this->Disconnect(wxEVT_ACTIVATE_APP);
	//子窗口会自动半闭，所以不需要这个语句，否则有可能会出错
	//if (m_frame)
	//	wxDELETE(m_frame);
	m_frame = NULL;
	wxDELETE(m_checker);
	wxDELETE(m_server);
	__DEBUG_END("")
	#if _DEBUG_LOG
	if (m_pLogFile)
	{
		fclose(m_pLogFile);
		m_pLogFile = NULL;
	}
	#endif
	#ifdef __WXMSW__
	if(hHook){
		UnhookWindowsHookEx(hHook);
	}
	#endif
	return 0;
}

void MerryApp::EvtActive(wxActivateEvent &e)
{
	if (!m_frame)
		return;
	bool Changed = false;
	if (g_config)
		Changed = g_config->Changed();
	if (!e.GetActive())
		m_frame->Hide();
#ifdef _ALMRUN_CONFIG_H_
	else if (Changed)
		m_frame->NewConfig();
#endif//ifdef _ALMRUN_CONFIG_H_
//	e.Skip();
}

void MerryApp::stServerDisconnect()
{
	m_server->Disconnect();
}

void MerryApp::NewFrame()
{
	if (m_frame)
	{
		bool ok = m_frame->Close();
		assert(ok);
	}
	
	m_frame = new MerryFrame();
	m_frame->OnInit();
}

MerryFrame& MerryApp::GetFrame()
{
	assert(m_frame);
	return *m_frame;
}
