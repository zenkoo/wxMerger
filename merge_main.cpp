#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/textctrl.h>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <wx/icon.h>
#include <wx/image.h>
#include <vector>
#include <sstream>

#if 1
// 定义 crc32_calc 函数
#include <stdint.h>
#include <stddef.h>

// CRC32 表
static uint32_t crc32_table[256];

// 初始化 CRC32 表
void crc32_init() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320; // 反转多项式原始形式是0x04C11DB7
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
}

// 计算 CRC32
uint32_t crc32(const uint8_t *data, size_t length, uint32_t crc) {
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF];
    }
    return ~crc; // 取反
}
#else

static uint32_t crc32(const uint8_t * src, unsigned len, unsigned state)
{
    static uint32_t crctab[256];

    /* check whether we have generated the CRC table yet */
    /* this is much smaller than a static table */
    if (crctab[1] == 0) {
        for (unsigned i = 0; i < 256; i++) {
            uint32_t c = i;

            for (unsigned j = 0; j < 8; j++) {
                if (c & 1) {
                    c = 0xedb88320U ^ (c >> 1);
                } else {
                    c = c >> 1;
                }
            }
            crctab[i] = c;
        }
    }
    printf("len: %d\n", len);
    for (unsigned i = 0; i < len; i++)
        state = crctab[(state ^ src[i]) & 0xff] ^ (state >> 8);
    return state;
}
#endif
uint32_t calculate_crc32(const std::string& filename, size_t *outputFileSize) {
    std::ifstream fileApp(filename, std::ios::binary);
    if (!fileApp) {
        std::cerr << "Could not open file: " << filename << std::endl;
        return 0;
    }

    crc32_init();

    fileApp.seekg(0, std::ios::end);
    std::streamsize fileSize = fileApp.tellg();
    fileApp.seekg(0, std::ios::beg);

    // 将file内容读入buffer
    std::vector<uint8_t> buffer(512*1024); // 8KB buffer
    uint32_t crc = 0xFFFFFFFF;
    fileApp.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    crc = crc32(buffer.data(), fileSize, crc);
    *outputFileSize = fileSize;
    return crc;
}

class MergeApp : public wxApp {
public:
    virtual bool OnInit();
};

class MergeFrame : public wxFrame {
public:
    MergeFrame();

private:
    void OnSelectFile1(wxCommandEvent& event);
    void OnSelectFile2(wxCommandEvent& event);
    void OnMerge(wxCommandEvent& event);
    void UpdateStatus(const wxString& message);
    void UpdateLogText(const wxString& message);
    void OnCrc32(wxCommandEvent& event);
    void OnOta(wxCommandEvent& event);

    wxTextCtrl* file1Path;
    wxTextCtrl* file2Path;
    wxTextCtrl* file1AddrEntry;
    wxTextCtrl* file2AddrEntry;
    wxTextCtrl* logText;
    wxStatusBar* statusBar;

    wxDECLARE_EVENT_TABLE();
};

enum {
    ID_FILE1_SELECT = wxID_HIGHEST + 1,
    ID_FILE2_SELECT,
    ID_MERGE,
    ID_CRC,
    ID_OTA
};

wxBEGIN_EVENT_TABLE(MergeFrame, wxFrame)
    EVT_BUTTON(ID_FILE1_SELECT, MergeFrame::OnSelectFile1)
    EVT_BUTTON(ID_FILE2_SELECT, MergeFrame::OnSelectFile2)
    EVT_BUTTON(ID_MERGE, MergeFrame::OnMerge)
    EVT_BUTTON(ID_CRC, MergeFrame::OnCrc32)
    EVT_BUTTON(ID_OTA, MergeFrame::OnOta)
wxEND_EVENT_TABLE()

wxIMPLEMENT_APP(MergeApp);

bool MergeApp::OnInit() {
    MergeFrame* frame = new MergeFrame();
    frame->Show(true);
    return true;
}

