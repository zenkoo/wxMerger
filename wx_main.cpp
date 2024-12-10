// main.cpp
#include <wx/wx.h>
#include <wx/notebook.h>
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

#pragma comment(lib, "setupapi.lib")

// 定义串口设备类 GUID
// 这个 GUID 值来自 Microsoft 文档
DEFINE_GUID(GUID_DEVCLASS_PORTS, 0x4D36E978, 0xE325, 0x11CE, 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18);

class SerialFrame : public wxFrame {
private:
    // 现有的成员变量
    wxComboBox* portCombo;
    wxComboBox* baudCombo;
    wxButton* refreshBtn;
    wxButton* connectBtn;
    wxNotebook* notebook;
    wxTextCtrl* logText;
    wxTextCtrl* sendText;
    wxButton* sendBtn;
    wxStatusBar* statusBar;

    // Battery 面板相关的成员变量
    wxTextCtrl* batteryEntries[8];
    wxButton* resetBtn;
    wxButton* calibrateBtn;
    wxButton* tolerateBtn;
    wxButton* cleanBtn;
    wxButton* setSnBtn;
    wxCheckBox* pauseCheck;
    wxCheckBox* factoryCheck;
    wxCheckBox* autoIncCheck;
    wxTextCtrl* snEntry;

    // 串口通信相关成员
    HANDLE hSerial;
    std::thread* serialThread;
    std::atomic<bool> isRunning;
    
    // 添加一个标志来追踪断开连接的状态
    std::atomic<bool> isDisconnecting;
    
    // 添加发送队列相关成员
    std::queue<wxString> sendQueue;
    std::mutex sendMutex;
    std::atomic<bool> isSending;
    std::condition_variable sendCondition;
    std::thread* sendThread;

    // 串口通信相关函数
    bool OpenSerial(const wxString& portName, int baudRate) {
        // 确保端口名称格式正确（如果用户只输入了 "COM3"，需要加上完整路径）
        wxString fullPortName = portName;
        if (!portName.StartsWith("\\\\.\\")) {
            fullPortName = "\\\\.\\" + portName;
        }
        
        hSerial = CreateFile(fullPortName.wc_str(),
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           0,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           0);
                           
        if (hSerial == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            wxString errorMsg = wxString::Format(
                "Failed to open serial port: %s\nError code: %d\n", 
                fullPortName, error);
            wxMessageBox(errorMsg, "Error", wxOK | wxICON_ERROR);
            return false;
        }

        // 设置串口参数
        DCB dcbSerialParams = {0};
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
        
        if (!GetCommState(hSerial, &dcbSerialParams)) {
            CloseHandle(hSerial);
            wxMessageBox("Error getting serial port state", "Error", wxOK | wxICON_ERROR);
            return false;
        }

        dcbSerialParams.BaudRate = baudRate;
        dcbSerialParams.ByteSize = 8;
        dcbSerialParams.StopBits = ONESTOPBIT;
        dcbSerialParams.Parity = NOPARITY;
        
        // 优化串口设置
        dcbSerialParams.fBinary = TRUE;
        dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
        dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;
        dcbSerialParams.fOutxCtsFlow = FALSE;
        dcbSerialParams.fOutxDsrFlow = FALSE;
        dcbSerialParams.fDsrSensitivity = FALSE;
        dcbSerialParams.fAbortOnError = FALSE;

        if (!SetCommState(hSerial, &dcbSerialParams)) {
            CloseHandle(hSerial);
            wxMessageBox("Error setting serial port state", "Error", wxOK | wxICON_ERROR);
            return false;
        }

        // 设置缓冲区大小
        if (!SetupComm(hSerial, 1024, 1024)) {  // 设置输入输出缓冲区大小
            CloseHandle(hSerial);
            return false;
        }

        // 设置超时
        COMMTIMEOUTS timeouts = {0};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 1;
        timeouts.WriteTotalTimeoutMultiplier = 0;

        if (!SetCommTimeouts(hSerial, &timeouts)) {
            CloseHandle(hSerial);
            return false;
        }

        return true;
    }

    void CloseSerial() {
        if (hSerial != INVALID_HANDLE_VALUE) {
            CloseHandle(hSerial);
            hSerial = INVALID_HANDLE_VALUE;
        }
    }

    // 串口数据接收线程
    void SerialThread() {
        char buffer[1024];
        DWORD bytesRead;

        while (isRunning) {
            if (ReadFile(hSerial, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                // 创建事件并设置数据
                wxThreadEvent* event = new wxThreadEvent(wxEVT_THREAD, ID_SERIAL_DATA);
                event->SetString(wxString(buffer, wxConvUTF8));
                wxQueueEvent(this, event);
            }
            wxMilliSleep(10);  // 避免过度占用CPU
        }
    }

    // 发送线程
    void SendThreadFunction() {
        DWORD bytesWritten;
        COMMTIMEOUTS timeouts = {0};
        timeouts.WriteTotalTimeoutConstant = 1;    // 1ms超时
        timeouts.WriteTotalTimeoutMultiplier = 0;
        
        while (isRunning) {
            wxString dataToSend;
            bool hasData = false;
            
            {
                std::unique_lock<std::mutex> lock(sendMutex);
                if (!sendQueue.empty()) {
                    dataToSend = sendQueue.front();
                    sendQueue.pop();
                    hasData = true;
                } else {
                    // 如果队列为空，等待新数据
                    sendCondition.wait(lock, [this] { 
                        return !sendQueue.empty() || !isRunning; 
                    });
                }
            }
            
            if (!isRunning) break;
            
            if (hasData) {
                SetCommTimeouts(hSerial, &timeouts);
                wxCharBuffer buffer = dataToSend.ToUTF8();
                size_t dataLength = strlen(buffer.data());

                if (WriteFile(hSerial, buffer.data(), dataLength, &bytesWritten, NULL)) {
                    FlushFileBuffers(hSerial);
                    
                    // 发送成功，通知UI更新
                    wxThreadEvent* event = new wxThreadEvent(wxEVT_THREAD, ID_SEND_COMPLETE);
                    event->SetString(dataToSend);
                    wxQueueEvent(this, event);
                } else {
                    // 发送失败，通知UI
                    wxThreadEvent* event = new wxThreadEvent(wxEVT_THREAD, ID_SEND_ERROR);
                    event->SetString("Send failed: " + wxString::Format("Error code: %d", GetLastError()));
                    wxQueueEvent(this, event);
                }
            }
        }
    }

    // 发送数据
    bool SendSerialData(const wxString& data) {
        if (hSerial == INVALID_HANDLE_VALUE) return false;

        DWORD bytesWritten;
        wxCharBuffer buffer = data.ToUTF8();
        return WriteFile(hSerial, buffer.data(), strlen(buffer.data()), &bytesWritten, NULL) != 0;
    }

    // 事件处理函数
    void OnConnect(wxCommandEvent& event) {
        if (hSerial == INVALID_HANDLE_VALUE) {
            wxString portName = portCombo->GetValue();
            if (portName.empty()) {
                wxMessageBox("Please select a COM port", "Error", wxOK | wxICON_ERROR);
                return;
            }

            long baudRate;
            if (!baudCombo->GetValue().ToLong(&baudRate)) {
                wxMessageBox("Invalid baud rate", "Error", wxOK | wxICON_ERROR);
                return;
            }

            if (OpenSerial(portName, baudRate)) {
                isRunning = true;
                serialThread = new std::thread(&SerialFrame::SerialThread, this);
                sendThread = new std::thread(&SerialFrame::SendThreadFunction, this);
                connectBtn->SetLabel("Disconnect");
                statusBar->SetStatusText("Connected to " + portName);
            }
        } else {
            
            isRunning = false;
            sendCondition.notify_one(); // 唤醒发送线程

            if (sendThread) {
                sendThread->join();
                delete sendThread;
                sendThread = nullptr;
            }

            // 断开连接
            if (!isDisconnecting) {
                isDisconnecting = true;
                isRunning = false;
                
                // 创建一个新线程来处理断开连接
                std::thread([this]() {
                    if (serialThread) {
                        serialThread->join();
                        delete serialThread;
                        serialThread = nullptr;
                    }
                    CloseSerial();
                    
                    // 使用事件来更新UI
                    wxQueueEvent(this, new wxThreadEvent(wxEVT_THREAD, ID_DISCONNECT_COMPLETE));
                }).detach();

                // 立即禁用连接按钮，防止重复点击
                connectBtn->Enable(false);
            }

            
        }
    }

    void OnDisconnectComplete(wxThreadEvent& event) {
        // 在主线程中更新UI
        connectBtn->SetLabel("Connect");
        connectBtn->Enable(true);
        statusBar->SetStatusText("Disconnected");
        isDisconnecting = false;
    }

    void OnSend(wxCommandEvent& event) {
        if (hSerial != INVALID_HANDLE_VALUE) {
            wxString data = sendText->GetValue();
            if (!data.empty()) {
                {
                    std::lock_guard<std::mutex> lock(sendMutex);
                    sendQueue.push(data);
                }
                sendCondition.notify_one();
                sendText->Clear();
            }
        } else {
            logText->AppendText("Error: Serial port not open\n");
        }
    }
    void OnSerialData(wxThreadEvent& event) {
        // 直接从事件获取字符串
        wxString data = event.GetString();
        logText->AppendText("RX: " + data + "\n");
    }
    // 添加发送完成事件处理
    void OnSendComplete(wxThreadEvent& event) {
        wxString data = event.GetString();
        logText->AppendText("TX: " + data + "\n");
    }
    wxPanel* CreateBatteryPanel(wxWindow* parent) {
        wxPanel* batteryPanel = new wxPanel(parent);
        wxBoxSizer* batterySizer = new wxBoxSizer(wxHORIZONTAL);
        
        // 第一列布局
        wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
        
        const wxString labels[] = {
            "Battery Time:", "Voltage(V):", "Cell Voltage(V):", 
            "Battery Current(A):", "Temperature(C):", "Capacity(mAH):",
            "Cycles:", "Serial Number:"
        };
        
        // 计算最长标签的宽度
        int maxWidth = 0;
        wxFont font = GetFont();
        font.Scale(GetContentScaleFactor());
        
        for(const wxString& label : labels) {
            int width;
            GetTextExtent(label, &width, nullptr, nullptr, nullptr, &font);
            maxWidth = std::max(maxWidth, width);
        }
        
        // 为标签创建一个固定宽度的面板，添加一些额外空间
        int labelWidth = maxWidth + FromDIP(10);
        
        // 创建左侧的标签和文本框
        for(int i = 0; i < 8; i++) {
            wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
            
            // 创建标签，设置固定宽度和右对齐
            wxStaticText* label = new wxStaticText(batteryPanel, wxID_ANY, labels[i],
                                                 wxDefaultPosition, 
                                                 wxSize(labelWidth, -1),
                                                 wxALIGN_RIGHT);
            label->SetFont(label->GetFont().Scale(GetContentScaleFactor()));
            
            batteryEntries[i] = new wxTextCtrl(batteryPanel, wxID_ANY);
            batteryEntries[i]->SetFont(batteryEntries[i]->GetFont().Scale(GetContentScaleFactor()));
            
            row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
            row->Add(batteryEntries[i], 1, wxEXPAND);
            
            leftSizer->Add(row, 0, wxEXPAND | wxALL, FromDIP(5));
        }
        
        // 第二列布局
        wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
        
        // 第一行：Reset + Pause
        wxBoxSizer* row1 = new wxBoxSizer(wxHORIZONTAL);
        resetBtn = new wxButton(batteryPanel, wxID_ANY, "Reset");
        pauseCheck = new wxCheckBox(batteryPanel, wxID_ANY, "Pause");
        row1->Add(resetBtn, 0, wxRIGHT, FromDIP(5));
        row1->Add(pauseCheck, 0);
        rightSizer->Add(row1, 0, wxALL, FromDIP(5));
        
        // 第二行：Calibrate + By Factory
        wxBoxSizer* row2 = new wxBoxSizer(wxHORIZONTAL);
        calibrateBtn = new wxButton(batteryPanel, wxID_ANY, "Calibrate");
        factoryCheck = new wxCheckBox(batteryPanel, wxID_ANY, "By Factory");
        row2->Add(calibrateBtn, 0, wxRIGHT, FromDIP(5));
        row2->Add(factoryCheck, 0);
        rightSizer->Add(row2, 0, wxALL, FromDIP(5));
        
        // 第三行：空行
        rightSizer->AddSpacer(FromDIP(30));
        
        // 第四行：Tolerate + Clean
        wxBoxSizer* row4 = new wxBoxSizer(wxHORIZONTAL);
        tolerateBtn = new wxButton(batteryPanel, wxID_ANY, "Tolerate");
        cleanBtn = new wxButton(batteryPanel, wxID_ANY, "Clean");
        row4->Add(tolerateBtn, 0, wxRIGHT, FromDIP(5));
        row4->Add(cleanBtn, 0);
        rightSizer->Add(row4, 0, wxALL, FromDIP(5));
        
        // 第五、六行：空行
        rightSizer->AddSpacer(FromDIP(60));
        
        // 第七行：Set SN + Auto Inc SN
        wxBoxSizer* row7 = new wxBoxSizer(wxHORIZONTAL);
        setSnBtn = new wxButton(batteryPanel, wxID_ANY, "Set SN");
        autoIncCheck = new wxCheckBox(batteryPanel, wxID_ANY, "Auto Inc SN");
        row7->Add(setSnBtn, 0, wxRIGHT, FromDIP(5));
        row7->Add(autoIncCheck, 0);
        rightSizer->Add(row7, 0, wxALL, FromDIP(5));
        
        // 第八行：SN输入框
        snEntry = new wxTextCtrl(batteryPanel, wxID_ANY);
        rightSizer->Add(snEntry, 0, wxEXPAND | wxALL, FromDIP(5));
        
        // 将两列添加到主布局
        batterySizer->Add(leftSizer, 1, wxEXPAND | wxALL, FromDIP(10));
        batterySizer->Add(rightSizer, 1, wxEXPAND | wxALL, FromDIP(10));
        
        batteryPanel->SetSizer(batterySizer);
        return batteryPanel;
    }

    void OnRefreshPorts(wxCommandEvent& event) {
        portCombo->Clear();
        
        HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
        if (hDevInfo == INVALID_HANDLE_VALUE) {
            wxMessageBox("Failed to get device information", "Error", wxOK | wxICON_ERROR);
            return;
        }

        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
            WCHAR friendlyName[256];
            WCHAR portName[256];
            DWORD propertyType;
            DWORD requiredSize;

            // 获取友好名称
            if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME,
                &propertyType, (PBYTE)friendlyName, sizeof(friendlyName), &requiredSize)) {
                
                // 从注册表获取实际的COM端口名
                HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfoData,
                    DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
                    
                if (hKey != INVALID_HANDLE_VALUE) {
                    DWORD portNameSize = sizeof(portName);
                    DWORD type;
                    if (RegQueryValueExW(hKey, L"PortName", NULL, &type,
                        (LPBYTE)portName, &portNameSize) == ERROR_SUCCESS) {
                        // 只添加COM端口
                        if (wcsstr(portName, L"COM")) {
                            portCombo->Append(portName);
                        }
                    }
                    RegCloseKey(hKey);
                }
            }
        }

        SetupDiDestroyDeviceInfoList(hDevInfo);

        // 如果有可用端口，选择第一个
        if (portCombo->GetCount() > 0) {
            portCombo->SetSelection(0);
        }
    }
    void OnSendError(wxThreadEvent& event) {
        wxString errorMsg = event.GetString();
        logText->AppendText("Error: " + errorMsg + "\n");
        statusBar->SetStatusText(errorMsg);
    }