MergeFrame::MergeFrame() : wxFrame(nullptr, wxID_ANY, "Binary File Merger", wxDefaultPosition, wxSize(800, 600)) {
    // 加载图标
    wxIcon icon("icon.ico", wxBITMAP_TYPE_ICO);
    SetIcon(icon);

    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

    // File 1 section, 设置label对齐为右对齐
    wxStaticText* file1Label = new wxStaticText(panel, wxID_ANY, "Bootloader:", wxDefaultPosition, wxSize(100, -1), wxALIGN_RIGHT);
    file1Path = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(250, -1), wxTE_READONLY);
    wxButton* file1Button = new wxButton(panel, ID_FILE1_SELECT, "Select File");
    wxStaticText* file1PosLabel = new wxStaticText(panel, wxID_ANY, "Address:");
    file1AddrEntry = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1), wxTE_LEFT);
    file1AddrEntry->SetValue("0x0000");

    wxBoxSizer* file1Sizer = new wxBoxSizer(wxHORIZONTAL);
    file1Sizer->Add(file1Label, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    file1Sizer->Add(file1Path, 1, wxEXPAND | wxALL, 5);
    file1Sizer->Add(file1Button, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    file1Sizer->Add(file1PosLabel, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    file1Sizer->Add(file1AddrEntry, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    vbox->Add(file1Sizer, 0, wxEXPAND);

    // File 2 section
    wxStaticText* file2Label = new wxStaticText(panel, wxID_ANY, "Application:", wxDefaultPosition, wxSize(100, -1), wxALIGN_RIGHT);
    file2Path = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(250, -1), wxTE_READONLY);
    wxButton* file2Button = new wxButton(panel, ID_FILE2_SELECT, "Select File");
    wxStaticText* file2PosLabel = new wxStaticText(panel, wxID_ANY, "Address:");
    file2AddrEntry = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1), wxTE_LEFT);
    file2AddrEntry->SetValue("0x2000");

    wxBoxSizer* file2Sizer = new wxBoxSizer(wxHORIZONTAL);
    file2Sizer->Add(file2Label, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    file2Sizer->Add(file2Path, 1, wxEXPAND | wxALL, 5);
    file2Sizer->Add(file2Button, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    file2Sizer->Add(file2PosLabel, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    file2Sizer->Add(file2AddrEntry, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    vbox->Add(file2Sizer, 0, wxEXPAND);

    // Merge button
    wxButton* mergeButton = new wxButton(panel, ID_MERGE, "Merge");
    wxButton* crcButton = new wxButton(panel, ID_CRC, "CRC32");
    wxButton* otaButton = new wxButton(panel, ID_OTA, "Make OTA bin");

    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->Add(mergeButton, 0, wxALIGN_CENTER | wxALL, 5);
    buttonSizer->Add(crcButton, 0, wxALIGN_CENTER | wxALL, 5);
    buttonSizer->Add(otaButton, 0, wxALIGN_CENTER | wxALL, 5);
    vbox->Add(buttonSizer, 0, wxALIGN_CENTER | wxALL, 5);

    // 创建一个TEXT多行编辑框，用于显示提示信息
    logText = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(250, -1), wxTE_MULTILINE | wxTE_READONLY);
    vbox->Add(logText, 1, wxEXPAND | wxALL, 5);

    // Status bar
    statusBar = new wxStatusBar(this);
    SetStatusBar(statusBar);

    panel->SetSizer(vbox);
    panel->Layout(); 
}

void MergeFrame::OnSelectFile1(wxCommandEvent& event) {
    wxFileDialog openFileDialog(this, "Select First Binary File", "", "", "Binary files (*.bin)|*.bin", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (openFileDialog.ShowModal() == wxID_OK) {
        file1Path->SetValue(openFileDialog.GetPath());
    }
}

void MergeFrame::OnSelectFile2(wxCommandEvent& event) {
    wxFileDialog openFileDialog(this, "Select Second Binary File", "", "", "Binary files (*.bin)|*.bin", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (openFileDialog.ShowModal() == wxID_OK) {
        file2Path->SetValue(openFileDialog.GetPath());
    }
}

void MergeFrame::UpdateLogText(const wxString& message) {
    logText->AppendText(message + "\n");
}

// OnCrc32
void MergeFrame::OnCrc32(wxCommandEvent& event) {
    uint32_t crcValue = 0; // CRC 值
    size_t fileSize = 0;

    // 如果file2Path为空，则输出错误信息
    if (file2Path->GetValue().IsEmpty()) {
        UpdateLogText("Please select application bin file.");
        return;
    }

    // 输出 crc32 值
    UpdateLogText("\nStart CRC32...");
    crcValue = calculate_crc32(file2Path->GetValue().ToStdString(), &fileSize);
    // 将 crcValue 转换为十六进制字符串
    std::stringstream ss;
    ss << std::hex << crcValue;
    std::string crcStr = ss.str();
    // 将size转换为十六进制字符串
    std::stringstream ssSize;
    ssSize << std::hex << fileSize;
    std::string sizeStr = ssSize.str();
    UpdateLogText("Application bin file CRC32: 0x" + crcStr + ", size: " + std::to_string(fileSize) + "(0x" + sizeStr + ")"); // 输出十六进制值
    UpdateLogText("CRC32 completed successfully.");
}

// OnOta
void MergeFrame::OnOta(wxCommandEvent& event) {
    uint32_t crcValue = 0; // CRC 值
    size_t fileSize = 0;
    // 如果file2Path为空，则输出错误信息
    if (file2Path->GetValue().IsEmpty()) {
        UpdateLogText("Please select application bin file.");
        return;
    }
    UpdateLogText("\nStart Make OTA bin...");
    crcValue = calculate_crc32(file2Path->GetValue().ToStdString(), &fileSize);
    // 将 crcValue 转换为十六进制字符串
    std::stringstream ss;
    ss << std::hex << crcValue;
    std::string crcStr = ss.str();
    // 将size转换为十六进制字符串
    std::stringstream ssSize;
    ssSize << std::hex << fileSize;
    std::string sizeStr = ssSize.str();
    UpdateLogText("Application bin file CRC32: 0x" + crcStr + ", size: " + std::to_string(fileSize) + "(0x" + sizeStr + ")"); // 输出十六进制值
    // 读取file2
    std::ifstream file2(file2Path->GetValue().ToStdString(), std::ios::binary);
    file2.seekg(0, std::ios::end);
    std::streamsize file2Size = file2.tellg();
    file2.seekg(0, std::ios::beg);
    // 如果ota.bin存在，则删除
    if (std::remove("ota.bin") == 0) {
        UpdateLogText("File ota.bin already exists, deleted.");
    }
    // 将file2的内容写入ota.bin
    std::ofstream otaFile("ota.bin", std::ios::binary);
    char buffer[4096]; // 使用缓冲区逐块读取
    while (file2.read(buffer, sizeof(buffer))) {
        otaFile.write(buffer, file2.gcount());
    }
    // 写入最后一块，最后一块可能小于4096
    otaFile.write(buffer, file2.gcount()); 

    // 写入MAGIC NUMBER
    uint32_t magicNumber1 = 0x5A5A5A5A;
    otaFile.write(reinterpret_cast<const char*>(&magicNumber1), sizeof(magicNumber1));
    uint32_t magicNumber2 = 0x51709394;
    otaFile.write(reinterpret_cast<const char*>(&magicNumber2), sizeof(magicNumber2));
    // 再写4字节长度值, 将size强制转换为uint32_t
    uint32_t ota_size = static_cast<uint32_t>(file2Size);
    otaFile.write(reinterpret_cast<const char*>(&ota_size), sizeof(ota_size));
    // 再写4字节crc32值
    otaFile.write(reinterpret_cast<const char*>(&crcValue), sizeof(crcValue));
    // 再写入MAGIC NUMBER
    otaFile.write(reinterpret_cast<const char*>(&magicNumber2), sizeof(magicNumber2));
    otaFile.write(reinterpret_cast<const char*>(&magicNumber1), sizeof(magicNumber1));

    // 计算ota.bin的长度
    otaFile.seekp(0, std::ios::end);
    std::streamsize otaFileSize = otaFile.tellp();
    UpdateLogText("OTA bin file size: " + std::to_string(otaFileSize));
    otaFile.close();

    UpdateLogText("OTA bin completed successfully, file saved to ota.bin.");
}

void MergeFrame::OnMerge(wxCommandEvent& event) {
    wxString file1 = file1Path->GetValue();
    wxString file2 = file2Path->GetValue();
    long file1OffsetValue = 0; // file1 从偏移 0 开始
    long file2OffsetValue = 0; // file2 从偏移 0x2000 开始

    file1AddrEntry->GetValue().ToLong(&file1OffsetValue, 16);
    file2AddrEntry->GetValue().ToLong(&file2OffsetValue, 16);

    if (file1.IsEmpty() || file2.IsEmpty()) {
        UpdateStatus("Please select both files.");
        return;
    }
    UpdateLogText("\nStart merge...");
    std::ifstream inputFile1(file1.ToStdString(), std::ios::binary);
    std::ifstream inputFile2(file2.ToStdString(), std::ios::binary);
    // 如果文件存在，则删除
    if (std::remove("merged.bin") == 0) {
        UpdateLogText("File merged.bin already exists, deleted.");
    }
    std::ofstream outputFile("merged.bin", std::ios::binary);

    if (!inputFile1 || !inputFile2 || !outputFile) {
        UpdateStatus("Error opening files.");
        return;
    }

    // 计算 file1 的大小
    inputFile1.seekg(0, std::ios::end);
    std::streamsize file1Size = inputFile1.tellg();
    inputFile1.seekg(0, std::ios::beg);
    UpdateLogText("Bootloader size: " + std::to_string(file1Size));
    // 计算 file2 的大小
    inputFile2.seekg(0, std::ios::end);
    std::streamsize file2Size = inputFile2.tellg();
    inputFile2.seekg(0, std::ios::beg);

    // 将 file1 的内容写入输出文件
    outputFile << inputFile1.rdbuf();

    // 填充 file1 和 file2 之间的空白区域
    std::streamsize fillSize = file2OffsetValue - file1Size; // 计算填充大小
    UpdateLogText("Fill 0xFF size: " + std::to_string(fillSize));
    if (fillSize > 0) {
        char* fillBuffer = new char[fillSize]; // 创建填充缓冲区
        std::memset(fillBuffer, 0xFF, fillSize); // 填充缓冲区为 0xFF
        outputFile.write(fillBuffer, fillSize); // 写入填充缓冲区
        delete[] fillBuffer; // 释放缓冲区
    }

    // 将 file2 的内容写入输出文件
    UpdateLogText("Application size: " + std::to_string(file2Size));
    outputFile << inputFile2.rdbuf();
    // 计算outputFile的大小
    outputFile.seekp(0, std::ios::end);
    std::streamsize outputFileSize = outputFile.tellp();
    UpdateLogText("Merged file size: " + std::to_string(outputFileSize));

    UpdateLogText("Merge completed successfully, file saved as merged.bin.");
    // 更新状态栏
    UpdateStatus("Merge completed successfully.");
}

void MergeFrame::UpdateStatus(const wxString& message) {
    statusBar->SetStatusText(message);
}