public:
    // 事件ID
    enum {
        ID_SERIAL_EVENT = wxID_HIGHEST + 1,
        ID_SERIAL_DATA,
        ID_DISCONNECT_COMPLETE,
        ID_SEND_COMPLETE,
        ID_SEND_ERROR
    };
    SerialFrame() : wxFrame(nullptr, wxID_ANY, "Serial Tool", 
                           wxDefaultPosition, wxDefaultSize) 
    {
        // 初始化串口相关变量
        hSerial = INVALID_HANDLE_VALUE;
        serialThread = nullptr;
        isRunning = false;
        isDisconnecting = false;
        isSending = false;
        sendThread = nullptr;

        // 使用 DIP 设置窗口大小
        SetSize(FromDIP(wxSize(800, 600)));
        SetMinSize(FromDIP(wxSize(600, 400)));

        // 创建主面板和垂直布局
        wxPanel* mainPanel = new wxPanel(this);
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

        // 使用 FromDIP 转换所有边距值
        const int margin = FromDIP(5);

        // 第一行：端口选择区域
        wxBoxSizer* hbox1 = new wxBoxSizer(wxHORIZONTAL);
        
        wxStaticText* portLabel = new wxStaticText(mainPanel, wxID_ANY, "Ports:");
        portLabel->SetFont(portLabel->GetFont().Scale(GetContentScaleFactor()));
        hbox1->Add(portLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, margin);
        
        portCombo = new wxComboBox(mainPanel, wxID_ANY);
        hbox1->Add(portCombo, 2, wxALL | wxEXPAND, margin);
        
        baudCombo = new wxComboBox(mainPanel, wxID_ANY, wxEmptyString,
                                 wxDefaultPosition, 
                                 FromDIP(wxSize(100, -1)));
        wxArrayString baudRates;
        baudRates.Add("9600");
        baudRates.Add("19200");
        baudRates.Add("38400");
        baudRates.Add("57600");
        baudRates.Add("115200");
        baudCombo->Append(baudRates);
        baudCombo->SetValue("115200");
        hbox1->Add(baudCombo, 0, wxALL, margin);
        
        refreshBtn = new wxButton(mainPanel, wxID_ANY, "Refresh");
        refreshBtn->Bind(wxEVT_BUTTON, &SerialFrame::OnRefreshPorts, this);
        hbox1->Add(refreshBtn, 0, wxALL, margin);
        
        connectBtn = new wxButton(mainPanel, wxID_ANY, "Connect");
        hbox1->Add(connectBtn, 0, wxALL, margin);
        
        vbox->Add(hbox1, 0, wxEXPAND);

        // 第二行：Notebook
        notebook = new wxNotebook(mainPanel, wxID_ANY);
        notebook->AddPage(CreateBatteryPanel(notebook), "Battery");
        wxPanel* infoPanel = new wxPanel(notebook);
        wxPanel* dataPanel = new wxPanel(notebook);
        notebook->AddPage(infoPanel, "Information");
        notebook->AddPage(dataPanel, "Data");
        
        vbox->Add(notebook, 1, wxEXPAND | wxALL, margin);

        // 第三行：日志文本框
        logText = new wxTextCtrl(mainPanel, wxID_ANY, "", 
                                wxDefaultPosition, wxDefaultSize,
                                wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
        // 设置更好的字体渲染
        logText->SetFont(logText->GetFont().Scale(GetContentScaleFactor()));
        vbox->Add(logText, 2, wxEXPAND | wxALL, margin);

        // 第四行：发送区域
        wxBoxSizer* hbox4 = new wxBoxSizer(wxHORIZONTAL);
        
        sendText = new wxTextCtrl(mainPanel, wxID_ANY);
        sendText->SetFont(sendText->GetFont().Scale(GetContentScaleFactor()));
        hbox4->Add(sendText, 1, wxALL | wxEXPAND, margin);
        
        sendBtn = new wxButton(mainPanel, wxID_ANY, "Send");
        hbox4->Add(sendBtn, 0, wxALL, margin);
        
        vbox->Add(hbox4, 0, wxEXPAND);

        mainPanel->SetSizer(vbox);

        // 创建状态栏
        statusBar = CreateStatusBar();
        statusBar->SetStatusText("Disconnected");
        statusBar->SetFont(statusBar->GetFont().Scale(GetContentScaleFactor()));
        
        Centre();


        // 绑定事件
        connectBtn->Bind(wxEVT_BUTTON, &SerialFrame::OnConnect, this);
        sendBtn->Bind(wxEVT_BUTTON, &SerialFrame::OnSend, this);
        Bind(wxEVT_THREAD, &SerialFrame::OnSerialData, this, ID_SERIAL_DATA);
        // 绑定断开连接完成事件
        Bind(wxEVT_THREAD, &SerialFrame::OnDisconnectComplete, this, ID_DISCONNECT_COMPLETE);
        Bind(wxEVT_THREAD, &SerialFrame::OnSendComplete, this, ID_SEND_COMPLETE);
        Bind(wxEVT_THREAD, &SerialFrame::OnSendError, this, ID_SEND_ERROR);

        // 在析构时确保线程正确关闭
        // 修改窗口关闭事件处理
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
            if (hSerial != INVALID_HANDLE_VALUE) {
                isRunning = false;
                if (serialThread) {
                    serialThread->join();  // 在关闭窗口时我们必须等待
                    delete serialThread;
                }
                CloseSerial();
            }
            event.Skip();
        });

        wxCommandEvent dummy;
        OnRefreshPorts(dummy);
    }

    ~SerialFrame() {
        isRunning = false;
        sendCondition.notify_one();
        if (sendThread) {
            sendThread->join();
            delete sendThread;
        }
        // 确保清理
        if (hSerial != INVALID_HANDLE_VALUE) {
            isRunning = false;
            if (serialThread) {
                serialThread->join();
                delete serialThread;
            }
            CloseSerial();
        }
    }
};

class SerialApp : public wxApp {
public:
    bool OnInit() {
        // 启用 DPI 感知
        #ifdef __WINDOWS__
        SetProcessDPIAware();
        #endif
        
        SerialFrame* frame = new SerialFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(SerialApp);